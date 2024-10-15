#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"

#define SYS_INFO_MAX_STR_LEN    32

typedef struct {
    int device_serial_num;
    char device_name[SYS_INFO_MAX_STR_LEN];
    char firmware_ver[SYS_INFO_MAX_STR_LEN];
    char hardware_ver[SYS_INFO_MAX_STR_LEN];
    bool eth_is_connected;
    char eth_ip[SYS_INFO_MAX_STR_LEN];
    char eth_mask[SYS_INFO_MAX_STR_LEN];
    char eth_gw[SYS_INFO_MAX_STR_LEN];
    char eth_mac[SYS_INFO_MAX_STR_LEN];
    bool wifi_sta_is_connected;
    char wifi_sta_ip[SYS_INFO_MAX_STR_LEN];
    char wifi_sta_mask[SYS_INFO_MAX_STR_LEN];
    char wifi_sta_gw[SYS_INFO_MAX_STR_LEN];
    int wifi_ap_connections_count;
} sys_info_t;

extern sys_info_t sys_info;

esp_err_t sys_info_init(void);
esp_err_t sys_info_write_factory_data(void);
