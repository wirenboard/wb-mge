#pragma once


#define WIFI_CHAN_AP                9

#define BASE_HOSTNAME               "WB-MGE" // generated in setting_items.c: get_dynamic_hostname


// Default values

#define DEFAULT_LOGIN               "admin"
#define DEFAULT_PASS                "admin"
#define DEFAULT_WEB_PORT            "80"

#define DEFAULT_BAUDRATE            "9600"
#define DEFAULT_STOPBITS            UART_STOP_BITS_2_STR
#define DEFAULT_PARITY              UART_PARITY_DISABLE_STR
#define DEFAULT_DATABITS            UART_DATA_8_BITS_STR
#define DEFAULT_485_TERM            "true"
#define DEFAULT_485_FAIL_SAFE       "true"
#define DEFAULT_485_VOUT            "true"
#define DEFAULT_IO_BUS_ENABLED      "true"

#define DEFAULT_ETH_IP_STATIC       "192.168.0.7"
#define DEFAULT_ETH_MASK_STATIC     "255.255.255.0"
#define DEFAULT_ETH_GW_STATIC       "192.168.0.1"
#define DEFAULT_ETH_DHCPC           "true"

#define DEFAULT_WIFI_MODE           WIFI_MODE_APSTA_STR
#define DEFAULT_WIFI_AUTH           WIFI_AUTH_WPA2_PSK_STR
#define DEFAULT_AP_IP_STATIC        "192.168.5.1"
#define DEFAULT_AP_MASK_STATIC      "255.255.255.0"
#define DEFAULT_AP_GW_STATIC        "192.168.5.1"
#define DEFAULT_AP_PASS             "" // generated in setting_items.c: get_dynamic_ap_pass_default
#define DEFAULT_STA_SSID            "Telekom-670917"
#define DEFAULT_STA_PASS            "ep6ebbt4ap4b"
#define DEFAULT_STA_DHCPC           "true"
#define DEFAULT_STA_IP_STATIC       "192.168.1.7"
#define DEFAULT_STA_MASK_STATIC     "255.255.255.0"
#define DEFAULT_STA_GW_STATIC       "192.168.1.1"

#define DEFAULT_BRIDGE_MODE         BRIDGE_MODE_SERVER_STR
#define DEFAULT_BRIDGE_PORT         "502"
#define DEFAULT_BRIDGE_IP           "192.168.5.2"
#define DEFAULT_BRIDGE_PORT2        "503"
#define DEFAULT_BRIDGE_MB           "false"

#define DEFAULT_KNX_ENABLED         "true"
#define DEFAULT_KNX_PORT            "3671"
#define DEFAULT_KNX_DEVICE_AUTH     "trustme"
#define DEFAULT_KNX_USER_PASS       "secret"

/* WBE2-I-KNX module: NCN5121 TPUART on UART1 */
#define KNX_UART_NUM                UART_NUM_1
#define KNX_UART_RX_PIN            4
#define KNX_UART_TX_PIN            10
#define KNX_UART_BAUD              38400


#ifdef MODEL_mge_v3
    #define DEVICE_MODEL            "WB-MGE v.3"

#elif QEMU_BUILD
    // QEMU build
    #define DEVICE_MODEL            "QEMU WB-MGE v.3"

#elif defined(__unittest_env__)
    // Unit tests build
    #define DEVICE_MODEL            "TEST WB-MGE v.3"

#else
    #error "Unknown device signature"
#endif
