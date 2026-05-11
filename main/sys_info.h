#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "config.h"
#include "wb_app_desc.h"

#define SYS_INFO_MAX_STR_LEN    64

typedef struct {
    uint64_t device_serial_num;
    char firmware_ver[SYS_INFO_MAX_STR_LEN];
    char firmware_git_info[SYS_INFO_MAX_STR_LEN];

    char device_name[SYS_INFO_MAX_STR_LEN];
    char device_signature[DEVICE_SIGNATURE_LEN + 1];

    bool eth_is_connected;
    char eth_ip[SYS_INFO_MAX_STR_LEN];
    char eth_mask[SYS_INFO_MAX_STR_LEN];
    char eth_gw[SYS_INFO_MAX_STR_LEN];
    char eth_mac[SYS_INFO_MAX_STR_LEN];

    bool wifi_enabled;
    char wifi_mode[SYS_INFO_MAX_STR_LEN];
    bool wifi_sta_is_connected;
    char wifi_sta_ip[SYS_INFO_MAX_STR_LEN];
    char wifi_sta_mask[SYS_INFO_MAX_STR_LEN];
    char wifi_sta_gw[SYS_INFO_MAX_STR_LEN];
    char wifi_sta_mac[SYS_INFO_MAX_STR_LEN];
    char wifi_sta_con_ssid[SYS_INFO_MAX_STR_LEN];
    char wifi_ap_ip[SYS_INFO_MAX_STR_LEN];
    char wifi_ap_mask[SYS_INFO_MAX_STR_LEN];
    char wifi_ap_gw[SYS_INFO_MAX_STR_LEN];
    char wifi_ap_mac[SYS_INFO_MAX_STR_LEN];
    int wifi_ap_connections_count;
} sys_info_t;

extern sys_info_t sys_info;

esp_err_t sys_info_init(void);
