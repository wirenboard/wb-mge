#pragma once

#include "bridge.h"
#include <stdbool.h>

#define SETTING_ITEM_MAX_STR_LEN                   64

#define BRIDGE_MODE_SERVER_STR                     "server"
#define BRIDGE_MODE_CLIENT_STR                     "client"

#define UART_STOP_BITS_1_STR                       "1"
#define UART_STOP_BITS_1_5_STR                     "1.5"
#define UART_STOP_BITS_2_STR                       "2"

#define UART_DATA_5_BITS_STR                       "5"
#define UART_DATA_6_BITS_STR                       "6"
#define UART_DATA_7_BITS_STR                       "7"
#define UART_DATA_8_BITS_STR                       "8"

#define UART_PARITY_DISABLE_STR                    "none"
#define UART_PARITY_EVEN_STR                       "even"
#define UART_PARITY_ODD_STR                        "odd"

typedef struct {
    uint32_t baudrate;
    char parity[SETTING_ITEM_MAX_STR_LEN];
    char stopbits[SETTING_ITEM_MAX_STR_LEN];
    char databits[SETTING_ITEM_MAX_STR_LEN];
} serial_test_config_t;

typedef struct {
    serial_test_config_t serial_config;
    char bridge_mode[SETTING_ITEM_MAX_STR_LEN];
    char bridge_ip[SETTING_ITEM_MAX_STR_LEN];
    int bridge_port;
    bool bridge_mb;
} mock_bridge_test_config_t;

typedef struct {
    esp_err_t parity;
    esp_err_t stopbits;
    esp_err_t databits;
    esp_err_t bridge_mode;
    esp_err_t bridge_ip;
} mock_setting_items_read_results_t;

typedef struct {
    int read_called;
    int read_int_called;
    int read_bool_called;
    mock_setting_items_read_results_t read_result;
} mock_setting_items_calls_t;

extern mock_bridge_test_config_t mock_settings_items_bridge_cfg[BRIDGES_COUNT];
extern mock_setting_items_calls_t mock_setting_items_calls[BRIDGES_COUNT];

esp_err_t setting_items_read(const char *key, char *value);
int setting_items_read_int(const char *key);
bool setting_items_read_bool(const char *key);

void mock_setting_items_reset(void);
