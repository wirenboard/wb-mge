// Mock for setting_items used by the network unit tests.
// network.c reads about twenty keys, so a flat key/value store is simpler than the
// per-key accessors the smaller suites use. Boolean values follow the real
// setting_items_read_bool(): a key holds the literal "true", anything else is false.

#include <string.h>

#include "setting_items.h"
#include "setting_items_mock.h"

#define MOCK_MAX_ITEMS      32
#define MOCK_MAX_KEY_LEN    32

typedef struct {
    char key[MOCK_MAX_KEY_LEN];
    char value[SETTING_ITEM_MAX_STR_LEN];
} mock_setting_item_t;

static mock_setting_item_t items[MOCK_MAX_ITEMS];
static unsigned items_count = 0;

static mock_setting_item_t *find_item(const char *key)
{
    if (key == NULL) {
        return NULL;
    }
    for (unsigned index = 0; index < items_count; index++) {
        if (strncmp(items[index].key, key, MOCK_MAX_KEY_LEN - 1) == 0) {
            return &items[index];
        }
    }
    return NULL;
}

void mock_setting_items_reset(void)
{
    memset(items, 0, sizeof(items));
    items_count = 0;
}

void mock_setting_items_set(const char *key, const char *value)
{
    mock_setting_item_t *item = find_item(key);
    if (item == NULL) {
        if (items_count >= MOCK_MAX_ITEMS) {
            return;         // the test asked for more keys than the mock holds
        }
        item = &items[items_count++];
        strncpy(item->key, key, sizeof(item->key) - 1);
    }
    strncpy(item->value, value, sizeof(item->value) - 1);
    item->value[sizeof(item->value) - 1] = '\0';
}

void mock_setting_items_set_bool(const char *key, bool value)
{
    mock_setting_items_set(key, value ? "true" : "false");
}

esp_err_t setting_items_read(const char *key, char *value)
{
    mock_setting_item_t *item = find_item(key);
    if ((item == NULL) || (value == NULL)) {
        return ESP_FAIL;
    }
    strncpy(value, item->value, SETTING_ITEM_MAX_STR_LEN - 1);
    value[SETTING_ITEM_MAX_STR_LEN - 1] = '\0';
    return ESP_OK;
}

bool setting_items_read_bool(const char *key)
{
    mock_setting_item_t *item = find_item(key);
    if (item == NULL) {
        return false;
    }
    return (strncmp(item->value, "true", SETTING_ITEM_MAX_STR_LEN) == 0);
}
