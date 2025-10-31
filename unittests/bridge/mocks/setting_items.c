#include "setting_items.h"
#include <string.h>

bool mock_setting_items_read_should_fail = false;
int mock_setting_items_read_called = 0;
char mock_setting_items_read_keys[MOCK_SETTING_ITEMS_MAX_CALLS][SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_read_value_to_return_parity[SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_read_value_to_return_stopbits[SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_read_value_to_return_databits[SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_read_value_to_return_bridge_mode[SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_read_value_to_return_bridge_ip[SETTING_ITEM_MAX_STR_LEN] = {0};

bool mock_setting_items_read_int_should_fail = false;
int mock_setting_items_read_int_called = 0;
char mock_setting_items_read_int_keys[MOCK_SETTING_ITEMS_MAX_CALLS][SETTING_ITEM_MAX_STR_LEN] = {0};

int mock_setting_items_read_bool_called = 0;
bool mock_setting_items_read_bool_return_value = false;
char mock_setting_items_read_bool_keys[MOCK_SETTING_ITEMS_MAX_CALLS][SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_read_bool_last_key[SETTING_ITEM_MAX_STR_LEN] = {0};

typedef enum {
    KEY_UNKNOWN = 0,
    KEY_PARITY,
    KEY_STOPBITS,
    KEY_DATABITS,
    KEY_BRIDGE_MODE,
    KEY_BRIDGE_IP
} key_type_t;

static key_type_t key_type_from_key(const char *key)
{
    if (!key) {
        return KEY_UNKNOWN;
    }

    if (strncmp(key, "parity_", 7) == 0) {
        return KEY_PARITY;
    }
    if (strncmp(key, "stopbits_", 9) == 0) {
        return KEY_STOPBITS;
    }
    if (strncmp(key, "databits_", 9) == 0) {
        return KEY_DATABITS;
    }
    if (strncmp(key, "bridge_mode_", 12) == 0) {
        return KEY_BRIDGE_MODE;
    }
    if (strncmp(key, "bridge_ip_", 10) == 0) {
        return KEY_BRIDGE_IP;
    }

    return KEY_UNKNOWN;
}

esp_err_t setting_items_read(const char *key, char *value)
{
    if (mock_setting_items_read_should_fail) {
        return ESP_FAIL;
    }

    if (mock_setting_items_read_called < MOCK_SETTING_ITEMS_MAX_CALLS && key) {
        strncpy(mock_setting_items_read_keys[mock_setting_items_read_called], key,
                sizeof(mock_setting_items_read_keys[mock_setting_items_read_called]) - 1);
    }

    mock_setting_items_read_called++;

    if (value) {
        key_type_t kt = key_type_from_key(key);
        switch (kt) {
        case KEY_PARITY:
            if (mock_setting_items_read_value_to_return_parity[0] != '\0') {
                strncpy(value, mock_setting_items_read_value_to_return_parity, SETTING_ITEM_MAX_STR_LEN - 1);
                return ESP_OK;
            }
            break;
        case KEY_STOPBITS:
            if (mock_setting_items_read_value_to_return_stopbits[0] != '\0') {
                strncpy(value, mock_setting_items_read_value_to_return_stopbits, SETTING_ITEM_MAX_STR_LEN - 1);
                return ESP_OK;
            }
            break;
        case KEY_DATABITS:
            if (mock_setting_items_read_value_to_return_databits[0] != '\0') {
                strncpy(value, mock_setting_items_read_value_to_return_databits, SETTING_ITEM_MAX_STR_LEN - 1);
                return ESP_OK;
            }
            break;
        case KEY_BRIDGE_MODE:
            if (mock_setting_items_read_value_to_return_bridge_mode[0] != '\0') {
                strncpy(value, mock_setting_items_read_value_to_return_bridge_mode, SETTING_ITEM_MAX_STR_LEN - 1);
                return ESP_OK;
            }
            break;
        case KEY_BRIDGE_IP:
            if (mock_setting_items_read_value_to_return_bridge_ip[0] != '\0') {
                strncpy(value, mock_setting_items_read_value_to_return_bridge_ip, SETTING_ITEM_MAX_STR_LEN - 1);
                return ESP_OK;
            }
        case KEY_UNKNOWN:
            return ESP_FAIL;
        }
    }

    return ESP_FAIL;
}

int setting_items_read_int(const char *key)
{
    if (mock_setting_items_read_int_should_fail) {
        return 0;
    }

    if (mock_setting_items_read_int_called < MOCK_SETTING_ITEMS_MAX_CALLS && key) {
        strncpy(mock_setting_items_read_int_keys[mock_setting_items_read_int_called], key,
                sizeof(mock_setting_items_read_int_keys[mock_setting_items_read_int_called]) - 1);
    }

    mock_setting_items_read_int_called++;

    if (strcmp(key, "bridge_port_1") == 0) {
        return 502;
    } else if (strcmp(key, "bridge_port_2") == 0) {
        return 503;
    }
    if (strcmp(key, "baudrate_1") == 0 || strcmp(key, "baudrate_2") == 0) {
        return 9600;
    }

    return 0;
}

bool setting_items_read_bool(const char *key)
{
    if (mock_setting_items_read_bool_called < MOCK_SETTING_ITEMS_MAX_CALLS && key) {
        strncpy(mock_setting_items_read_bool_keys[mock_setting_items_read_bool_called], key,
                sizeof(mock_setting_items_read_bool_keys[0]) - 1);
    }

    mock_setting_items_read_bool_called++;

    if (key) {
        strncpy(mock_setting_items_read_bool_last_key, key, sizeof(mock_setting_items_read_bool_last_key) - 1);
    }
    return mock_setting_items_read_bool_return_value;
}

void mock_setting_items_reset(void)
{
    mock_setting_items_read_should_fail = false;
    mock_setting_items_read_called = 0;
    memset(mock_setting_items_read_keys, 0, sizeof(mock_setting_items_read_keys));
    memset(mock_setting_items_read_value_to_return_parity, 0, sizeof(mock_setting_items_read_value_to_return_parity));
    memset(mock_setting_items_read_value_to_return_stopbits, 0, sizeof(mock_setting_items_read_value_to_return_stopbits));
    memset(mock_setting_items_read_value_to_return_databits, 0, sizeof(mock_setting_items_read_value_to_return_databits));
    memset(mock_setting_items_read_value_to_return_bridge_mode, 0, sizeof(mock_setting_items_read_value_to_return_bridge_mode));
    memset(mock_setting_items_read_value_to_return_bridge_ip, 0, sizeof(mock_setting_items_read_value_to_return_bridge_ip));

    mock_setting_items_read_int_called = 0;
    memset(mock_setting_items_read_int_keys, 0, sizeof(mock_setting_items_read_int_keys));

    mock_setting_items_read_bool_called = 0;
    mock_setting_items_read_bool_return_value = false;
    memset(mock_setting_items_read_bool_keys, 0, sizeof(mock_setting_items_read_bool_keys));
}
