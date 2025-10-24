#include "sys_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nv_storage.h"
#include "esp_efuse.h"
#include <inttypes.h>

#if CONFIG_EFUSE_VIRTUAL
    #include "copy_protection.h"
#endif


#define SIGNATURE_EFUSE_BLOCK           EFUSE_BLK3
#define SIGNATURE_EFUSE_OFFSET          8

#define SECURITY_CODE_EFUSE_BLOCK       EFUSE_BLK3
#define SECURITY_CODE_EFUSE_OFFSET      (SIGNATURE_EFUSE_OFFSET + DEVICE_SIGNATURE_LEN)

// Auxilary macros
#define STR_HELPER_MACRO(x)             #x
#define STR_MACRO(x)                    STR_HELPER_MACRO(x)


static const char* TAG = "sys_info";

sys_info_t sys_info = {0};


static uint64_t generate_serial_from_mac(void)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address for serial number");
        return 0;
    }

    // Convert full 6-byte MAC to 64-bit serial number (maximum uniqueness)
    uint64_t mac_full = 0;
    for (int i = 0; i < 6; i++) {
        mac_full = (mac_full << 8) | mac[i];
    }
    // Use full 48-bit MAC as 64-bit serial number
    ESP_LOGD(TAG, "Generated serial number from MAC: %02X:%02X:%02X:%02X:%02X:%02X -> %" PRIu64,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac_full);

    return mac_full;
}


#if CONFIG_EFUSE_VIRTUAL

    static void write_device_signature_to_efuse(void)
    {
        char signature_buf[DEVICE_SIGNATURE_LEN + 1];
        memset(signature_buf, 0, sizeof(signature_buf));
        snprintf(signature_buf, sizeof(signature_buf), "%s", DEVICE_SIGNATURE);

        esp_err_t err = esp_efuse_write_block(SIGNATURE_EFUSE_BLOCK, signature_buf, SIGNATURE_EFUSE_OFFSET * 8, DEVICE_SIGNATURE_LEN * 8);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Unable to write device signature to virtual " STR_MACRO(SIGNATURE_EFUSE_BLOCK));
            return;
        }

        ESP_LOGW(TAG, "Device signature was written to virtual " STR_MACRO(SIGNATURE_EFUSE_BLOCK));
        char read_buf[32] = {0};
        esp_err_t ret = esp_efuse_read_block(SIGNATURE_EFUSE_BLOCK, read_buf, 0, sizeof(read_buf) * 8);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to read virtual " STR_MACRO(SIGNATURE_EFUSE_BLOCK));
        }

        ESP_LOGD(TAG, "Virtual " STR_MACRO(SIGNATURE_EFUSE_BLOCK) " content:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, read_buf, sizeof(read_buf), ESP_LOG_DEBUG);
    }

    static void write_security_code_to_efuse(void)
    {
        uint8_t sec_code[SECURITY_CODE_LEN] = {0};
        esp_err_t ret = copy_protection_get_security_code(sec_code);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to get security code");
            return;
        }

        ret = esp_efuse_write_block(SECURITY_CODE_EFUSE_BLOCK, sec_code, SECURITY_CODE_EFUSE_OFFSET * 8, SECURITY_CODE_LEN * 8);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to write device security code to virtual " STR_MACRO(SECURITY_CODE_EFUSE_BLOCK));
            return;
        }

        ESP_LOGW(TAG, "Device security code was written to virtual " STR_MACRO(SECURITY_CODE_EFUSE_BLOCK));
        char read_buf[32] = {0};
        ret = esp_efuse_read_block(SECURITY_CODE_EFUSE_BLOCK, read_buf, 0, sizeof(read_buf) * 8);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to read virtual " STR_MACRO(SECURITY_CODE_EFUSE_BLOCK));
        }

        ESP_LOGD(TAG, "Virtual " STR_MACRO(SECURITY_CODE_EFUSE_BLOCK) " content:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, read_buf, sizeof(read_buf), ESP_LOG_DEBUG);
    }

#endif


static void get_device_signature(char out_buf[DEVICE_SIGNATURE_LEN + 1])
{
    memset(out_buf, 0, DEVICE_SIGNATURE_LEN + 1);
    esp_err_t res = esp_efuse_read_block(SIGNATURE_EFUSE_BLOCK, out_buf, SIGNATURE_EFUSE_OFFSET * 8, DEVICE_SIGNATURE_LEN * 8);
    bool is_empty = !strlen(out_buf);

    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read device signature from " STR_MACRO(SIGNATURE_EFUSE_BLOCK));
    } else if (is_empty) {
        ESP_LOGW(TAG, "Device signature is empty");
    }
}


static void get_security_code(uint8_t out_buf[SECURITY_CODE_LEN])
{
    memset(out_buf, 0, SECURITY_CODE_LEN);
    esp_err_t res = esp_efuse_read_block(SECURITY_CODE_EFUSE_BLOCK, out_buf, SECURITY_CODE_EFUSE_OFFSET * 8, SECURITY_CODE_LEN * 8);

    // Show status only when debug logs are enabled
    #if DEBUG_LOG_ENABLE
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Unable to read device secure code " STR_MACRO(SIGNATURE_EFUSE_BLOCK));
        } else {
            uint8_t zero_buf[SECURITY_CODE_LEN];
            memset(zero_buf, 0, sizeof(zero_buf));
            bool is_empty = (memcmp(zero_buf, out_buf, SECURITY_CODE_LEN) == 0);
            if (is_empty) {
                ESP_LOGW(TAG, "Device security code is empty");
            }
        }
    #else
        (void)res;
    #endif
}


esp_err_t sys_info_init(void)
{
    sys_info.device_serial_num = generate_serial_from_mac();

    GET_APP_DESC_STR_FIELD(fw_version, sys_info.firmware_ver);
    GET_APP_DESC_STR_FIELD(fw_git_info, sys_info.firmware_git_info);
    GET_APP_DESC_STR_FIELD(device_model, sys_info.device_name);

    #if CONFIG_EFUSE_VIRTUAL
        write_device_signature_to_efuse();
        write_security_code_to_efuse();
    #endif

    get_device_signature(sys_info.device_signature);
    get_security_code(sys_info.security_code);

    ESP_LOGI(TAG, "Device name: %s", sys_info.device_name);
    if (strlen(sys_info.device_signature)) {
        ESP_LOGI(TAG, "Device signature: %s", sys_info.device_signature);
    } else {
        ESP_LOGI(TAG, "Device signature: <EMPTY>");
    }
    ESP_LOGI(TAG, "Serial number: %" PRIu64, sys_info.device_serial_num);
    ESP_LOGI(TAG, "Firmware version: %s", sys_info.firmware_ver);
    ESP_LOGI(TAG, "Firmware GIT info: %s", sys_info.firmware_git_info);

    return ESP_OK;
}
