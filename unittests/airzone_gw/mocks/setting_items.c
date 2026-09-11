/* setting_items mock for the airzone_gw unit test.
 *
 * airzone_gw.c uses exactly two calls from this module, so the mock is a tiny integer store on
 * top of them. It counts the reads, which is what lets a test assert the thing the RAM cache
 * exists for — that a poll of the register block reaches no storage at all — and it can be made
 * to fail a write, which is what the counter's persist-before-publish rule is judged on. */

#include "setting_items.h"

#include <string.h>

#define MOCK_STORE_SIZE   8
#define MOCK_KEY_LEN      32

typedef struct {
    char key[MOCK_KEY_LEN];
    int  value;
} mock_entry_t;

static mock_entry_t mock_store[MOCK_STORE_SIZE];
static size_t       mock_store_count;

int       mock_setting_items_read_count = 0;
int       mock_setting_items_save_count = 0;
esp_err_t mock_setting_items_save_error = ESP_OK;

static mock_entry_t *find_entry(const char *key)
{
    for (size_t i = 0; i < mock_store_count; i++) {
        if (strncmp(mock_store[i].key, key, MOCK_KEY_LEN) == 0) {
            return &mock_store[i];
        }
    }
    return NULL;
}

void mock_setting_items_reset(void)
{
    memset(mock_store, 0, sizeof(mock_store));
    mock_store_count = 0;
    mock_setting_items_read_count = 0;
    mock_setting_items_save_count = 0;
    mock_setting_items_save_error = ESP_OK;
}

/* Seed or overwrite a stored value without going through the module under test. */
void mock_setting_items_set_int(const char *key, int value)
{
    mock_entry_t *entry = find_entry(key);
    if (entry == NULL) {
        if (mock_store_count >= MOCK_STORE_SIZE) {
            return;
        }
        entry = &mock_store[mock_store_count++];
        strncpy(entry->key, key, MOCK_KEY_LEN - 1);
    }
    entry->value = value;
}

/* Read a stored value without counting as a read by the code under test. */
int mock_setting_items_get_int(const char *key)
{
    const mock_entry_t *entry = find_entry(key);
    return (entry != NULL) ? entry->value : 0;
}

int setting_items_read_int(const char *key)
{
    mock_setting_items_read_count++;
    const mock_entry_t *entry = find_entry(key);
    return (entry != NULL) ? entry->value : 0;
}

esp_err_t setting_items_save_int(const char *key, int value)
{
    mock_setting_items_save_count++;
    if (mock_setting_items_save_error != ESP_OK) {
        return mock_setting_items_save_error;
    }
    mock_setting_items_set_int(key, value);
    return ESP_OK;
}
