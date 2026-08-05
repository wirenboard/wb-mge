#pragma once

#include <stdbool.h>

#define MAX_FUNCTION_CALLS                  20
#define SETTING_ITEM_MAX_STR_LEN            64

#define KEY_485_VOUT                        "vout"
#define KEY_485_TERM_1                      "485_term_1"
#define KEY_485_TERM_2                      "485_term_2"
#define KEY_485_FAIL_SAFE_1                 "485_fail_safe_1"
#define KEY_485_FAIL_SAFE_2                 "485_fail_safe_2"
#define KEY_485_TX_DISABLED_1               "485_tx_dis_1"
#define KEY_485_TX_DISABLED_2               "485_tx_dis_2"
#define KEY_IO_BUS_ENABLED                  "io_bus"

extern int mock_setting_items_read_bool_called;
extern char mock_setting_items_read_bool_keys[MAX_FUNCTION_CALLS][SETTING_ITEM_MAX_STR_LEN];

bool setting_items_read_bool(const char *key);
void mock_setting_items_set_bool(const char *key, bool value);
void mock_setting_items_reset(void);
