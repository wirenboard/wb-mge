#include "gps_time.h"

#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
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
#include "freertos/task.h"

// The WBE2 slot of WB-MGU: the module's NMEA output and command input on the port-2 UART
// pins, no RTS/CTS.
#define GPS_TIME_UART_NUM                   UART_NUM_2
#define GPS_TIME_UART_TX_PIN                SERIAL_OUTPUT_PIN_2
#define GPS_TIME_UART_RX_PIN                SERIAL_INPUT_PIN_2
#define GPS_TIME_UART_BAUD_RATE             9600
#define GPS_TIME_UART_RX_BUFFER_SIZE        1024    // must exceed the 128-byte hardware FIFO
#define GPS_TIME_UART_TX_BUFFER_SIZE        0       // writes block until the FIFO takes them
#define GPS_TIME_UART_READ_CHUNK            128
#define GPS_TIME_UART_READ_TIMEOUT_MS       100     // also the period of the lock/loss check

// The module's PPS output, on the slot's "RTS" pin. Never driven by the firmware.
#define GPS_TIME_PPS_PIN                    SERIAL_IO_PIN_2

// AT6558 (CASIC) output rate command: RMC once per fix and the antenna status GPTXT, every
// other sentence off. Field order: GGA, GLL, GSA, GSV, RMC, VTG, ZDA, ANT, DHV, LPS, two
// reserved, UTC, GST, three reserved, TIM. The default burst can take longer than a second
// at 9600 baud, so its RMC may land after the NEXT PPS edge; RMC alone is about 70 bytes.
#define GPS_TIME_PCAS03_BODY                "PCAS03,0,0,0,0,1,0,0,1,0,0,,,0,0,,,,0"
#define GPS_TIME_NMEA_COMMAND_MAX           64

#define GPS_TIME_NMEA_LINE_MAX              128     // NMEA itself allows 82 characters
#define GPS_TIME_NMEA_START                 '$'
#define GPS_TIME_NMEA_CHECKSUM_MARK         '*'
#define GPS_TIME_NMEA_FIELD_SEPARATOR       ','
#define GPS_TIME_NMEA_CHECKSUM_DIGITS       2
#define GPS_TIME_NMEA_TYPE_OFFSET           3       // after "$" and the two-letter talker ID
#define GPS_TIME_NMEA_RMC_TYPE              "RMC,"
#define GPS_TIME_NMEA_TXT_PREFIX            "$GPTXT,"
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
// A sentence other than RMC or $GPTXT means the module is back on its default
// output. For this long after one (and after the command is first sent at start), no RMC is
// paired; the command is repeated at most this often.
#define GPS_TIME_FOREIGN_HOLDOFF_US         2000000LL
#define GPS_TIME_CONFIG_RETRY_PERIOD_US     10000000LL
#define GPS_TIME_PPS_TIMEOUT_US             2000000LL
#define GPS_TIME_ANCHOR_TIMEOUT_US          10000000LL

#define GPS_TIME_STATUS_PERIOD_US           60000000LL
#define GPS_TIME_UTC_TEXT_LEN               32
#define GPS_TIME_DELAY_TEXT_LEN             24

#define GPS_TIME_TASK_STACK_SIZE            (4 * 1024)
#define GPS_TIME_TASK_PRIORITY              5


typedef enum {
    GPS_TIME_ANTENNA_UNKNOWN = 0,
    GPS_TIME_ANTENNA_OK,
    GPS_TIME_ANTENNA_OPEN,
    GPS_TIME_ANTENNA_SHORT,
    GPS_TIME_ANTENNA_COUNT
} gps_time_antenna_t;

// Shared between the PPS interrupt, the reader task and gps_time_now() callers on either
// core; every access goes through gps_time_lock.
typedef struct {
    int64_t pps_us;                     // esp_timer time of the latest PPS edge
    uint32_t pps_count;
    bool anchor_valid;
    int64_t anchor_pps_us;              // a PPS edge paired with an RMC...
    int64_t anchor_sec;                 // ...and the unix second that edge started
} gps_time_shared_t;

// Owned by the reader task.
typedef struct {
    uint32_t rmc_valid;
    uint32_t rmc_invalid;
    uint32_t other;
    bool delay_known;
    int64_t delay_ms;                   // PPS edge to the completion of the last valid RMC
    gps_time_antenna_t antenna;
    bool foreign_seen;
    int64_t foreign_us;                 // holdoff start: latest non-RMC/$GPTXT sentence or the boot-time PCAS03
    int64_t config_retry_due_us;
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
static gps_time_shared_t gps_time_shared = {0};
static gps_time_stats_t gps_time_stats = {0};
static gps_time_line_t gps_time_line = {0};


static void gps_time_send_output_config(void);


static void IRAM_ATTR gps_time_pps_isr(void *arg)
{
    (void)arg;

    portENTER_CRITICAL_ISR(&gps_time_lock);
    gps_time_shared.pps_us = esp_timer_get_time();
    gps_time_shared.pps_count++;
    portEXIT_CRITICAL_ISR(&gps_time_lock);
}


bool gps_time_now(int64_t *unix_us, int64_t *ref_unix_us)
{
    // "now" is read inside the lock so that it can never predate the PPS edge read with it.
    portENTER_CRITICAL(&gps_time_lock);
    int64_t now_us = esp_timer_get_time();
    int64_t pps_us = gps_time_shared.pps_us;
    bool anchor_valid = gps_time_shared.anchor_valid;
    int64_t anchor_pps_us = gps_time_shared.anchor_pps_us;
    int64_t anchor_sec = gps_time_shared.anchor_sec;
    portEXIT_CRITICAL(&gps_time_lock);

    if (!anchor_valid) {
        *unix_us = 0;
        *ref_unix_us = 0;
        return false;
    }

    // Whole seconds from the anchor's edge to the latest one: the edges are GPS seconds apart
    // as measured by the crystal, and while synced the anchor is under 10 s old, so rounding
    // absorbs the drift. An older anchor only feeds unsynced (LI=3) answers.
    int64_t pps_elapsed_s = ((pps_us - anchor_pps_us) + GPS_TIME_HALF_SECOND_US) / GPS_TIME_US_PER_S;
    int64_t since_pps_us = now_us - pps_us;

    *ref_unix_us = (anchor_sec + pps_elapsed_s) * GPS_TIME_US_PER_S;
    *unix_us = *ref_unix_us + since_pps_us;

    return ((since_pps_us < GPS_TIME_PPS_TIMEOUT_US) && ((now_us - anchor_pps_us) < GPS_TIME_ANCHOR_TIMEOUT_US));
}


// XOR of every character of data, i.e. of what lies between '$' and '*'.
static uint8_t gps_time_nmea_checksum(const char *data, size_t len)
{
    uint8_t checksum = 0;

    for (size_t i = 0; i < len; i++) {
        checksum ^= (uint8_t)data[i];
    }
    return checksum;
}


// Checks "$<body>*hh" (hh in either case) and returns the length of "$<body>".
static bool gps_time_nmea_verify(const char *line, size_t len, size_t *body_len)
{
    const char *mark = memchr(line, GPS_TIME_NMEA_CHECKSUM_MARK, len);
    if (mark == NULL) {
        return false;
    }

    size_t mark_pos = (size_t)(mark - line);
    if ((mark_pos + 1 + GPS_TIME_NMEA_CHECKSUM_DIGITS) != len) {
        return false;
    }

    char expected[GPS_TIME_NMEA_CHECKSUM_DIGITS + 1];
    snprintf(expected, sizeof(expected), "%02X", (unsigned)gps_time_nmea_checksum(line + 1, mark_pos - 1));
    if (strncasecmp(mark + 1, expected, GPS_TIME_NMEA_CHECKSUM_DIGITS) != 0) {
        return false;
    }

    *body_len = mark_pos;
    return true;
}


// Finds field `index` of a NUL-terminated sentence body (field 0 is the "$xxRMC" ID).
// Empty fields are kept, unlike with strtok().
static bool gps_time_nmea_field(const char *body, unsigned index, const char **field, size_t *field_len)
{
    const char *start = body;

    for (unsigned i = 0; i < index; i++) {
        start = strchr(start, GPS_TIME_NMEA_FIELD_SEPARATOR);
        if (start == NULL) {
            return false;
        }
        start++;
    }

    const char *end = strchr(start, GPS_TIME_NMEA_FIELD_SEPARATOR);
    *field = start;
    *field_len = (end != NULL) ? (size_t)(end - start) : strlen(start);
    return true;
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


// Unix second an RMC body names, when its status is 'A', its time a whole second and its
// date plausible.
static bool gps_time_rmc_to_unix(const char *body, int64_t *unix_sec)
{
    const char *time_field = NULL;
    const char *status_field = NULL;
    const char *date_field = NULL;
    size_t time_len = 0;
    size_t status_len = 0;
    size_t date_len = 0;

    if ((!gps_time_nmea_field(body, GPS_TIME_RMC_FIELD_TIME, &time_field, &time_len)) ||
        (!gps_time_nmea_field(body, GPS_TIME_RMC_FIELD_STATUS, &status_field, &status_len)) ||
        (!gps_time_nmea_field(body, GPS_TIME_RMC_FIELD_DATE, &date_field, &date_len)))
    {
        return false;
    }

    if ((status_len != 1) || (status_field[0] != GPS_TIME_RMC_STATUS_ACTIVE)) {
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
    if ((date_len != (GPS_TIME_NMEA_PAIR_COUNT * GPS_TIME_NMEA_PAIR_LEN)) ||
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


// Pairs a valid RMC with the latest PPS edge, which it names if it completed soon enough.
static void gps_time_handle_rmc(const char *body)
{
    int64_t rmc_sec = 0;
    if (!gps_time_rmc_to_unix(body, &rmc_sec)) {
        gps_time_stats.rmc_invalid++;
        return;
    }
    gps_time_stats.rmc_valid++;

    portENTER_CRITICAL(&gps_time_lock);
    int64_t now_us = esp_timer_get_time();
    int64_t pps_us = gps_time_shared.pps_us;
    bool pps_seen = (gps_time_shared.pps_count > 0);
    // In a default-output burst this RMC may have landed after the next edge already.
    bool short_burst = ((!gps_time_stats.foreign_seen) ||
                        ((now_us - gps_time_stats.foreign_us) >= GPS_TIME_FOREIGN_HOLDOFF_US));
    if (pps_seen && short_burst && ((now_us - pps_us) < GPS_TIME_PAIR_WINDOW_US)) {
        gps_time_shared.anchor_valid = true;
        gps_time_shared.anchor_pps_us = pps_us;
        gps_time_shared.anchor_sec = rmc_sec;
    }
    portEXIT_CRITICAL(&gps_time_lock);

    gps_time_stats.delay_known = pps_seen;
    gps_time_stats.delay_ms = (now_us - pps_us) / GPS_TIME_US_PER_MS;
}


// Logs the antenna status the module reports in "$GPTXT,...ANTENNA OPEN|OK|SHORT" on change.
static void gps_time_handle_txt(const char *body)
{
    for (int antenna = GPS_TIME_ANTENNA_OK; antenna < GPS_TIME_ANTENNA_COUNT; antenna++) {
        if (strstr(body, gps_time_antenna_texts[antenna]) == NULL) {
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


static void gps_time_handle_line(char *line, size_t len)
{
    size_t body_len = 0;
    if (!gps_time_nmea_verify(line, len, &body_len)) {
        return;
    }
    line[body_len] = '\0';                  // cut the checksum off: "$xxRMC,...,..."

    if ((body_len >= (GPS_TIME_NMEA_TYPE_OFFSET + strlen(GPS_TIME_NMEA_RMC_TYPE))) &&
        (strncmp(line + GPS_TIME_NMEA_TYPE_OFFSET, GPS_TIME_NMEA_RMC_TYPE, strlen(GPS_TIME_NMEA_RMC_TYPE)) == 0))
    {
        gps_time_handle_rmc(line);
        return;
    }

    gps_time_stats.other++;
    if (strncmp(line, GPS_TIME_NMEA_TXT_PREFIX, strlen(GPS_TIME_NMEA_TXT_PREFIX)) == 0) {
        gps_time_handle_txt(line);
        return;
    }

    // The module did not take the command at boot, or has restarted since: hold off pairing and
    // repeat the command.
    int64_t now_us = esp_timer_get_time();
    gps_time_stats.foreign_seen = true;
    gps_time_stats.foreign_us = now_us;
    if (now_us >= gps_time_stats.config_retry_due_us) {
        gps_time_send_output_config();
        gps_time_stats.config_retry_due_us = now_us + GPS_TIME_CONFIG_RETRY_PERIOD_US;
    }
}


// Assembles "$...*hh" lines; a '$' always starts a new one, CR or LF ends it.
static void gps_time_feed(char c)
{
    if (c == GPS_TIME_NMEA_START) {
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


static void gps_time_check_state(bool *synced)
{
    int64_t unix_us = 0;
    int64_t ref_unix_us = 0;
    bool now_synced = gps_time_now(&unix_us, &ref_unix_us);

    if (now_synced == *synced) {
        return;
    }
    *synced = now_synced;

    char utc[GPS_TIME_UTC_TEXT_LEN];
    char delay[GPS_TIME_DELAY_TEXT_LEN];
    gps_time_format_utc(unix_us, utc, sizeof(utc));
    gps_time_format_delay(delay, sizeof(delay));

    if (now_synced) {
        ESP_LOGI(TAG, "Locked to GPS: %s UTC, PPS->RMC delay %s", utc, delay);
    } else {
        ESP_LOGW(TAG, "Lost GPS lock: %s UTC, last PPS->RMC delay %s", utc, delay);
    }
}


static void gps_time_log_status(bool synced)
{
    portENTER_CRITICAL(&gps_time_lock);
    uint32_t pps_count = gps_time_shared.pps_count;
    portEXIT_CRITICAL(&gps_time_lock);

    char delay[GPS_TIME_DELAY_TEXT_LEN];
    gps_time_format_delay(delay, sizeof(delay));

    ESP_LOGI(TAG, "Status: %s, PPS %" PRIu32 ", RMC %" PRIu32 " valid / %" PRIu32 " invalid, other sentences %"
             PRIu32 ", last PPS->RMC delay %s", synced ? "synced" : "not synced", pps_count, gps_time_stats.rmc_valid,
             gps_time_stats.rmc_invalid, gps_time_stats.other, delay);
}


static void gps_time_task(void *arg)
{
    (void)arg;

    uint8_t rx[GPS_TIME_UART_READ_CHUNK];
    bool synced = false;
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
        }
        for (int i = 0; i < received; i++) {
            gps_time_feed((char)rx[i]);
        }

        gps_time_check_state(&synced);

        int64_t now_us = esp_timer_get_time();
        if (now_us >= status_due_us) {
            gps_time_log_status(synced);
            status_due_us = now_us + GPS_TIME_STATUS_PERIOD_US;
        }
    }
}


static esp_err_t gps_time_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = GPS_TIME_UART_BAUD_RATE,
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


// Sends $PCAS03 with its checksum computed here. A failure only costs the short burst.
static void gps_time_send_output_config(void)
{
    char command[GPS_TIME_NMEA_COMMAND_MAX];
    const char *body = GPS_TIME_PCAS03_BODY;

    int len = snprintf(command, sizeof(command), "%c%s%c%02X\r\n", GPS_TIME_NMEA_START, body,
                       GPS_TIME_NMEA_CHECKSUM_MARK, (unsigned)gps_time_nmea_checksum(body, strlen(body)));
    if ((len < 0) || ((size_t)len >= sizeof(command))) {
        ESP_LOGE(TAG, "PCAS03 command does not fit its buffer");
        return;
    }

    int written = uart_write_bytes(GPS_TIME_UART_NUM, command, (size_t)len);
    if (written != len) {
        ESP_LOGE(TAG, "Failed to send the PCAS03 command (%d of %d bytes)", written, len);
        return;
    }
    ESP_LOGI(TAG, "Sent %.*s", (int)strcspn(command, "\r\n"), command);
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
    if (gps_time_uart_init() != ESP_OK) {
        return;
    }
    if (gps_time_pps_init() != ESP_OK) {
        return;
    }
    gps_time_send_output_config();

    // Start inside the holdoff: until the command has taken effect the module may still be
    // sending its long default burst, and the first RMC read could belong to the previous edge.
    gps_time_stats.foreign_seen = true;
    gps_time_stats.foreign_us = esp_timer_get_time();

    BaseType_t ret = xTaskCreate(gps_time_task, "gps_time_task", GPS_TIME_TASK_STACK_SIZE, NULL,
                                 GPS_TIME_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the NMEA reader task");
        return;
    }

    ESP_LOGI(TAG, "Started: NMEA on UART%d (TX GPIO%d, RX GPIO%d, %d 8N1), PPS on GPIO%d", GPS_TIME_UART_NUM,
             GPS_TIME_UART_TX_PIN, GPS_TIME_UART_RX_PIN, GPS_TIME_UART_BAUD_RATE, GPS_TIME_PPS_PIN);
}

#endif // GPS_NTP_ENABLED
