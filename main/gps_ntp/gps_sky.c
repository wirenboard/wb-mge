#include "gps_sky.h"

#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "array_size.h"

#define GPS_SKY_STALE_US                    5000000LL   // a list or sentence older than this is left out

#define GPS_SKY_PRN_MIN                     1
#define GPS_SKY_PRN_MAX                     999

// $--GGA,time,lat,N/S,lon,E/W,quality,numSv,HDOP,altitude,M,separation,M,diffAge,diffStation
#define GPS_SKY_GGA_FIELD_QUALITY           6
#define GPS_SKY_GGA_FIELD_SATS_USED         7
#define GPS_SKY_GGA_FIELD_HDOP              8
#define GPS_SKY_GGA_FIELD_ALTITUDE          9
#define GPS_SKY_GGA_QUALITY_MAX             9
#define GPS_SKY_GGA_SATS_USED_MAX           99

// $--GSA,selection,mode,12 x PRN,PDOP,HDOP,VDOP[,systemId]: the PRN slots are always there,
// empty when unused, and NMEA 4.1 appends the system ID.
#define GPS_SKY_GSA_FIELD_MODE              2
#define GPS_SKY_GSA_FIELD_FIRST_PRN         3
#define GPS_SKY_GSA_PRN_SLOTS               12
#define GPS_SKY_GSA_FIELD_PDOP              15
#define GPS_SKY_GSA_FIELD_HDOP              16
#define GPS_SKY_GSA_FIELD_VDOP              17
#define GPS_SKY_GSA_FIELD_SYSTEM_ID         18
#define GPS_SKY_GSA_MODE_MIN                1
#define GPS_SKY_GSA_MODE_MAX                3

// NMEA system IDs (GSA field 18).
#define GPS_SKY_SYSTEM_ID_MIN               1
#define GPS_SKY_SYSTEM_ID_MAX               5

// $--GSV,total,number,inView{,PRN,elevation,azimuth,SNR}[,signalId]
#define GPS_SKY_GSV_FIELD_TOTAL             1
#define GPS_SKY_GSV_FIELD_NUMBER            2
#define GPS_SKY_GSV_FIELD_IN_VIEW           3
#define GPS_SKY_GSV_FIELD_FIRST_SATELLITE   4
#define GPS_SKY_GSV_SATELLITE_FIELDS        4
#define GPS_SKY_GSV_OFFSET_ELEVATION        1
#define GPS_SKY_GSV_OFFSET_AZIMUTH          2
#define GPS_SKY_GSV_OFFSET_SNR              3
#define GPS_SKY_GSV_SENTENCES_MAX           9
#define GPS_SKY_GSV_IN_VIEW_MAX             999
#define GPS_SKY_ELEVATION_MIN               (-90)
#define GPS_SKY_ELEVATION_MAX               90
#define GPS_SKY_AZIMUTH_MIN                 0
#define GPS_SKY_AZIMUTH_MAX                 360
#define GPS_SKY_SNR_MIN                     0
#define GPS_SKY_SNR_MAX                     99

// $--RMC,time,status,lat,N/S,lon,E/W,speed,course,date,...
#define GPS_SKY_RMC_FIELD_STATUS            2
#define GPS_SKY_RMC_FIELD_LATITUDE          3
#define GPS_SKY_RMC_FIELD_LATITUDE_HEMI     4
#define GPS_SKY_RMC_FIELD_LONGITUDE         5
#define GPS_SKY_RMC_FIELD_LONGITUDE_HEMI    6
#define GPS_SKY_RMC_FIELD_SPEED             7
#define GPS_SKY_RMC_FIELD_COURSE            8
#define GPS_SKY_RMC_STATUS_ACTIVE           'A'
#define GPS_SKY_RMC_STATUS_VOID             'V'
#define GPS_SKY_KMH_PER_KNOT                1.852


typedef struct {
    const char *talker;
    gps_sky_system_t system;
} gps_sky_talker_t;

// NMEA 4.0 numbering, for a GSA that names no system of its own.
typedef struct {
    long first;
    long last;
    gps_sky_system_t system;
} gps_sky_prn_range_t;

typedef struct {
    bool present;
    int64_t updated_us;
    gps_sky_value_t quality;
    gps_sky_value_t sats_used;
    gps_sky_value_t hdop;
    gps_sky_value_t alt_m;
} gps_sky_gga_t;

typedef struct {
    bool present;
    int64_t updated_us;
    gps_sky_value_t mode;
    gps_sky_value_t pdop;
    gps_sky_value_t hdop;
    gps_sky_value_t vdop;
} gps_sky_dop_t;

typedef struct {
    bool present;
    int64_t updated_us;
    char status;
    gps_sky_value_t lat;
    gps_sky_value_t lon;
    gps_sky_value_t speed_kmh;
    gps_sky_value_t course_deg;
} gps_sky_rmc_t;

// The satellites one GSA lists as used in the solution.
typedef struct {
    bool present;
    int64_t updated_us;
    size_t count;
    uint8_t system[GPS_SKY_GSA_PRN_SLOTS];
    uint16_t prn[GPS_SKY_GSA_PRN_SLOTS];
} gps_sky_used_t;

// One system's satellites in view, from its latest complete GSV sequence.
typedef struct {
    bool present;
    int64_t updated_us;
    long in_view;
    size_t count;
    gps_sky_satellite_t satellites[GPS_SKY_SYSTEM_SATELLITES_MAX];
} gps_sky_view_t;

// The GSV sequence being received; a system's sequence is sent whole before the next starts.
typedef struct {
    bool active;
    gps_sky_system_t system;
    long total;
    long next;
    long in_view;
    size_t count;
    gps_sky_satellite_t satellites[GPS_SKY_SYSTEM_SATELLITES_MAX];
} gps_sky_assembly_t;

typedef struct {
    gps_sky_gga_t gga;
    gps_sky_dop_t dop;
    gps_sky_rmc_t rmc;
    gps_sky_used_t used[GPS_SKY_SYSTEM_COUNT];     // indexed by the system the GSA was for
    gps_sky_view_t view[GPS_SKY_SYSTEM_COUNT];
    gps_sky_assembly_t assembly;
} gps_sky_state_t;


static const char *TAG = "gps_sky";

static const char *const gps_sky_system_names[GPS_SKY_SYSTEM_COUNT] = {
    [GPS_SKY_SYSTEM_GPS] = "GPS",
    [GPS_SKY_SYSTEM_GLONASS] = "GLONASS",
    [GPS_SKY_SYSTEM_GALILEO] = "Galileo",
    [GPS_SKY_SYSTEM_BEIDOU] = "BeiDou",
    [GPS_SKY_SYSTEM_QZSS] = "QZSS",
    [GPS_SKY_SYSTEM_UNKNOWN] = "unknown",
};

static const gps_sky_talker_t gps_sky_talkers[] = {
    {"GP", GPS_SKY_SYSTEM_GPS},
    {"GL", GPS_SKY_SYSTEM_GLONASS},
    {"GA", GPS_SKY_SYSTEM_GALILEO},
    {"BD", GPS_SKY_SYSTEM_BEIDOU},
    {"GB", GPS_SKY_SYSTEM_BEIDOU},
    {"GQ", GPS_SKY_SYSTEM_QZSS},
};

static const gps_sky_system_t gps_sky_system_ids[GPS_SKY_SYSTEM_ID_MAX + 1] = {
    [0] = GPS_SKY_SYSTEM_UNKNOWN,
    [1] = GPS_SKY_SYSTEM_GPS,
    [2] = GPS_SKY_SYSTEM_GLONASS,
    [3] = GPS_SKY_SYSTEM_GALILEO,
    [4] = GPS_SKY_SYSTEM_BEIDOU,
    [5] = GPS_SKY_SYSTEM_QZSS,
};

static const gps_sky_prn_range_t gps_sky_prn_ranges[] = {
    {1, 64, GPS_SKY_SYSTEM_GPS},                // GPS, then SBAS, which the GP talker also reports
    {65, 96, GPS_SKY_SYSTEM_GLONASS},
    {193, 199, GPS_SKY_SYSTEM_QZSS},
    {201, 263, GPS_SKY_SYSTEM_BEIDOU},
    {301, 336, GPS_SKY_SYSTEM_GALILEO},
};

static SemaphoreHandle_t gps_sky_mutex = NULL;
static gps_sky_state_t gps_sky_state = {0};


esp_err_t gps_sky_init(void)
{
    gps_sky_mutex = xSemaphoreCreateMutex();
    if (gps_sky_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create the mutex: no fix or satellite statistics");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}


const char *gps_sky_system_name(gps_sky_system_t system)
{
    if (system >= GPS_SKY_SYSTEM_COUNT) {
        system = GPS_SKY_SYSTEM_UNKNOWN;
    }
    return gps_sky_system_names[system];
}


static gps_sky_system_t gps_sky_system_from_talker(const gps_nmea_sentence_t *sentence)
{
    for (size_t i = 0; i < ARRAY_SIZE(gps_sky_talkers); i++) {
        if (gps_nmea_talker_is(sentence, gps_sky_talkers[i].talker)) {
            return gps_sky_talkers[i].system;
        }
    }
    return GPS_SKY_SYSTEM_UNKNOWN;
}


static gps_sky_system_t gps_sky_system_from_prn(long prn)
{
    for (size_t i = 0; i < ARRAY_SIZE(gps_sky_prn_ranges); i++) {
        if ((prn >= gps_sky_prn_ranges[i].first) && (prn <= gps_sky_prn_ranges[i].last)) {
            return gps_sky_prn_ranges[i].system;
        }
    }
    return GPS_SKY_SYSTEM_UNKNOWN;
}


static gps_sky_value_t gps_sky_int_value(const char *field, long min, long max)
{
    gps_sky_value_t value = {0};
    long parsed = 0;

    if (gps_nmea_parse_int(field, min, max, &parsed)) {
        value.valid = true;
        value.value = (double)parsed;
    }
    return value;
}


static gps_sky_value_t gps_sky_double_value(const char *field)
{
    gps_sky_value_t value = {0};

    value.valid = gps_nmea_parse_double(field, &value.value);
    return value;
}


static int16_t gps_sky_small_value(const char *field, long min, long max)
{
    long parsed = 0;

    return gps_nmea_parse_int(field, min, max, &parsed) ? (int16_t)parsed : GPS_SKY_NO_VALUE;
}


static bool gps_sky_is_fresh(bool present, int64_t updated_us, int64_t now_us)
{
    return (present && ((now_us - updated_us) < GPS_SKY_STALE_US));
}


static bool gps_sky_lock(void)
{
    return ((gps_sky_mutex != NULL) && (xSemaphoreTake(gps_sky_mutex, portMAX_DELAY) == pdTRUE));
}


static void gps_sky_unlock(void)
{
    xSemaphoreGive(gps_sky_mutex);
}


void gps_sky_handle_gga(const gps_nmea_sentence_t *sentence)
{
    gps_sky_gga_t gga = {
        .present = true,
        .updated_us = esp_timer_get_time(),
        .quality = gps_sky_int_value(gps_nmea_field(sentence, GPS_SKY_GGA_FIELD_QUALITY), 0, GPS_SKY_GGA_QUALITY_MAX),
        .sats_used = gps_sky_int_value(gps_nmea_field(sentence, GPS_SKY_GGA_FIELD_SATS_USED), 0,
                                       GPS_SKY_GGA_SATS_USED_MAX),
        .hdop = gps_sky_double_value(gps_nmea_field(sentence, GPS_SKY_GGA_FIELD_HDOP)),
        .alt_m = gps_sky_double_value(gps_nmea_field(sentence, GPS_SKY_GGA_FIELD_ALTITUDE)),
    };

    if (!gps_sky_lock()) {
        return;
    }
    gps_sky_state.gga = gga;
    gps_sky_unlock();
}


void gps_sky_handle_gsa(const gps_nmea_sentence_t *sentence)
{
    // Without all twelve PRN slots the DOP fields are not where they belong.
    if (sentence->count <= GPS_SKY_GSA_FIELD_VDOP) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    gps_sky_dop_t dop = {
        .present = true,
        .updated_us = now_us,
        .mode = gps_sky_int_value(gps_nmea_field(sentence, GPS_SKY_GSA_FIELD_MODE), GPS_SKY_GSA_MODE_MIN,
                                  GPS_SKY_GSA_MODE_MAX),
        .pdop = gps_sky_double_value(gps_nmea_field(sentence, GPS_SKY_GSA_FIELD_PDOP)),
        .hdop = gps_sky_double_value(gps_nmea_field(sentence, GPS_SKY_GSA_FIELD_HDOP)),
        .vdop = gps_sky_double_value(gps_nmea_field(sentence, GPS_SKY_GSA_FIELD_VDOP)),
    };

    // The system this GSA is for: its NMEA 4.1 system ID, else its talker. A "GN" sentence
    // with no system ID leaves each PRN to be placed by its NMEA 4.0 number range.
    long system_id = 0;
    gps_sky_system_t system = GPS_SKY_SYSTEM_UNKNOWN;
    if (gps_nmea_parse_int(gps_nmea_field(sentence, GPS_SKY_GSA_FIELD_SYSTEM_ID), GPS_SKY_SYSTEM_ID_MIN,
                           GPS_SKY_SYSTEM_ID_MAX, &system_id))
    {
        system = gps_sky_system_ids[system_id];
    } else {
        system = gps_sky_system_from_talker(sentence);
    }

    gps_sky_used_t used = {.present = true, .updated_us = now_us};
    for (size_t i = 0; i < GPS_SKY_GSA_PRN_SLOTS; i++) {
        long prn = 0;
        if (!gps_nmea_parse_int(gps_nmea_field(sentence, GPS_SKY_GSA_FIELD_FIRST_PRN + i), GPS_SKY_PRN_MIN,
                                GPS_SKY_PRN_MAX, &prn))
        {
            continue;
        }
        used.system[used.count] = (uint8_t)((system != GPS_SKY_SYSTEM_UNKNOWN) ? system : gps_sky_system_from_prn(prn));
        used.prn[used.count] = (uint16_t)prn;
        used.count++;
    }

    // The list this sentence replaces: that of its system, or of its first satellite's.
    gps_sky_system_t slot = system;
    if ((slot == GPS_SKY_SYSTEM_UNKNOWN) && (used.count > 0)) {
        slot = (gps_sky_system_t)used.system[0];
    }

    if (!gps_sky_lock()) {
        return;
    }
    gps_sky_state.dop = dop;
    if ((system != GPS_SKY_SYSTEM_UNKNOWN) || (used.count > 0)) {
        gps_sky_state.used[slot] = used;
    }
    gps_sky_unlock();
}


// Appends the satellites of one GSV sentence; a group with no PRN is padding.
static void gps_sky_gsv_append(gps_sky_assembly_t *assembly, const gps_nmea_sentence_t *sentence)
{
    if (sentence->count <= GPS_SKY_GSV_FIELD_FIRST_SATELLITE) {
        return;
    }

    size_t groups = (sentence->count - GPS_SKY_GSV_FIELD_FIRST_SATELLITE) / GPS_SKY_GSV_SATELLITE_FIELDS;
    for (size_t i = 0; i < groups; i++) {
        size_t base = GPS_SKY_GSV_FIELD_FIRST_SATELLITE + (i * GPS_SKY_GSV_SATELLITE_FIELDS);
        long prn = 0;

        if (!gps_nmea_parse_int(gps_nmea_field(sentence, base), GPS_SKY_PRN_MIN, GPS_SKY_PRN_MAX, &prn)) {
            continue;
        }
        if (assembly->count >= GPS_SKY_SYSTEM_SATELLITES_MAX) {
            return;
        }

        gps_sky_satellite_t *satellite = &assembly->satellites[assembly->count];
        satellite->system = (uint8_t)assembly->system;
        satellite->used = false;
        satellite->prn = (uint16_t)prn;
        satellite->elevation = gps_sky_small_value(gps_nmea_field(sentence, base + GPS_SKY_GSV_OFFSET_ELEVATION),
                                                   GPS_SKY_ELEVATION_MIN, GPS_SKY_ELEVATION_MAX);
        satellite->azimuth = gps_sky_small_value(gps_nmea_field(sentence, base + GPS_SKY_GSV_OFFSET_AZIMUTH),
                                                 GPS_SKY_AZIMUTH_MIN, GPS_SKY_AZIMUTH_MAX);
        satellite->snr = gps_sky_small_value(gps_nmea_field(sentence, base + GPS_SKY_GSV_OFFSET_SNR), GPS_SKY_SNR_MIN,
                                             GPS_SKY_SNR_MAX);
        assembly->count++;
    }
}


// Sentences 1..N of a system's sequence are gathered and replace its list at N; a gap drops
// the sequence, and the list stays as it was until the next complete one.
void gps_sky_handle_gsv(const gps_nmea_sentence_t *sentence)
{
    long total = 0;
    long number = 0;
    long in_view = 0;
    if ((!gps_nmea_parse_int(gps_nmea_field(sentence, GPS_SKY_GSV_FIELD_TOTAL), 1, GPS_SKY_GSV_SENTENCES_MAX,
                             &total)) ||
        (!gps_nmea_parse_int(gps_nmea_field(sentence, GPS_SKY_GSV_FIELD_NUMBER), 1, total, &number)) ||
        (!gps_nmea_parse_int(gps_nmea_field(sentence, GPS_SKY_GSV_FIELD_IN_VIEW), 0, GPS_SKY_GSV_IN_VIEW_MAX,
                             &in_view)))
    {
        return;
    }
    gps_sky_system_t system = gps_sky_system_from_talker(sentence);

    if (!gps_sky_lock()) {
        return;
    }

    gps_sky_assembly_t *assembly = &gps_sky_state.assembly;
    if (number == 1) {
        assembly->active = true;
        assembly->system = system;
        assembly->total = total;
        assembly->in_view = in_view;
        assembly->count = 0;
    } else if ((!assembly->active) || (assembly->system != system) || (assembly->total != total) ||
               (assembly->next != number))
    {
        assembly->active = false;
        gps_sky_unlock();
        return;
    }

    gps_sky_gsv_append(assembly, sentence);
    assembly->next = number + 1;

    if (number == total) {
        gps_sky_view_t *view = &gps_sky_state.view[system];
        view->present = true;
        view->updated_us = esp_timer_get_time();
        view->in_view = assembly->in_view;
        view->count = assembly->count;
        memcpy(view->satellites, assembly->satellites, assembly->count * sizeof(assembly->satellites[0]));
        assembly->active = false;
    }

    gps_sky_unlock();
}


void gps_sky_handle_rmc(const gps_nmea_sentence_t *sentence)
{
    gps_sky_rmc_t rmc = {.present = true, .updated_us = esp_timer_get_time()};

    const char *status = gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_STATUS);
    if (((status[0] == GPS_SKY_RMC_STATUS_ACTIVE) || (status[0] == GPS_SKY_RMC_STATUS_VOID)) && (status[1] == '\0')) {
        rmc.status = status[0];
    }

    double lat = 0.0;
    double lon = 0.0;
    if ((gps_nmea_parse_latitude(gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_LATITUDE),
                                 gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_LATITUDE_HEMI), &lat)) &&
        (gps_nmea_parse_longitude(gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_LONGITUDE),
                                  gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_LONGITUDE_HEMI), &lon)))
    {
        rmc.lat = (gps_sky_value_t){.valid = true, .value = lat};
        rmc.lon = (gps_sky_value_t){.valid = true, .value = lon};
    }

    double knots = 0.0;
    if (gps_nmea_parse_double(gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_SPEED), &knots)) {
        rmc.speed_kmh = (gps_sky_value_t){.valid = true, .value = (knots * GPS_SKY_KMH_PER_KNOT)};
    }
    rmc.course_deg = gps_sky_double_value(gps_nmea_field(sentence, GPS_SKY_RMC_FIELD_COURSE));

    if (!gps_sky_lock()) {
        return;
    }
    gps_sky_state.rmc = rmc;
    gps_sky_unlock();
}


static bool gps_sky_is_used(const bool used_fresh[GPS_SKY_SYSTEM_COUNT], uint8_t system, uint16_t prn)
{
    for (size_t slot = 0; slot < GPS_SKY_SYSTEM_COUNT; slot++) {
        if (!used_fresh[slot]) {
            continue;
        }
        const gps_sky_used_t *used = &gps_sky_state.used[slot];
        for (size_t i = 0; i < used->count; i++) {
            if ((used->system[i] == system) && (used->prn[i] == prn)) {
                return true;
            }
        }
    }
    return false;
}


static void gps_sky_fill_fix(gps_sky_fix_t *fix, int64_t now_us)
{
    const gps_sky_rmc_t *rmc = &gps_sky_state.rmc;
    if (gps_sky_is_fresh(rmc->present, rmc->updated_us, now_us)) {
        fix->rmc_status = rmc->status;
        fix->lat = rmc->lat;
        fix->lon = rmc->lon;
        fix->speed_kmh = rmc->speed_kmh;
        fix->course_deg = rmc->course_deg;
    }

    const gps_sky_gga_t *gga = &gps_sky_state.gga;
    if (gps_sky_is_fresh(gga->present, gga->updated_us, now_us)) {
        fix->quality = gga->quality;
        fix->sats_used = gga->sats_used;
        fix->hdop = gga->hdop;
        fix->alt_m = gga->alt_m;
    }

    const gps_sky_dop_t *dop = &gps_sky_state.dop;
    if (gps_sky_is_fresh(dop->present, dop->updated_us, now_us)) {
        fix->mode = dop->mode;
        fix->pdop = dop->pdop;
        fix->vdop = dop->vdop;
        if (dop->hdop.valid) {
            fix->hdop = dop->hdop;
        }
    }
}


void gps_sky_get_snapshot(gps_sky_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));

    if (!gps_sky_lock()) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    gps_sky_fill_fix(&snapshot->fix, now_us);

    bool used_fresh[GPS_SKY_SYSTEM_COUNT];
    for (size_t slot = 0; slot < GPS_SKY_SYSTEM_COUNT; slot++) {
        used_fresh[slot] = gps_sky_is_fresh(gps_sky_state.used[slot].present, gps_sky_state.used[slot].updated_us,
                                            now_us);
    }

    bool any_view = false;
    long in_view = 0;
    for (size_t system = 0; system < GPS_SKY_SYSTEM_COUNT; system++) {
        const gps_sky_view_t *view = &gps_sky_state.view[system];
        if (!gps_sky_is_fresh(view->present, view->updated_us, now_us)) {
            continue;
        }
        any_view = true;
        in_view += view->in_view;

        for (size_t i = 0; i < view->count; i++) {
            gps_sky_satellite_t *satellite = &snapshot->satellites[snapshot->satellite_count];
            *satellite = view->satellites[i];
            satellite->used = gps_sky_is_used(used_fresh, satellite->system, satellite->prn);
            snapshot->satellite_count++;
        }
    }
    if (any_view) {
        snapshot->fix.sats_in_view = (gps_sky_value_t){.valid = true, .value = (double)in_view};
    }

    gps_sky_unlock();
}

#endif // GPS_NTP_ENABLED
