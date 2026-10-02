#include "setting_items_fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "setting_items.h"

#define MOCK_SETTINGS_MAX           16
#define MOCK_SETTINGS_KEY_LEN       32

typedef struct {
    char key[MOCK_SETTINGS_KEY_LEN];
    char value[SETTING_ITEM_MAX_STR_LEN];
} mock_setting_t;

static mock_setting_t mock_settings[MOCK_SETTINGS_MAX];
static size_t         mock_settings_count;

int mock_settings_reads = 0;

static mock_setting_t *mock_settings_find(const char *key)
{
    for (size_t i = 0; i < mock_settings_count; i++) {
        if (strcmp(mock_settings[i].key, key) == 0) {
            return &mock_settings[i];
        }
    }
    return NULL;
}

void mock_settings_set(const char *key, const char *value)
{
    mock_setting_t *item = mock_settings_find(key);
    if (item == NULL) {
        if (mock_settings_count >= MOCK_SETTINGS_MAX) {
            return;
        }
        item = &mock_settings[mock_settings_count++];
        snprintf(item->key, sizeof(item->key), "%s", key);
    }
    snprintf(item->value, sizeof(item->value), "%s", value);
}

void mock_settings_unset(const char *key)
{
    mock_setting_t *item = mock_settings_find(key);
    if (item == NULL) {
        return;
    }
    *item = mock_settings[--mock_settings_count];
}

void mock_settings_reset(void)
{
    memset(mock_settings, 0, sizeof(mock_settings));
    mock_settings_count = 0;
    mock_settings_reads = 0;
}

esp_err_t setting_items_read(const char *key, char *value)
{
    mock_settings_reads++;
    const mock_setting_t *item = mock_settings_find(key);
    if (item == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(value, SETTING_ITEM_MAX_STR_LEN, "%s", item->value);
    return ESP_OK;
}

int setting_items_read_int(const char *key)
{
    mock_settings_reads++;
    const mock_setting_t *item = mock_settings_find(key);
    return (item != NULL) ? atoi(item->value) : 0;
}
