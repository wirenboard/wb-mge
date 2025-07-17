#include "wifi_scan.h"
#include "json_utils.h"
#include "auth.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static const char *TAG = "wifi_scan";

#define WIFI_SCAN_MAX_RESULTS       20
#define WIFI_SCAN_BSSID_STR_SIZE    18

static struct {
    bool scan_in_progress;
    bool scan_completed;
    wifi_ap_record_t ap_records[WIFI_SCAN_MAX_RESULTS];
    uint16_t ap_count;
    esp_err_t last_scan_result;
} wifi_scan_state = {
    .scan_in_progress = false,
    .scan_completed = false,
    .ap_count = 0,
    .last_scan_result = ESP_OK
};

static SemaphoreHandle_t wifi_scan_mutex = NULL;

static void wifi_scan_reset_state(void)
{
    wifi_scan_state.scan_in_progress = false;
    wifi_scan_state.scan_completed = false;
    wifi_scan_state.ap_count = 0;
    wifi_scan_state.last_scan_result = ESP_OK;
}

static void wifi_scan_prepare_for_start(void)
{
    wifi_scan_state.scan_in_progress = true;
    wifi_scan_state.scan_completed = false;
    wifi_scan_state.ap_count = 0;
    // Keep last_scan_result as is - don't reset it here
}

static void wifi_scan_handle_start_failure(esp_err_t error)
{
    wifi_scan_state.scan_in_progress = false;
    wifi_scan_state.last_scan_result = error;
}

static void wifi_scan_handle_completion(uint16_t ap_count)
{
    wifi_scan_state.scan_completed = true;
    wifi_scan_state.scan_in_progress = false;
    wifi_scan_state.ap_count = ap_count;
    wifi_scan_state.last_scan_result = ESP_OK;
}

esp_err_t wifi_scan_init(void)
{
    if (wifi_scan_mutex != NULL) {
        ESP_LOGW(TAG, "WiFi scan already initialized");
        return ESP_OK;  // Don't fail, just warn
    }

    wifi_scan_mutex = xSemaphoreCreateMutex();
    if (wifi_scan_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi scan mutex");
        return ESP_FAIL;
    }

    wifi_scan_reset_state();

    ESP_LOGI(TAG, "WiFi scan module initialized");
    return ESP_OK;
}

static esp_err_t wifi_scan_start(void)
{
    if (wifi_scan_mutex == NULL) {
        ESP_LOGE(TAG, "WiFi scan not initialized");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take WiFi scan mutex");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;

    if (wifi_scan_state.scan_in_progress) {
        ESP_LOGW(TAG, "WiFi scan already in progress");
        ret = ESP_FAIL;
        goto exit;
    }

    wifi_scan_prepare_for_start();

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };

    esp_err_t scan_result = esp_wifi_scan_start(&scan_config, false);

    if (scan_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi scan: 0x%x", scan_result);
        wifi_scan_handle_start_failure(scan_result);
        ret = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "WiFi scan started successfully");
    }

exit:
    xSemaphoreGive(wifi_scan_mutex);
    return ret;
}

static bool wifi_scan_is_in_progress(void)
{
    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool in_progress = wifi_scan_state.scan_in_progress;
    xSemaphoreGive(wifi_scan_mutex);
    return in_progress;
}

static bool wifi_scan_is_completed(void)
{
    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    bool completed = wifi_scan_state.scan_completed;
    xSemaphoreGive(wifi_scan_mutex);
    return completed;
}

static esp_err_t wifi_scan_get_results_json(cJSON **results_json)
{
    if (results_json == NULL) {
        return ESP_FAIL;
    }

    if (wifi_scan_mutex == NULL) {
        ESP_LOGE(TAG, "WiFi scan not initialized");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take WiFi scan mutex");
        return ESP_FAIL;
    }

    *results_json = cJSON_CreateArray();
    if (*results_json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON array");
        xSemaphoreGive(wifi_scan_mutex);
        return ESP_FAIL;
    }

    // Only return results if scan is completed
    if (wifi_scan_state.scan_completed) {
        for (int i = 0; i < wifi_scan_state.ap_count; i++) {
            cJSON *ap_json = cJSON_CreateObject();
            if (ap_json == NULL) {
                ESP_LOGE(TAG, "Failed to create JSON object for AP %d", i);
                continue;
            }

            cJSON_AddStringToObject(ap_json, "ssid", (const char *)wifi_scan_state.ap_records[i].ssid);
            cJSON_AddNumberToObject(ap_json, "rssi", wifi_scan_state.ap_records[i].rssi);

            char bssid_str[WIFI_SCAN_BSSID_STR_SIZE];
            snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     wifi_scan_state.ap_records[i].bssid[0], wifi_scan_state.ap_records[i].bssid[1],
                     wifi_scan_state.ap_records[i].bssid[2], wifi_scan_state.ap_records[i].bssid[3],
                     wifi_scan_state.ap_records[i].bssid[4], wifi_scan_state.ap_records[i].bssid[5]);
            cJSON_AddStringToObject(ap_json, "bssid", bssid_str);
            cJSON_AddNumberToObject(ap_json, "channel", wifi_scan_state.ap_records[i].primary);

            cJSON_AddItemToArray(*results_json, ap_json);
        }
    }

    xSemaphoreGive(wifi_scan_mutex);
    return ESP_OK;
}

static esp_err_t wifi_scan_get_status_json(cJSON **status_json)
{
    if (status_json == NULL) {
        return ESP_FAIL;
    }

    if (wifi_scan_mutex == NULL) {
        ESP_LOGE(TAG, "WiFi scan not initialized");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take WiFi scan mutex");
        return ESP_FAIL;
    }

    *status_json = cJSON_CreateObject();
    if (*status_json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        xSemaphoreGive(wifi_scan_mutex);
        return ESP_FAIL;
    }

    // Check scan status and try to get results if needed
    if (wifi_scan_state.scan_in_progress && !wifi_scan_state.scan_completed) {
        // Try to get results to see if scan completed
        uint16_t ap_count = WIFI_SCAN_MAX_RESULTS;
        esp_err_t result = esp_wifi_scan_get_ap_records(&ap_count, wifi_scan_state.ap_records);

        if (result == ESP_OK) {
            wifi_scan_handle_completion(ap_count);
            ESP_LOGI(TAG, "WiFi scan completed, found %d networks", ap_count);
        } else if (result == ESP_ERR_WIFI_NOT_STARTED) {
            wifi_scan_handle_start_failure(result);
            ESP_LOGW(TAG, "WiFi scan failed: WiFi not started");
        }
    }

    // Add status information
    cJSON_AddBoolToObject(*status_json, "scan_in_progress", wifi_scan_state.scan_in_progress);
    cJSON_AddBoolToObject(*status_json, "scan_completed", wifi_scan_state.scan_completed);

    // Add error information if scan failed
    if (!wifi_scan_state.scan_in_progress && wifi_scan_state.last_scan_result != ESP_OK) {
        cJSON_AddStringToObject(*status_json, "error", "Scan failed");
    }

    xSemaphoreGive(wifi_scan_mutex);
    return ESP_OK;
}

esp_err_t wifi_scan_start_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi scan start request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *resp_json = cJSON_CreateObject();
    if (resp_json == NULL) {
        ESP_LOGE(TAG, "Failed to create response JSON");
        return ESP_FAIL;
    }

    esp_err_t result = wifi_scan_start();
    if (result == ESP_OK) {
        cJSON_AddBoolToObject(resp_json, "success", true);
        cJSON_AddStringToObject(resp_json, "message", "Scan started");
    } else {
        cJSON_AddBoolToObject(resp_json, "success", false);
        if (wifi_scan_is_in_progress()) {
            cJSON_AddStringToObject(resp_json, "error", "Scan already in progress");
        } else {
            cJSON_AddStringToObject(resp_json, "error", "Failed to start scan");
        }
    }

    json_utils_send_response(req, NULL, resp_json);
    return ESP_OK;
}

esp_err_t wifi_scan_results_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi scan results request");

    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *resp_json = NULL;
    esp_err_t result = wifi_scan_get_status_json(&resp_json);

    if ((result != ESP_OK) || (resp_json == NULL)) {
        ESP_LOGE(TAG, "Failed to get scan status");
        return json_utils_send_error(req, "Failed to get scan status");
    }

    // Add scan results if completed
    if (wifi_scan_is_completed()) {
        cJSON *networks = NULL;
        if ((wifi_scan_get_results_json(&networks) == ESP_OK) && (networks != NULL)) {
            cJSON_AddItemToObject(resp_json, "networks", networks);
        }
    }

    json_utils_send_response(req, NULL, resp_json);
    return ESP_OK;
}
