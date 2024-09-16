#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SETTING_ITEM_NUM_MAX 50
#define SETTING_ITEM_MAX_STR_LEN 32

typedef enum {
    SETTING_ITEM_TYPE_NUM,
    SETTING_ITEM_TYPE_STR,
    SETTING_ITEM_TYPE_BOOL,
} setting_item_type_t;

typedef bool (*save_to_storage)(const char *, const void *);
typedef bool (*read_from_storage)(const char *, void *);
typedef bool (*read_from_storage_raw)(const char *, void *, setting_item_type_t);

typedef int (*iface_storage_save_num)(const char *, uint32_t);
typedef int (*iface_storage_save_str)(const char *, const char *);
typedef int (*iface_storage_save_bool)(const char *, uint8_t);
typedef int (*iface_storage_read_num)(const char *, uint32_t *);
typedef int (*iface_storage_read_str)(const char *, char *);
typedef int (*iface_storage_read_bool)(const char *, uint8_t *);

typedef struct {
    iface_storage_save_num save_num;
    iface_storage_save_str save_str;
    iface_storage_save_bool save_bool;
    iface_storage_read_num read_num;
    iface_storage_read_str read_str;
    iface_storage_read_bool read_bool;
} setting_item_iface_t;

typedef struct {
    const char *key;
    const void *default_value;
    setting_item_type_t type_in_storage;
    setting_item_type_t type_in_json;
    save_to_storage save_to_storage;
    read_from_storage read_from_storage;
    read_from_storage_raw read_from_storage_raw;
} setting_item_t;

int setting_items_init(char * def_hostname, setting_item_iface_t *setting_item_iface);
int setting_items_set_defaults();
// Чтение данных из хранилища, без преобразования
int setting_items_read_raw(const char *key, void *value, setting_item_type_t type_in_storage);
// Чтение данных из хранилища, с преобразованием для использования в json
int setting_items_read(const char *key, void *value);
int setting_items_save(const char *key, void *value);
int setting_items_get_keys(const char **keys);
setting_item_type_t setting_items_get_type_in_json(const char *key);
