#include "setting_items.h"

#include "array_size.h"
#include "config.h"
#include "driver/uart.h"
#include "esp_mac.h"
#include "esp_wifi_types_generic.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#define IP_ADDR_OCTETS_NUM      4
#define IP_ADDR_STR_LEN         16
#define MIN_TCP_PORT            1
#define MAX_DYNAMIC_PORT        65535

#define SETTING_ITEMS_NUM       ARRAY_SIZE(setting_items)

typedef struct {
    const char *str;
    int value;
} string_int_map_t;

static setting_item_iface_t iface = {0};
static const setting_item_t setting_items[SETTING_ITEMS_NUM_MAX];  // инициализируется в конце файла

static char generated_hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
static const int default_baudrate = DEFAULT_BAUDRATE;
static const bool default_eth_dhcpc = DEFAULT_ETH_DHCPC;
static const int default_bridge_port = DEFAULT_BRIDGE_PORT;
static const int default_bridge_port2 = DEFAULT_BRIDGE_PORT2;
static const bool default_bridge_mb = DEFAULT_BRIDGE_MB;

static const string_int_map_t wifi_mode_map[] = {
    {WIFI_MODE_AP_STR, WIFI_MODE_AP},
    {WIFI_MODE_STA_STR, WIFI_MODE_STA},
    {WIFI_MODE_APSTA_STR, WIFI_MODE_APSTA},
    {WIFI_MODE_NULL_STR, WIFI_MODE_NULL},
    {NULL, -1}
};

static const string_int_map_t stopbits_map[] = {
    {UART_STOP_BITS_1_STR, UART_STOP_BITS_1},
    {UART_STOP_BITS_1_5_STR, UART_STOP_BITS_1_5},
    {UART_STOP_BITS_2_STR, UART_STOP_BITS_2},
    {NULL, -1}
};

static const string_int_map_t databits_map[] = {
    {UART_DATA_5_BITS_STR, UART_DATA_5_BITS},
    {UART_DATA_6_BITS_STR, UART_DATA_6_BITS},
    {UART_DATA_7_BITS_STR, UART_DATA_7_BITS},
    {UART_DATA_8_BITS_STR, UART_DATA_8_BITS},
    {NULL, -1}
};

static const string_int_map_t parity_map[] = {
    {UART_PARITY_DISABLE_STR, UART_PARITY_DISABLE},
    {UART_PARITY_EVEN_STR, UART_PARITY_EVEN},
    {UART_PARITY_ODD_STR, UART_PARITY_ODD},
    {NULL, -1}
};

static const string_int_map_t bridge_mode_map[] = {
    {BRIDGE_MODE_SERVER_STR, BRIDGE_MODE_SERVER},
    {BRIDGE_MODE_CLIENT_STR, BRIDGE_MODE_CLIENT},
    {NULL, -1}
};

static bool string2int(const char *str, int *value, const string_int_map_t *map)
{
    for (int i = 0; map[i].str != NULL; i++) {
        if (strncmp(str, map[i].str, SETTING_ITEM_MAX_STR_LEN) == 0) {
            if (value != NULL) {
                *value = map[i].value;
            }
            return true;
        }
    }
    return false;
}

static bool int2string(int value, char *str, const string_int_map_t *map)
{
    for (int i = 0; map[i].str != NULL; i++) {
        if (value == map[i].value) {
            strncpy(str, map[i].str, SETTING_ITEM_MAX_STR_LEN);
            return true;
        }
    }
    return false;
}

static bool save_map_value(const char *key, const void *value, const string_int_map_t *map)
{
    uint32_t num_value = 0;
    if (string2int((char *)value, (int *)&num_value, map)) {
        if (iface.save_num(key, num_value) != 0) {
            return false;
        }
        return true;
    }
    return false;
}

static bool read_map_value(const char *key, void *value, const string_int_map_t *map)
{
    uint32_t num_value = 0;
    if (iface.read_num(key, &num_value) != 0) {
        return false;
    }
    if (int2string((int)num_value, value, map) == false) {
        return false;
    }
    return true;
}

static bool ip2string(uint32_t ip, char *str)
{
    int ret = snprintf(str, IP_ADDR_STR_LEN, "%u.%u.%u.%u",
                       (uint8_t)(ip & 0xFF),           // 4st octet
                       (uint8_t)((ip >> 8) & 0xFF),    // 3nd octet
                       (uint8_t)((ip >> 16) & 0xFF),   // 2rd octet
                       (uint8_t)((ip >> 24) & 0xFF));  // 1th octet
    if ((ret < 0) || (ret >= IP_ADDR_STR_LEN)) {
        return false;
    }
    return true;
}

static bool string2ip(const char *str, uint32_t *ip)
{
    if (str == NULL) {
        return false;
    }
    if (strnlen(str, (IP_ADDR_STR_LEN + 1)) > IP_ADDR_STR_LEN) {
        return false;
    }

    // Дополнительная ячейка нужна для проверки наличия лишнего байта ip адреса
    int octets[IP_ADDR_OCTETS_NUM + 1] = {0};
    if (sscanf(str, "%d.%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3], &octets[4]) ==
        IP_ADDR_OCTETS_NUM) {
        for (int i = 0; i < IP_ADDR_OCTETS_NUM; i++) {
            if ((octets[i] < 0) || (octets[i] > 255)) {
                return false;
            }
        }
        if (ip != NULL) {
            *ip = ((uint32_t)((octets[3]) & 0xff) << 24) |  // 1st octet
                  ((uint32_t)((octets[2]) & 0xff) << 16) |  // 2nd octet
                  ((uint32_t)((octets[1]) & 0xff) << 8) |   // 3rd octet
                  (uint32_t)((octets[0]) & 0xff);           // 4th octet
        }
        return true;
    }

    return false;
}

static bool save_wifi_mode(const char *key, const void *value)
{
    return save_map_value(key, value, wifi_mode_map);
}

static bool read_wifi_mode(const char *key, void *value)
{
    return read_map_value(key, value, wifi_mode_map);
}

static bool save_baudrate(const char *key, const void *value)
{
    uint32_t baudrate = *(uint32_t *)value;
    if ((baudrate < UART_BAUD_RATE_MIN) || (baudrate > UART_BAUD_RATE_MAX)) {
        return false;
    }
    if (iface.save_num(key, baudrate) != 0) {
        return false;
    }
    return true;
}

static bool read_baudrate(const char *key, void *value)
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
    return save_map_value(key, value, databits_map);
}

static bool read_databits(const char *key, void *value)
{
    return read_map_value(key, value, databits_map);
}

static bool save_stopbits(const char *key, const void *value)
{
    return save_map_value(key, value, stopbits_map);
}

static bool read_stopbits(const char *key, void *value)
{
    return read_map_value(key, value, stopbits_map);
}

static bool save_parity(const char *key, const void *value)
{
    return save_map_value(key, value, parity_map);
}

static bool read_parity(const char *key, void *value)
{
    return read_map_value(key, value, parity_map);
}

static bool save_bridge_mode(const char *key, const void *value)
{
    return save_map_value(key, value, bridge_mode_map);
}

static bool read_bridge_mode(const char *key, void *value)
{
    return read_map_value(key, value, bridge_mode_map);
}

static bool save_bridge_port(const char *key, const void *value)
{
    int bridge_port = *(int *)value;
    if ((bridge_port < MIN_TCP_PORT) || (bridge_port > MAX_DYNAMIC_PORT)) {
        return false;
    }
    if (iface.save_num(key, bridge_port) != 0) {
        return false;
    }
    return true;
}

static bool read_bridge_port(const char *key, void *value)
{
    uint32_t bridge_port = 0;
    if (iface.read_num(key, &bridge_port) != 0) {
        return false;
    }
    *(uint32_t *)value = bridge_port;
    return true;
}

static bool save_string_value(const char *key, const void *value)
{
    char *str = (char *)value;
    if (strnlen(str, (SETTING_ITEM_MAX_STR_LEN + 1)) > SETTING_ITEM_MAX_STR_LEN) {
        return false;
    }
    if (iface.save_str(key, value) != 0) {
        return false;
    }
    return true;
}

static bool save_non_zero_len_string_value(const char *key, const void *value)
{
    char *str = (char *)value;
    if (strnlen(str, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return false;
    }

    return save_string_value(key, value);
}

static bool read_string_value(const char *key, void *value)
{
    char str[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (iface.read_str(key, str) != 0) {
        return false;
    }
    strncpy((char *)value, str, SETTING_ITEM_MAX_STR_LEN);
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
            strncpy((char *)value, str, SETTING_ITEM_MAX_STR_LEN);
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

static bool set_default_value_for_item(int index)
{
    if (setting_items[index].save_to_storage != NULL) {
        return setting_items[index].save_to_storage(setting_items[index].key,
            setting_items[index].default_value);
    }
    return false;
}

int setting_items_init(char *hostname, setting_item_iface_t *setting_item_iface)
{
    if (setting_item_iface == NULL) {
        return -1;
    }
    if ((setting_item_iface->save_bool == NULL) || (setting_item_iface->save_num == NULL) ||
        (setting_item_iface->save_str == NULL) || (setting_item_iface->read_bool == NULL) ||
        (setting_item_iface->read_num == NULL) || (setting_item_iface->read_str == NULL) ||
        (setting_item_iface->has_key == NULL))
    {
        return -1;
    }
    if (hostname == NULL) {
        return -1;
    }

    int len = strnlen(hostname, (SETTING_ITEM_MAX_STR_LEN + 1));
    if ((len > SETTING_ITEM_MAX_STR_LEN) || (len == 0)) {
        return -1;
    }

    iface.save_bool = setting_item_iface->save_bool;
    iface.save_num = setting_item_iface->save_num;
    iface.save_str = setting_item_iface->save_str;
    iface.read_bool = setting_item_iface->read_bool;
    iface.read_num = setting_item_iface->read_num;
    iface.read_str = setting_item_iface->read_str;
    iface.has_key = setting_item_iface->has_key;

    strncpy(generated_hostname, hostname, SETTING_ITEM_MAX_STR_LEN);

    // Проверка, есть ли такие ключи в хранилище. Если нет, то запись значений по умолчанию.
    for (int i = 0; i < SETTING_ITEMS_NUM; i++) {
        if (setting_items[i].key == NULL) {
            break;
        }
        if (iface.has_key(setting_items[i].key) != true) {
            if (set_default_value_for_item(i) != true) {
                return -1;
            }
        }
    }

    return 0;
}

static int setting_items_get_index(const char *key)
{
    for (int i = 0; i < SETTING_ITEMS_NUM; i++) {
        if (setting_items[i].key == NULL) {
            return -1;
        }
        if (strncmp(setting_items[i].key, key, SETTING_ITEM_MAX_STR_LEN) == 0) {
            return i;
        }
    }
    return -1;
}

int setting_items_get_keys(const char **keys)
{
    int num = 0;
    for (int i = 0; i < SETTING_ITEMS_NUM; i++) {
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
    for (int i = 0; i < SETTING_ITEMS_NUM; i++) {
        if (setting_items[i].key == NULL) {
            break;
        }
        if (set_default_value_for_item(i) != true) {
            return -1;
        }
    }
    return 0;
}

int setting_items_read_raw(const char *key, void *value, setting_item_type_t type_in_storage)
{
    if ((key == NULL) || (value == NULL)) {
        return -1;
    }

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
    if ((key == NULL) || (value == NULL)) {
        return -1;
    }

    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    if (setting_items[index].read_from_storage != NULL) {
        bool ret = setting_items[index].read_from_storage(key, value);
        if (ret != true) {
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

setting_item_type_t setting_items_get_type_in_json(const char *key)
{
    if (key == NULL) {
        return -1;
    }

    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    return setting_items[index].type_in_json;
}

int setting_items_save(const char *key, const void *value)
{
    if ((key == NULL) || (value == NULL)) {
        return -1;
    }

    int index = setting_items_get_index(key);
    if (index == -1) {
        return -1;
    }
    if (setting_items[index].save_to_storage != NULL) {
        bool ret = setting_items[index].save_to_storage(key, value);
        if (ret != true) {
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

static const setting_item_t setting_items[] = {
    {
        .key = KEY_HOSTNAME,
        .default_value = generated_hostname,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_non_zero_len_string_value,
        .read_from_storage = read_string_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_LOGIN,
        .default_value = DEFAULT_LOGIN,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_non_zero_len_string_value,
        .read_from_storage = read_string_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_PASS,
        .default_value = DEFAULT_PASS,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_non_zero_len_string_value,
        .read_from_storage = NULL,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BAUDRATE1,
        .default_value = &default_baudrate,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_NUM,
        .save_to_storage = save_baudrate,
        .read_from_storage = read_baudrate,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_STOPBITS1,
        .default_value = DEFAULT_STOPBITS,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_stopbits,
        .read_from_storage = read_stopbits,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_PARITY1,
        .default_value = DEFAULT_PARITY,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_parity,
        .read_from_storage = read_parity,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_DATABITS1,
        .default_value = DEFAULT_DATABITS,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_databits,
        .read_from_storage = read_databits,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BAUDRATE2,
        .default_value = &default_baudrate,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_NUM,
        .save_to_storage = save_baudrate,
        .read_from_storage = read_baudrate,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_STOPBITS2,
        .default_value = DEFAULT_STOPBITS,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_stopbits,
        .read_from_storage = read_stopbits,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_PARITY2,
        .default_value = DEFAULT_PARITY,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_parity,
        .read_from_storage = read_parity,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_DATABITS2,
        .default_value = DEFAULT_DATABITS,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_databits,
        .read_from_storage = read_databits,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_ETH_IP_STATIC,
        .default_value = DEFAULT_ETH_IP_STATIC,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_ETH_MASK_STATIC,
        .default_value = DEFAULT_ETH_MASK_STATIC,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_ETH_GW_STATIC,
        .default_value = DEFAULT_ETH_GW_STATIC,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_ETH_DHCPC,
        .default_value = &default_eth_dhcpc,
        .type_in_storage = SETTING_ITEM_TYPE_BOOL,
        .type_in_json = SETTING_ITEM_TYPE_BOOL,
        .save_to_storage = save_bool_value,
        .read_from_storage = read_bool_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_WIFI_MODE,
        .default_value = DEFAULT_WIFI_MODE,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_wifi_mode,
        .read_from_storage = read_wifi_mode,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_AP_IP_STATIC,
        .default_value = DEFAULT_AP_IP_STATIC,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_AP_MASK_STATIC,
        .default_value = DEFAULT_AP_MASK_STATIC,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_AP_GW_STATIC,
        .default_value = DEFAULT_AP_GW_STATIC,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_AP_SSID,
        .default_value = generated_hostname,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_non_zero_len_string_value,
        .read_from_storage = read_string_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_AP_PASS,
        .default_value = DEFAULT_AP_PASS,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_string_value,
        .read_from_storage = NULL,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_STA_SSID,
        .default_value = DEFAULT_STA_SSID,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_string_value,
        .read_from_storage = read_string_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_STA_PASS,
        .default_value = DEFAULT_STA_PASS,
        .type_in_storage = SETTING_ITEM_TYPE_STR,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_string_value,
        .read_from_storage = NULL,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_MODE1,
        .default_value = DEFAULT_BRIDGE_MODE,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_bridge_mode,
        .read_from_storage = read_bridge_mode,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_PORT1,
        .default_value = &default_bridge_port,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_NUM,
        .save_to_storage = save_bridge_port,
        .read_from_storage = read_bridge_port,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_IP1,
        .default_value = DEFAULT_BRIDGE_IP,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_MB1,
        .default_value = &default_bridge_mb,
        .type_in_storage = SETTING_ITEM_TYPE_BOOL,
        .type_in_json = SETTING_ITEM_TYPE_BOOL,
        .save_to_storage = save_bool_value,
        .read_from_storage = read_bool_value,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_MODE2,
        .default_value = DEFAULT_BRIDGE_MODE,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_bridge_mode,
        .read_from_storage = read_bridge_mode,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_PORT2,
        .default_value = &default_bridge_port2,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_NUM,
        .save_to_storage = save_bridge_port,
        .read_from_storage = read_bridge_port,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_IP2,
        .default_value = DEFAULT_BRIDGE_IP,
        .type_in_storage = SETTING_ITEM_TYPE_NUM,
        .type_in_json = SETTING_ITEM_TYPE_STR,
        .save_to_storage = save_ip,
        .read_from_storage = read_ip,
        .read_from_storage_raw = read_raw_value,
    },
    {
        .key = KEY_BRIDGE_MB2,
        .default_value = &default_bridge_mb,
        .type_in_storage = SETTING_ITEM_TYPE_BOOL,
        .type_in_json = SETTING_ITEM_TYPE_BOOL,
        .save_to_storage = save_bool_value,
        .read_from_storage = read_bool_value,
        .read_from_storage_raw = read_raw_value,
    },
};
