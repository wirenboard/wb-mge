#pragma once

#define BASE_HOSTNAME               "WB-MGE"
#define FIRMWARE_VERSION            FW_VERSION_STRING

// Значения по умолчанию
#define DEFAULT_LOGIN               "wirenboard"
#define DEFAULT_PASS                "wirenboard"
#define DEFAULT_BAUDRATE            9600
#define DEFAULT_STOPBITS            UART_STOP_BITS_1_STR
#define DEFAULT_PARITY              UART_PARITY_DISABLE_STR
#define DEFAULT_DATABITS            UART_DATA_8_BITS_STR
#define DEFAULT_ETH_IP_STATIC       "192.168.5.1"
#define DEFAULT_ETH_MASK_STATIC     "255.255.255.0"
#define DEFAULT_ETH_GW_STATIC       "192.168.5.1"
#define DEFAULT_ETH_DHCPC           true
#define DEFAULT_WIFI_MODE           WIFI_MODE_AP_STR
#define DEFAULT_WIFI_AUTH           WIFI_AUTH_OPEN_STR
#define DEFAULT_AP_IP_STATIC        "192.168.4.1"
#define DEFAULT_AP_MASK_STATIC      "255.255.255.0"
#define DEFAULT_AP_GW_STATIC        "192.168.4.1"
#define DEFAULT_AP_PASS             ""
#define DEFAULT_STA_SSID            ""
#define DEFAULT_STA_PASS            ""
#define DEFAULT_BRIDGE_MODE         BRIDGE_MODE_SERVER_STR
#define DEFAULT_BRIDGE_PORT         502
#define DEFAULT_BRIDGE_IP           "192.168.4.2"
#define DEFAULT_BRIDGE_PORT2        503
#define DEFAULT_BRIDGE_MB           false
#define DEFAULT_485_VOUT            true
#define DEFAULT_485_TERM            false
#define DEFAULT_485_FAIL_SAFE       false
#define DEFAULT_IO_BUS_ENABLED      false
#define DEFAULT_WEB_PORT            80
