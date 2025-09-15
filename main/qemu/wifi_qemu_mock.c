#include "wifi_qemu_mock.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>

static const char *TAG = "wifi_qemu_mock";
static wifi_mode_t mock_wifi_mode = WIFI_MODE_NULL;
static bool mock_wifi_initialized = false;

#if QEMU_BUILD

esp_err_t wifi_init_apsta_qemu(wifi_apsta_config_t *cfg, const char *ap_ssid)
{
    ESP_LOGI(TAG, "Initializing WiFi mock for QEMU");
    ESP_LOGI(TAG, "WiFi functionality is mocked in QEMU environment");
    
    if (cfg == NULL) {
        ESP_LOGE(TAG, "WiFi configuration is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Store the WiFi mode from configuration
    mock_wifi_mode = cfg->wifi_mode;
    mock_wifi_initialized = true;

    ESP_LOGI(TAG, "Mock WiFi mode set to: %d", mock_wifi_mode);
    ESP_LOGI(TAG, "Mock AP SSID: %s", ap_ssid ? ap_ssid : "unknown");
    
    // Log the configuration for debugging
    if (cfg->wifi_mode == WIFI_MODE_AP || cfg->wifi_mode == WIFI_MODE_APSTA) {
        ESP_LOGI(TAG, "Mock AP configuration:");
        ESP_LOGI(TAG, "  SSID: %s", cfg->ap_ssid);
        ESP_LOGI(TAG, "  Auth mode: %d", cfg->wifi_auth_mode_ap);
    }
    
    if (cfg->wifi_mode == WIFI_MODE_STA || cfg->wifi_mode == WIFI_MODE_APSTA) {
        ESP_LOGI(TAG, "Mock STA configuration:");
        ESP_LOGI(TAG, "  SSID: %s", cfg->sta_ssid);
        ESP_LOGI(TAG, "  Auth mode: %d", cfg->wifi_auth_mode_sta);
    }

    // Simulate successful initialization
    ESP_LOGI(TAG, "WiFi mock initialization completed successfully");
    return ESP_OK;
}

esp_err_t wifi_mock_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    if (mac == NULL) {
        ESP_LOGE(TAG, "MAC address buffer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Generate deterministic mock MAC addresses
    if (ifx == WIFI_IF_STA) {
        // Mock STA MAC: 02:00:00:00:00:01
        mac[0] = 0x02; // Locally administered bit set
        mac[1] = 0x00;
        mac[2] = 0x00;
        mac[3] = 0x00;
        mac[4] = 0x00;
        mac[5] = 0x01;
        ESP_LOGD(TAG, "Mock STA MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else if (ifx == WIFI_IF_AP) {
        // Mock AP MAC: 02:00:00:00:00:02
        mac[0] = 0x02; // Locally administered bit set
        mac[1] = 0x00;
        mac[2] = 0x00;
        mac[3] = 0x00;
        mac[4] = 0x00;
        mac[5] = 0x02;
        ESP_LOGD(TAG, "Mock AP MAC: %02x:%02x:%02x:%02x:%02x:%02x", 
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGE(TAG, "Invalid WiFi interface: %d", ifx);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t wifi_mock_set_mode(wifi_mode_t mode)
{
    ESP_LOGI(TAG, "Setting mock WiFi mode to: %d", mode);
    mock_wifi_mode = mode;
    return ESP_OK;
}

wifi_mode_t wifi_mock_get_mode(void)
{
    ESP_LOGD(TAG, "Getting mock WiFi mode: %d", mock_wifi_mode);
    return mock_wifi_mode;
}

#endif // QEMU_BUILD
