#include "gps_time.h"

#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "gps_nmea.h"
#include "gps_sky.h"

// The WBE2 slot of WB-MGU: the module's NMEA output and command input on the port-2 UART
// pins, no RTS/CTS.
#define GPS_TIME_UART_NUM                   UART_NUM_2
#define GPS_TIME_UART_TX_PIN                SERIAL_OUTPUT_PIN_2
#define GPS_TIME_UART_RX_PIN                SERIAL_INPUT_PIN_2
#define GPS_TIME_UART_RX_BUFFER_SIZE        2048    // a whole second of output, well over the 128-byte FIFO
#define GPS_TIME_UART_TX_BUFFER_SIZE        0       // writes block until the FIFO takes them
#define GPS_TIME_UART_TX_DONE_TIMEOUT_MS    100
#define GPS_TIME_UART_READ_CHUNK            128
#define GPS_TIME_UART_READ_TIMEOUT_MS       100     // also the period of the link and lock checks

// The AT6558 (CASIC) starts at 9600 baud, where its full output takes about a second and an RMC
// can land after the NEXT PPS edge; at 115200 the same output takes about 0.1 s. The link is
// looked for at both rates, and a module found at 9600 is moved to 115200 with $PCAS01.
#define GPS_TIME_BAUD_FAST                  115200
#define GPS_TIME_BAUD_DEFAULT               9600
#define GPS_TIME_PCAS01_BODY                "PCAS01,5"      // 5 = 115200 baud

// Output rate command: GGA, GSA, GSV, RMC and the antenna status GPTXT once per fix, every
// other sentence off. Field order: GGA, GLL, GSA, GSV, RMC, VTG, ZDA, ANT, DHV, LPS, two
// reserved, UTC, GST, three reserved, TIM.
#define GPS_TIME_PCAS03_BODY                "PCAS03,1,0,1,1,1,0,0,1,0,0,,,0,0,,,,0"
#define GPS_TIME_NMEA_COMMAND_MAX           64

// A probe waits this long for a sentence that passes its checksum; a running link that goes
// this long without one is looked for again.
#define GPS_TIME_LINK_TIMEOUT_US            3000000LL
// The average PPS interval (and the crystal error the page derives from it) takes only intervals
// in this range: the gap across a PPS outage (no fix, antenna off) is not a second at all.
#define GPS_TIME_PPS_INTERVAL_SANE_MIN_US   500000LL
#define GPS_TIME_PPS_INTERVAL_SANE_MAX_US   1500000LL
// No RMC is paired with a PPS edge this soon after the link comes up: the module may still be
// finishing output begun before the rate or the output configuration changed.
#define GPS_TIME_LINK_SETTLE_US             2000000LL

// The module's PPS output, on the slot's "RTS" pin. Never driven by the firmware.
#define GPS_TIME_PPS_PIN                    SERIAL_IO_PIN_2

#define GPS_TIME_NMEA_LINE_MAX              128     // NMEA itself allows 82 characters
#define GPS_TIME_NMEA_TYPE_RMC              "RMC"
#define GPS_TIME_NMEA_TYPE_GGA              "GGA"
#define GPS_TIME_NMEA_TYPE_GSA              "GSA"
#define GPS_TIME_NMEA_TYPE_GSV              "GSV"
#define GPS_TIME_NMEA_TYPE_TXT              "TXT"
#define GPS_TIME_NMEA_PAIR_COUNT            3       // "hhmmss", "ddmmyy"
#define GPS_TIME_NMEA_PAIR_LEN              2
#define GPS_TIME_NMEA_YEAR_BASE             2000

#define GPS_TIME_RMC_FIELD_TIME             1
#define GPS_TIME_RMC_FIELD_STATUS           2
#define GPS_TIME_RMC_FIELD_DATE             9
#define GPS_TIME_RMC_STATUS_ACTIVE          'A'
#define GPS_TIME_RMC_FRACTION_MARK          '.'

#define GPS_TIME_DECIMAL_BASE               10
#define GPS_TIME_HOURS_PER_DAY              24
#define GPS_TIME_MINUTES_PER_HOUR           60
#define GPS_TIME_SECONDS_PER_MINUTE         60
#define GPS_TIME_SECONDS_PER_HOUR           3600
#define GPS_TIME_SECONDS_PER_DAY            86400
#define GPS_TIME_MONTHS_PER_YEAR            12
#define GPS_TIME_DAYS_PER_MONTH_MAX         31
#define GPS_TIME_US_PER_S                   1000000LL
#define GPS_TIME_US_PER_MS                  1000LL
#define GPS_TIME_HALF_SECOND_US             (GPS_TIME_US_PER_S / 2)

// days_from_civil() by Howard Hinnant, on a year that starts on March 1st so that the leap
// day is the last day of it.
#define GPS_TIME_CIVIL_YEARS_PER_ERA        400
#define GPS_TIME_CIVIL_DAYS_PER_ERA         146097
#define GPS_TIME_CIVIL_DAYS_PER_YEAR        365
#define GPS_TIME_CIVIL_LEAP_YEAR_PERIOD     4
#define GPS_TIME_CIVIL_CENTURY_YEARS        100
#define GPS_TIME_CIVIL_FEBRUARY             2       // January and February belong to the previous year
#define GPS_TIME_CIVIL_MONTH_SHIFT          9       // (month + 9) % 12 makes March month 0
#define GPS_TIME_CIVIL_MONTH_DAYS_MUL       153     // days before month m (from March): (153 * m + 2) / 5
#define GPS_TIME_CIVIL_MONTH_DAYS_ADD       2
#define GPS_TIME_CIVIL_MONTH_DAYS_DIV       5
#define GPS_TIME_CIVIL_UNIX_EPOCH_DAYS      719468  // days from 0000-03-01 to 1970-01-01

// An RMC is taken to name the PPS edge before it only when it completes within this long of
// that edge; a later one may already belong to the next edge.
#define GPS_TIME_PAIR_WINDOW_US             900000LL
#define GPS_TIME_PPS_TIMEOUT_US             2000000LL
#define GPS_TIME_ANCHOR_TIMEOUT_US          10000000LL

#define GPS_TIME_STATUS_PERIOD_US           60000000LL
#define GPS_TIME_UTC_TEXT_LEN               32
#define GPS_TIME_DELAY_TEXT_LEN             24

#define GPS_TIME_TASK_STACK_SIZE            (4 * 1024)
#define GPS_TIME_TASK_PRIORITY              5


// The timebase, shared between the PPS interrupt, the reader task and gps_time_now() callers
// on either core; every access goes through gps_time_lock.
typedef struct {
    int64_t pps_us;                     // esp_timer time of the latest PPS edge
    uint32_t pps_count;
    bool anchor_valid;
    int64_t anchor_pps_us;              // a PPS edge paired with an RMC...
    int64_t anchor_sec;                 // ...and the unix second that edge started
} gps_time_edge_t;

// The latest intervals between PPS edges, written by the interrupt under gps_time_lock.
typedef struct {
    int32_t interval_us[GPS_TIME_PPS_HISTORY_LEN];
    uint32_t next;                      // slot the next interval goes to
    uint32_t count;                     // slots filled
} gps_time_pps_history_t;

typedef enum {
    GPS_TIME_LINK_STATE_PROBE_115200 = 0,
    GPS_TIME_LINK_STATE_PROBE_9600,
    GPS_TIME_LINK_STATE_RUNNING,
} gps_time_link_state_t;

// Everything below is the reader task's, and read by gps_time_get_status(); both go through
// gps_time_mutex.
typedef struct {
    gps_time_link_state_t state;
    uint32_t baud;
    int64_t state_us;                   // when the current state was entered
    int64_t sentence_us;                // latest valid sentence while running
    bool sentence_seen;                 // a valid sentence since the last gps_time_link_step()
    bool absent_logged;                 // "no valid NMEA" already logged since the link was last up
} gps_time_link_t;

typedef struct {
    uint64_t nmea_bytes;
    uint32_t sentences;
    uint32_t checksum_errors;
    uint32_t rmc_valid;
    uint32_t rmc_invalid;
    bool delay_known;
    int64_t delay_ms;                   // PPS edge to the completion of the last valid RMC
    gps_time_antenna_t antenna;
    bool locked;                        // gps_time_now() at the latest check
    int64_t lock_start_us;
    uint32_t lock_count;
    uint32_t loss_count;
} gps_time_stats_t;

typedef struct {
    char text[GPS_TIME_NMEA_LINE_MAX + 1];
    size_t len;
    bool active;
} gps_time_line_t;


static const char *TAG = "gps_time";

static const char *const gps_time_antenna_texts[GPS_TIME_ANTENNA_COUNT] = {
    [GPS_TIME_ANTENNA_UNKNOWN] = "",
    [GPS_TIME_ANTENNA_OK] = "ANTENNA OK",
    [GPS_TIME_ANTENNA_OPEN] = "ANTENNA OPEN",
    [GPS_TIME_ANTENNA_SHORT] = "ANTENNA SHORT",
};

static portMUX_TYPE gps_time_lock = portMUX_INITIALIZER_UNLOCKED;
static gps_time_edge_t gps_time_edge = {0};
static gps_time_pps_history_t gps_time_pps_history = {0};

static SemaphoreHandle_t gps_time_mutex = NULL;
static gps_time_link_t gps_time_link = {
    .state = GPS_TIME_LINK_STATE_PROBE_115200,
    .baud = GPS_TIME_BAUD_FAST,
};
static gps_time_stats_t gps_time_stats = {0};
static gps_time_line_t gps_time_line = {0};


static void IRAM_ATTR gps_time_pps_isr(void *arg)
{
    (void)arg;

    portENTER_CRITICAL_ISR(&gps_time_lock);
    int64_t now_us = esp_timer_get_time();
    if (gps_time_edge.pps_count > 0) {
        int64_t interval_us = now_us - gps_time_edge.pps_us;
        if (interval_us > INT32_MAX) {
            interval_us = INT32_MAX;
        }
        gps_time_pps_history.interval_us[gps_time_pps_history.next] = (int32_t)interval_us;
        gps_time_pps_history.next++;
        if (gps_time_pps_history.next >= GPS_TIME_PPS_HISTORY_LEN) {
            gps_time_pps_history.next = 0;
        }
        if (gps_time_pps_history.count < GPS_TIME_PPS_HISTORY_LEN) {
            gps_time_pps_history.count++;
        }
    }
    gps_time_edge.pps_us = now_us;
    gps_time_edge.pps_count++;
    portEXIT_CRITICAL_ISR(&gps_time_lock);
}


// The time `edge` gives at now_us; see gps_time_now().
static bool gps_time_derive(const gps_time_edge_t *edge, int64_t now_us, int64_t *unix_us, int64_t *ref_unix_us)
{
    if (!edge->anchor_valid) {
        *unix_us = 0;
        *ref_unix_us = 0;
        return false;
    }

    // Whole seconds from the anchor's edge to the latest one: the edges are GPS seconds apart
    // as measured by the crystal, and while synced the anchor is under 10 s old, so rounding
    // absorbs the drift. An older anchor only feeds unsynced (LI=3) answers.
    int64_t pps_elapsed_s = ((edge->pps_us - edge->anchor_pps_us) + GPS_TIME_HALF_SECOND_US) / GPS_TIME_US_PER_S;
    int64_t since_pps_us = now_us - edge->pps_us;

    *ref_unix_us = (edge->anchor_sec + pps_elapsed_s) * GPS_TIME_US_PER_S;
    *unix_us = *ref_unix_us + since_pps_us;

    return ((since_pps_us < GPS_TIME_PPS_TIMEOUT_US) && ((now_us - edge->anchor_pps_us) < GPS_TIME_ANCHOR_TIMEOUT_US));
}


bool gps_time_now(int64_t *unix_us, int64_t *ref_unix_us)
{
    // "now" is read inside the lock so that it can never predate the PPS edge read with it.
    portENTER_CRITICAL(&gps_time_lock);
    int64_t now_us = esp_timer_get_time();
    gps_time_edge_t edge = gps_time_edge;
    portEXIT_CRITICAL(&gps_time_lock);

    return gps_time_derive(&edge, now_us, unix_us, ref_unix_us);
}


// Parses GPS_TIME_NMEA_PAIR_COUNT consecutive two-digit decimal numbers ("hhmmss", "ddmmyy").
static bool gps_time_parse_pairs(const char *text, unsigned values[GPS_TIME_NMEA_PAIR_COUNT])
{
    for (size_t i = 0; i < GPS_TIME_NMEA_PAIR_COUNT; i++) {
        unsigned value = 0;

        for (size_t j = 0; j < GPS_TIME_NMEA_PAIR_LEN; j++) {
            char c = text[(i * GPS_TIME_NMEA_PAIR_LEN) + j];
            if ((c < '0') || (c > '9')) {
                return false;
            }
            value = (value * GPS_TIME_DECIMAL_BASE) + (unsigned)(c - '0');
        }
        values[i] = value;
    }
    return true;
}


// Days from 1970-01-01 to the given proleptic Gregorian date; year must be positive.
static int64_t gps_time_days_from_civil(unsigned year, unsigned month, unsigned day)
{
    if (month <= GPS_TIME_CIVIL_FEBRUARY) {
        year--;
    }

    unsigned era = year / GPS_TIME_CIVIL_YEARS_PER_ERA;
    unsigned year_of_era = year - (era * GPS_TIME_CIVIL_YEARS_PER_ERA);
    unsigned month_from_march = (month + GPS_TIME_CIVIL_MONTH_SHIFT) % GPS_TIME_MONTHS_PER_YEAR;
    unsigned day_of_year = ((((GPS_TIME_CIVIL_MONTH_DAYS_MUL * month_from_march) + GPS_TIME_CIVIL_MONTH_DAYS_ADD) /
                             GPS_TIME_CIVIL_MONTH_DAYS_DIV) + day) - 1;
    unsigned day_of_era = (((year_of_era * GPS_TIME_CIVIL_DAYS_PER_YEAR) +
                            (year_of_era / GPS_TIME_CIVIL_LEAP_YEAR_PERIOD)) -
                           (year_of_era / GPS_TIME_CIVIL_CENTURY_YEARS)) + day_of_year;

    return (((int64_t)era * GPS_TIME_CIVIL_DAYS_PER_ERA) + (int64_t)day_of_era) - GPS_TIME_CIVIL_UNIX_EPOCH_DAYS;
}


// Unix second an RMC names, when its status is 'A', its time a whole second and its date
// plausible.
static bool gps_time_rmc_to_unix(const gps_nmea_sentence_t *sentence, int64_t *unix_sec)
{
    const char *time_field = gps_nmea_field(sentence, GPS_TIME_RMC_FIELD_TIME);
    const char *status_field = gps_nmea_field(sentence, GPS_TIME_RMC_FIELD_STATUS);
    const char *date_field = gps_nmea_field(sentence, GPS_TIME_RMC_FIELD_DATE);
    size_t time_len = strlen(time_field);

    if ((status_field[0] != GPS_TIME_RMC_STATUS_ACTIVE) || (status_field[1] != '\0')) {
        return false;
    }

    // hhmmss[.sss]: the fraction, if any, must be all zeros - the sentence names a PPS edge.
    const size_t hms_len = GPS_TIME_NMEA_PAIR_COUNT * GPS_TIME_NMEA_PAIR_LEN;
    if (time_len < hms_len) {
        return false;
    }
    if (time_len > hms_len) {
        if (time_field[hms_len] != GPS_TIME_RMC_FRACTION_MARK) {
            return false;
        }
        for (size_t i = hms_len + 1; i < time_len; i++) {
            if (time_field[i] != '0') {
                return false;
            }
        }
    }

    unsigned hms[GPS_TIME_NMEA_PAIR_COUNT];
    unsigned dmy[GPS_TIME_NMEA_PAIR_COUNT];
    if ((strlen(date_field) != (GPS_TIME_NMEA_PAIR_COUNT * GPS_TIME_NMEA_PAIR_LEN)) ||
        (!gps_time_parse_pairs(time_field, hms)) ||
        (!gps_time_parse_pairs(date_field, dmy)))
    {
        return false;
    }

    unsigned hour = hms[0];
    unsigned minute = hms[1];
    unsigned second = hms[2];
    unsigned day = dmy[0];
    unsigned month = dmy[1];
    unsigned year = GPS_TIME_NMEA_YEAR_BASE + dmy[2];

    if ((hour >= GPS_TIME_HOURS_PER_DAY) || (minute >= GPS_TIME_MINUTES_PER_HOUR) ||
        (second >= GPS_TIME_SECONDS_PER_MINUTE) || (day < 1) || (day > GPS_TIME_DAYS_PER_MONTH_MAX) ||
        (month < 1) || (month > GPS_TIME_MONTHS_PER_YEAR))
    {
        return false;
    }

    *unix_sec = (gps_time_days_from_civil(year, month, day) * GPS_TIME_SECONDS_PER_DAY) +
                ((int64_t)hour * GPS_TIME_SECONDS_PER_HOUR) + ((int64_t)minute * GPS_TIME_SECONDS_PER_MINUTE) +
                (int64_t)second;
    return true;
}


// Pairs a valid RMC with the latest PPS edge, which it names if it completed soon enough and
// the link has settled.
static void gps_time_handle_rmc(const gps_nmea_sentence_t *sentence)
{
    int64_t rmc_sec = 0;
    if (!gps_time_rmc_to_unix(sentence, &rmc_sec)) {
        gps_time_stats.rmc_invalid++;
        return;
    }
    gps_time_stats.rmc_valid++;

    portENTER_CRITICAL(&gps_time_lock);
    int64_t now_us = esp_timer_get_time();
    int64_t pps_us = gps_time_edge.pps_us;
    bool pps_seen = (gps_time_edge.pps_count > 0);
    bool link_settled = ((gps_time_link.state == GPS_TIME_LINK_STATE_RUNNING) &&
                         ((now_us - gps_time_link.state_us) >= GPS_TIME_LINK_SETTLE_US));
    if (pps_seen && link_settled && ((now_us - pps_us) < GPS_TIME_PAIR_WINDOW_US)) {
        gps_time_edge.anchor_valid = true;
        gps_time_edge.anchor_pps_us = pps_us;
        gps_time_edge.anchor_sec = rmc_sec;
    }
    portEXIT_CRITICAL(&gps_time_lock);

    gps_time_stats.delay_known = pps_seen;
    gps_time_stats.delay_ms = (now_us - pps_us) / GPS_TIME_US_PER_MS;
}


// Logs the antenna status the module reports in "$GPTXT,...ANTENNA OPEN|OK|SHORT" on change.
static void gps_time_handle_txt(const gps_nmea_sentence_t *sentence)
{
    for (size_t i = 1; i < sentence->count; i++) {
        for (int antenna = GPS_TIME_ANTENNA_OK; antenna < GPS_TIME_ANTENNA_COUNT; antenna++) {
            if (strstr(sentence->field[i], gps_time_antenna_texts[antenna]) == NULL) {
                continue;
            }
            if (antenna != (int)gps_time_stats.antenna) {
                gps_time_stats.antenna = (gps_time_antenna_t)antenna;
                if (antenna == GPS_TIME_ANTENNA_OK) {
                    ESP_LOGI(TAG, "Module reports %s", gps_time_antenna_texts[antenna]);
                } else {
                    ESP_LOGW(TAG, "Module reports %s", gps_time_antenna_texts[antenna]);
                }
            }
            return;
        }
    }
}


static void gps_time_handle_line(char *line, size_t len)
{
    size_t body_len = 0;
    if (!gps_nmea_verify(line, len, &body_len)) {
        gps_time_stats.checksum_errors++;
        return;
    }
    gps_time_stats.sentences++;
    gps_time_link.sentence_seen = true;
    line[body_len] = '\0';                  // cut the checksum off: "$xxRMC,...,..."

    gps_nmea_sentence_t sentence;
    if (!gps_nmea_split(line, &sentence)) {
        return;                             // more fields than any sentence handled here has
    }

    if (gps_nmea_is_type(&sentence, GPS_TIME_NMEA_TYPE_RMC)) {
        gps_time_handle_rmc(&sentence);
        gps_sky_handle_rmc(&sentence);
    } else if (gps_nmea_is_type(&sentence, GPS_TIME_NMEA_TYPE_GGA)) {
        gps_sky_handle_gga(&sentence);
    } else if (gps_nmea_is_type(&sentence, GPS_TIME_NMEA_TYPE_GSA)) {
        gps_sky_handle_gsa(&sentence);
    } else if (gps_nmea_is_type(&sentence, GPS_TIME_NMEA_TYPE_GSV)) {
        gps_sky_handle_gsv(&sentence);
    } else if (gps_nmea_is_type(&sentence, GPS_TIME_NMEA_TYPE_TXT)) {
        gps_time_handle_txt(&sentence);
    }
}


// Assembles "$...*hh" lines; a '$' always starts a new one, CR or LF ends it.
static void gps_time_feed(char c)
{
    if (c == GPS_NMEA_START) {
        gps_time_line.text[0] = c;
        gps_time_line.len = 1;
        gps_time_line.active = true;
        return;
    }
    if (!gps_time_line.active) {
        return;
    }
    if ((c == '\r') || (c == '\n')) {
        gps_time_line.text[gps_time_line.len] = '\0';
        gps_time_line.active = false;
        gps_time_handle_line(gps_time_line.text, gps_time_line.len);
        return;
    }
    if (gps_time_line.len >= GPS_TIME_NMEA_LINE_MAX) {
        gps_time_line.active = false;       // longer than any NMEA sentence: drop it
        return;
    }
    gps_time_line.text[gps_time_line.len] = c;
    gps_time_line.len++;
}


// Sends "$<body>*hh" with its checksum computed here. A failure is logged; the link state
// machine recovers from whatever it costs.
static void gps_time_send_command(const char *body)
{
    char command[GPS_TIME_NMEA_COMMAND_MAX];

    int len = gps_nmea_format_command(body, command, sizeof(command));
    if (len < 0) {
        ESP_LOGE(TAG, "$%s does not fit the command buffer", body);
        return;
    }

    int written = uart_write_bytes(GPS_TIME_UART_NUM, command, (size_t)len);
    if (written != len) {
        ESP_LOGE(TAG, "Failed to send $%s (%d of %d bytes)", body, written, len);
        return;
    }
    ESP_LOGI(TAG, "Sent %.*s", (int)strcspn(command, "\r\n"), command);
}


// Moves the UART to `baud`, dropping what was received at the old rate.
static void gps_time_link_set_baud(uint32_t baud)
{
    esp_err_t err = uart_set_baudrate(GPS_TIME_UART_NUM, baud);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_baudrate(%" PRIu32 ") failed: %s", baud, esp_err_to_name(err));
    } else {
        gps_time_link.baud = baud;
    }

    err = uart_flush_input(GPS_TIME_UART_NUM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_flush_input failed: %s", esp_err_to_name(err));
    }
    gps_time_line.active = false;
}


static void gps_time_link_enter(gps_time_link_state_t state)
{
    gps_time_link.state = state;
    gps_time_link.state_us = esp_timer_get_time();
    gps_time_link.sentence_us = gps_time_link.state_us;
    gps_time_link.sentence_seen = false;
}


// PROBE_115200: a valid sentence within 3 s means RUNNING, none means PROBE_9600.
// PROBE_9600: a valid sentence within 3 s means the module is there at its default rate - it is
// told to switch and PROBE_115200 checks that it did; none means PROBE_115200 again.
// RUNNING: 3 s without a valid sentence means PROBE_115200.
static void gps_time_link_step(void)
{
    int64_t now_us = esp_timer_get_time();
    bool seen = gps_time_link.sentence_seen;
    bool timed_out = ((now_us - gps_time_link.state_us) >= GPS_TIME_LINK_TIMEOUT_US);

    gps_time_link.sentence_seen = false;

    switch (gps_time_link.state) {
        case GPS_TIME_LINK_STATE_PROBE_115200: {
            if (seen) {
                ESP_LOGI(TAG, "NMEA link up at %" PRIu32 " baud", gps_time_link.baud);
                gps_time_link.absent_logged = false;
                gps_time_link_enter(GPS_TIME_LINK_STATE_RUNNING);
                gps_time_send_command(GPS_TIME_PCAS03_BODY);
            } else if (timed_out) {
                gps_time_link_set_baud(GPS_TIME_BAUD_DEFAULT);
                gps_time_link_enter(GPS_TIME_LINK_STATE_PROBE_9600);
            }
            break;
        }
        case GPS_TIME_LINK_STATE_PROBE_9600: {
            if (seen) {
                ESP_LOGI(TAG, "Module found at %d baud, moving it to %d", GPS_TIME_BAUD_DEFAULT, GPS_TIME_BAUD_FAST);
                gps_time_send_command(GPS_TIME_PCAS01_BODY);
                esp_err_t err = uart_wait_tx_done(GPS_TIME_UART_NUM, pdMS_TO_TICKS(GPS_TIME_UART_TX_DONE_TIMEOUT_MS));
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "uart_wait_tx_done failed: %s", esp_err_to_name(err));
                }
                gps_time_link_set_baud(GPS_TIME_BAUD_FAST);
                gps_time_link_enter(GPS_TIME_LINK_STATE_PROBE_115200);
            } else if (timed_out) {
                // Once per absence: with no module fitted this branch runs every ~6 s forever.
                if (!gps_time_link.absent_logged) {
                    ESP_LOGW(TAG, "No valid NMEA at %d or %d baud, looking again", GPS_TIME_BAUD_FAST,
                             GPS_TIME_BAUD_DEFAULT);
                    gps_time_link.absent_logged = true;
                }
                gps_time_link_set_baud(GPS_TIME_BAUD_FAST);
                gps_time_link_enter(GPS_TIME_LINK_STATE_PROBE_115200);
            }
            break;
        }
        case GPS_TIME_LINK_STATE_RUNNING: {
            if (seen) {
                gps_time_link.sentence_us = now_us;
            } else if ((now_us - gps_time_link.sentence_us) >= GPS_TIME_LINK_TIMEOUT_US) {
                ESP_LOGW(TAG, "NMEA link lost: no valid sentence for %lld s",
                         GPS_TIME_LINK_TIMEOUT_US / GPS_TIME_US_PER_S);
                gps_time_link_set_baud(GPS_TIME_BAUD_FAST);
                gps_time_link_enter(GPS_TIME_LINK_STATE_PROBE_115200);
            }
            break;
        }
        default: {
            gps_time_link_enter(GPS_TIME_LINK_STATE_PROBE_115200);
            break;
        }
    }
}


static const char *gps_time_link_text(void)
{
    return (gps_time_link.state == GPS_TIME_LINK_STATE_RUNNING) ? "ok" : "detecting";
}


static void gps_time_format_utc(int64_t unix_us, char *text, size_t text_len)
{
    time_t unix_sec = (time_t)(unix_us / GPS_TIME_US_PER_S);
    struct tm utc;

    if ((gmtime_r(&unix_sec, &utc) == NULL) || (strftime(text, text_len, "%Y-%m-%d %H:%M:%S", &utc) == 0)) {
        snprintf(text, text_len, "?");
    }
}


static void gps_time_format_delay(char *text, size_t text_len)
{
    if (gps_time_stats.delay_known) {
        snprintf(text, text_len, "%" PRId64 " ms", gps_time_stats.delay_ms);
    } else {
        snprintf(text, text_len, "n/a");
    }
}


// Registers and logs the moments gps_time_now() turns synced and back.
static void gps_time_check_state(void)
{
    int64_t unix_us = 0;
    int64_t ref_unix_us = 0;
    bool now_synced = gps_time_now(&unix_us, &ref_unix_us);

    if (now_synced == gps_time_stats.locked) {
        return;
    }
    gps_time_stats.locked = now_synced;

    char utc[GPS_TIME_UTC_TEXT_LEN];
    char delay[GPS_TIME_DELAY_TEXT_LEN];
    gps_time_format_utc(unix_us, utc, sizeof(utc));
    gps_time_format_delay(delay, sizeof(delay));

    if (now_synced) {
        gps_time_stats.lock_count++;
        gps_time_stats.lock_start_us = esp_timer_get_time();
        ESP_LOGI(TAG, "Locked to GPS: %s UTC, PPS->RMC delay %s", utc, delay);
    } else {
        gps_time_stats.loss_count++;
        ESP_LOGW(TAG, "Lost GPS lock: %s UTC, last PPS->RMC delay %s", utc, delay);
    }
}


static void gps_time_log_status(void)
{
    portENTER_CRITICAL(&gps_time_lock);
    uint32_t pps_count = gps_time_edge.pps_count;
    portEXIT_CRITICAL(&gps_time_lock);

    char delay[GPS_TIME_DELAY_TEXT_LEN];
    gps_time_format_delay(delay, sizeof(delay));

    ESP_LOGI(TAG, "Status: %s, link %s at %" PRIu32 " baud, PPS %" PRIu32 ", sentences %" PRIu32 " (%" PRIu32
             " checksum errors), RMC %" PRIu32 " valid / %" PRIu32 " invalid, last PPS->RMC delay %s",
             gps_time_stats.locked ? "synced" : "not synced", gps_time_link_text(), gps_time_link.baud, pps_count,
             gps_time_stats.sentences, gps_time_stats.checksum_errors, gps_time_stats.rmc_valid,
             gps_time_stats.rmc_invalid, delay);
}


static void gps_time_task(void *arg)
{
    (void)arg;

    uint8_t rx[GPS_TIME_UART_READ_CHUNK];
    int64_t status_due_us = esp_timer_get_time() + GPS_TIME_STATUS_PERIOD_US;

    while (1) {
        // uart_read_bytes() waits for the whole requested length, so ask for exactly what is
        // buffered - or for one byte, with the timeout, when nothing is. A line is then handled
        // as soon as its last byte reaches the driver.
        size_t buffered = 0;
        if (uart_get_buffered_data_len(GPS_TIME_UART_NUM, &buffered) != ESP_OK) {
            buffered = 0;
        }
        size_t wanted = (buffered == 0) ? 1 : ((buffered < sizeof(rx)) ? buffered : sizeof(rx));

        int received = uart_read_bytes(GPS_TIME_UART_NUM, rx, wanted, pdMS_TO_TICKS(GPS_TIME_UART_READ_TIMEOUT_MS));
        if (received < 0) {
            ESP_LOGE(TAG, "uart_read_bytes failed");
            vTaskDelay(pdMS_TO_TICKS(GPS_TIME_UART_READ_TIMEOUT_MS));
            received = 0;
        }

        xSemaphoreTake(gps_time_mutex, portMAX_DELAY);

        gps_time_stats.nmea_bytes += (uint64_t)received;
        for (int i = 0; i < received; i++) {
            gps_time_feed((char)rx[i]);
            // A module found at 9600 baud is about to be moved to 115200: the rest of the chunk
            // belongs to the rate being left.
            if ((gps_time_link.state == GPS_TIME_LINK_STATE_PROBE_9600) && gps_time_link.sentence_seen) {
                break;
            }
        }

        gps_time_link_step();
        gps_time_check_state();

        int64_t now_us = esp_timer_get_time();
        if (now_us >= status_due_us) {
            gps_time_log_status();
            status_due_us = now_us + GPS_TIME_STATUS_PERIOD_US;
        }

        xSemaphoreGive(gps_time_mutex);
    }
}


static void gps_time_fill_intervals(gps_time_status_t *status, const gps_time_pps_history_t *history)
{
    status->interval_count = history->count;
    if (history->count == 0) {
        return;
    }

    int64_t min_us = INT64_MAX;
    int64_t max_us = INT64_MIN;
    int64_t sane_sum_us = 0;
    uint32_t sane_count = 0;
    for (uint32_t i = 0; i < history->count; i++) {
        int64_t interval_us = history->interval_us[i];
        if (interval_us < min_us) {
            min_us = interval_us;
        }
        if (interval_us > max_us) {
            max_us = interval_us;
        }
        if ((interval_us >= GPS_TIME_PPS_INTERVAL_SANE_MIN_US) && (interval_us <= GPS_TIME_PPS_INTERVAL_SANE_MAX_US)) {
            sane_sum_us += interval_us;
            sane_count++;
        }
    }

    uint32_t last = (history->next == 0) ? (GPS_TIME_PPS_HISTORY_LEN - 1) : (history->next - 1);
    status->interval_last_us = history->interval_us[last];
    status->interval_min_us = min_us;
    status->interval_max_us = max_us;
    status->interval_avg_known = (sane_count > 0);
    status->interval_avg_us = (sane_count > 0) ? ((double)sane_sum_us / (double)sane_count) : 0.0;
}


void gps_time_get_status(gps_time_status_t *status)
{
    memset(status, 0, sizeof(*status));

    // The reader task's half first, so that its lock start predates the "now" read below.
    // Without the mutex the task was never started and nothing writes this state.
    if (gps_time_mutex != NULL) {
        xSemaphoreTake(gps_time_mutex, portMAX_DELAY);
    }
    bool locked = gps_time_stats.locked;
    int64_t lock_start_us = gps_time_stats.lock_start_us;
    status->lock_count = gps_time_stats.lock_count;
    status->loss_count = gps_time_stats.loss_count;
    status->rmc_delay_known = gps_time_stats.delay_known;
    status->rmc_delay_ms = gps_time_stats.delay_ms;
    status->baud = gps_time_link.baud;
    status->link_ok = (gps_time_link.state == GPS_TIME_LINK_STATE_RUNNING);
    status->nmea_bytes = gps_time_stats.nmea_bytes;
    status->sentences = gps_time_stats.sentences;
    status->checksum_errors = gps_time_stats.checksum_errors;
    status->rmc_valid = gps_time_stats.rmc_valid;
    status->rmc_invalid = gps_time_stats.rmc_invalid;
    status->antenna = gps_time_stats.antenna;
    if (gps_time_mutex != NULL) {
        xSemaphoreGive(gps_time_mutex);
    }

    portENTER_CRITICAL(&gps_time_lock);
    int64_t now_us = esp_timer_get_time();
    gps_time_edge_t edge = gps_time_edge;
    gps_time_pps_history_t history = gps_time_pps_history;
    portEXIT_CRITICAL(&gps_time_lock);

    int64_t ref_unix_us = 0;
    status->synced = gps_time_derive(&edge, now_us, &status->unix_us, &ref_unix_us);
    status->anchored = edge.anchor_valid;
    status->anchor_age_us = edge.anchor_valid ? (now_us - edge.anchor_pps_us) : 0;
    status->locked = (status->synced && locked);
    status->locked_for_us = status->locked ? (now_us - lock_start_us) : 0;

    status->pps_count = edge.pps_count;
    status->last_pps_age_us = (edge.pps_count > 0) ? (now_us - edge.pps_us) : 0;
    gps_time_fill_intervals(status, &history);
}


static esp_err_t gps_time_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = GPS_TIME_BAUD_FAST,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(GPS_TIME_UART_NUM, GPS_TIME_UART_RX_BUFFER_SIZE, GPS_TIME_UART_TX_BUFFER_SIZE,
                                        0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(GPS_TIME_UART_NUM, &config);
    if (err == ESP_OK) {
        err = uart_set_pin(GPS_TIME_UART_NUM, GPS_TIME_UART_TX_PIN, GPS_TIME_UART_RX_PIN, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d setup failed: %s", GPS_TIME_UART_NUM, esp_err_to_name(err));
        uart_driver_delete(GPS_TIME_UART_NUM);
        return err;
    }
    return ESP_OK;
}


static esp_err_t gps_time_pps_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << GPS_TIME_PPS_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };

    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PPS GPIO%d config failed: %s", GPS_TIME_PPS_PIN, esp_err_to_name(err));
        return err;
    }

    // An IRAM interrupt keeps firing while the flash cache is off (NVS writes, OTA), so an edge
    // is never timestamped late by a flash operation; the handler and esp_timer_get_time() are
    // both in IRAM. ESP_ERR_INVALID_STATE means the service is already installed.
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(GPS_TIME_PPS_PIN, gps_time_pps_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PPS handler on GPIO%d failed: %s", GPS_TIME_PPS_PIN, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}


void gps_time_start(void)
{
    gps_time_mutex = xSemaphoreCreateMutex();
    if (gps_time_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create the reader mutex");
        return;
    }
    // Only the statistics page needs it; the timebase runs without, and gps_sky logs the failure.
    (void)gps_sky_init();

    if (gps_time_uart_init() != ESP_OK) {
        return;
    }
    if (gps_time_pps_init() != ESP_OK) {
        return;
    }
    gps_time_link_enter(GPS_TIME_LINK_STATE_PROBE_115200);

    BaseType_t ret = xTaskCreate(gps_time_task, "gps_time_task", GPS_TIME_TASK_STACK_SIZE, NULL,
                                 GPS_TIME_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the NMEA reader task");
        return;
    }

    ESP_LOGI(TAG, "Started: NMEA on UART%d (TX GPIO%d, RX GPIO%d, 8N1, %d or %d baud), PPS on GPIO%d",
             GPS_TIME_UART_NUM, GPS_TIME_UART_TX_PIN, GPS_TIME_UART_RX_PIN, GPS_TIME_BAUD_FAST, GPS_TIME_BAUD_DEFAULT,
             GPS_TIME_PPS_PIN);
}

#endif // GPS_NTP_ENABLED
