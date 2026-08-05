#pragma once

/* Minimal esp_wifi.h stub for the info_handlers unit test. Declares only the
 * Wi-Fi types and entry points that info_handlers.c references. The functions
 * are stubbed in mocks/esp_wifi.c; the wifi_perm_disable early-return path under
 * test never reaches them. */

#include "esp_err.h"
#include <stdint.h>

#define ESP_WIFI_MAX_CONN_NUM 10

typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
} wifi_sta_info_t;

typedef struct {
    int             num;
    wifi_sta_info_t sta[ESP_WIFI_MAX_CONN_NUM];
} wifi_sta_list_t;

typedef struct {
    uint8_t bssid[6];
    int8_t  rssi;
} wifi_ap_record_t;

esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *sta_list);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
