#include "setting_validators.h"
#include "setting_items.h"
#include "config.h"
// Headers only: the mixer bounds below are the engine's and the drive's, so they are taken
// from where they are defined rather than copied as bare numbers.
#include "mixer.h"
#include "program.h"
#include "vfd_t13.h"

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
// In bytes, and the whole room a setting value has (SETTING_ITEM_MAX_STR_LEN - 1). A name in
// Cyrillic costs two bytes per character, so this is ~31 characters there and 63 in Latin.
#define MAX_MIXER_NAME_LEN                  63
// The whole room a setting value has, as above. The device id is held to the same bound because
// it IS a setting value, and the topic buffers it goes into are sized for it.
#define MAX_MQTT_STR_LEN                    63
#define MAX_MQTT_ID_LEN                     63

// Every mixer setting but the program itself is a small bounded integer, so they share one
// helper instead of four copies of the same strtol.
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

// Mixer program: the base64 step list itself, checked by the model that will have to decode
// it (main/mixer/program.c). The empty string is a program with no steps, which is both the
// factory default and how a user clears one, so it is accepted here.
bool validate_mixer_program(const char *value)
{
    return mixer_program_is_valid(value);
}

// Which of the four stored programs is the active one.
bool validate_mixer_prog_index(const char *value)
{
    return validate_int_range(value, 0, MIXER_PROGRAM_COUNT - 1);
}

// Display name of a stored program: any printable text, never empty - a nameless program
// cannot be told from another one in the web UI. What "printable" excludes is spelled out
// in the loop below.
bool validate_mixer_prog_name(const char *value)
{
    if (!value) {
        return false;
    }

    size_t len = strlen(value);
    if ((len == 0) || (len > MAX_MIXER_NAME_LEN)) {
        return false;
    }

    // A program name is a label a human reads, so it carries whatever alphabet that human
    // writes in: the length above is in BYTES, and a UTF-8 name simply spends more of them
    // per character. Only the C0 control codes and DEL are refused - they would break the
    // JSON of GET /settings and mean nothing on a button.
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];
        if ((c < ' ') || (c == 0x7F)) {
            return false;
        }
    }
    return true;
}

// Shaft rpm at 50 Hz: the gearbox ratio, as the number the user can actually measure. It is
// the divisor that turns a step's speed into a frequency, so zero is not allowed.
bool validate_mixer_rpm_50hz(const char *value)
{
    return validate_int_range(value, 1, 20000);
}

// Which RS-485 port the drive hangs off, numbered 1..2 the way the case is.
bool validate_mixer_vfd_port(const char *value)
{
    return validate_int_range(value, 1, RS485_PORTS_COUNT);
}

// The reverse command code, which differs between drive revisions: 5 on new ones, 3 on old
// ones. Nothing else is a reverse, so the range is these two values and not 3..5.
bool validate_mixer_rev_code(const char *value)
{
    return (validate_int_range(value, VFD_T13_CMD_REVERSE_OLD, VFD_T13_CMD_REVERSE_OLD) ||
            validate_int_range(value, VFD_T13_CMD_REVERSE_NEW, VFD_T13_CMD_REVERSE_NEW));
}

// Printable ASCII of a bounded length, the empty string included. Shared by the three MQTT
// string settings, which differ only in what they additionally refuse.
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
