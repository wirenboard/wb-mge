#include "setting_items.h"
#include "nv_storage.h"
#include "setting_items_const.h"
#include "config.h"
#include "nvs.h"
#include "esp_mac.h"

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>

static const char *TAG = "setting_items";

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

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
bool validate_hostname_or_ssid(const char *value)
{
    if (!value || strlen(value) == 0 || strlen(value) >= 32) {
        return false;
    }
    // Basic hostname/SSID validation - only alphanumeric and hyphens
    for (size_t i = 0; i < strlen(value); i++) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) {
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
    return (*endptr == '\0' && port >= 1 && port <= 65535);
}

bool validate_baudrate(const char *value)
{
    if (!value) {
        return false;
    }
    char *endptr;
    long baudrate = strtol(value, &endptr, 10);
    return (*endptr == '\0' && baudrate >= UART_BAUD_RATE_MIN && baudrate <= UART_BAUD_RATE_MAX);
}

bool validate_stopbits(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, "1", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "1.5", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "2", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_parity(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, "none", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "even", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "odd", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_databits(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, "5", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "6", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "7", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "8", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_ip(const char *value)
{
    if (!value || strlen(value) == 0) {
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
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) {
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
    return (strncmp(value, "ap", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "sta", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "apsta", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "null", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_wifi_auth(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, "open", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "wpa2_psk", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "wpa3_psk", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_bridge_mode(const char *value)
{
    if (!value) {
        return false;
    }
    return (strncmp(value, "server", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "client", SETTING_ITEM_MAX_STR_LEN) == 0);
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
    if (len == 0 || len >= 32) {
        return false;
    }

    // Basic alphanumeric validation for login
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
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
    if (len < 8 || len >= 32) {
        return false;  // Password must be between 8 and 32 characters
    }

    // Basic password validation - can be enhanced with regex or additional rules
    for (size_t i = 0; i < len; i++) {
        char c = value[i];
        if (!(c >= ' ' && c <= '~')) {  // Printable ASCII characters
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
            snprintf(generated_password, sizeof(generated_password), "%010lu", (unsigned long)password_num);
            ESP_LOGI(TAG, "Generated AP password from MAC: %02X:%02X:%02X:%02X:%02X:%02X -> %s",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], generated_password);
        } else {
            // Fallback to default if MAC read fails
            strncpy(generated_password, "wirenboard", sizeof(generated_password) - 1);
            ESP_LOGW(TAG, "Failed to read MAC for AP password generation, using fallback");
        }
        password_generated = true;
    }

    return generated_password;
}

// TODO: use default values from setting_items_const.h or delete those defines
static const setting_item_t setting_items[] = {
    {"hostname", BASE_HOSTNAME, validate_hostname_or_ssid, SETTING_ITEM_TYPE_STRING},
    {"login", DEFAULT_LOGIN, validate_login, SETTING_ITEM_TYPE_STRING},
    {"pass", DEFAULT_PASS, validate_password, SETTING_ITEM_TYPE_STRING},
    {"web_port", "80", validate_port, SETTING_ITEM_TYPE_INT},
    {"io_bus", "false", validate_bool, SETTING_ITEM_TYPE_BOOL},
    {"vout", "false", validate_bool, SETTING_ITEM_TYPE_BOOL},

    // WiFi settings
    {"wifi_mode", "ap", validate_wifi_mode, SETTING_ITEM_TYPE_STRING},
    {"ap_auth", "wpa2_psk", validate_wifi_auth, SETTING_ITEM_TYPE_STRING},
    {"sta_auth", "wpa2_psk", validate_wifi_auth, SETTING_ITEM_TYPE_STRING},
    {"ap_ssid", BASE_HOSTNAME, validate_hostname_or_ssid, SETTING_ITEM_TYPE_STRING},
    {"ap_pass", "wirenboard", validate_password, SETTING_ITEM_TYPE_STRING},
    {"sta_ssid", "", validate_hostname_or_ssid, SETTING_ITEM_TYPE_STRING},
    {"sta_pass", "", validate_password, SETTING_ITEM_TYPE_STRING},
    {"ap_ip_static", "192.168.1.1", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"ap_mask_static", "255.255.255.0", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"ap_gw_static", "192.168.1.1", validate_ip, SETTING_ITEM_TYPE_STRING},

    // Ethernet settings
    {"eth_ip_static", "192.168.1.100", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"eth_mask_static", "255.255.255.0", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"eth_gw_static", "192.168.1.1", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"eth_dhcpc", "true", validate_bool, SETTING_ITEM_TYPE_BOOL},

    // RS485 port 1 settings
    {"baudrate_1", "9600", validate_baudrate, SETTING_ITEM_TYPE_INT},
    {"stopbits_1", "1", validate_stopbits, SETTING_ITEM_TYPE_STRING},
    {"parity_1", "none", validate_parity, SETTING_ITEM_TYPE_STRING},
    {"databits_1", "8", validate_databits, SETTING_ITEM_TYPE_STRING},
    {"term_1", "false", validate_bool, SETTING_ITEM_TYPE_BOOL},
    {"fail_safe_1", "false", validate_bool, SETTING_ITEM_TYPE_BOOL},
    {"bridge_mode_1", "client", validate_bridge_mode, SETTING_ITEM_TYPE_STRING},
    {"bridge_port_1", "502", validate_port, SETTING_ITEM_TYPE_INT},
    {"bridge_ip_1", "192.168.1.10", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"bridge_mb_1", "true", validate_bool, SETTING_ITEM_TYPE_BOOL},

    // RS485 port 2 settings
    {"baudrate_2", "9600", validate_baudrate, SETTING_ITEM_TYPE_INT},
    {"stopbits_2", "1", validate_stopbits, SETTING_ITEM_TYPE_STRING},
    {"parity_2", "none", validate_parity, SETTING_ITEM_TYPE_STRING},
    {"databits_2", "8", validate_databits, SETTING_ITEM_TYPE_STRING},
    {"term_2", "false", validate_bool, SETTING_ITEM_TYPE_BOOL},
    {"fail_safe_2", "false", validate_bool, SETTING_ITEM_TYPE_BOOL},
    {"bridge_mode_2", "client", validate_bridge_mode, SETTING_ITEM_TYPE_STRING},
    {"bridge_port_2", "503", validate_port, SETTING_ITEM_TYPE_INT},
    {"bridge_ip_2", "192.168.1.10", validate_ip, SETTING_ITEM_TYPE_STRING},
    {"bridge_mb_2", "true", validate_bool, SETTING_ITEM_TYPE_BOOL},
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

esp_err_t setting_items_init(void)
{
    ESP_LOGI(TAG, "Initializing settings with string storage");
    storage_iface = &nvs_storage_iface;
    return ESP_OK;
}

esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *test_storage_iface)
{
    ESP_LOGI(TAG, "Initializing settings with custom storage for testing");
    storage_iface = test_storage_iface;
    return ESP_OK;
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
        ESP_LOGI(TAG, "Saved setting %s = %s", key, value);
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
            const char *default_value;

            // Special case: generate dynamic default for AP password
            if (strncmp(key, "ap_pass", SETTING_ITEM_MAX_STR_LEN) == 0) {
                default_value = get_dynamic_ap_pass_default();
            } else {
                default_value = item->default_value;
            }

            strncpy(value, default_value, SETTING_ITEM_MAX_STR_LEN - 1);
            value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Using default value for %s: %s", key, value);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to read setting %s: %s", key, esp_err_to_name(result));

        // For other errors, still try to use default value as fallback
        strncpy(value, item->default_value, SETTING_ITEM_MAX_STR_LEN - 1);
        value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
        ESP_LOGW(TAG, "Using default value for %s due to read error", key);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Read setting %s = %s", key, value);
    return ESP_OK;
}

bool setting_items_has_key(const char *key)
{
    return find_setting_item(key) != NULL;
}

esp_err_t setting_items_set_default(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        return ESP_ERR_NOT_FOUND;
    }

    return setting_items_save(key, item->default_value);
}

const char *setting_items_get_default(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (item) {
        return item->default_value;
    } else {
        return NULL;
    }
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

// Convenience wrapper functions for common types with type checking
uint32_t setting_items_read_u32(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Setting '%s' not found", key);
        return 0;
    }

    if (item->type != SETTING_ITEM_TYPE_INT && item->type != SETTING_ITEM_TYPE_UINT32) {
        ESP_LOGE(TAG, "Type mismatch: '%s' is %s, not INT/UINT32",
                 key, setting_items_type_to_string(item->type));
        return 0;
    }

    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) != ESP_OK) {
        return (uint32_t)strtoul(item->default_value, NULL, 10);
    }
    return (uint32_t)strtoul(value, NULL, 10);
}

bool setting_items_read_bool(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Setting '%s' not found", key);
        return false;
    }

    if (item->type != SETTING_ITEM_TYPE_BOOL) {
        ESP_LOGE(TAG, "Type mismatch: '%s' is %s, not BOOL",
                 key, setting_items_type_to_string(item->type));
        return false;
    }

    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) != ESP_OK) {
        return (strncmp(item->default_value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
    }
    return (strncmp(value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
}

int setting_items_read_int(const char *key)
{
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        ESP_LOGE(TAG, "Setting '%s' not found", key);
        return 0;
    }

    if (item->type != SETTING_ITEM_TYPE_INT && item->type != SETTING_ITEM_TYPE_UINT32) {
        ESP_LOGE(TAG, "Type mismatch: '%s' is %s, not INT",
                 key, setting_items_type_to_string(item->type));
        return 0;
    }

    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) != ESP_OK) {
        return (int)strtol(item->default_value, NULL, 10);
    }
    return (int)strtol(value, NULL, 10);
}

esp_err_t setting_items_save_u32(const char *key, uint32_t value)
{
    char str_value[SETTING_ITEM_MAX_STR_LEN];
    snprintf(str_value, sizeof(str_value), "%lu", (unsigned long)value);
    return setting_items_save(key, str_value);
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
    snprintf(str_value, sizeof(str_value), "%d", value);
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
    case SETTING_ITEM_TYPE_UINT32:
        return "UINT32";
    case SETTING_ITEM_TYPE_INVALID:
        return "INVALID";
    default:
        return "UNKNOWN";
    }
}
