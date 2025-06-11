#pragma once

#define UART_BAUD_RATE_MIN          300
#define UART_BAUD_RATE_MAX          460800

#define SETTING_ITEMS_NUM_MAX       50
#define SETTING_ITEM_MAX_STR_LEN    32

// Ключи для хранения настроек
#define KEY_HOSTNAME                "hostname"
#define KEY_LOGIN                   "login"
#define KEY_PASS                    "pass"

#define KEY_BAUDRATE                "baudrate"
#define KEY_STOPBITS                "stopbits"
#define KEY_PARITY                  "parity"
#define KEY_DATABITS                "databits"

#define KEY_BAUDRATE2               "baudrate_2"
#define KEY_STOPBITS2               "stopbits_2"
#define KEY_PARITY2                 "parity_2"
#define KEY_DATABITS2               "databits_2"

#define KEY_ETH_IP_STATIC           "eth_ip_static"
#define KEY_ETH_MASK_STATIC         "eth_mask_static"
#define KEY_ETH_GW_STATIC           "eth_gw_static"
#define KEY_ETH_DHCPC               "eth_dhcpc"

#define KEY_WIFI_MODE               "wifi_mode"
#define KEY_AP_IP_STATIC            "ap_ip_static"
#define KEY_AP_MASK_STATIC          "ap_mask_static"
#define KEY_AP_GW_STATIC            "ap_gw_static"
#define KEY_AP_SSID                 "ap_ssid"
#define KEY_AP_PASS                 "ap_pass"
#define KEY_STA_SSID                "sta_ssid"
#define KEY_STA_PASS                "sta_pass"

#define KEY_BRIDGE_MODE             "bridge_mode"
#define KEY_BRIDGE_PORT             "bridge_port"
#define KEY_BRIDGE_IP               "bridge_ip"
#define KEY_BRIDGE_MB               "bridge_mb"
#define KEY_BRIDGE_MODE2            "bridge_mode_2"
#define KEY_BRIDGE_PORT2            "bridge_port_2"
#define KEY_BRIDGE_IP2              "bridge_ip_2"
#define KEY_BRIDGE_MB2              "bridge_mb_2"

// WiFi режимы
#define WIFI_MODE_AP_STR            "ap"
#define WIFI_MODE_STA_STR           "sta"
#define WIFI_MODE_APSTA_STR         "apsta"
#define WIFI_MODE_NULL_STR          "none"

// UART стоп-биты
#define UART_STOP_BITS_1_STR        "1-bit"
#define UART_STOP_BITS_1_5_STR      "1.5-bit"
#define UART_STOP_BITS_2_STR        "2-bit"

// UART биты данных
#define UART_DATA_5_BITS_STR        "5-bit"
#define UART_DATA_6_BITS_STR        "6-bit"
#define UART_DATA_7_BITS_STR        "7-bit"
#define UART_DATA_8_BITS_STR        "8-bit"

// UART четность
#define UART_PARITY_DISABLE_STR     "none"
#define UART_PARITY_EVEN_STR        "even"
#define UART_PARITY_ODD_STR         "odd"

// Режимы моста
#define BRIDGE_MODE_SERVER_STR      "tcps-serial"
#define BRIDGE_MODE_CLIENT_STR      "tcpc-serial"
