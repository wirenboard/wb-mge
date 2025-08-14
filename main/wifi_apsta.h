#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASS_MAX_LEN       64

typedef struct {
    char ap_ssid[WIFI_SSID_MAX_LEN];
    char ap_pass[WIFI_PASS_MAX_LEN];
    esp_netif_ip_info_t* ap_ip_info;
    esp_event_handler_t ap_event_handler;
    char sta_ssid[WIFI_SSID_MAX_LEN];
    char sta_pass[WIFI_PASS_MAX_LEN];
    esp_event_handler_t sta_event_handler;
    wifi_mode_t wifi_mode;
    wifi_auth_mode_t wifi_auth_mode_ap; // Authentication mode for AP
    wifi_auth_mode_t wifi_auth_mode_sta; // Authentication mode for STA
} wifi_apsta_config_t;

esp_err_t wifi_init_apsta(wifi_apsta_config_t* apsta_cfg, char* netif_hostname);
