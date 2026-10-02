#pragma once


#define WIFI_CHAN_AP                9

#define BASE_HOSTNAME               "WB-MGE" // generated in setting_items.c: get_dynamic_hostname

// Number of RS-485 ports on the board.
#define RS485_PORTS_COUNT           2


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
#define DEFAULT_485_TX_DISABLED     "false"
#define DEFAULT_485_VOUT            "true"
#define DEFAULT_IO_BUS_ENABLED      "true"

#define DEFAULT_MB_SLAVE_ID         "1"
#define DEFAULT_MB_TCP_PORT         "502"

#define DEFAULT_ETH_IP_STATIC       "192.168.0.7"
#define DEFAULT_ETH_MASK_STATIC     "255.255.255.0"
#define DEFAULT_ETH_GW_STATIC       "192.168.0.1"
#define DEFAULT_ETH_DHCPC           "true"

#define DEFAULT_WIFI_PERM_DISABLE   "false"
#define DEFAULT_WIFI_MODE           WIFI_MODE_AP_STR
#define DEFAULT_WIFI_AUTH           WIFI_AUTH_WPA2_PSK_STR
#define DEFAULT_AP_IP_STATIC        "192.168.5.1"
#define DEFAULT_AP_MASK_STATIC      "255.255.255.0"
#define DEFAULT_AP_GW_STATIC        "192.168.5.1"
#define DEFAULT_AP_PASS             "" // generated in setting_items.c: get_dynamic_ap_pass_default
#define DEFAULT_STA_SSID            ""
#define DEFAULT_STA_PASS            ""
#define DEFAULT_STA_DHCPC           "true"
#define DEFAULT_STA_IP_STATIC       "192.168.1.7"
#define DEFAULT_STA_MASK_STATIC     "255.255.255.0"
#define DEFAULT_STA_GW_STATIC       "192.168.1.1"

#define DEFAULT_UPDATE_CHANNEL              UPDATE_CHANNEL_STABLE_STR

// Fingerprint access control (main/fp_acs/): the relay of the lock - unit id on RS-485 port 1,
// channel counted from 1 -, how long an opening lasts and which relay state opens the door (an
// electric strike: relay ON opens it, as before the setting existed).
#define DEFAULT_LOCK_ADDR           "1"
#define DEFAULT_LOCK_CHANNEL        "1"
#define DEFAULT_LOCK_PULSE_MS       "1000"
#define DEFAULT_LOCK_MODE           LOCK_MODE_STRIKE_STR

// Telegram bot: no token means no bot; the admins are numeric Telegram user ids; the Bot API
// server is Telegram's own unless it is blocked where the device runs; no chat to notify means
// fingerprint openings are not reported.
#define DEFAULT_TG_TOKEN            ""
#define DEFAULT_TG_ADMINS           ""
#define DEFAULT_TG_API_URL          "https://api.telegram.org"
#define DEFAULT_TG_NOTIFY_CHAT      ""

// MQTT. Off out of the box with nothing configured: a device that has never been told about a
// broker must not go looking for one. The empty id means "build one from the model and the MAC".
#define DEFAULT_MQTT_ENABLED        "false"
#define DEFAULT_MQTT_HOST           ""
#define DEFAULT_MQTT_PORT           "1883"
#define DEFAULT_MQTT_USER           ""
#define DEFAULT_MQTT_PASS           ""
#define DEFAULT_MQTT_ID             ""


#ifdef MODEL_mge_v3
    #define DEVICE_MODEL            "WB-MGE v.3"

#elif defined(MODEL_mgu_v1)
    #define DEVICE_MODEL            "WB-MGU v.1"

#elif QEMU_BUILD
    // QEMU build
    #define DEVICE_MODEL            "QEMU WB-MGE v.3"

#elif defined(__unittest_env__)
    // Unit tests build
    #define DEVICE_MODEL            "TEST WB-MGE v.3"

#else
    #error "Unknown device signature"
#endif
