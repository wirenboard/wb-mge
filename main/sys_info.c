#include "sys_info.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "nv_storage.h"

#define SYS_INFO_DEV_NAME_KEY       "device_name"
#define SYS_INFO_HW_KEY             "hardware"

static const char* TAG = "sys_info";

sys_info_t sys_info = {0};

esp_err_t sys_info_init(void)
{
    nvs_read_str(SYS_INFO_DEV_NAME_KEY, sys_info.device_name);
    nvs_read_str(SYS_INFO_HW_KEY, sys_info.hardware_ver);

    // Read MAC address and use it as serial number
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    if (ret == ESP_OK) {
        // Convert full 6-byte MAC to 64-bit serial number (maximum uniqueness)
        uint64_t mac_full = 0;
        for (int i = 0; i < 6; i++) {
            mac_full = (mac_full << 8) | mac[i];
        }
        // Use full 48-bit MAC as 64-bit serial number
        sys_info.device_serial_num = mac_full;
        ESP_LOGI(TAG, "Generated serial number from MAC: %02X:%02X:%02X:%02X:%02X:%02X -> %llu",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], sys_info.device_serial_num);
    } else {
        ESP_LOGE(TAG, "Failed to read MAC address for serial number");
        sys_info.device_serial_num = 0;
    }

    ESP_LOGI(TAG, "Device name: %s", sys_info.device_name);
    ESP_LOGI(TAG, "Hardware: %s", sys_info.hardware_ver);
    ESP_LOGI(TAG, "Serial number: %llu", sys_info.device_serial_num);

    return ESP_OK;
}

// TODO: подумать
esp_err_t sys_info_write_factory_data(void)
{
    esp_err_t err = ESP_OK;

    err = nvs_write_str(SYS_INFO_DEV_NAME_KEY, sys_info.device_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write device name to NVS");
        return err;
    }
    err = nvs_write_str(SYS_INFO_HW_KEY, sys_info.hardware_ver);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write hardware to NVS");
        return err;
    }

    return err;
}
