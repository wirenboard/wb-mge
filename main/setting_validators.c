#include "setting_validators.h"
#include "setting_items.h"

#include <stddef.h>
#include <string.h>

#define MAX_HOSTNAME_LEN                    31
#define MAX_SSID_LEN                        31          // In ESP-IDF there is ssid[32], terminating '\0' included
#define MAX_LOGIN_LEN                       31
#define MAX_PASS_LEN                        31
#define MIN_WIFI_PASS_LEN                   8
#define MAX_WIFI_PASS_LEN                   63
#define BAUDRATE_MIN                        1200
#define BAUDRATE_MAX                        115200

// Validation functions
bool validate_hostname(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return false;
    }
    if (len > MAX_HOSTNAME_LEN) {
        return false;
    }

    // Basic hostname validation, allow only alphanumeric and hyphens
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (c >= 'a') {
            if (c <= 'z') {
                continue;
            }
        }
        if (c >= 'A') {
            if (c <= 'Z') {
                continue;
            }
        }
        if (c >= '0') {
            if (c <= '9') {
                continue;
            }
        }
        if (c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

bool validate_ssid(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return false;
    }
    if (len > MAX_SSID_LEN) {
        return false;
    }

    // Basic SSID validation, allow all printable symbols in range 0x20 - 0x7E
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (c < '\x20') {
            return false;
        }
        if (c > '\x7E') {
            return false;
        }
    }
    return true;
}

// Station SSID validation. The empty string is accepted here and rejected by
// validate_ssid() because the two keys mean different things: an access point always
// transmits a name, while a station has a legitimate "no network configured" state —
// and that state is exactly the factory default (DEFAULT_STA_SSID is ""). With the
// stricter validator the device refused the value it had stored itself, so a
// configuration exported by GET /settings could not be imported back by POST /settings
// on any device that had never joined a Wi-Fi network. Non-empty values keep the
// AP rules, so the check is delegated instead of copied.
bool validate_sta_ssid(const char *value)
{
    if (!value) {
        return false;
    }
    if (value[0] == '\0') {
        return true;
    }
    return validate_ssid(value);
}

bool validate_port(const char *value)
{
    if (!value) {
        return false;
    }

    char *endptr;
    long port = strtol(value, &endptr, 10);
    if ((*endptr == '\0') && (port >= 1) && (port <= 65535)) {
        return true;
    } else {
        return false;
    }
}

bool validate_timeout(const char *value)
{
    if (!value || *value == '\0') {
        return false;
    }
    char *endptr;
    long v = strtol(value, &endptr, 10);
    /* 0 = disable timeout; 1..65535 = timeout in seconds */
    if ((*endptr == '\0') && (v >= 0) && (v <= 65535)) {
        return true;
    } else {
        return false;
    }
}

bool validate_baudrate(const char *value)
{
    if (!value) {
        return false;
    }

    char *endptr;
    long baudrate = strtol(value, &endptr, 10);
    if ((*endptr == '\0') && (baudrate >= BAUDRATE_MIN) && (baudrate <= BAUDRATE_MAX)) {
        return true;
    } else {
        return false;
    }
}

bool validate_stopbits(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, UART_STOP_BITS_1_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_STOP_BITS_1_5_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_STOP_BITS_2_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_parity(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, UART_PARITY_DISABLE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_PARITY_EVEN_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_PARITY_ODD_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_databits(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, UART_DATA_5_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_DATA_6_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_DATA_7_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UART_DATA_8_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_ip(const char *value)
{
    if (!value) {
        return false;
    }
    if (strlen(value) == 0) {
        return false;
    }

    // Enhanced IP validation with proper range checking
    int a = 0, b = 0, c = 0, d = 0;
    char extra = 0;

    // Check format and ensure no extra characters
    if (sscanf(value, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) {
        return false;
    }

    // Check ranges (0-255 for each octet)
    if (a < 0) {
        return false;
    }
    if (a > 255) {
        return false;
    }
    if (b < 0) {
        return false;
    }
    if (b > 255) {
        return false;
    }
    if (c < 0) {
        return false;
    }
    if (c > 255) {
        return false;
    }
    if (d < 0) {
        return false;
    }
    if (d > 255) {
        return false;
    }

    // Additional checks for reserved ranges could be added here
    return true;
}

bool validate_wifi_mode(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, WIFI_MODE_AP_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, WIFI_MODE_STA_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, WIFI_MODE_APSTA_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, WIFI_MODE_NONE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_wifi_auth(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, WIFI_AUTH_OPEN_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, WIFI_AUTH_WPA2_PSK_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, WIFI_AUTH_WPA3_PSK_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_bridge_mode(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, BRIDGE_MODE_SERVER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, BRIDGE_MODE_CLIENT_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_port_mode(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, PORT_MODE_DISABLED_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, PORT_MODE_TCP_BRIDGE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, PORT_MODE_PASSIVE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, PORT_MODE_REPEATER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_update_channel(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, UPDATE_CHANNEL_STABLE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, UPDATE_CHANNEL_TESTING_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

bool validate_bool(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, "true", SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, "false", SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

// Add validation for login strings
bool validate_login(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return false;
    }
    if (len > MAX_LOGIN_LEN) {
        return false;
    }

    // Basic alphanumeric validation for login
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (c >= 'a') {
            if (c <= 'z') {
                continue;
            }
        }
        if (c >= 'A') {
            if (c <= 'Z') {
                continue;
            }
        }
        if (c >= '0') {
            if (c <= '9') {
                continue;
            }
        }
        if (c == '_') {
            continue;
        }
        if (c == '-') {
            continue;
        }
        return false;
    }
    return true;
}

bool validate_password(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return false;
    }
    if (len > MAX_PASS_LEN) {
        return false;
    }

    // Basic password validation - can be enhanced with regex or additional rules
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (c < ' ') {
            return false;
        }
        if (c > '~') {
            return false;
        }
    }
    return true;
}

// Wi-Fi passphrase validation (sta_pass / ap_pass).
// Allows either an empty string (open network, no passphrase) or a WPA2
// passphrase of 8..63 printable characters (0x20..0x7E inclusive).
bool validate_wifi_password(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return true;  // empty means open network (no passphrase)
    }
    if (len < MIN_WIFI_PASS_LEN) {
        return false;
    }
    if (len > MAX_WIFI_PASS_LEN) {
        return false;
    }

    // Allow only printable symbols in range 0x20 - 0x7E
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (c < ' ') {
            return false;
        }
        if (c > '~') {
            return false;
        }
    }
    return true;
}
