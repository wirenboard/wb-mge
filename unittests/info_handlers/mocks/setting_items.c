/* setting_items mock for the info_handlers unit test.
 *
 * The function under test, info_build_ap_clients_json(), branches on
 * setting_items_read_bool(KEY_WIFI_PERM_DISABLE). This mock exposes a single
 * controllable flag for that key so the test can drive both the early-return
 * (perm-disabled) path and the normal path. Other reads return benign defaults. */

#include "setting_items.h"
#include <string.h>

static bool mock_wifi_perm_disable = false;

void mock_setting_items_set_wifi_perm_disable(bool value)
{
    mock_wifi_perm_disable = value;
}

void mock_setting_items_reset(void)
{
    mock_wifi_perm_disable = false;
}

bool setting_items_read_bool(const char *key)
{
    if ((key != NULL) && (strcmp(key, KEY_WIFI_PERM_DISABLE) == 0)) {
        return mock_wifi_perm_disable;
    }
    return false;
}

int setting_items_read_int(const char *key)
{
    (void)key;
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
