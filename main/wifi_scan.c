#include "wifi_scan.h"
#include "json_utils.h"
#include "auth.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "wifi_apsta.h"


#define WIFI_SCAN_MAX_RESULTS           20
#define WIFI_SCAN_BSSID_STR_SIZE        18      // MAC address format (xx:xx:xx:xx:xx:xx + terminating '\0')

#define WIFI_SCAN_TASK_STACK_SIZE       4096
#define WIFI_SCAN_TASK_PRIORITY         1

#define WIFI_SCAN_START_BIT             BIT0
#define WIFI_SCAN_DONE_BIT              BIT1


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

static const char *TAG = "wifi_scan";

static SemaphoreHandle_t wifi_scan_mutex = NULL;
static esp_event_handler_instance_t wifi_scan_event_handler_instance = NULL;
static EventGroupHandle_t wifi_scan_event_group = NULL;

static const wifi_scan_config_t wifi_scan_config = {
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


static void wifi_scan_handle_completion(uint16_t ap_count, esp_err_t result)
{
    wifi_scan_state.scan_completed = true;
    wifi_scan_state.scan_in_progress = false;
    wifi_scan_state.ap_count = ap_count;
    wifi_scan_state.last_scan_result = result;
}


static void wifi_scan_event_handler(void *arg, esp_event_base_t event_base,
                                    int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        xEventGroupSetBits(wifi_scan_event_group, WIFI_SCAN_DONE_BIT);
    }
}


static void wifi_scan_task(void* pvParameter)
{
    while (1) {
        EventBits_t bits_to_wait = WIFI_SCAN_START_BIT | WIFI_SCAN_DONE_BIT;
        EventBits_t bits = xEventGroupWaitBits(wifi_scan_event_group, bits_to_wait, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & WIFI_SCAN_START_BIT) {
            wifi_sta_connect_scan_lock();
            esp_err_t scan_result = esp_wifi_scan_start(&wifi_scan_config, false);
            if (scan_result != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi scan: %s", esp_err_to_name(scan_result));
                xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);
                wifi_scan_handle_start_failure(scan_result);
                xSemaphoreGive(wifi_scan_mutex);
                wifi_sta_connect_scan_unlock();
            } else {
                ESP_LOGI(TAG, "WiFi networks scan started");
            }
        }
        if (bits & WIFI_SCAN_DONE_BIT) {
            xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);
            uint16_t ap_count = WIFI_SCAN_MAX_RESULTS;
            esp_err_t result = esp_wifi_scan_get_ap_records(&ap_count, wifi_scan_state.ap_records);
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "WiFi scan finished, networks found: %d", ap_count);
                wifi_scan_handle_completion(ap_count, ESP_OK);
            } else {
                ESP_LOGE(TAG, "Failed to get WiFi scan results: %s", esp_err_to_name(result));
                wifi_scan_handle_completion(0, ESP_FAIL);
            }
            wifi_sta_connect_scan_unlock();
            xSemaphoreGive(wifi_scan_mutex);
        }
    }
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

    wifi_scan_event_group = xEventGroupCreate();
    if (wifi_scan_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi scan event group");
        vSemaphoreDelete(wifi_scan_mutex);
        return ESP_FAIL;
    }

    wifi_scan_reset_state();

    esp_err_t ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_scan_event_handler,
                                                        NULL, &wifi_scan_event_handler_instance);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi scan event handler");
        vSemaphoreDelete(wifi_scan_mutex);
        vEventGroupDelete(wifi_scan_event_group);
        wifi_scan_mutex = NULL;
        return ESP_FAIL;
    }

    xTaskCreate(wifi_scan_task, "wifi_scan_task", WIFI_SCAN_TASK_STACK_SIZE,
                NULL, WIFI_SCAN_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "WiFi scan module initialized");
    return ESP_OK;
}


static bool wifi_scan_is_in_progress(void)
{
    xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);
    bool in_progress = wifi_scan_state.scan_in_progress;
    xSemaphoreGive(wifi_scan_mutex);
    return in_progress;
}


static bool wifi_scan_is_completed(void)
{
    xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);
    bool completed = wifi_scan_state.scan_completed;
    xSemaphoreGive(wifi_scan_mutex);
    return completed;
}


static esp_err_t wifi_scan_start(void)
{
    if (wifi_scan_mutex == NULL) {
        ESP_LOGE(TAG, "WiFi scan not initialized");
        return ESP_FAIL;
    }

    if (wifi_scan_is_in_progress()) {
        ESP_LOGW(TAG, "WiFi scan already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);
    wifi_scan_prepare_for_start();
    xSemaphoreGive(wifi_scan_mutex);

    xEventGroupSetBits(wifi_scan_event_group, WIFI_SCAN_START_BIT);

    return ESP_OK;
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

    *results_json = cJSON_CreateArray();
    if (*results_json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON array");
        return ESP_FAIL;
    }

    xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);

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

    *status_json = cJSON_CreateObject();
    if (*status_json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }

    xSemaphoreTake(wifi_scan_mutex, portMAX_DELAY);

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
        json_utils_send_error(req, "Failed to create response");
        return ESP_OK;
    }

    esp_err_t result = wifi_scan_start();
    if (result == ESP_OK) {
        cJSON_AddBoolToObject(resp_json, "success", true);
        cJSON_AddStringToObject(resp_json, "message", "Scan started");
    } else {
        cJSON_AddBoolToObject(resp_json, "success", false);
        if (result == ESP_ERR_INVALID_STATE) {
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
        json_utils_send_error(req, "Failed to get scan status");
        return ESP_OK;
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
