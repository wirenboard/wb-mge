#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"

#define SYS_INFO_MAX_STR_LEN    32

extern char sys_info_device_name[SYS_INFO_MAX_STR_LEN];
extern char sys_info_firmware[SYS_INFO_MAX_STR_LEN];
extern char sys_info_hardware[SYS_INFO_MAX_STR_LEN];
extern int sys_info_serial_num;

extern bool sys_info_con_eth;
extern char sys_info_eth_ip[SYS_INFO_MAX_STR_LEN];
extern char sys_info_eth_mask[SYS_INFO_MAX_STR_LEN];
extern char sys_info_eth_gw[SYS_INFO_MAX_STR_LEN];
extern char sys_info_eth_mac[SYS_INFO_MAX_STR_LEN];

extern bool sys_info_con_sta;
extern char sys_info_sta_ip[SYS_INFO_MAX_STR_LEN];
extern char sys_info_sta_mask[SYS_INFO_MAX_STR_LEN];
extern char sys_info_sta_gw[SYS_INFO_MAX_STR_LEN];

esp_err_t sys_info_init(void);
esp_err_t sys_info_write(void);
