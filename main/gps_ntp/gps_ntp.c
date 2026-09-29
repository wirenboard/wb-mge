#include "gps_ntp.h"

#if GPS_NTP_ENABLED

#include <stdlib.h>

#include "cJSON.h"

#include "auth.h"
#include "gps_sky.h"
#include "gps_time.h"
#include "json_utils.h"
#include "ntp_server.h"

#define GPS_NTP_US_PER_MS                   1000
#define GPS_NTP_US_PER_S                    1000000
#define GPS_NTP_ERROR_STATUS                "500 Internal Server Error"


// The server is started even when the timebase could not come up: it then answers every
// request as unsynchronised, which a client reports instead of timing out.
void gps_ntp_start(void)
{
    gps_time_start();
    ntp_server_start();
}


// Adds `key`: value when valid, null otherwise. False when cJSON ran out of memory.
static bool gps_ntp_json_number(cJSON *object, const char *key, bool valid, double value)
{
    cJSON *item = valid ? cJSON_AddNumberToObject(object, key, value) : cJSON_AddNullToObject(object, key);
    return (item != NULL);
}


// Adds `key`: whole units of `divisor` (ms or s from us), truncated, when valid; null otherwise.
static bool gps_ntp_json_units(cJSON *object, const char *key, bool valid, int64_t value_us, int64_t divisor)
{
    int64_t units = value_us / divisor;
    return gps_ntp_json_number(object, key, valid, (double)units);
}


static bool gps_ntp_json_value(cJSON *object, const char *key, const gps_sky_value_t *value)
{
    return gps_ntp_json_number(object, key, value->valid, value->value);
}


static bool gps_ntp_json_small(cJSON *object, const char *key, int16_t value)
{
    return gps_ntp_json_number(object, key, (value != GPS_SKY_NO_VALUE), (double)value);
}


static bool gps_ntp_json_string(cJSON *object, const char *key, const char *value)
{
    cJSON *item = (value != NULL) ? cJSON_AddStringToObject(object, key, value) : cJSON_AddNullToObject(object, key);
    return (item != NULL);
}


static bool gps_ntp_json_bool(cJSON *object, const char *key, bool value)
{
    return (cJSON_AddBoolToObject(object, key, value) != NULL);
}


static bool gps_ntp_json_time(cJSON *root, const gps_time_status_t *timebase)
{
    cJSON *object = cJSON_AddObjectToObject(root, "time");
    if (object == NULL) {
        return false;
    }

    bool ok = gps_ntp_json_bool(object, "synced", timebase->synced);
    ok = gps_ntp_json_number(object, "unix_us", timebase->anchored, (double)timebase->unix_us) && ok;
    ok = gps_ntp_json_units(object, "last_pps_age_ms", (timebase->pps_count > 0), timebase->last_pps_age_us,
                            GPS_NTP_US_PER_MS) && ok;
    ok = gps_ntp_json_units(object, "anchor_age_ms", timebase->anchored, timebase->anchor_age_us,
                            GPS_NTP_US_PER_MS) && ok;
    ok = gps_ntp_json_units(object, "locked_for_s", timebase->locked, timebase->locked_for_us,
                            GPS_NTP_US_PER_S) && ok;
    ok = gps_ntp_json_number(object, "lock_count", true, (double)timebase->lock_count) && ok;
    ok = gps_ntp_json_number(object, "loss_count", true, (double)timebase->loss_count) && ok;
    return ok;
}


static bool gps_ntp_json_pps(cJSON *root, const gps_time_status_t *timebase)
{
    cJSON *object = cJSON_AddObjectToObject(root, "pps");
    if (object == NULL) {
        return false;
    }

    bool intervals = (timebase->interval_count > 0);
    bool ok = gps_ntp_json_number(object, "count", true, (double)timebase->pps_count);
    ok = gps_ntp_json_number(object, "interval_us", intervals, (double)timebase->interval_last_us) && ok;
    ok = gps_ntp_json_number(object, "interval_min_us", intervals, (double)timebase->interval_min_us) && ok;
    ok = gps_ntp_json_number(object, "interval_max_us", intervals, (double)timebase->interval_max_us) && ok;
    ok = gps_ntp_json_number(object, "interval_avg_us", timebase->interval_avg_known, timebase->interval_avg_us) && ok;
    ok = gps_ntp_json_number(object, "rmc_delay_ms", timebase->rmc_delay_known, (double)timebase->rmc_delay_ms) && ok;
    return ok;
}


static bool gps_ntp_json_fix(cJSON *root, const gps_sky_fix_t *fix)
{
    cJSON *object = cJSON_AddObjectToObject(root, "fix");
    if (object == NULL) {
        return false;
    }

    char status[] = {fix->rmc_status, '\0'};
    bool ok = gps_ntp_json_string(object, "rmc_status", (fix->rmc_status != '\0') ? status : NULL);
    ok = gps_ntp_json_value(object, "mode", &fix->mode) && ok;
    ok = gps_ntp_json_value(object, "quality", &fix->quality) && ok;
    ok = gps_ntp_json_value(object, "sats_used", &fix->sats_used) && ok;
    ok = gps_ntp_json_value(object, "sats_in_view", &fix->sats_in_view) && ok;
    ok = gps_ntp_json_value(object, "pdop", &fix->pdop) && ok;
    ok = gps_ntp_json_value(object, "hdop", &fix->hdop) && ok;
    ok = gps_ntp_json_value(object, "vdop", &fix->vdop) && ok;
    ok = gps_ntp_json_value(object, "lat", &fix->lat) && ok;
    ok = gps_ntp_json_value(object, "lon", &fix->lon) && ok;
    ok = gps_ntp_json_value(object, "alt_m", &fix->alt_m) && ok;
    ok = gps_ntp_json_value(object, "speed_kmh", &fix->speed_kmh) && ok;
    ok = gps_ntp_json_value(object, "course_deg", &fix->course_deg) && ok;
    return ok;
}


static bool gps_ntp_json_satellites(cJSON *root, const gps_sky_snapshot_t *sky)
{
    cJSON *array = cJSON_AddArrayToObject(root, "satellites");
    if (array == NULL) {
        return false;
    }

    for (size_t i = 0; i < sky->satellite_count; i++) {
        const gps_sky_satellite_t *satellite = &sky->satellites[i];
        cJSON *object = cJSON_CreateObject();
        if ((object == NULL) || (!cJSON_AddItemToArray(array, object))) {
            cJSON_Delete(object);
            return false;
        }

        bool ok = gps_ntp_json_string(object, "system", gps_sky_system_name((gps_sky_system_t)satellite->system));
        ok = gps_ntp_json_number(object, "prn", true, (double)satellite->prn) && ok;
        ok = gps_ntp_json_small(object, "elevation", satellite->elevation) && ok;
        ok = gps_ntp_json_small(object, "azimuth", satellite->azimuth) && ok;
        ok = gps_ntp_json_small(object, "snr", satellite->snr) && ok;
        ok = gps_ntp_json_bool(object, "used", satellite->used) && ok;
        if (!ok) {
            return false;
        }
    }
    return true;
}


static bool gps_ntp_json_nmea(cJSON *root, const gps_time_status_t *timebase)
{
    cJSON *object = cJSON_AddObjectToObject(root, "nmea");
    if (object == NULL) {
        return false;
    }

    bool ok = gps_ntp_json_number(object, "baud", true, (double)timebase->baud);
    ok = gps_ntp_json_string(object, "link", timebase->link_ok ? "ok" : "detecting") && ok;
    ok = gps_ntp_json_number(object, "bytes", true, (double)timebase->nmea_bytes) && ok;
    ok = gps_ntp_json_number(object, "sentences", true, (double)timebase->sentences) && ok;
    ok = gps_ntp_json_number(object, "checksum_errors", true, (double)timebase->checksum_errors) && ok;
    ok = gps_ntp_json_number(object, "rmc_valid", true, (double)timebase->rmc_valid) && ok;
    ok = gps_ntp_json_number(object, "rmc_invalid", true, (double)timebase->rmc_invalid) && ok;
    return ok;
}


static const char *gps_ntp_antenna_text(gps_time_antenna_t antenna)
{
    switch (antenna) {
        case GPS_TIME_ANTENNA_OK: {
            return "ok";
        }
        case GPS_TIME_ANTENNA_OPEN: {
            return "open";
        }
        case GPS_TIME_ANTENNA_SHORT: {
            return "short";
        }
        default: {
            return "unknown";
        }
    }
}


static bool gps_ntp_json_ntp(cJSON *root, const ntp_server_stats_t *ntp)
{
    cJSON *object = cJSON_AddObjectToObject(root, "ntp");
    if (object == NULL) {
        return false;
    }

    bool ok = gps_ntp_json_number(object, "requests", true, (double)ntp->requests);
    ok = gps_ntp_json_number(object, "answered_synced", true, (double)ntp->answered_synced) && ok;
    ok = gps_ntp_json_number(object, "answered_unsynced", true, (double)ntp->answered_unsynced) && ok;
    ok = gps_ntp_json_number(object, "ignored", true, (double)ntp->ignored) && ok;
    ok = gps_ntp_json_string(object, "last_client", ntp->has_request ? ntp->last_client : NULL) && ok;
    ok = gps_ntp_json_units(object, "last_request_age_ms", ntp->has_request, ntp->last_request_age_us,
                            GPS_NTP_US_PER_MS) && ok;
    return ok;
}


esp_err_t gps_ntp_get_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    // The satellite list alone is a few KB: too much for the httpd task's stack.
    gps_sky_snapshot_t *sky = malloc(sizeof(*sky));
    cJSON *root = cJSON_CreateObject();
    if ((sky == NULL) || (root == NULL)) {
        free(sky);
        cJSON_Delete(root);
        json_utils_send_error_status(req, GPS_NTP_ERROR_STATUS, "Out of memory");
        return ESP_OK;
    }

    gps_time_status_t timebase;
    ntp_server_stats_t ntp;
    gps_time_get_status(&timebase);
    gps_sky_get_snapshot(sky);
    ntp_server_get_stats(&ntp);

    bool ok = gps_ntp_json_time(root, &timebase);
    ok = gps_ntp_json_pps(root, &timebase) && ok;
    ok = gps_ntp_json_fix(root, &sky->fix) && ok;
    ok = gps_ntp_json_satellites(root, sky) && ok;
    ok = gps_ntp_json_nmea(root, &timebase) && ok;
    ok = gps_ntp_json_string(root, "antenna", gps_ntp_antenna_text(timebase.antenna)) && ok;
    ok = gps_ntp_json_ntp(root, &ntp) && ok;
    free(sky);

    if (!ok) {
        cJSON_Delete(root);
        json_utils_send_error_status(req, GPS_NTP_ERROR_STATUS, "Out of memory");
        return ESP_OK;
    }

    json_utils_send_response(req, NULL, root);
    return ESP_OK;
}

#endif // GPS_NTP_ENABLED
