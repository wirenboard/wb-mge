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
/* Port 2 line format. On a WB-MGU port 2 is not a user-facing RS-485 header but the
 * internal WBE2 bus to the Z-Wave board, and that board opens it as 115200 8E1 — a
 * hardcoded constant on its side, not one of its parameters. All three of rate, parity
 * and stop bits therefore have to match out of the box, or the two ends cannot talk
 * until someone changes the settings by hand. Every other board keeps port 2 identical
 * to port 1. These remain ordinary user-editable settings on both boards.
 * Not to be confused with DEFAULT_AIRZONE_SPEED below: that one is the Airzone RS-485
 * line on the far side of the Z-Wave board and has nothing to do with the WBE2 rate. */
#ifdef MODEL_mgu_v1
    #define DEFAULT_BAUDRATE_2      "115200"
    #define DEFAULT_STOPBITS_2      UART_STOP_BITS_1_STR
    #define DEFAULT_PARITY_2        UART_PARITY_EVEN_STR
#else
    #define DEFAULT_BAUDRATE_2      DEFAULT_BAUDRATE
    #define DEFAULT_STOPBITS_2      DEFAULT_STOPBITS
    #define DEFAULT_PARITY_2        DEFAULT_PARITY
#endif

/* Marker for the one-time port 2 line-format migration, see
 * setting_items_migrate_port2_line_format(). "true" means the unit's port 2 already carries
 * the line format the board's defaults prescribe.
 *
 * The migration runs wherever the marker is absent or false, a factory-fresh unit included
 * — on that unit it simply writes the same values the defaults would have written a moment
 * later, so nothing is observably different. The default is "true" for one reason only: a
 * factory reset rewrites every key, and a "false" default would re-arm the migration to fire
 * on the next boot and stamp over a line format the installer had set straight after the
 * reset. */
#define DEFAULT_PORT2_MIGRATED      "true"

#define DEFAULT_485_TERM            "true"
#define DEFAULT_485_FAIL_SAFE       "true"
#define DEFAULT_485_TX_DISABLED     "false"
#define DEFAULT_485_VOUT            "true"
#define DEFAULT_IO_BUS_ENABLED      "true"

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

#define DEFAULT_BRIDGE_MODE         BRIDGE_MODE_SERVER_STR
#define DEFAULT_BRIDGE_PORT         "502"
#define DEFAULT_BRIDGE_IP           "192.168.5.2"
#define DEFAULT_BRIDGE_PORT2        "503"
#define DEFAULT_BRIDGE_MB           "false"
// Per-port cache overlay enable flag (cache_en_N)
#define DEFAULT_CACHE_EN                    "false"
#define DEFAULT_CACHE_MODBUS_PORT           "504"
#define DEFAULT_CACHE_MODBUS_SERVER_ENABLED "true"
#define DEFAULT_CACHE_VALUE_TIMEOUT_S       "60"

#define DEFAULT_UPDATE_CHANNEL              UPDATE_CHANNEL_STABLE_STR

/* Airzone gateway settings. The ranges behind these defaults come from the Z-Wave
 * board and must match it (see setting_validators.c). The line speed is a MULTIPLIER
 * of 1200 baud, not a baud rate: 16 means 19200. */
#define DEFAULT_AIRZONE_ADDRESS             "1"
#define DEFAULT_AIRZONE_ZONE                "1"
#define DEFAULT_AIRZONE_SPEED               "16"
#define DEFAULT_AIRZONE_PRODUCT             "1"
/* Both event counters start at zero on a factory reset and only ever increase. */
#define DEFAULT_AIRZONE_COUNTER             "0"


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
