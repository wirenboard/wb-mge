#include "setting_validators.h"
#include "setting_items.h"

#include <stddef.h>
#include <string.h>

static bool validate_hostname_ssid(const char *value)
{
    // Basic hostname/ssid validation - only alphanumeric and hyphens
    size_t len = strlen(value); // Calculate once for security
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
              ((c >= '0') && (c <= '9')) || (c == '-'))) {
            return false;
        }
    }
    return true;
}

// Validation functions
bool validate_hostname(const char *value)
{
    if ((!value) || (strlen(value) == 0) || (strlen(value) >= 32)) {
        return false;
    }
    return validate_hostname_ssid(value);
}

bool validate_ssid(const char *value)
{
    if ((!value) || (strlen(value) >= 32)) {
        return false;
    }
    return validate_hostname_ssid(value);
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

bool validate_baudrate(const char *value)
{
    if (!value) {
        return false;
    }
    char *endptr;
    long baudrate = strtol(value, &endptr, 10);
    if ((*endptr == '\0') && (baudrate >= 1200) && (baudrate <= 115200)) {
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
    return (strncmp(value, UART_STOP_BITS_1_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_STOP_BITS_1_5_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_STOP_BITS_2_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_parity(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, UART_PARITY_DISABLE_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_PARITY_EVEN_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_PARITY_ODD_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_databits(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, UART_DATA_5_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_DATA_6_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_DATA_7_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, UART_DATA_8_BITS_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_ip(const char *value)
{
    if ((!value) || (strlen(value) == 0)) {
        return false;
    }

    // Enhanced IP validation with proper range checking
    int a, b, c, d;
    char extra;

    // Check format and ensure no extra characters
    if (sscanf(value, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) {
        return false;
    }

    // Check ranges (0-255 for each octet)
    if ((a < 0) || (a > 255) || (b < 0) || (b > 255) ||
        (c < 0) || (c > 255) || (d < 0) || (d > 255)) {
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
    return (strncmp(value, WIFI_MODE_AP_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, WIFI_MODE_STA_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, WIFI_MODE_APSTA_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, WIFI_MODE_NONE_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_wifi_auth(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, WIFI_AUTH_OPEN_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, WIFI_AUTH_WPA2_PSK_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, WIFI_AUTH_WPA3_PSK_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_bridge_mode(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, BRIDGE_MODE_SERVER_STR, SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, BRIDGE_MODE_CLIENT_STR, SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_bool(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, "true", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "false", SETTING_ITEM_MAX_STR_LEN) == 0);
}

// Add validation for login strings
bool validate_login(const char *value)
{
    if (!value) {
        return false;
    }
    size_t len = strlen(value);
    if ((len == 0) || (len >= 32)) {
        return false;
    }

    // Basic alphanumeric validation for login
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
              ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-'))) {
            return false;
        }
    }
    return true;
}

bool validate_password(const char *value)
{
    if (!value) {
        return false;
    }
    size_t len = strlen(value);
    if (len >= 32) { // пока минимальную длину не задаём
        return false;  // Password must be not longer than 32 characters
    }

    // Basic password validation - can be enhanced with regex or additional rules
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (!((c >= ' ') && (c <= '~'))) {  // Printable ASCII characters
            return false;
        }
    }
    return true;
}
