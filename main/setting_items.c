#include "setting_items.h"
#include "nv_storage.h"
#include "config.h"
#include "nvs.h"
#include "esp_mac.h"
#include "array_size.h"

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>


#define SETTING_ITEMS_DEBUG_LOG_ENABLE      0           // TODO: Возможно, вынести в настройки

#define MAX_HOSTNAME_LEN                    32
#define MAX_SSID_LEN                        31          // In ESP-IDF there is ssid[32], terminating '\0' included

static const char *TAG = "setting_items";

typedef bool (*setting_validator_t)(const char *value);

typedef struct {
    const char *key;
    const char *default_value;
    setting_validator_t validator;
    setting_item_type_t type;  // Type information for validation and JSON mapping
} setting_item_t;

// Storage interface - defaults to NVS but can be overridden for testing
static const setting_storage_iface_t *storage_iface = NULL;

// Default NVS storage interface
static const setting_storage_iface_t nvs_storage_iface = {
    .has_key = nvs_has_key,
    .write_str = nvs_write_str,
    .read_str = nvs_read_str,
};


// Validation functions
bool validate_hostname(const char *value)
{
    if (!value) {
        return false;
    }
    size_t len = strlen(value);
    if ((len == 0) || (len > MAX_HOSTNAME_LEN)) {
        return false;
    }
    // Basic hostname validation, allow only alphanumeric and hyphens
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
              ((c >= '0') && (c <= '9')) || (c == '-'))) {
            return false;
        }
    }
    return true;
}

bool validate_ssid(const char *value)
{
    if (!value) {
        return false;
    }
    size_t len = strlen(value);
    if ((len == 0) || (len > MAX_SSID_LEN)) {
        return false;
    }
    // Basic SSID validation, allow all printable symbols in range 0x20 - 0x7E
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if ((c < '\x20') || (c > '\x7E')) {
            return false;
        }
    }
    return true;
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

// Generate dynamic default AP password from MAC address
static const char *get_dynamic_ap_pass_default(void)
{
    static char generated_password[16] = {0};
    static bool password_generated = false;

    if (!password_generated) {
        uint8_t mac[6];
        esp_err_t ret = esp_read_mac(mac, ESP_MAC_ETH);
        if (ret == ESP_OK) {
            // Convert MAC to decimal and trim to 10 digits
            uint64_t mac_decimal = 0;
            for (int i = 0; i < 6; i++) {
                mac_decimal = (mac_decimal * 256) + mac[i];
            }
            // Take last 10 digits and ensure it's at least 8 digits for password policy
            uint32_t password_num = (uint32_t)(mac_decimal % 10000000000ULL);
            if (password_num < 10000000) {  // Less than 8 digits
                password_num += 10000000;   // Make it 8 digits minimum
            }
            int ret = snprintf(generated_password, sizeof(generated_password), "%010lu", (unsigned long)password_num);
            if (ret >= sizeof(generated_password)) {
                ESP_LOGW(TAG, "Generated password was truncated");
            }
            ESP_LOGI(TAG, "Generated AP password from MAC: %02X:%02X:%02X:%02X:%02X:%02X -> %s",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], generated_password);
        } else {
            // Fallback to default if MAC read fails
            strncpy(generated_password, "wirenboard", sizeof(generated_password) - 1);
            generated_password[sizeof(generated_password) - 1] = '\0';
            ESP_LOGW(TAG, "Failed to read MAC for AP password generation, using fallback");
        }
        password_generated = true;
    }

    return generated_password;
}

// Generate dynamic hostname from MAC address
static const char *get_dynamic_hostname_default(void)
{
    static char generated_hostname[32] = {0};
    static bool hostname_generated = false;

    if (!hostname_generated) {
        uint8_t mac[6];
        esp_err_t ret = esp_read_mac(mac, ESP_MAC_ETH);
        if (ret == ESP_OK) {
            int ret = snprintf(generated_hostname, sizeof(generated_hostname), "%s-%02X%02X%02X",
                              BASE_HOSTNAME, mac[3], mac[4], mac[5]);
            if (ret >= sizeof(generated_hostname)) {
                ESP_LOGW(TAG, "Generated hostname was truncated");
            }
            ESP_LOGI(TAG, "Generated hostname from MAC: %02X:%02X:%02X:%02X:%02X:%02X -> %s",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], generated_hostname);
        } else {
            // Fallback to base hostname if MAC read fails
            strncpy(generated_hostname, BASE_HOSTNAME, sizeof(generated_hostname) - 1);
            generated_hostname[sizeof(generated_hostname) - 1] = '\0';
            ESP_LOGW(TAG, "Failed to read MAC for hostname generation, using fallback");
        }
        hostname_generated = true;
    }

    return generated_hostname;
}

// Helper function to get the appropriate default value (including dynamic ones)
static const char *get_setting_default_value(const setting_item_t *item)
{
    // Special cases for dynamic defaults
    if (strncmp(item->key, KEY_AP_PASS, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return get_dynamic_ap_pass_default();
    } else if (strncmp(item->key, KEY_HOSTNAME, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return get_dynamic_hostname_default();
    } else if (strncmp(item->key, KEY_AP_SSID, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return get_dynamic_hostname_default();
    } else {
        return item->default_value;
    }
}

static const setting_item_t setting_items[] = {
    {KEY_HOSTNAME, BASE_HOSTNAME, validate_hostname, SETTING_ITEM_TYPE_STRING},
    {KEY_LOGIN, DEFAULT_LOGIN, validate_login, SETTING_ITEM_TYPE_STRING},
    {KEY_PASS, DEFAULT_PASS, validate_password, SETTING_ITEM_TYPE_STRING},
    {KEY_WEB_PORT, DEFAULT_WEB_PORT, validate_port, SETTING_ITEM_TYPE_INT},
    {KEY_IO_BUS_ENABLED, DEFAULT_IO_BUS_ENABLED, validate_bool, SETTING_ITEM_TYPE_BOOL},
    {KEY_485_VOUT, DEFAULT_485_VOUT, validate_bool, SETTING_ITEM_TYPE_BOOL},

    // WiFi settings
    {KEY_WIFI_MODE, DEFAULT_WIFI_MODE, validate_wifi_mode, SETTING_ITEM_TYPE_STRING},
    {KEY_WIFI_AUTH_AP, DEFAULT_WIFI_AUTH, validate_wifi_auth, SETTING_ITEM_TYPE_STRING},
    {KEY_WIFI_AUTH_STA, DEFAULT_WIFI_AUTH, validate_wifi_auth, SETTING_ITEM_TYPE_STRING},
    {KEY_AP_SSID, BASE_HOSTNAME, validate_ssid, SETTING_ITEM_TYPE_STRING},
    {KEY_AP_PASS, DEFAULT_AP_PASS, validate_password, SETTING_ITEM_TYPE_STRING},
    {KEY_AP_IP_STATIC, DEFAULT_AP_IP_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_AP_MASK_STATIC, DEFAULT_AP_MASK_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_AP_GW_STATIC, DEFAULT_AP_GW_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_STA_SSID, DEFAULT_STA_SSID, validate_ssid, SETTING_ITEM_TYPE_STRING},
    {KEY_STA_PASS, DEFAULT_STA_PASS, validate_password, SETTING_ITEM_TYPE_STRING},
    {KEY_STA_DHCPC, DEFAULT_STA_DHCPC, validate_bool, SETTING_ITEM_TYPE_BOOL},
    {KEY_STA_IP_STATIC, DEFAULT_STA_IP_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_STA_MASK_STATIC, DEFAULT_STA_MASK_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_STA_GW_STATIC, DEFAULT_STA_GW_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},

    // Ethernet settings
    {KEY_ETH_IP_STATIC, DEFAULT_ETH_IP_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_ETH_MASK_STATIC, DEFAULT_ETH_MASK_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_ETH_GW_STATIC, DEFAULT_ETH_GW_STATIC, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_ETH_DHCPC, DEFAULT_ETH_DHCPC, validate_bool, SETTING_ITEM_TYPE_BOOL},

    // RS485 port 1 settings
    {KEY_BAUDRATE1, DEFAULT_BAUDRATE, validate_baudrate, SETTING_ITEM_TYPE_INT},
    {KEY_STOPBITS1, DEFAULT_STOPBITS, validate_stopbits, SETTING_ITEM_TYPE_STRING},
    {KEY_PARITY1, DEFAULT_PARITY, validate_parity, SETTING_ITEM_TYPE_STRING},
    {KEY_DATABITS1, DEFAULT_DATABITS, validate_databits, SETTING_ITEM_TYPE_STRING},
    {KEY_485_TERM_1, DEFAULT_485_TERM, validate_bool, SETTING_ITEM_TYPE_BOOL},
    {KEY_485_FAIL_SAFE_1, DEFAULT_485_FAIL_SAFE, validate_bool, SETTING_ITEM_TYPE_BOOL},
    {KEY_BRIDGE_MODE1, DEFAULT_BRIDGE_MODE, validate_bridge_mode, SETTING_ITEM_TYPE_STRING},
    {KEY_BRIDGE_PORT1, DEFAULT_BRIDGE_PORT, validate_port, SETTING_ITEM_TYPE_INT},
    {KEY_BRIDGE_IP1, DEFAULT_BRIDGE_IP, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_BRIDGE_MB1, DEFAULT_BRIDGE_MB, validate_bool, SETTING_ITEM_TYPE_BOOL},

    // RS485 port 2 settings
    {KEY_BAUDRATE2, DEFAULT_BAUDRATE, validate_baudrate, SETTING_ITEM_TYPE_INT},
    {KEY_STOPBITS2, DEFAULT_STOPBITS, validate_stopbits, SETTING_ITEM_TYPE_STRING},
    {KEY_PARITY2, DEFAULT_PARITY, validate_parity, SETTING_ITEM_TYPE_STRING},
    {KEY_DATABITS2, DEFAULT_DATABITS, validate_databits, SETTING_ITEM_TYPE_STRING},
    {KEY_485_TERM_2, DEFAULT_485_TERM, validate_bool, SETTING_ITEM_TYPE_BOOL},
    {KEY_485_FAIL_SAFE_2, DEFAULT_485_FAIL_SAFE, validate_bool, SETTING_ITEM_TYPE_BOOL},
    {KEY_BRIDGE_MODE2, DEFAULT_BRIDGE_MODE, validate_bridge_mode, SETTING_ITEM_TYPE_STRING},
    {KEY_BRIDGE_PORT2, DEFAULT_BRIDGE_PORT2, validate_port, SETTING_ITEM_TYPE_INT},
    {KEY_BRIDGE_IP2, DEFAULT_BRIDGE_IP, validate_ip, SETTING_ITEM_TYPE_STRING},
    {KEY_BRIDGE_MB2, DEFAULT_BRIDGE_MB, validate_bool, SETTING_ITEM_TYPE_BOOL},
};

static const setting_item_t *find_setting_item(const char *key)
{
    for (size_t i = 0; i < ARRAY_SIZE(setting_items); i++) {
        if (strncmp(setting_items[i].key, key, SETTING_ITEM_MAX_STR_LEN) == 0) {
            return &setting_items[i];
        }
    }
    return NULL;
}

esp_err_t setting_items_set_defaults(bool only_uninitialized)
{
    ESP_LOGI(TAG, "Setting default values for %s settings", only_uninitialized ? "uninitialized" : "all");

    for (size_t i = 0; i < ARRAY_SIZE(setting_items); i++) {
        const setting_item_t *item = &setting_items[i];

        // Check if the setting already exists in storage
        if ((only_uninitialized == false) || !storage_iface->has_key(item->key)) {
            const char *default_value = get_setting_default_value(item);

            esp_err_t ret = storage_iface->write_str(item->key, default_value);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Set default %s = %s", item->key, default_value);
            } else {
                ESP_LOGE(TAG, "Failed to set default for %s: %s", item->key, esp_err_to_name(ret));
                return ret;
            }
        }
    }

    return ESP_OK;
}

esp_err_t setting_items_init(void)
{
    if (SETTING_ITEMS_DEBUG_LOG_ENABLE) {
        esp_log_level_set(TAG, ESP_LOG_DEBUG);
    }

    ESP_LOGI(TAG, "Initializing settings with string storage");
    storage_iface = &nvs_storage_iface;

    return setting_items_set_defaults(true);
}

esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *test_storage_iface)
{
    ESP_LOGI(TAG, "Initializing settings with custom storage for testing");
    storage_iface = test_storage_iface;

    return setting_items_set_defaults(true);
}

esp_err_t setting_items_save(const char *key, const char *value)
{
    if (!key || !value || !storage_iface) {
        return ESP_ERR_INVALID_ARG;
    }

    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Unknown setting key: %s", key);
        return ESP_ERR_NOT_FOUND;
    }

    // Validate value if validator exists
    if (item->validator && !item->validator(value)) {
        ESP_LOGE(TAG, "Invalid value '%s' for setting '%s'", value, key);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = storage_iface->write_str(key, value);
    if (result == ESP_OK) {
        ESP_LOGD(TAG, "Saved setting %s = %s", key, value);
    } else {
        ESP_LOGE(TAG, "Failed to save setting %s: %s", key, esp_err_to_name(result));
    }

    return result;
}

esp_err_t setting_items_read(const char *key, char *value)
{
    if (!key || !value || !storage_iface) {
        return ESP_ERR_INVALID_ARG;
    }

    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Unknown setting key: %s", key);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result = storage_iface->read_str(key, value);
    if (result != ESP_OK) {
        // Use default value if not found in storage
        if (result == ESP_ERR_NOT_FOUND) {
            const char *default_value = get_setting_default_value(item);

            strncpy(value, default_value, SETTING_ITEM_MAX_STR_LEN - 1);
            value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
            ESP_LOGW(TAG, "Using default value for %s: %s", key, value);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to read setting %s: %s", key, esp_err_to_name(result));

        // For other errors, still try to use default value as fallback
        strncpy(value, item->default_value, SETTING_ITEM_MAX_STR_LEN - 1);
        value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
        ESP_LOGW(TAG, "Using default value for %s due to read error", key);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Read setting %s = %s", key, value);
    return ESP_OK;
}

size_t setting_items_get_count(void)
{
    return ARRAY_SIZE(setting_items);
}

const char *setting_items_get_key_at(size_t index)
{
    if (index >= ARRAY_SIZE(setting_items)) {
        return NULL;
    }
    return setting_items[index].key;
}

// Helper functions to validate setting types semantically
static bool is_numeric_type(setting_item_type_t type)
{
    return (type == SETTING_ITEM_TYPE_INT);
}

static const setting_item_t *validate_numeric_setting(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Setting '%s' not found", key);
        return NULL;
    }

    if (!is_numeric_type(item->type)) {
        ESP_LOGE(TAG, "Type mismatch: '%s' is %s, expected numeric type",
                 key, setting_items_type_to_string(item->type));
        return NULL;
    }

    return item;
}

static const setting_item_t *validate_bool_setting(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Setting '%s' not found", key);
        return NULL;
    }

    if (item->type != SETTING_ITEM_TYPE_BOOL) {
        ESP_LOGE(TAG, "Type mismatch: '%s' is %s, expected BOOL",
                 key, setting_items_type_to_string(item->type));
        return NULL;
    }

    return item;
}

// Generic helper function for reading numeric values
static long read_numeric_value(const char *key)
{
    const setting_item_t *item = validate_numeric_setting(key);
    if (!item) {
        return 0;
    }

    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) != ESP_OK) {
        return strtol(item->default_value, NULL, 10);
    }
    return strtol(value, NULL, 10);
}

// Convenience wrapper functions for common types with semantic type checking
int setting_items_read_int(const char *key)
{
    return (int)read_numeric_value(key);
}

bool setting_items_read_bool(const char *key)
{
    const setting_item_t *item = validate_bool_setting(key);
    if (!item) {
        return false;
    }

    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) != ESP_OK) {
        return (strncmp(item->default_value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
    }
    return (strncmp(value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
}

esp_err_t setting_items_save_bool(const char *key, bool value)
{
    if (value) {
        return setting_items_save(key, "true");
    } else {
        return setting_items_save(key, "false");
    }
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    char str_value[SETTING_ITEM_MAX_STR_LEN];
    int ret = snprintf(str_value, sizeof(str_value), "%d", value);
    if (ret >= sizeof(str_value)) {
        ESP_LOGE(TAG, "Value too large for buffer when saving %s", key);
        return ESP_ERR_INVALID_SIZE;
    }
    return setting_items_save(key, str_value);
}

// Type introspection functions
setting_item_type_t setting_items_get_type(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (item) {
        return item->type;
    } else {
        return SETTING_ITEM_TYPE_INVALID;
    }
}

const char *setting_items_type_to_string(setting_item_type_t type)
{
    switch (type) {
    case SETTING_ITEM_TYPE_STRING:
        return "STRING";
    case SETTING_ITEM_TYPE_BOOL:
        return "BOOL";
    case SETTING_ITEM_TYPE_INT:
        return "INT";
    case SETTING_ITEM_TYPE_INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
}
