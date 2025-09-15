#pragma once

#include "wifi_apsta.h"
#include "esp_wifi_types.h"
#include "esp_netif.h"

// QEMU build detection
#ifdef CONFIG_ETH_USE_OPENETH
    #define QEMU_BUILD 1
#else
    #define QEMU_BUILD 0
#endif

#if QEMU_BUILD

// Mock WiFi functions for QEMU builds
esp_err_t wifi_init_apsta_qemu(wifi_apsta_config_t *cfg, const char *ap_ssid);
esp_err_t wifi_mock_get_mac(wifi_interface_t ifx, uint8_t mac[6]);
esp_err_t wifi_mock_set_mode(wifi_mode_t mode);
wifi_mode_t wifi_mock_get_mode(void);

// Override WiFi functions with mocks when building for QEMU
#define esp_wifi_get_mac wifi_mock_get_mac
#define esp_wifi_set_mode wifi_mock_set_mode
#define esp_wifi_get_mode wifi_mock_get_mode

#endif // QEMU_BUILD
