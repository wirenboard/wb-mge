// Mock for setting_items used by settings_manager unit tests.
// Provides controllable storage and per-call failure injection.

#include "setting_items.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// -------------------------------------------------------------------
// In-memory key/value store (mirrors ram_storage pattern)
// -------------------------------------------------------------------
#define MOCK_MAX_ENTRIES    64
#define MOCK_KEY_LEN        32
#define MOCK_VAL_LEN        64

typedef struct {
    char key[MOCK_KEY_LEN];
    char value[MOCK_VAL_LEN];
} mock_entry_t;

static mock_entry_t mock_store[MOCK_MAX_ENTRIES];
static int          mock_store_count = 0;

// -------------------------------------------------------------------
// Fault-injection controls (set by test before calling SUT)
// -------------------------------------------------------------------
esp_err_t mock_setting_items_save_error = ESP_OK;     // error to return from save*
esp_err_t mock_setting_items_validate_error = ESP_OK; // error to return from validate

// Save-call counter (useful to check how many saves were attempted)
int mock_setting_items_save_call_count = 0;

// -------------------------------------------------------------------
// Reset helper — call from setUp()
// -------------------------------------------------------------------
void mock_setting_items_reset(void)
{
    memset(mock_store, 0, sizeof(mock_store));
    mock_store_count = 0;
    mock_setting_items_save_error = ESP_OK;
    mock_setting_items_validate_error = ESP_OK;
    mock_setting_items_save_call_count = 0;

    // Pre-populate defaults so type introspection works
    setting_items_set_defaults(false);
}

// -------------------------------------------------------------------
// Internal helpers
// -------------------------------------------------------------------
static int find_entry(const char *key)
{
    for (int i = 0; i < mock_store_count; i++) {
        if (strncmp(mock_store[i].key, key, MOCK_KEY_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

static esp_err_t store_write(const char *key, const char *value)
{
    int idx = find_entry(key);
    if (idx >= 0) {
        strncpy(mock_store[idx].value, value, MOCK_VAL_LEN - 1);
        mock_store[idx].value[MOCK_VAL_LEN - 1] = '\0';
        return ESP_OK;
    }
    if (mock_store_count >= MOCK_MAX_ENTRIES) {
        return ESP_ERR_NO_MEM;
    }
    strncpy(mock_store[mock_store_count].key, key, MOCK_KEY_LEN - 1);
    mock_store[mock_store_count].key[MOCK_KEY_LEN - 1] = '\0';
    strncpy(mock_store[mock_store_count].value, value, MOCK_VAL_LEN - 1);
    mock_store[mock_store_count].value[MOCK_VAL_LEN - 1] = '\0';
    mock_store_count++;
    return ESP_OK;
}

static esp_err_t store_read(const char *key, char *value)
{
    int idx = find_entry(key);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }
    strncpy(value, mock_store[idx].value, MOCK_VAL_LEN - 1);
    value[MOCK_VAL_LEN - 1] = '\0';
    return ESP_OK;
}

// -------------------------------------------------------------------
// setting_items API implementation
// -------------------------------------------------------------------

esp_err_t setting_items_save(const char *key, const char *value)
{
    mock_setting_items_save_call_count++;
    if (mock_setting_items_save_error != ESP_OK) {
        return mock_setting_items_save_error;
    }
    return store_write(key, value);
}

esp_err_t setting_items_save_bool(const char *key, bool value)
{
    mock_setting_items_save_call_count++;
    if (mock_setting_items_save_error != ESP_OK) {
        return mock_setting_items_save_error;
    }
    return store_write(key, value ? "true" : "false");
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    mock_setting_items_save_call_count++;
    if (mock_setting_items_save_error != ESP_OK) {
        return mock_setting_items_save_error;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return store_write(key, buf);
}

esp_err_t setting_items_read(const char *key, char *value)
{
    return store_read(key, value);
}

bool setting_items_read_bool(const char *key)
{
    char buf[MOCK_VAL_LEN] = { 0 };
    if (store_read(key, buf) != ESP_OK) {
        return false;
    }
    return (strcmp(buf, "true") == 0);
}

int setting_items_read_int(const char *key)
{
    char buf[MOCK_VAL_LEN] = { 0 };
    if (store_read(key, buf) != ESP_OK) {
        return 0;
    }
    return atoi(buf);
}

esp_err_t setting_items_validate(const char *key, const char *value)
{
    (void)key;
    (void)value;
    return mock_setting_items_validate_error;
}

// -------------------------------------------------------------------
// Type-registry stubs: must match setting_items.c's real table.
// We inline the minimal subset needed for settings_manager tests.
// -------------------------------------------------------------------

typedef struct {
    const char *key;
    setting_item_type_t type;
    const char *default_value;
} mock_type_entry_t;

static const mock_type_entry_t type_table[] = {
    { "hostname",           SETTING_ITEM_TYPE_STRING, "WB-MGE"            },
    { "login",              SETTING_ITEM_TYPE_STRING, "admin"             },
    { "pass",               SETTING_ITEM_TYPE_STRING, "admin"             },
    { "web_port",           SETTING_ITEM_TYPE_INT,    "80"                },
    { "io_bus",             SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "vout",               SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "wifi_perm_dis",      SETTING_ITEM_TYPE_BOOL,   "false"             },
    { "wifi_mode",          SETTING_ITEM_TYPE_STRING, "ap"                },
    { "ap_auth",            SETTING_ITEM_TYPE_STRING, "wpa2_psk"          },
    { "sta_auth",           SETTING_ITEM_TYPE_STRING, "wpa2_psk"          },
    { "ap_ssid",            SETTING_ITEM_TYPE_STRING, "WB-MGE"            },
    { "ap_pass",            SETTING_ITEM_TYPE_STRING, ""                  },
    { "ap_ip_static",       SETTING_ITEM_TYPE_STRING, "192.168.5.1"       },
    { "ap_mask_static",     SETTING_ITEM_TYPE_STRING, "255.255.255.0"     },
    { "ap_gw_static",       SETTING_ITEM_TYPE_STRING, "192.168.5.1"       },
    { "sta_ssid",           SETTING_ITEM_TYPE_STRING, ""                  },
    { "sta_pass",           SETTING_ITEM_TYPE_STRING, ""                  },
    { "sta_dhcpc",          SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "sta_ip_static",      SETTING_ITEM_TYPE_STRING, "192.168.1.7"       },
    { "sta_mask_static",    SETTING_ITEM_TYPE_STRING, "255.255.255.0"     },
    { "sta_gw_static",      SETTING_ITEM_TYPE_STRING, "192.168.1.1"       },
    { "eth_ip_static",      SETTING_ITEM_TYPE_STRING, "192.168.0.7"       },
    { "eth_mask_static",    SETTING_ITEM_TYPE_STRING, "255.255.255.0"     },
    { "eth_gw_static",      SETTING_ITEM_TYPE_STRING, "192.168.0.1"       },
    { "eth_dhcpc",          SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "baudrate_1",         SETTING_ITEM_TYPE_INT,    "9600"              },
    { "stopbits_1",         SETTING_ITEM_TYPE_STRING, "2"                 },
    { "parity_1",           SETTING_ITEM_TYPE_STRING, "none"              },
    { "databits_1",         SETTING_ITEM_TYPE_STRING, "8"                 },
    { "485_term_1",         SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "485_fail_safe_1",    SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "485_tx_dis_1",       SETTING_ITEM_TYPE_BOOL,   "false"             },
    { "bridge_mode_1",      SETTING_ITEM_TYPE_STRING, "server"            },
    { "bridge_port_1",      SETTING_ITEM_TYPE_INT,    "502"               },
    { "bridge_ip_1",        SETTING_ITEM_TYPE_STRING, "192.168.5.2"       },
    { "bridge_modbus_1",    SETTING_ITEM_TYPE_BOOL,   "false"             },
    { "baudrate_2",         SETTING_ITEM_TYPE_INT,    "9600"              },
    { "stopbits_2",         SETTING_ITEM_TYPE_STRING, "2"                 },
    { "parity_2",           SETTING_ITEM_TYPE_STRING, "none"              },
    { "databits_2",         SETTING_ITEM_TYPE_STRING, "8"                 },
    { "485_term_2",         SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "485_fail_safe_2",    SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "485_tx_dis_2",       SETTING_ITEM_TYPE_BOOL,   "false"             },
    { "bridge_mode_2",      SETTING_ITEM_TYPE_STRING, "server"            },
    { "bridge_port_2",      SETTING_ITEM_TYPE_INT,    "503"               },
    { "bridge_ip_2",        SETTING_ITEM_TYPE_STRING, "192.168.5.2"       },
    { "bridge_modbus_2",    SETTING_ITEM_TYPE_BOOL,   "false"             },
    { "port_mode_1",        SETTING_ITEM_TYPE_STRING, "tcp_bridge"        },
    { "port_mode_2",        SETTING_ITEM_TYPE_STRING, "tcp_bridge"        },
    { "cache_mb_port",      SETTING_ITEM_TYPE_INT,    "504"               },
    { "cache_mb_srv_en",    SETTING_ITEM_TYPE_BOOL,   "true"              },
    { "cache_val_tout",     SETTING_ITEM_TYPE_INT,    "60"                },
};

#define TYPE_TABLE_SIZE (sizeof(type_table) / sizeof(type_table[0]))

setting_item_type_t setting_items_get_type(const char *key)
{
    for (size_t i = 0; i < TYPE_TABLE_SIZE; i++) {
        if (strcmp(type_table[i].key, key) == 0) {
            return type_table[i].type;
        }
    }
    return SETTING_ITEM_TYPE_INVALID;
}

const char *setting_items_get_default_value(const char *key)
{
    for (size_t i = 0; i < TYPE_TABLE_SIZE; i++) {
        if (strcmp(type_table[i].key, key) == 0) {
            return type_table[i].default_value;
        }
    }
    return NULL;
}

esp_err_t setting_items_set_defaults(bool only_uninitialized)
{
    for (size_t i = 0; i < TYPE_TABLE_SIZE; i++) {
        if (!only_uninitialized || (find_entry(type_table[i].key) < 0)) {
            store_write(type_table[i].key, type_table[i].default_value);
        }
    }
    return ESP_OK;
}

esp_err_t setting_items_init(void)
{
    return setting_items_set_defaults(false);
}

esp_err_t setting_items_init_with_storage(const setting_storage_iface_t *storage_iface)
{
    (void)storage_iface;
    return setting_items_set_defaults(false);
}

size_t setting_items_get_count(void)
{
    return TYPE_TABLE_SIZE;
}

const char *setting_items_get_key_at(size_t index)
{
    if (index >= TYPE_TABLE_SIZE) {
        return NULL;
    }
    return type_table[index].key;
}

const char *setting_items_type_to_string(setting_item_type_t type)
{
    switch (type) {
    case SETTING_ITEM_TYPE_STRING: return "string";
    case SETTING_ITEM_TYPE_BOOL:   return "bool";
    case SETTING_ITEM_TYPE_INT:    return "int";
    default:                       return "invalid";
    }
}
