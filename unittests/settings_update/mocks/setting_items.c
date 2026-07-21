// Mock for setting_items used by the settings_update unit tests.
// settings_update reads the persisted cache Modbus server, MQTT and MQTT-serial-bridge
// settings, so the mock exposes those as directly settable values instead of a full
// key/value store.

#include "setting_items.h"

#include <string.h>

bool mock_setting_items_cache_server_enabled = false;
int  mock_setting_items_cache_port = 0;

bool mock_setting_items_mqtt_enabled = false;
int  mock_setting_items_mqtt_port = 1883;
char mock_setting_items_mqtt_host[SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_mqtt_user[SETTING_ITEM_MAX_STR_LEN] = {0};
char mock_setting_items_mqtt_pass[SETTING_ITEM_MAX_STR_LEN] = {0};

bool mock_setting_items_mqts_enabled = false;
int  mock_setting_items_mqts_port = 1;
int  mock_setting_items_mqts_slave_id = 1;

void mock_setting_items_reset(void)
{
    mock_setting_items_cache_server_enabled = false;
    mock_setting_items_cache_port = 0;

    mock_setting_items_mqtt_enabled = false;
    mock_setting_items_mqtt_port = 1883;
    mock_setting_items_mqtt_host[0] = '\0';
    mock_setting_items_mqtt_user[0] = '\0';
    mock_setting_items_mqtt_pass[0] = '\0';

    mock_setting_items_mqts_enabled = false;
    mock_setting_items_mqts_port = 1;
    mock_setting_items_mqts_slave_id = 1;
}

bool setting_items_read_bool(const char *key)
{
    if (key && (strcmp(key, KEY_CACHE_MODBUS_SERVER_ENABLED) == 0)) {
        return mock_setting_items_cache_server_enabled;
    }
    if (key && (strcmp(key, KEY_MQTT_ENABLED) == 0)) {
        return mock_setting_items_mqtt_enabled;
    }
    if (key && (strcmp(key, KEY_MQTS_ENABLED) == 0)) {
        return mock_setting_items_mqts_enabled;
    }
    return false;
}

int setting_items_read_int(const char *key)
{
    if (key && (strcmp(key, KEY_CACHE_MODBUS_PORT) == 0)) {
        return mock_setting_items_cache_port;
    }
    if (key && (strcmp(key, KEY_MQTT_PORT) == 0)) {
        return mock_setting_items_mqtt_port;
    }
    if (key && (strcmp(key, KEY_MQTS_PORT) == 0)) {
        return mock_setting_items_mqts_port;
    }
    if (key && (strcmp(key, KEY_MQTS_SLAVE_ID) == 0)) {
        return mock_setting_items_mqts_slave_id;
    }
    return 0;
}

// String settings: only the MQTT credentials are modelled; everything else reads back
// empty so the change detector sees a stable value.
esp_err_t setting_items_read(const char *key, char *value)
{
    if (!value) {
        return ESP_OK;
    }
    const char *src = "";
    if (key && (strcmp(key, KEY_MQTT_HOST) == 0)) {
        src = mock_setting_items_mqtt_host;
    } else if (key && (strcmp(key, KEY_MQTT_USER) == 0)) {
        src = mock_setting_items_mqtt_user;
    } else if (key && (strcmp(key, KEY_MQTT_PASS) == 0)) {
        src = mock_setting_items_mqtt_pass;
    }
    strncpy(value, src, SETTING_ITEM_MAX_STR_LEN - 1);
    value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
    return ESP_OK;
}
