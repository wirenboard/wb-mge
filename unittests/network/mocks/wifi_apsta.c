// Mock for wifi_apsta used by the network unit tests.
// The real module owns the ESP-IDF WiFi driver, FreeRTOS tasks and event groups; none of
// that is needed to exercise network.c. What IS reproduced faithfully is the initialized
// flag and the two behaviours that hang off it: wifi_init_apsta() raises it on success,
// and wifi_set_apsta_config() refuses an uninitialized radio with ESP_ERR_NOT_ALLOWED.

#include "wifi_apsta.h"
#include "wifi_apsta_mock.h"

static bool initialized = false;

int mock_wifi_init_apsta_called = 0;
esp_err_t mock_wifi_init_apsta_return_value = ESP_OK;

int mock_wifi_set_apsta_config_called = 0;
esp_err_t mock_wifi_set_apsta_config_return_value = ESP_OK;

void mock_wifi_apsta_reset(void)
{
    initialized = false;

    mock_wifi_init_apsta_called = 0;
    mock_wifi_init_apsta_return_value = ESP_OK;

    mock_wifi_set_apsta_config_called = 0;
    mock_wifi_set_apsta_config_return_value = ESP_OK;
}

esp_err_t wifi_init_apsta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    (void)apsta_cfg;
    (void)netif_hostname;

    mock_wifi_init_apsta_called++;
    if (mock_wifi_init_apsta_return_value == ESP_OK) {
        initialized = true;
    }
    return mock_wifi_init_apsta_return_value;
}

esp_err_t wifi_set_apsta_config(wifi_apsta_config_t* apsta_cfg, char* netif_hostname)
{
    (void)apsta_cfg;
    (void)netif_hostname;

    mock_wifi_set_apsta_config_called++;
    if (!initialized) {
        return ESP_ERR_NOT_ALLOWED;
    }
    return mock_wifi_set_apsta_config_return_value;
}

bool wifi_apsta_is_initialized(void)
{
    return initialized;
}
