#pragma once

#include <esp_err.h>
#include <stdbool.h>

#define SETTING_ITEM_MAX_STR_LEN            64          // WPA2 passwords can be up to 63 characters + null terminator
#define SETTING_ITEMS_NUM_MAX               50

// Keys for storing settings
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
#define KEY_STA_DHCPC               "sta_dhcpc"
#define KEY_STA_IP_STATIC           "sta_ip_static"
#define KEY_STA_MASK_STATIC         "sta_mask_static"
#define KEY_STA_GW_STATIC           "sta_gw_static"

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

// Enable MIO on RS485-2
#define KEY_IO_BUS_ENABLED          "io_bus"

// WiFi modes
#define WIFI_MODE_AP_STR            "ap"
#define WIFI_MODE_STA_STR           "sta"
#define WIFI_MODE_APSTA_STR         "apsta"
#define WIFI_MODE_NONE_STR          "none"

// Wifi auth
#define WIFI_AUTH_OPEN_STR          "open"
#define WIFI_AUTH_WPA2_PSK_STR      "wpa2_psk"
#define WIFI_AUTH_WPA3_PSK_STR      "wpa3_psk"

// UART stop bits
#define UART_STOP_BITS_1_STR        "1"
#define UART_STOP_BITS_1_5_STR      "1.5"
#define UART_STOP_BITS_2_STR        "2"

// UART data bits
#define UART_DATA_5_BITS_STR        "5"
#define UART_DATA_6_BITS_STR        "6"
#define UART_DATA_7_BITS_STR        "7"
#define UART_DATA_8_BITS_STR        "8"

// UART parity
#define UART_PARITY_DISABLE_STR     "none"
#define UART_PARITY_EVEN_STR        "even"
#define UART_PARITY_ODD_STR         "odd"

// Bridge modes
#define BRIDGE_MODE_SERVER_STR      "server"
#define BRIDGE_MODE_CLIENT_STR      "client"

// Port manager mode keys (per-port NVS keys)
#define KEY_PORT_MODE1              "port_mode_1"
#define KEY_PORT_MODE2              "port_mode_2"

// Cache Modbus TCP server port NVS key (max 15 chars for ESP32 NVS)
#define KEY_CACHE_MODBUS_PORT               "cache_mb_port"
// Cache Modbus TCP server enable/disable NVS key (max 15 chars for ESP32 NVS)
#define KEY_CACHE_MODBUS_SERVER_ENABLED     "cache_mb_srv_en"
// Cache value timeout in seconds NVS key (max 15 chars for ESP32 NVS)
#define KEY_CACHE_VALUE_TIMEOUT_S           "cache_val_tout"

// Port manager mode string values
#define PORT_MODE_DISABLED_STR      "disabled"
#define PORT_MODE_TCP_BRIDGE_STR    "tcp_bridge"
#define PORT_MODE_SNIFFER_STR       "sniffer"
#define PORT_MODE_CACHE_BUS_STR     "cache_bus"

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
esp_err_t setting_items_set_defaults(bool only_uninitialized);

// Iterator functions for all settings
size_t setting_items_get_count(void);
const char *setting_items_get_key_at(size_t index);

// Get default value for specified key from setting_items array
const char *setting_items_get_default_value(const char *key);

// Type introspection functions
setting_item_type_t setting_items_get_type(const char *key);
const char *setting_items_type_to_string(setting_item_type_t type);
