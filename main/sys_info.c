#include "sys_info.h"

#include "efuse_table.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "nv_storage.h"

#define SYS_INFO_DEV_NAME_KEY       "device_name"
#define SYS_INFO_HW_KEY             "hardware"

static const char* TAG = "sys_info";

char sys_info_device_name[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_firmware[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_hardware[SYS_INFO_MAX_STR_LEN] = {0};
int sys_info_serial_num = 0;

bool sys_info_con_eth = false;
char sys_info_eth_ip[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_eth_mask[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_eth_gw[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_eth_mac[SYS_INFO_MAX_STR_LEN] = {0};

bool sys_info_con_sta = false;
char sys_info_sta_ip[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_sta_mask[SYS_INFO_MAX_STR_LEN] = {0};
char sys_info_sta_gw[SYS_INFO_MAX_STR_LEN] = {0};

int sys_info_con_ap = 0;

esp_err_t sys_info_init(void)
{
    nvs_read_str(SYS_INFO_DEV_NAME_KEY, sys_info_device_name);
    nvs_read_str(SYS_INFO_HW_KEY, sys_info_hardware);
    esp_efuse_read_field_blob(ESP_EFUSE_SERIAL_NUMBER, &sys_info_serial_num,
        ESP_EFUSE_SERIAL_NUMBER[0]->bit_count);
    ESP_LOGI(TAG, "Device name: %s", sys_info_device_name);
    ESP_LOGI(TAG, "Hardware: %s", sys_info_hardware);
    ESP_LOGI(TAG, "Serial number: %d", sys_info_serial_num);

    return ESP_OK;
}

esp_err_t sys_info_write(void)
{
    esp_err_t err = ESP_OK;

    err = nvs_write_str(SYS_INFO_DEV_NAME_KEY, sys_info_device_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write device name to NVS");
        return err;
    }
    err = nvs_write_str(SYS_INFO_HW_KEY, sys_info_hardware);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write hardware to NVS");
        return err;
    }
    err = esp_efuse_write_field_blob(ESP_EFUSE_SERIAL_NUMBER, &sys_info_serial_num,
        ESP_EFUSE_SERIAL_NUMBER[0]->bit_count);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write serial number to efuse");
        return err;
    }

    return err;
}
