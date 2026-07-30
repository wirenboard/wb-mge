/* setting_items mock for the info_handlers unit test.
 *
 * info_build_ap_clients_json() branches on setting_items_read_bool(KEY_WIFI_PERM_DISABLE),
 * so that key is exposed as a controllable flag to drive both the early-return
 * (perm-disabled) path and the normal path. info_get_handler() additionally reports the
 * configured cache Modbus port and enable flag straight from NVS; those two are
 * controllable as well, so a test can set them independently of the server's runtime
 * state. Other reads return benign defaults. */

#include "setting_items.h"
#include <string.h>

/* The two cache Modbus defaults mirror main/config.h (DEFAULT_CACHE_MODBUS_PORT "504",
 * DEFAULT_CACHE_MODBUS_SERVER_ENABLED "true"), which is what setting_items_read_*()
 * returns on hardware for a key that is absent from NVS. That matters most for the port:
 * resetting it to 0 would hand a test that forgets to set it an NVS state the firmware
 * never produces, because every save goes through validate_port(), which accepts only
 * 1..65535. false, by contrast, is an ordinary stored value for the enable flag, so that
 * half is purely about matching what an absent key reads as. */
#define MOCK_DEFAULT_CACHE_MODBUS_PORT              504
#define MOCK_DEFAULT_CACHE_MODBUS_SERVER_ENABLED    true

static bool mock_wifi_perm_disable = false;
static int  mock_cache_modbus_port = MOCK_DEFAULT_CACHE_MODBUS_PORT;
static bool mock_cache_modbus_server_enabled = MOCK_DEFAULT_CACHE_MODBUS_SERVER_ENABLED;

void mock_setting_items_set_wifi_perm_disable(bool value)
{
    mock_wifi_perm_disable = value;
}

void mock_setting_items_set_cache_modbus_port(int value)
{
    mock_cache_modbus_port = value;
}

void mock_setting_items_set_cache_modbus_server_enabled(bool value)
{
    mock_cache_modbus_server_enabled = value;
}

void mock_setting_items_reset(void)
{
    mock_wifi_perm_disable = false;
    mock_cache_modbus_port = MOCK_DEFAULT_CACHE_MODBUS_PORT;
    mock_cache_modbus_server_enabled = MOCK_DEFAULT_CACHE_MODBUS_SERVER_ENABLED;
}

bool setting_items_read_bool(const char *key)
{
    if (key == NULL) {
        return false;
    }
    if (strcmp(key, KEY_WIFI_PERM_DISABLE) == 0) {
        return mock_wifi_perm_disable;
    }
    if (strcmp(key, KEY_CACHE_MODBUS_SERVER_ENABLED) == 0) {
        return mock_cache_modbus_server_enabled;
    }
    return false;
}

int setting_items_read_int(const char *key)
{
    if ((key != NULL) && (strcmp(key, KEY_CACHE_MODBUS_PORT) == 0)) {
        return mock_cache_modbus_port;
    }
    return 0;
}

esp_err_t setting_items_read(const char *key, char *value)
{
    (void)key;
    if (value != NULL) {
        value[0] = '\0';
    }
    return ESP_OK;
}
