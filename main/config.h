#pragma once

#define WIFI_CHAN_AP                9

#define BASE_HOSTNAME               "WB-MGE" // генерируется в setting_items.c: get_dynamic_hostname
#define FIRMWARE_VERSION            FW_VERSION_STRING

// Значения по умолчанию
#define DEFAULT_LOGIN               "admin"
#define DEFAULT_PASS                "admin"
#define DEFAULT_BAUDRATE            "9600"
#define DEFAULT_STOPBITS            UART_STOP_BITS_2_STR
#define DEFAULT_PARITY              UART_PARITY_DISABLE_STR
#define DEFAULT_DATABITS            UART_DATA_8_BITS_STR
#define DEFAULT_ETH_IP_STATIC       "192.168.0.7"
#define DEFAULT_ETH_MASK_STATIC     "255.255.255.0"
#define DEFAULT_ETH_GW_STATIC       "192.168.0.1"
#define DEFAULT_ETH_DHCPC           "true"
#define DEFAULT_WIFI_MODE           WIFI_MODE_AP_STR
#define DEFAULT_WIFI_AUTH           WIFI_AUTH_OPEN_STR // TODO: в релизе поменять на WIFI_AUTH_WPA2_PSK_STR. Пароль будет печататься на наклейке.
#define DEFAULT_AP_IP_STATIC        "192.168.42.1"
#define DEFAULT_AP_MASK_STATIC      "255.255.255.0"
#define DEFAULT_AP_GW_STATIC        "192.168.42.1"
#define DEFAULT_AP_PASS             "" // генерируется в setting_items.c: get_dynamic_ap_pass_default
#define DEFAULT_STA_SSID            ""
#define DEFAULT_STA_PASS            ""
#define DEFAULT_BRIDGE_MODE         BRIDGE_MODE_SERVER_STR
#define DEFAULT_BRIDGE_PORT         "502"
#define DEFAULT_BRIDGE_IP           "192.168.42.2"
#define DEFAULT_BRIDGE_PORT2        "503"
#define DEFAULT_BRIDGE_MB           "false"
#define DEFAULT_485_VOUT            "false"
#define DEFAULT_485_TERM            "true"
#define DEFAULT_485_FAIL_SAFE       "true"
#define DEFAULT_IO_BUS_ENABLED      "true"
#define DEFAULT_WEB_PORT            "80"
