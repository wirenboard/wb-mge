#include "sdr_http.h"
#include "sdr_radio.h"
#include "sdr_spectrum.h"
#include "array_size.h"
#include "auth.h"
#include "json_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <esp_err.h>
#include <esp_log.h>

#define SDR_HTTP_QUERY_BUF_SIZE             128
#define SDR_HTTP_VALUE_BUF_SIZE             16
#define SDR_HTTP_NUMBER_BASE                10

#define SDR_HTTP_STATUS_CAPTURE_FAILED      "500 Internal Server Error"

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
static int8_t sdr_http_step_db[SDR_HTTP_STEP_BINS];


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
        ((gain != SDR_RADIO_GAIN_AGC) && ((gain < 0) || (gain > sdr_radio_get_gain_max()))))
    {
        json_utils_send_error(req, "invalid_gain");
        return ESP_OK;
    }

    size_t bins_sent = 0;

    // Runs on the single httpd task, so captures never overlap and the words stay ours until the next one
    for (size_t step = 0; step < ARRAY_SIZE(sdr_http_steps_mhz); step++) {
        unsigned centre_mhz = sdr_http_steps_mhz[step];
        const uint32_t *words = NULL;
        esp_err_t ret = sdr_radio_capture(centre_mhz, (int)gain, &words);
        if (ret != ESP_OK) {
            if (step == 0) {
                const char *error = (ret == ESP_ERR_TIMEOUT) ? "capture_timeout" : "capture_memory";
                json_utils_send_error_status(req, SDR_HTTP_STATUS_CAPTURE_FAILED, error);
                return ESP_OK;
            }
            // The body ends short of the full scan, which the client detects
            ESP_LOGW(TAG, "Scan stopped at %u MHz after %u bins: %s", centre_mhz, (unsigned)bins_sent,
                     esp_err_to_name(ret));
            break;
        }
        if (step == 0) {
            httpd_resp_set_type(req, "application/octet-stream");
        }

        sdr_spectrum_compute_db(words, SDR_RADIO_MAX_SAMPLES, SDR_HTTP_STEP_FIRST_BIN, SDR_HTTP_STEP_BINS,
                                sdr_http_step_db);
        if (httpd_resp_send_chunk(req, (const char *)sdr_http_step_db, SDR_HTTP_STEP_BINS) != ESP_OK) {
            return ESP_FAIL;
        }
        bins_sent += SDR_HTTP_STEP_BINS;
    }

    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
