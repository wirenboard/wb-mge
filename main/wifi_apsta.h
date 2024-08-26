#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"

typedef struct {
    char* ap_ssid;
    char* ap_pass;
    esp_netif_ip_info_t* ap_ip_info;
    esp_event_handler_t ap_event_handler;
    char* sta_ssid;
    char* sta_pass;
    esp_event_handler_t sta_event_handler;
    wifi_mode_t wifi_mode;
} wifi_apsta_config_t;

esp_err_t wifi_init_apsta(wifi_apsta_config_t* wifi_cfg);
