#include "setting_items.h"

#include "stdint.h"
#include "stdbool.h"
#include "string.h"

#define UART_BAUD_RATE_MIN 300
#define UART_BAUD_RATE_MAX 460800
#define IP_ADDR_NUM 4
#define IP_ADDR_STR_LEN 16


setting_item_iface_t iface;
const setting_item_t setting_items[SETTING_ITEM_NUM_MAX];

static bool string2stopbits(const char *str, uint32_t *stopbits)
{
    uint32_t val;
    if (strcmp(str, "1-bit") == 0) {
        val = UART_STOP_BITS_1;
    } else if (strcmp(str, "1.5-bit") == 0) {
        val = UART_STOP_BITS_1_5;
    } else if (strcmp(str, "2-bit") == 0) {
        val = UART_STOP_BITS_2;
    } else {
        return false;
    }
    if (stopbits != NULL) {
        *stopbits = val;
    }
    return true;
}

static bool stopbits2string(uint32_t stopbits, char *str)
{
    switch (stopbits) {
        case UART_STOP_BITS_1:
            strcpy(str, "1-bit");
            return true;
        case UART_STOP_BITS_1_5:
            strcpy(str, "1.5-bit");
            return true;
        case UART_STOP_BITS_2:
            strcpy(str, "2-bit");
            return true;
    }
    return false;
}

static bool string2databits(const char *str, uint32_t *databits)
{
    uint32_t val;
    if (strcmp(str, "5-bit") == 0) {
        val = UART_DATA_5_BITS;
    } else if (strcmp(str, "6-bit") == 0) {
        val = UART_DATA_6_BITS;
    } else if (strcmp(str, "7-bit") == 0) {
        val = UART_DATA_7_BITS;
    } else if (strcmp(str, "8-bit") == 0) {
        val = UART_DATA_8_BITS;
    } else {
        return false;
    }
    if (databits != NULL) {
        *databits = val;
    }
    return true;
}

static bool databits2string(uint32_t databits, char *str)
{
    switch (databits) {
        case UART_DATA_5_BITS:
            strcpy(str, "5-bit");
            return true;
        case UART_DATA_6_BITS:
            strcpy(str, "6-bit");
            return true;
        case UART_DATA_7_BITS:
            strcpy(str, "7-bit");
            return true;
        case UART_DATA_8_BITS:
            strcpy(str, "8-bit");
            return true;
    }
    return false;
}

static bool string2parity(const char *str, uint32_t *parity)
{
    uint32_t val;
    if (strcmp(str, "none") == 0) {
        val = UART_PARITY_DISABLE;
    } else if (strcmp(str, "even") == 0) {
        val = UART_PARITY_EVEN;
    } else if (strcmp(str, "odd") == 0) {
        val = UART_PARITY_ODD;
    } else {
        return false;
    }
    if (parity != NULL) {
        *parity = val;
    }
    return true;
}

static bool parity2string(uint32_t parity, char *str)
{
    switch (parity) {
        case UART_PARITY_DISABLE:
            strcpy(str, "none");
            return true;
        case UART_PARITY_EVEN:
            strcpy(str, "even");
            return true;
        case UART_PARITY_ODD:
            strcpy(str, "odd");
            return true;
    }
    return false;
}

static bool ip2string(uint32_t ip, char *str)
{
    int ret = snprintf(str, IP_ADDR_STR_LEN, "%lu.%lu.%lu.%lu", ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    if ((ret < 0) || (ret >= IP_ADDR_STR_LEN)) {
        return false;
    }
    return true;
}

static bool string2ip(const char *str, uint32_t *ip)
{
    int ip_arr[IP_ADDR_NUM + 1] = {0};
    if (sscanf(str, "%d.%d.%d.%d.%d", &ip_arr[3], &ip_arr[2], &ip_arr[1], &ip_arr[0], &ip_arr[4]) == IP_ADDR_NUM) {
        for (int i = 0; i < IP_ADDR_NUM; i++) {
            if (ip_arr[i] < 0 || ip_arr[i] > 255) {
                return false;
            }
        }
        if (ip != NULL) {
            *ip = ((uint32_t)((ip_arr[0]) & 0xff) << 24) | ((uint32_t)((ip_arr[1]) & 0xff) << 16) | ((uint32_t)((ip_arr[2]) & 0xff) << 8) |
                  (uint32_t)((ip_arr[3]) & 0xff);
        }
        return true;
    } else {
        return false;
    }
}

static bool save_boudrate(const char *key, const void *value)
{
    uint32_t baudrate = *(uint32_t *)value;
    if (baudrate < UART_BAUD_RATE_MIN || baudrate > UART_BAUD_RATE_MAX) {
        return false;
    }
    if (iface.save_num(key, baudrate) != 0) {
        return false;
    }
    return true;
}

static bool read_boudrate(const char *key, void *value)
{
    uint32_t baudrate = 0;
    if (iface.read_num(key, &baudrate) != 0) {
        return false;
    }
    *(uint32_t *)value = baudrate;
    return true;
}

static bool save_ip(const char *key, const void *value)
{
    uint32_t ip = 0;
    if (string2ip(value, &ip)) {
        if (iface.save_num(key, ip) != 0) {
            return false;
        }
        return true;
    }
    return false;
}

static bool read_ip(const char *key, void *value)
{
    uint32_t ip = 0;
    if (iface.read_num(key, &ip) != 0) {
        return false;
    }
    if (ip2string(ip, value) == false) {
        return false;
    }
    return true;
}

static bool save_databits(const char *key, const void *value)
{
    uint32_t databits = 0;
    if (string2databits((char *)value, &databits)) {
        if (iface.save_num(key, databits) != 0) {
            return false;
        }
        return true;
    }
    return false;
}

static bool read_databits(const char *key, void *value)
{
    uint32_t databits = 0;
    if (iface.read_num(key, &databits) != 0) {
        return false;
    }
    if (databits2string(databits, value) == false) {
        return false;
    }
    return true;
}

static bool save_stopbits(const char *key, const void *value)
{
    uint32_t stopbits = 0;
    if (string2stopbits((char *)value, &stopbits)) {
        if (iface.save_num(key, stopbits) != 0) {
            return false;
        }
        return true;
    }
    return false;
}

static bool read_stopbits(const char *key, void *value)
{
    uint32_t stopbits = 0;
    if (iface.read_num(key, &stopbits) != 0) {
        return false;
    }
    if (stopbits2string(stopbits, value) == false) {
        return false;
    }
    return true;
}

static bool save_parity(const char *key, const void *value)
{
    uint32_t parity = 0;
    if (string2parity(value, &parity)) {
        if (iface.save_num(key, parity) != 0) {
            return false;
        }
        return true;
    }
    return false;
}

static bool read_parity(const char *key, void *value)
{
    uint32_t parity = 0;
    if (iface.read_num(key, &parity) != 0) {
        return false;
    }
    if (parity2string(parity, value) == false) {
        return false;
    }
    return true;
}

static bool save_string_value(const char *key, const void *value)
{
    char *str = (char *)value;
    if (strlen(str) > SETTING_ITEM_MAX_STR_LEN) {
        return false;
    }
    if (iface.save_str(key, value) != 0) {
        return false;
    }
    return true;
}

static bool read_string_value(const char *key, void *value)
{
    char str[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (iface.read_str(key, str) != 0) {
        return false;
    }
    strcpy((char *)value, str);
    return true;
}

static bool save_bool_value(const char *key, const void *value)
{
    uint8_t val = *(uint8_t *)value;
    if (iface.save_bool(key, val) != 0) {
        return false;
    }
    return true;
}

static bool read_bool_value(const char *key, void *value)
{
    uint8_t val = 0;
    if (iface.read_bool(key, &val) != 0) {
        return false;
    }
    *(uint8_t *)value = val;
    return true;
}

static bool read_raw_value(const char *key, void *value, setting_item_type_t type)
{
    switch (type) {
        case SETTING_ITEM_TYPE_NUM: {
            uint32_t val = 0;
            if (iface.read_num(key, &val) != 0) {
                return false;
            }
            *(uint32_t *)value = val;
            return true;
        }
        case SETTING_ITEM_TYPE_STR: {
            char str[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (iface.read_str(key, str) != 0) {
                return false;
            }
            strcpy((char *)value, str);
            return true;
        }
        case SETTING_ITEM_TYPE_BOOL: {
            uint8_t val = 0;
            if (iface.read_bool(key, &val) != 0) {
                return false;
            }
            *(uint8_t *)value = val;
            return true;
        }
        default:
            return false;
    }
}

int setting_items_init(setting_item_iface_t *setting_item_iface)
{
    if (setting_item_iface == NULL) {
        return -1;
    }
    if (setting_item_iface->save_bool == NULL || setting_item_iface->save_num == NULL || setting_item_iface->save_str == NULL ||
        setting_item_iface->read_bool == NULL || setting_item_iface->read_num == NULL || setting_item_iface->read_str == NULL) {
        return -1;
    }
    iface.save_bool = setting_item_iface->save_bool;
    iface.save_num = setting_item_iface->save_num;
    iface.save_str = setting_item_iface->save_str;
    iface.read_bool = setting_item_iface->read_bool;
    iface.read_num = setting_item_iface->read_num;
    iface.read_str = setting_item_iface->read_str;
    return 0;
}

static int setting_items_get_index(const char *key)
{
    for (int i = 0; i < sizeof(setting_items) / sizeof(setting_items[0]); i++) {
        if (setting_items[i].key == NULL) {
            return -1;
        }
        if (strcmp(setting_items[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

int setting_items_get_keys(const char **keys)
{
    int num = 0;
    for (int i = 0; i < sizeof(setting_items) / sizeof(setting_items[0]); i++) {
        if (setting_items[i].key == NULL) {
            break;
        }
        if (keys != NULL) {
            keys[i] = setting_items[i].key;
        }
        num++;
    }
    return num;
}

int setting_items_set_defaults(void)
{
    for (int i = 0; i < sizeof(setting_items) / sizeof(setting_items[0]); i++) {
        if (setting_items[i].save_to_storage != NULL) {
            bool ret = setting_items[i].save_to_storage(setting_items[i].key, setting_items[i].default_value);
            if (ret != true) {
                return -1;
            }
        }
    }
    return 0;
}

int setting_items_read_raw(const char *key, void *value, setting_item_type_t type_in_storage)
{
    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    if (type_in_storage != setting_items[index].type_in_storage) {
        return -1;
    }
    if (setting_items[index].read_from_storage_raw != NULL) {
        bool ret = setting_items[index].read_from_storage_raw(key, value, type_in_storage);
        if (ret != true) {
            return -1;
        }
    }
    return 0;
}

int setting_items_read(const char *key, void *value)
{
    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    if (setting_items[index].read_from_storage != NULL) {
        bool ret = setting_items[index].read_from_storage(key, value);
        if (ret != true) {
            return -1;
        }
    }
    return 0;
}

setting_item_type_t setting_items_get_type_in_json(const char *key)
{
    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    return setting_items[index].type_in_json;
}

int setting_items_save(const char *key, void *value)
{
    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    if (setting_items[index].save_to_storage != NULL) {
        bool ret = setting_items[index].save_to_storage(key, value);
        if (ret != true) {
            return -1;
        }
    }
    return 0;
}

const int default_baudrate = 9600;
const bool default_eth_dhcpc = true;

const setting_item_t setting_items[] = {
    {
        .key = "hostname",
        .default_value = "WB-MGE",
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_string_value,
        .read_from_storage = read_string_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = "baudrate",
        .default_value = &default_baudrate,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_NUM,
        .save_to_storage = save_boudrate,
        .read_from_storage = read_boudrate,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = "stopbits",
        .default_value = "1-bit",
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_stopbits,
        .read_from_storage = read_stopbits,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = "parity",
        .default_value = "none",
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_parity,
        .read_from_storage = read_parity,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = "databits",
        .default_value = "8-bit",
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_databits,
        .read_from_storage = read_databits,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = "eth_ip",
        .default_value = "192.168.5.1",
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = "eth_dhcpc",
        .default_value = &default_eth_dhcpc,
        .type_in_storage = SETTING_ITEM_TYPE_BOOL,
        .type_in_json = SETTING_ITEM_TYPE_BOOL,
        .save_to_storage = save_bool_value,
        .read_from_storage = read_bool_value,
        .read_from_storage_raw = read_raw_value,
    },
};
