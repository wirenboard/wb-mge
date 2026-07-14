#include "setting_items.h"
#include <string.h>

/* Credentials auth.c authorizes against. */
static char mock_login[SETTING_ITEM_MAX_STR_LEN] = "admin";
static char mock_pass[SETTING_ITEM_MAX_STR_LEN] = "wirenboard";

/* When set, setting_items_read() fails (NVS read error). */
bool mock_setting_items_read_should_fail = false;

esp_err_t setting_items_read(const char *key, char *value)
{
    if (mock_setting_items_read_should_fail) {
        return ESP_FAIL;
    }
    if (strcmp(key, KEY_LOGIN) == 0) {
        strncpy(value, mock_login, SETTING_ITEM_MAX_STR_LEN - 1);
        return ESP_OK;
    }
    if (strcmp(key, KEY_PASS) == 0) {
        strncpy(value, mock_pass, SETTING_ITEM_MAX_STR_LEN - 1);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

void mock_setting_items_reset(void)
{
    mock_setting_items_read_should_fail = false;
    strncpy(mock_login, "admin", sizeof(mock_login) - 1);
    strncpy(mock_pass, "wirenboard", sizeof(mock_pass) - 1);
}
