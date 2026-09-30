#include "sdr_http.h"
#include "sdr_agc.h"
#include "sdr_radio.h"
#include "sdr_spectrum.h"
#include "array_size.h"
#include "auth.h"
#include "json_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>

#define SDR_HTTP_QUERY_BUF_SIZE             128
#define SDR_HTTP_VALUE_BUF_SIZE             16
#define SDR_HTTP_NUMBER_BASE                10

#define SDR_HTTP_STATUS_INTERNAL_ERROR      "500 Internal Server Error"

#define SDR_HTTP_GAIN_AUTO                  (-1)        // gain=-1: the software AGC picks the gain
#define SDR_HTTP_GAIN_HEADER                "X-Sdr-Gain"
#define SDR_HTTP_GAIN_HEADER_BUF_SIZE       12          // Any int in decimal, sign and NUL included
#define SDR_HTTP_DWELL_MAX_MS               2000
#define SDR_HTTP_US_PER_MS                  1000

#define SDR_HTTP_NETWORKS_MAX               32
#define SDR_HTTP_BSSID_BUF_SIZE             18          // "aa:bb:cc:dd:ee:ff" and the NUL

#define SDR_HTTP_KHZ_PER_MHZ                1000
#define SDR_HTTP_BIN_KHZ                    ((double)SDR_RADIO_SAMPLE_RATE_KHZ / SDR_SPECTRUM_FFT_SIZE)
// A scan keeps the centre +-30 MHz of each capture, where the widest analog filter is flat
#define SDR_HTTP_STEP_HALF_MHZ              30
#define SDR_HTTP_STEP_MHZ                   (2 * SDR_HTTP_STEP_HALF_MHZ)
#define SDR_HTTP_STEP_BINS                \
    (((SDR_HTTP_STEP_MHZ * SDR_HTTP_KHZ_PER_MHZ) * SDR_SPECTRUM_FFT_SIZE) / SDR_RADIO_SAMPLE_RATE_KHZ)
#define SDR_HTTP_STEP_FIRST_BIN             ((SDR_SPECTRUM_FFT_SIZE - SDR_HTTP_STEP_BINS) / 2)

// Measured on the device: only Wi-Fi channel centres tune. The RF frequency offset path lands about 7 MHz
// above channel 1 whatever frequency it is asked for, so every scan is the same two steps on channels 1 and
// 13, 60 MHz apart: 2382..2502 MHz with neither gap nor overlap
#define SDR_HTTP_STEP_CHANNEL_1_MHZ         2412
#define SDR_HTTP_STEP_CHANNEL_13_MHZ        2472
#define SDR_HTTP_START_MHZ                  (SDR_HTTP_STEP_CHANNEL_1_MHZ - SDR_HTTP_STEP_HALF_MHZ)
#define SDR_HTTP_STOP_MHZ                   (SDR_HTTP_STEP_CHANNEL_13_MHZ + SDR_HTTP_STEP_HALF_MHZ)

static const char *TAG = "sdr_http";

static const unsigned sdr_http_steps_mhz[] = {SDR_HTTP_STEP_CHANNEL_1_MHZ, SDR_HTTP_STEP_CHANNEL_13_MHZ};
static int8_t sdr_http_step_db[SDR_HTTP_STEP_BINS];     // Per-bin maximum over the captures of a step
static int8_t sdr_http_capture_db[SDR_HTTP_STEP_BINS];  // Spectrum of one capture
static wifi_ap_record_t sdr_http_networks[SDR_HTTP_NETWORKS_MAX];


// Strict: the whole value must be a decimal integer, optionally negative - no blanks, no '+', no suffix
static bool sdr_http_query_int(const char *query, const char *key, long *value)
{
    char buf[SDR_HTTP_VALUE_BUF_SIZE] = {0};
    if (httpd_query_key_value(query, key, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    if ((buf[0] != '-') && (!isdigit((unsigned char)buf[0]))) {
        return false;
    }

    char *end = NULL;
    errno = 0;
    long parsed = strtol(buf, &end, SDR_HTTP_NUMBER_BASE);
    if ((errno != 0) || (end == buf) || (*end != '\0')) {
        return false;
    }

    *value = parsed;
    return true;
}

// One scan step into sdr_http_step_db: captures at centre_mhz until dwell_us has passed since the first capture
// (dwell 0: exactly one) and keeps each bin's maximum. Every capture also feeds agc_measure unless it is NULL.
static esp_err_t sdr_http_capture_step(unsigned centre_mhz, int gain, int64_t dwell_us,
                                       sdr_agc_measure_t *agc_measure)
{
    for (size_t i = 0; i < SDR_HTTP_STEP_BINS; i++) {
        sdr_http_step_db[i] = INT8_MIN;
    }

    int64_t start_us = esp_timer_get_time();
    do {
        const uint32_t *words = NULL;
        esp_err_t ret = sdr_radio_capture(centre_mhz, gain, &words);
        if (ret != ESP_OK) {
            return ret;
        }
        if (agc_measure != NULL) {
            sdr_agc_measure_add(agc_measure, words, SDR_RADIO_MAX_SAMPLES);
        }

        sdr_spectrum_compute_db(words, SDR_RADIO_MAX_SAMPLES, SDR_HTTP_STEP_FIRST_BIN, SDR_HTTP_STEP_BINS,
                                sdr_http_capture_db);
        for (size_t i = 0; i < SDR_HTTP_STEP_BINS; i++) {
            if (sdr_http_capture_db[i] > sdr_http_step_db[i]) {
                sdr_http_step_db[i] = sdr_http_capture_db[i];
            }
        }
    } while ((esp_timer_get_time() - start_us) < dwell_us);

    return ESP_OK;
}

static const char *sdr_http_second_name(wifi_second_chan_t second)
{
    switch (second) {
        case WIFI_SECOND_CHAN_ABOVE:
            return "above";
        case WIFI_SECOND_CHAN_BELOW:
            return "below";
        default:
            return "none";
    }
}


esp_err_t sdr_http_info_get_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *response_json = cJSON_CreateObject();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_send_error(req, "Failed to create response");
        return ESP_OK;
    }

    cJSON_AddNumberToObject(response_json, "gain_max", sdr_radio_get_gain_max());
    cJSON_AddNumberToObject(response_json, "start_mhz", SDR_HTTP_START_MHZ);
    cJSON_AddNumberToObject(response_json, "stop_mhz", SDR_HTTP_STOP_MHZ);
    cJSON_AddNumberToObject(response_json, "bin_khz", SDR_HTTP_BIN_KHZ);
    cJSON_AddNumberToObject(response_json, "dwell_max_ms", SDR_HTTP_DWELL_MAX_MS);

    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}


esp_err_t sdr_http_scan_get_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    char query[SDR_HTTP_QUERY_BUF_SIZE] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        json_utils_send_error(req, "invalid_query");
        return ESP_OK;
    }

    long gain = 0;
    if ((!sdr_http_query_int(query, "gain", &gain)) ||
        ((gain != SDR_HTTP_GAIN_AUTO) && ((gain < 0) || (gain > sdr_radio_get_gain_max()))))
    {
        json_utils_send_error(req, "invalid_gain");
        return ESP_OK;
    }

    long dwell_ms = 0;
    if ((!sdr_http_query_int(query, "dwell_ms", &dwell_ms)) || (dwell_ms < 0) || (dwell_ms > SDR_HTTP_DWELL_MAX_MS)) {
        json_utils_send_error(req, "invalid_dwell");
        return ESP_OK;
    }

    // Every capture of an auto scan uses the same gain, and the AGC moves only after the whole scan
    bool auto_gain = (gain == SDR_HTTP_GAIN_AUTO);
    int scan_gain = auto_gain ? sdr_agc_get_gain() : (int)gain;
    int64_t dwell_us = (int64_t)dwell_ms * SDR_HTTP_US_PER_MS;
    sdr_agc_measure_t agc_measure;
    sdr_agc_measure_reset(&agc_measure);
    char gain_header[SDR_HTTP_GAIN_HEADER_BUF_SIZE] = {0};  // httpd keeps the pointer until the first chunk
    size_t bins_sent = 0;

    // Runs on the single httpd task, so captures never overlap and the words stay ours until the next one
    for (size_t step = 0; step < ARRAY_SIZE(sdr_http_steps_mhz); step++) {
        unsigned centre_mhz = sdr_http_steps_mhz[step];
        esp_err_t ret = sdr_http_capture_step(centre_mhz, scan_gain, dwell_us, (auto_gain ? &agc_measure : NULL));
        if (ret != ESP_OK) {
            if (step == 0) {
                const char *error = (ret == ESP_ERR_TIMEOUT) ? "capture_timeout" : "capture_memory";
                json_utils_send_error_status(req, SDR_HTTP_STATUS_INTERNAL_ERROR, error);
                return ESP_OK;
            }
            // The body ends short of the full scan, which the client detects
            ESP_LOGW(TAG, "Scan stopped at %u MHz after %u bins: %s", centre_mhz, (unsigned)bins_sent,
                     esp_err_to_name(ret));
            break;
        }
        if (step == 0) {
            httpd_resp_set_type(req, "application/octet-stream");
            snprintf(gain_header, sizeof(gain_header), "%d", scan_gain);
            httpd_resp_set_hdr(req, SDR_HTTP_GAIN_HEADER, gain_header);
        }

        if (httpd_resp_send_chunk(req, (const char *)sdr_http_step_db, SDR_HTTP_STEP_BINS) != ESP_OK) {
            return ESP_FAIL;
        }
        bins_sent += SDR_HTTP_STEP_BINS;
    }

    // A scan cut short measured only part of the band, so it leaves the AGC alone
    if (auto_gain && (bins_sent == (ARRAY_SIZE(sdr_http_steps_mhz) * SDR_HTTP_STEP_BINS))) {
        sdr_agc_update(&agc_measure, sdr_radio_get_gain_max());
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}


esp_err_t sdr_http_networks_get_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    // Runs on the single httpd task, the same one that captures, so the scan never overlaps a capture
    uint16_t count = SDR_HTTP_NETWORKS_MAX;
    if (sdr_radio_scan_networks(sdr_http_networks, &count) != ESP_OK) {
        json_utils_send_error_status(req, SDR_HTTP_STATUS_INTERNAL_ERROR, "scan_failed");
        return ESP_OK;
    }

    cJSON *response_json = cJSON_CreateArray();
    if (response_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        json_utils_send_error(req, "Failed to create response");
        return ESP_OK;
    }

    for (uint16_t i = 0; i < count; i++) {
        const wifi_ap_record_t *record = &sdr_http_networks[i];
        cJSON *network_json = cJSON_CreateObject();
        if (network_json == NULL) {
            ESP_LOGE(TAG, "Failed to create JSON object for network %u", (unsigned)i);
            continue;
        }

        char bssid[SDR_HTTP_BSSID_BUF_SIZE] = {0};
        snprintf(bssid, sizeof(bssid), MACSTR, MAC2STR(record->bssid));
        cJSON_AddStringToObject(network_json, "ssid", (const char *)record->ssid);
        cJSON_AddStringToObject(network_json, "bssid", bssid);
        cJSON_AddNumberToObject(network_json, "channel", record->primary);
        cJSON_AddStringToObject(network_json, "second", sdr_http_second_name(record->second));
        cJSON_AddNumberToObject(network_json, "rssi", record->rssi);
        cJSON_AddItemToArray(response_json, network_json);
    }

    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}
