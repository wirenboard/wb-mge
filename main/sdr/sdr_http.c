#include "sdr_http.h"
#include "sdr_radio.h"
#include "auth.h"
#include "json_utils.h"
#include "array_size.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <esp_log.h>

#define SDR_HTTP_QUERY_BUF_SIZE             128
#define SDR_HTTP_VALUE_BUF_SIZE             16
#define SDR_HTTP_NUMBER_BASE                10

#define SDR_HTTP_STATUS_CAPTURE_FAILED      "500 Internal Server Error"

static const char *TAG = "sdr_http";

typedef struct {
    int msps;
    sdr_radio_rate_t rate;
} sdr_http_rate_t;

// Accepted sample rates, in the order /sdr/info lists them
static const sdr_http_rate_t sdr_http_rates[] = {
    {80, SDR_RADIO_RATE_80_MSPS},
    {40, SDR_RADIO_RATE_40_MSPS},
    {16, SDR_RADIO_RATE_16_MSPS},
};


static const sdr_http_rate_t *sdr_http_find_rate(long msps)
{
    for (size_t i = 0; i < ARRAY_SIZE(sdr_http_rates); i++) {
        if (sdr_http_rates[i].msps == msps) {
            return &sdr_http_rates[i];
        }
    }
    return NULL;
}

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

    int rates_msps[ARRAY_SIZE(sdr_http_rates)];
    for (size_t i = 0; i < ARRAY_SIZE(sdr_http_rates); i++) {
        rates_msps[i] = sdr_http_rates[i].msps;
    }

    cJSON_AddNumberToObject(response_json, "gain_max", sdr_radio_get_gain_max());
    cJSON_AddNumberToObject(response_json, "samples", SDR_RADIO_MAX_SAMPLES);
    cJSON_AddItemToObject(response_json, "rates_msps", cJSON_CreateIntArray(rates_msps, ARRAY_SIZE(rates_msps)));
    cJSON_AddNumberToObject(response_json, "freq_min_mhz", SDR_RADIO_FREQ_MIN_MHZ);
    cJSON_AddNumberToObject(response_json, "freq_max_mhz", SDR_RADIO_FREQ_MAX_MHZ);
    cJSON_AddNumberToObject(response_json, "bw_min_mhz", SDR_RADIO_BW_MIN_MHZ);
    cJSON_AddNumberToObject(response_json, "bw_max_mhz", SDR_RADIO_BW_MAX_MHZ);

    json_utils_send_response(req, NULL, response_json);
    return ESP_OK;
}


esp_err_t sdr_http_capture_get_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    char query[SDR_HTTP_QUERY_BUF_SIZE] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        json_utils_send_error(req, "invalid_query");
        return ESP_OK;
    }

    long freq_mhz = 0;
    if ((!sdr_http_query_int(query, "freq", &freq_mhz)) ||
        (freq_mhz < SDR_RADIO_FREQ_MIN_MHZ) || (freq_mhz > SDR_RADIO_FREQ_MAX_MHZ))
    {
        json_utils_send_error(req, "invalid_freq");
        return ESP_OK;
    }

    long rate_msps = 0;
    const sdr_http_rate_t *rate = NULL;
    if (sdr_http_query_int(query, "rate", &rate_msps)) {
        rate = sdr_http_find_rate(rate_msps);
    }
    if (rate == NULL) {
        json_utils_send_error(req, "invalid_rate");
        return ESP_OK;
    }

    long bw_mhz = 0;
    if ((!sdr_http_query_int(query, "bw", &bw_mhz)) ||
        ((bw_mhz != SDR_RADIO_BW_KEEP) && ((bw_mhz < SDR_RADIO_BW_MIN_MHZ) || (bw_mhz > SDR_RADIO_BW_MAX_MHZ))))
    {
        json_utils_send_error(req, "invalid_bw");
        return ESP_OK;
    }

    long gain = 0;
    if ((!sdr_http_query_int(query, "gain", &gain)) ||
        ((gain != SDR_RADIO_GAIN_AGC) && ((gain < 0) || (gain > sdr_radio_get_gain_max()))))
    {
        json_utils_send_error(req, "invalid_gain");
        return ESP_OK;
    }

    // Runs on the single httpd task, so captures never overlap and the buffer stays ours until sent
    const uint8_t *iq = NULL;
    esp_err_t ret = sdr_radio_capture((unsigned)freq_mhz, rate->rate, (unsigned)bw_mhz, (int)gain, &iq);
    if (ret != ESP_OK) {
        const char *error = (ret == ESP_ERR_TIMEOUT) ? "capture_timeout" : "capture_memory";
        json_utils_send_error_status(req, SDR_HTTP_STATUS_CAPTURE_FAILED, error);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_send(req, (const char *)iq, SDR_RADIO_CAPTURE_BYTES);
    return ESP_OK;
}
