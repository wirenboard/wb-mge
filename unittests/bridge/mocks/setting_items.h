#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define MOCK_SETTING_ITEMS_MAX_CALLS               32
#define MOCK_SETTING_ITEMS_KEY_MAX_LEN             128

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

extern bool mock_setting_items_read_should_fail;
extern int mock_setting_items_read_called;
extern char mock_setting_items_read_keys[MOCK_SETTING_ITEMS_MAX_CALLS][SETTING_ITEM_MAX_STR_LEN];
extern char mock_setting_items_read_value_to_return_parity[SETTING_ITEM_MAX_STR_LEN];
extern char mock_setting_items_read_value_to_return_stopbits[SETTING_ITEM_MAX_STR_LEN];
extern char mock_setting_items_read_value_to_return_databits[SETTING_ITEM_MAX_STR_LEN];
extern char mock_setting_items_read_value_to_return_bridge_mode[SETTING_ITEM_MAX_STR_LEN];
extern char mock_setting_items_read_value_to_return_bridge_ip[SETTING_ITEM_MAX_STR_LEN];

extern bool mock_setting_items_read_int_should_fail;
extern int mock_setting_items_read_int_called;
extern char mock_setting_items_read_int_keys[MOCK_SETTING_ITEMS_MAX_CALLS][SETTING_ITEM_MAX_STR_LEN];

extern int mock_setting_items_read_bool_called;
extern bool mock_setting_items_read_bool_return_value;
extern char mock_setting_items_read_bool_keys[MOCK_SETTING_ITEMS_MAX_CALLS][SETTING_ITEM_MAX_STR_LEN];

esp_err_t setting_items_read(const char *key, char *value);
int setting_items_read_int(const char *key);
bool setting_items_read_bool(const char *key);

void mock_setting_items_reset(void);
