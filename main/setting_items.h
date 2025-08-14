#pragma once

#include <esp_err.h>
#include <stdbool.h>

#define SETTING_ITEM_MAX_STR_LEN    64 // WPA2 passwords can be up to 63 characters + null terminator

#define SETTING_ITEMS_NUM_MAX       50

// Ключи для хранения настроек
#define KEY_HOSTNAME                "hostname"
#define KEY_WEB_PORT                "web_port"
#define KEY_LOGIN                   "login"
#define KEY_PASS                    "pass"

#define KEY_BAUDRATE1               "baudrate_1"
#define KEY_STOPBITS1               "stopbits_1"
#define KEY_PARITY1                 "parity_1"
#define KEY_DATABITS1               "databits_1"

#define KEY_BAUDRATE2               "baudrate_2"
#define KEY_STOPBITS2               "stopbits_2"
#define KEY_PARITY2                 "parity_2"
#define KEY_DATABITS2               "databits_2"

#define KEY_ETH_IP_STATIC           "eth_ip_static"
#define KEY_ETH_MASK_STATIC         "eth_mask_static"
#define KEY_ETH_GW_STATIC           "eth_gw_static"
#define KEY_ETH_DHCPC               "eth_dhcpc"

#define KEY_WIFI_MODE               "wifi_mode"
#define KEY_WIFI_AUTH_AP            "ap_auth"
#define KEY_WIFI_AUTH_STA           "sta_auth"
#define KEY_AP_IP_STATIC            "ap_ip_static"
#define KEY_AP_MASK_STATIC          "ap_mask_static"
#define KEY_AP_GW_STATIC            "ap_gw_static"
#define KEY_AP_SSID                 "ap_ssid"
#define KEY_AP_PASS                 "ap_pass"
#define KEY_STA_SSID                "sta_ssid"
#define KEY_STA_PASS                "sta_pass"

#define KEY_BRIDGE_MODE1            "bridge_mode_1"
#define KEY_BRIDGE_PORT1            "bridge_port_1"
#define KEY_BRIDGE_IP1              "bridge_ip_1"
#define KEY_BRIDGE_MB1              "bridge_modbus_1"
#define KEY_BRIDGE_MODE2            "bridge_mode_2"
#define KEY_BRIDGE_PORT2            "bridge_port_2"
#define KEY_BRIDGE_IP2              "bridge_ip_2"
#define KEY_BRIDGE_MB2              "bridge_modbus_2"

#define KEY_485_VOUT                "vout"
#define KEY_485_TERM_1              "485_term_1"
#define KEY_485_TERM_2              "485_term_2"
#define KEY_485_FAIL_SAFE_1         "485_fail_safe_1"
#define KEY_485_FAIL_SAFE_2         "485_fail_safe_2"

// Включение MIO на RS485-2
#define KEY_IO_BUS_ENABLED          "io_bus"

// WiFi режимы
#define WIFI_MODE_AP_STR            "ap"
#define WIFI_MODE_STA_STR           "sta"
#define WIFI_MODE_APSTA_STR         "apsta"
#define WIFI_MODE_NONE_STR          "none"

// Wifi auth
#define WIFI_AUTH_OPEN_STR          "open"
#define WIFI_AUTH_WPA2_PSK_STR      "wpa2_psk"
#define WIFI_AUTH_WPA3_PSK_STR      "wpa3_psk"

// UART стоп-биты
#define UART_STOP_BITS_1_STR        "1"
#define UART_STOP_BITS_1_5_STR      "1.5"
#define UART_STOP_BITS_2_STR        "2"

// UART биты данных
#define UART_DATA_5_BITS_STR        "5"
#define UART_DATA_6_BITS_STR        "6"
#define UART_DATA_7_BITS_STR        "7"
#define UART_DATA_8_BITS_STR        "8"

// UART четность
#define UART_PARITY_DISABLE_STR     "none"
#define UART_PARITY_EVEN_STR        "even"
#define UART_PARITY_ODD_STR         "odd"

// Режимы моста
#define BRIDGE_MODE_SERVER_STR      "server"
#define BRIDGE_MODE_CLIENT_STR      "client"

// Setting types - used for type checking and JSON mapping
typedef enum {
    SETTING_ITEM_TYPE_STRING,
    SETTING_ITEM_TYPE_BOOL,
    SETTING_ITEM_TYPE_INT,
    SETTING_ITEM_TYPE_INVALID
} setting_item_type_t;

// Storage interface for dependency injection (mainly for testing)
typedef struct {
    bool (*has_key)(const char* key);
    esp_err_t (*write_str)(const char* key, const char* value);
    esp_err_t (*read_str)(const char* key, char* value);
} setting_storage_iface_t;

// Core functions
esp_err_t setting_items_init(void);
esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *storage_iface);
esp_err_t setting_items_save(const char *key, const char *value);
esp_err_t setting_items_read(const char *key, char *value);

// Convenience wrapper functions for common types
bool setting_items_read_bool(const char *key);
int setting_items_read_int(const char *key);

esp_err_t setting_items_save_bool(const char *key, bool value);
esp_err_t setting_items_save_int(const char *key, int value);
esp_err_t setting_items_set_default(const char *key);

// Iterator functions for all settings
size_t setting_items_get_count(void);
const char *setting_items_get_key_at(size_t index);

// Type introspection functions
setting_item_type_t setting_items_get_type(const char *key);
const char *setting_items_type_to_string(setting_item_type_t type);
