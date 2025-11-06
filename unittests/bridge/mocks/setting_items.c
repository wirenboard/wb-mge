#include "unity.h"
#include "setting_items.h"
#include <string.h>

#define MOCK_DEFAULT_BRIDGE_IP                     "192.168.5.2"

mock_bridge_test_config_t mock_settings_items_bridge_cfg[BRIDGES_COUNT] = {0};
mock_setting_items_calls_t mock_setting_items_calls[BRIDGES_COUNT] = {0};

static unsigned index_from_key(const char *key)
{
    size_t key_len = strlen(key);

    if (key[key_len - 1] == '1') {
        return 0;
    }

    if (key[key_len - 1] == '2') {
        return 1;
    }

    TEST_FAIL_MESSAGE("Invalid key index");
}

esp_err_t setting_items_read(const char *key, char *value)
{
    unsigned index = index_from_key(key);

    mock_setting_items_calls[index].read_called++;

    TEST_ASSERT_NOT_NULL_MESSAGE(key, "setting_items_read called with NULL key pointer");
    TEST_ASSERT_NOT_NULL_MESSAGE(value, "setting_items_read called with NULL value pointer");

    if (strcmp(key, "parity_1") == 0 || strcmp(key, "parity_2") == 0) {
        if (mock_setting_items_calls[index].read_result.parity != ESP_OK) {
            return mock_setting_items_calls[index].read_result.parity;
        }
        strcpy(value, mock_settings_items_bridge_cfg[index].serial_config.parity);
        return ESP_OK;
    }

    if (strcmp(key, "stopbits_1") == 0 || strcmp(key, "stopbits_2") == 0) {
        if (mock_setting_items_calls[index].read_result.stopbits != ESP_OK) {
            return mock_setting_items_calls[index].read_result.stopbits;
        }
        strcpy(value, mock_settings_items_bridge_cfg[index].serial_config.stopbits);
        return ESP_OK;
    }

    if (strcmp(key, "databits_1") == 0 || strcmp(key, "databits_2") == 0) {
        if (mock_setting_items_calls[index].read_result.databits != ESP_OK) {
            return mock_setting_items_calls[index].read_result.databits;
        }
        strcpy(value, mock_settings_items_bridge_cfg[index].serial_config.databits);
        return ESP_OK;
    }

    if (strcmp(key, "bridge_mode_1") == 0 || strcmp(key, "bridge_mode_2") == 0) {
        if (mock_setting_items_calls[index].read_result.bridge_mode != ESP_OK) {
            return mock_setting_items_calls[index].read_result.bridge_mode;
        }
        strcpy(value, mock_settings_items_bridge_cfg[index].bridge_mode);
        return ESP_OK;
    }

    if (strcmp(key, "bridge_ip_1") == 0 || strcmp(key, "bridge_ip_2") == 0) {
        if (mock_setting_items_calls[index].read_result.bridge_ip != ESP_OK) {
            return mock_setting_items_calls[index].read_result.bridge_ip;
        }
        strcpy(value, mock_settings_items_bridge_cfg[index].bridge_ip);
        return ESP_OK;
    }

    return ESP_FAIL;
}

int setting_items_read_int(const char *key)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(key, "setting_items_read_int called with NULL key pointer");

    unsigned index = index_from_key(key);

    mock_setting_items_calls[index].read_int_called++;

    if (strcmp(key, "bridge_port_1") == 0 || strcmp(key, "bridge_port_2") == 0) {
        return mock_settings_items_bridge_cfg[index].bridge_port;
    }

    if (strcmp(key, "baudrate_1") == 0 || strcmp(key, "baudrate_2") == 0) {
        return mock_settings_items_bridge_cfg[index].serial_config.baudrate;
    }

    return 0;
}

bool setting_items_read_bool(const char *key)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(key, "setting_items_read_bool called with NULL key pointer");

    unsigned index = index_from_key(key);

    mock_setting_items_calls[index].read_bool_called++;

    if (strcmp(key, "bridge_modbus_1") == 0 || strcmp(key, "bridge_modbus_2") == 0) {
        return mock_settings_items_bridge_cfg[index].bridge_mb;
    }

    return false;
}

void mock_setting_items_reset(void)
{
    memset(mock_settings_items_bridge_cfg, 0, sizeof(mock_settings_items_bridge_cfg));

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        mock_settings_items_bridge_cfg[index].serial_config.baudrate = MOCK_DEFAULT_BAUDRATE;
        strcpy(mock_settings_items_bridge_cfg[index].bridge_ip, MOCK_DEFAULT_BRIDGE_IP);
        if (index == 0) {
            mock_settings_items_bridge_cfg[index].bridge_port = MOCK_DEFAULT_BRIDGE_PORT;
        } else {
            mock_settings_items_bridge_cfg[index].bridge_port = MOCK_DEFAULT_BRIDGE_PORT2;
        }
    }

    memset(mock_setting_items_calls, 0, sizeof(mock_setting_items_calls));
}
