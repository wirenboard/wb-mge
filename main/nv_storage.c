#include "nv_storage.h"
#include "setting_items.h"

#include <string.h>
#include <esp_log.h>
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nv_storage";
static const char *NVS_NAMESPACE = "storage";

esp_err_t nvs_init(void)
{
    ESP_LOGI(TAG, "Initializing NVS");

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

    ESP_LOGI(TAG, "NVS initialized successfully");
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
    nvs_close(nvs_handle);

    return (ret == ESP_OK);
}

esp_err_t nvs_erase_setting_key(const char* key)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_key(nvs_handle, key);
    if (ret != ESP_OK) {
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to erase key %s: %s", key, esp_err_to_name(ret));
        }
    } else {
        ret = nvs_commit(nvs_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        }
    }

    nvs_close(nvs_handle);
    return ret;
}
