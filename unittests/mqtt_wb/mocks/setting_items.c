/* Mock of the settings storage.
 *
 * The MQTT side only reads: six mqtt_* keys and the four program names. One value per key in a
 * small table, so a test configures a device and the code under test reads it back. Unknown keys
 * read as the empty string / 0 / false, which is what a device with nothing stored would give. */

#include "setting_items.h"
#include "mock_setting_items.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOCK_SETTINGS_MAX       32

typedef struct {
    char key[32];
    char value[SETTING_ITEM_MAX_STR_LEN];
} mock_setting_t;

static mock_setting_t settings[MOCK_SETTINGS_MAX];
static size_t settings_count;

static mock_setting_t *find(const char *key)
{
    for (size_t i = 0; i < settings_count; i++) {
        if (strncmp(settings[i].key, key, sizeof(settings[i].key)) == 0) {
            return &settings[i];
        }
    }
    return NULL;
}

void mock_setting_items_set(const char *key, const char *value)
{
    mock_setting_t *item = find(key);
    if (item == NULL) {
        if (settings_count >= MOCK_SETTINGS_MAX) {
            return;
        }
        item = &settings[settings_count++];
        strncpy(item->key, key, sizeof(item->key) - 1);
    }
    strncpy(item->value, value, sizeof(item->value) - 1);
    item->value[sizeof(item->value) - 1] = '\0';
}

const char *mock_setting_items_get(const char *key)
{
    mock_setting_t *item = find(key);
    return (item != NULL) ? item->value : "";
}

void mock_setting_items_reset(void)
{
    memset(settings, 0, sizeof(settings));
    settings_count = 0;
}

esp_err_t setting_items_read(const char *key, char *value)
{
    mock_setting_t *item = find(key);
    if (item == NULL) {
        value[0] = '\0';
        return ESP_OK;
    }
    strncpy(value, item->value, SETTING_ITEM_MAX_STR_LEN - 1);
    value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
    return ESP_OK;
}

esp_err_t setting_items_save(const char *key, const char *value)
{
    mock_setting_items_set(key, value);
    return ESP_OK;
}

bool setting_items_read_bool(const char *key)
{
    return (strcmp(mock_setting_items_get(key), "true") == 0);
}

int setting_items_read_int(const char *key)
{
    return atoi(mock_setting_items_get(key));
}

esp_err_t setting_items_save_bool(const char *key, bool value)
{
    return setting_items_save(key, value ? "true" : "false");
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    char buf[SETTING_ITEM_MAX_STR_LEN];
    snprintf(buf, sizeof(buf), "%d", value);
    return setting_items_save(key, buf);
}
