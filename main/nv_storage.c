#include "nv_storage.h"
#include "setting_items.h"

#include <string.h>
#include <esp_log.h>
#include "nvs.h"
#include "nvs_flash.h"


#define NV_STORAGE_DEBUG_LOG_ENABLE     0       // TODO: Возможно, вынести в настройки


static const char *TAG = "nv_storage";
static const char *NVS_NAMESPACE = "storage";


esp_err_t nvs_init(void)
{
    if (NV_STORAGE_DEBUG_LOG_ENABLE) {
        esp_log_level_set(TAG, ESP_LOG_DEBUG);
    }

    ESP_LOGD(TAG, "Initializing NVS");

    esp_err_t ret = nvs_flash_init();
    if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Print NVS statistics for debugging
    nvs_stats_t nvs_stats;
    ret = nvs_get_stats(NULL, &nvs_stats);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS Stats - Used: %d, Free: %d, Total: %d",
                 nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
    } else {
        ESP_LOGW(TAG, "Failed to get NVS stats: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}


esp_err_t nvs_write_str(const char* key, const char* value)
{
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(nvs_handle, key, value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write string %s: %s", key, esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Written string %s = %s", key, value);
    }

    nvs_close(nvs_handle);
    return ret;
}


esp_err_t nvs_read_str(const char* key, char* value)
{
    if (!key || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t required_size = SETTING_ITEM_MAX_STR_LEN;
    ret = nvs_get_str(nvs_handle, key, value, &required_size);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to read string %s: %s", key, esp_err_to_name(ret));
        } else {
            ESP_LOGD(TAG, "String %s not found in NVS", key);
        }
    } else {
        ESP_LOGD(TAG, "Read string %s = %s", key, value);
    }

    nvs_close(nvs_handle);
    return ret;
}


bool nvs_has_key(const char* key)
{
    if (!key) {
        return false;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return false;
    }

    size_t required_size = 0;
    ret = nvs_get_str(nvs_handle, key, NULL, &required_size);

    if (required_size > SETTING_ITEM_MAX_STR_LEN) {
        ESP_LOGE(TAG, "Key %s exceeds maximum size", key);
        ret = ESP_ERR_INVALID_SIZE;
    }

    nvs_close(nvs_handle);

    return (ret == ESP_OK);
}
