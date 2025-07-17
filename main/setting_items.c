#include "setting_items.h"
#include "nv_storage.h"
#include "setting_items_const.h"
#include "config.h"
#include "nvs.h"

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
    bool is_private;  // For sensitive data like passwords
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
bool validate_hostname(const char *value) {
    if (!value || strlen(value) == 0 || strlen(value) >= 32) return false;
    // Basic hostname validation - only alphanumeric and hyphens
    for (size_t i = 0; i < strlen(value); i++) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

bool validate_port(const char *value) {
    if (!value) return false;
    char *endptr;
    long port = strtol(value, &endptr, 10);
    return (*endptr == '\0' && port >= 1 && port <= 65535);
}

bool validate_baudrate(const char *value) {
    if (!value) return false;
    char *endptr;
    long baudrate = strtol(value, &endptr, 10);
    return (*endptr == '\0' && baudrate >= UART_BAUD_RATE_MIN && baudrate <= UART_BAUD_RATE_MAX);
}

bool validate_stopbits(const char *value) {
    if (!value) return false;
    return (strncmp(value, "1", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "1.5", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "2", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_parity(const char *value) {
    if (!value) return false;
    return (strncmp(value, "none", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "even", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "odd", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_databits(const char *value) {
    if (!value) return false;
    return (strncmp(value, "5", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "6", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "7", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "8", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_ip(const char *value) {
    if (!value) return false;
    // Basic IP validation - should be enhanced for production
    int a, b, c, d;
    if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;
    return (a >= 0 && a <= 255 && b >= 0 && b <= 255 &&
            c >= 0 && c <= 255 && d >= 0 && d <= 255);
}

bool validate_wifi_mode(const char *value) {
    if (!value) return false;
    return (strncmp(value, "ap", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "sta", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "apsta", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "null", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_wifi_auth(const char *value) {
    if (!value) return false;
    return (strncmp(value, "open", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "wpa2_psk", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "wpa3_psk", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_bridge_mode(const char *value) {
    if (!value) return false;
    return (strncmp(value, "server", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "client", SETTING_ITEM_MAX_STR_LEN) == 0);
}

bool validate_bool(const char *value) {
    if (!value) return false;
    return (strncmp(value, "true", SETTING_ITEM_MAX_STR_LEN) == 0 ||
            strncmp(value, "false", SETTING_ITEM_MAX_STR_LEN) == 0);
}

// TODO: use default values from setting_items_const.h
static const setting_item_t setting_items[] = {
    // Device settings
    {"hostname", BASE_HOSTNAME, validate_hostname, false},
    {"login", DEFAULT_LOGIN, NULL, false}, // TODO: add validation
    {"web_port", "80", validate_port, false},
    {"io_bus", "false", validate_bool, false},
    {"vout", "false", validate_bool, false},

    // WiFi settings
    {"wifi_mode", "ap", validate_wifi_mode, false},
    {"ap_auth", "wpa2_psk", validate_wifi_auth, false},
    {"sta_auth", "sta_auth", validate_wifi_auth, false},
    {"ap_ssid", BASE_HOSTNAME, NULL, false}, // TODO: add validation
    {"ap_pass", "wirenboard", NULL, true}, // TODO: add validation
    {"sta_ssid", "", NULL, false},// TODO: add validation
    {"sta_pass", "", NULL, true}, // TODO: add validation
    {"ap_ip_static", "192.168.1.1", validate_ip, false},
    {"ap_mask_static", "255.255.255.0", validate_ip, false},
    {"ap_gw_static", "192.168.1.1", validate_ip, false},

    // Ethernet settings
    {"eth_ip_static", "192.168.1.100", validate_ip, false},
    {"eth_mask_static", "255.255.255.0", validate_ip, false},
    {"eth_gw_static", "192.168.1.1", validate_ip, false},
    {"eth_dhcpc", "true", validate_bool, false},

    // RS485 port 1 settings
    {"baudrate_1", "9600", validate_baudrate, false},
    {"stopbits_1", "1", validate_stopbits, false},
    {"parity_1", "none", validate_parity, false},
    {"databits_1", "8", validate_databits, false},
    {"term_1", "false", validate_bool, false},
    {"fail_safe_1", "false", validate_bool, false},
    {"bridge_mode_1", "client", validate_bridge_mode, false},
    {"bridge_port_1", "502", validate_port, false},
    {"bridge_ip_1", "192.168.1.10", validate_ip, false},
    {"bridge_mb_1", "true", validate_bool, false},

    // RS485 port 2 settings
    {"baudrate_2", "9600", validate_baudrate, false},
    {"stopbits_2", "1", validate_stopbits, false},
    {"parity_2", "none", validate_parity, false},
    {"databits_2", "8", validate_databits, false},
    {"term_2", "false", validate_bool, false},
    {"fail_safe_2", "false", validate_bool, false},
    {"bridge_mode_2", "client", validate_bridge_mode, false},
    {"bridge_port_2", "503", validate_port, false},
    {"bridge_ip_2", "192.168.1.10", validate_ip, false},
    {"bridge_mb_2", "true", validate_bool, false},
};

static const setting_item_t *find_setting_item(const char *key) {
    for (size_t i = 0; i < ARRAY_SIZE(setting_items); i++) {
        if (strncmp(setting_items[i].key, key, SETTING_ITEM_MAX_STR_LEN) == 0) {
            return &setting_items[i];
        }
    }
    return NULL;
}

esp_err_t setting_items_init(void) {
    ESP_LOGI(TAG, "Initializing settings with string storage");
    storage_iface = &nvs_storage_iface;
    return ESP_OK;
}

esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *test_storage_iface) {
    ESP_LOGI(TAG, "Initializing settings with custom storage for testing");
    storage_iface = test_storage_iface;
    return ESP_OK;
}

esp_err_t setting_items_save(const char *key, const char *value) {
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
        ESP_LOGI(TAG, "Saved setting %s = %s", key, item->is_private ? "[HIDDEN]" : value);
    } else {
        ESP_LOGE(TAG, "Failed to save setting %s: %s", key, esp_err_to_name(result));
    }

    return result;
}

esp_err_t setting_items_read(const char *key, char *value) {
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
            strncpy(value, item->default_value, SETTING_ITEM_MAX_STR_LEN - 1);
            value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
            ESP_LOGI(TAG, "Using default value for %s: %s", key,
                    item->is_private ? "[HIDDEN]" : value);
            return ESP_OK;
        }
        ESP_LOGE(TAG, "Failed to read setting %s: %s", key, esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "Read setting %s = %s", key, item->is_private ? "[HIDDEN]" : value);
    return ESP_OK;
}

bool setting_items_has_key(const char *key) {
    return find_setting_item(key) != NULL;
}

esp_err_t setting_items_set_default(const char *key) {
    const setting_item_t *item = find_setting_item(key);
    if (!item) {
        return ESP_ERR_NOT_FOUND;
    }

    return setting_items_save(key, item->default_value);
}

const char *setting_items_get_default(const char *key) {
    const setting_item_t *item = find_setting_item(key);
    return item ? item->default_value : NULL;
}

size_t setting_items_get_count(void) {
    return ARRAY_SIZE(setting_items);
}

const char *setting_items_get_key_at(size_t index) {
    if (index >= ARRAY_SIZE(setting_items)) {
        return NULL;
    }
    return setting_items[index].key;
}

bool setting_items_is_private(const char *key) {
    const setting_item_t *item = find_setting_item(key);
    return item ? item->is_private : false;
}

// Convenience wrapper functions for common types
uint32_t setting_items_read_u32(const char *key) {
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) == ESP_OK) {
        return (uint32_t)strtoul(value, NULL, 10);
    }

    // Return default value if read fails
    const setting_item_t *item = find_setting_item(key);
    if (item && item->default_value) {
        return (uint32_t)strtoul(item->default_value, NULL, 10);
    }
    return 0;
}

bool setting_items_read_bool(const char *key) {
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) == ESP_OK) {
        return (strncmp(value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
    }

    // Return default value if read fails
    const setting_item_t *item = find_setting_item(key);
    if (item && item->default_value) {
        return (strncmp(item->default_value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
    }
    return false;
}

int setting_items_read_int(const char *key) {
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(key, value) == ESP_OK) {
        return (int)strtol(value, NULL, 10);
    }

    // Return default value if read fails
    const setting_item_t *item = find_setting_item(key);
    if (item && item->default_value) {
        return (int)strtol(item->default_value, NULL, 10);
    }
    return 0;
}

esp_err_t setting_items_save_u32(const char *key, uint32_t value) {
    char str_value[SETTING_ITEM_MAX_STR_LEN];
    snprintf(str_value, sizeof(str_value), "%lu", (unsigned long)value);
    return setting_items_save(key, str_value);
}

esp_err_t setting_items_save_bool(const char *key, bool value) {
    return setting_items_save(key, value ? "true" : "false");
}

esp_err_t setting_items_save_int(const char *key, int value) {
    char str_value[SETTING_ITEM_MAX_STR_LEN];
    snprintf(str_value, sizeof(str_value), "%d", value);
    return setting_items_save(key, str_value);
}
