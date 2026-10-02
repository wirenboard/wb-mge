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
// The relay modules have up to 8 channels; the pulse is long enough for a lock to react and
// short enough not to hold a door open for good.
#define LOCK_CHANNEL_MIN                    1
#define LOCK_CHANNEL_MAX                    8
#define LOCK_PULSE_MS_MIN                   100
#define LOCK_PULSE_MS_MAX                   60000
// The exit button's input on the relay module: one digit, inputs 1..6 of a WB-MR6C.
#define LOCK_EXIT_INPUT_MIN                 '1'
#define LOCK_EXIT_INPUT_MAX                 '6'
// The whole room a setting value has (SETTING_ITEM_MAX_STR_LEN - 1).
#define MAX_TG_STR_LEN                      63
// The whole room a setting value has, as above. The device id is held to the same bound because
// it IS a setting value, and the topic buffers it goes into are sized for it.
#define MAX_MQTT_STR_LEN                    63
#define MAX_MQTT_ID_LEN                     63
#define TG_ADMINS_SEPARATOR                 ','
#define TG_API_URL_HTTP_PREFIX              "http://"
#define TG_API_URL_HTTPS_PREFIX             "https://"
#define TG_API_URL_PATH_SEPARATOR           '/'
#define TG_API_URL_PORT_SEPARATOR           ':'
#define TG_API_URL_QUERY_START              '?'
#define TG_API_URL_FRAGMENT_START           '#'

// A decimal integer in min..max and nothing else.
static bool validate_int_range(const char *value, long min, long max)
{
    if (!value) {
        return false;
    }

    char *endptr;
    long parsed = strtol(value, &endptr, 10);
    if (endptr == value) {
        return false;
    }
    return ((*endptr == '\0') && (parsed >= min) && (parsed <= max));
}

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

// Modbus unit id. 0 is the broadcast address and 248-255 are reserved by the Modbus
// specification, so a slave may only ever answer as 1..247.
bool validate_slave_id(const char *value)
{
    if (!value) {
        return false;
    }

    char *endptr;
    long slave_id = strtol(value, &endptr, 10);
    if ((*endptr == '\0') && (slave_id >= 1) && (slave_id <= 247)) {
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

// Which relay of the module opens the lock, counted from 1 the way the case is labelled.
bool validate_lock_channel(const char *value)
{
    return validate_int_range(value, LOCK_CHANNEL_MIN, LOCK_CHANNEL_MAX);
}

// How long the relay stays on, in milliseconds.
bool validate_lock_pulse_ms(const char *value)
{
    return validate_int_range(value, LOCK_PULSE_MS_MIN, LOCK_PULSE_MS_MAX);
}

// Which relay state opens the door: exactly one of the two mode names.
bool validate_lock_mode(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, LOCK_MODE_STRIKE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    if (strncmp(value, LOCK_MODE_MAGNET_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return false;
}

// The relay module input the exit button is wired to: "off" or a single digit 1..6, nothing else.
bool validate_lock_exit_input(const char *value)
{
    if (!value) {
        return false;
    }
    if (strncmp(value, LOCK_EXIT_INPUT_OFF_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return true;
    }
    return ((value[0] >= LOCK_EXIT_INPUT_MIN) && (value[0] <= LOCK_EXIT_INPUT_MAX) && (value[1] == '\0'));
}

// Printable ASCII of a bounded length, the empty string included. Shared by the Telegram token
// and the three MQTT string settings, which differ only in what they additionally refuse.
static bool validate_printable_or_empty(const char *value, size_t max_len)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return true;
    }
    if (len > max_len) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if ((c < ' ') || (c > '~')) {
            return false;
        }
    }
    return true;
}

// The bot token, or nothing when there is no bot. A token never contains a space, so one is a
// paste error rather than part of it.
bool validate_tg_token(const char *value)
{
    if (!validate_printable_or_empty(value, MAX_TG_STR_LEN)) {
        return false;
    }
    return (strchr(value, ' ') == NULL);
}

// Numeric Telegram user ids separated by commas ("123,456"), or nothing: no empty item, no
// leading or trailing comma, no spaces.
bool validate_tg_admins(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len == 0) {
        return true;
    }
    if (len > MAX_TG_STR_LEN) {
        return false;
    }

    bool digit_seen = false;
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if ((c >= '0') && (c <= '9')) {
            digit_seen = true;
        } else if ((c == TG_ADMINS_SEPARATOR) && digit_seen) {
            digit_seen = false;
        } else {
            return false;
        }
    }
    return digit_seen;
}

// The Bot API server the bot talks to: "http://" or "https://" (lower case), a host, then
// optionally a port and a path - Telegram's own server, a self-hosted telegram-bot-api (usually
// plain http on port 8081) or a mirror. The bot appends "/bot<token>/<method>" to it, so a
// trailing '/' is refused rather than doubled, and so are '?' and '#', which would turn that
// path into a query or a fragment. Printable ASCII only and no spaces: a URL has no other bytes.
bool validate_tg_api_url(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if (len > MAX_TG_STR_LEN) {
        return false;
    }

    const char *host = NULL;
    if (strncmp(value, TG_API_URL_HTTPS_PREFIX, sizeof(TG_API_URL_HTTPS_PREFIX) - 1) == 0) {
        host = value + (sizeof(TG_API_URL_HTTPS_PREFIX) - 1);
    } else if (strncmp(value, TG_API_URL_HTTP_PREFIX, sizeof(TG_API_URL_HTTP_PREFIX) - 1) == 0) {
        host = value + (sizeof(TG_API_URL_HTTP_PREFIX) - 1);
    } else {
        return false;
    }
    // The scheme is followed by a host, not by nothing, a path or a bare port.
    if ((*host == '\0') || (*host == TG_API_URL_PATH_SEPARATOR) || (*host == TG_API_URL_PORT_SEPARATOR)) {
        return false;
    }
    if (value[len - 1] == TG_API_URL_PATH_SEPARATOR) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if ((c <= ' ') || (c > '~') || (c == TG_API_URL_QUERY_START) || (c == TG_API_URL_FRAGMENT_START)) {
            return false;
        }
    }
    return true;
}

// The broker's address: a host name or an IP literal, or nothing at all - "no broker configured"
// is the factory state and the way a user removes one. Spaces are refused because a host name
// cannot contain one and an address that does is a typo, not a host.
bool validate_mqtt_host(const char *value)
{
    if (!validate_printable_or_empty(value, MAX_MQTT_STR_LEN)) {
        return false;
    }
    return (strchr(value, ' ') == NULL);
}

// The user name and the password. Both may be empty: a broker that takes anonymous connections
// is the common case on a controller's own bus.
bool validate_mqtt_credential(const char *value)
{
    return validate_printable_or_empty(value, MAX_MQTT_STR_LEN);
}

// The name this device appears under in /devices/<id>/... . It is pasted into a topic, so the
// characters MQTT gives a meaning of their own are refused rather than escaped: '/' would split
// the id across topic levels, and '+' and '#' are the wildcards a subscription is built from.
// The empty string is allowed and means "derive one from the model and the MAC".
bool validate_mqtt_id(const char *value)
{
    if (!validate_printable_or_empty(value, MAX_MQTT_ID_LEN)) {
        return false;
    }
    if (strchr(value, '/') != NULL) {
        return false;
    }
    if (strchr(value, '+') != NULL) {
        return false;
    }
    if (strchr(value, '#') != NULL) {
        return false;
    }
    return (strchr(value, ' ') == NULL);
}
