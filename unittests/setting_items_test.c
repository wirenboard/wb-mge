#include "setting_items.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "array_size.h"
#include "common.h"
#include "ram_storage.h"

int baudrate_test(void)
{
    const uint32_t valid_test_baudrate[] = {9600, 115200, 460800};
    const uint32_t invalid_test_baudrate[] = {0, 299, 100, 1000000};
    const char* keys[] = {"baudrate", "baudrate_2"};

    for (int k = 0; k < ARRAY_SIZE(keys); k++) {
        const char* key = keys[k];
        for (int i = 0; i < ARRAY_SIZE(valid_test_baudrate); i++) {
            uint32_t expected_baudrate = valid_test_baudrate[i];
            if (setting_items_save(key, &expected_baudrate) != 0) {
                TEST_FAILED("Failed to save %s %u", key, expected_baudrate);
                return TEST_ERR;
            }
            uint32_t got_baudrate = 0;
            if (setting_items_read(key, &got_baudrate)) {
                TEST_FAILED("Failed to read %s", key);
                return TEST_ERR;
            }
            if (expected_baudrate != got_baudrate) {
                TEST_FAILED("Expected %u, got %u for %s", expected_baudrate, got_baudrate, key);
                return TEST_ERR;
            }
        }

        for (int i = 0; i < ARRAY_SIZE(invalid_test_baudrate); i++) {
            uint32_t invalid_baudrate = invalid_test_baudrate[i];
            if (setting_items_save(key, &invalid_baudrate) == 0) {
                TEST_FAILED("Saved invalid %s %u", key, invalid_baudrate);
                return TEST_ERR;
            }
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int parity_test(void)
{
    const char* valid_test_parity[] = {UART_PARITY_DISABLE_STR, UART_PARITY_EVEN_STR,
                                       UART_PARITY_ODD_STR};
    const char* invalid_test_parity[] = {"n", "e", "o", "nonee", "evenn", "oddd"};
    const char* keys[] = {"parity", "parity_2"};

    for (int k = 0; k < ARRAY_SIZE(keys); k++) {
        const char* key = keys[k];
        for (int i = 0; i < ARRAY_SIZE(valid_test_parity); i++) {
            const char* expected_str = valid_test_parity[i];
            if (setting_items_save(key, expected_str) != 0) {
                TEST_FAILED("Failed to save %s \"%s\"", key, expected_str);
                return TEST_ERR;
            }
            char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(key, got_str)) {
                TEST_FAILED("Failed to read %s", key);
                return TEST_ERR;
            }
            if (strncmp(expected_str, got_str, SETTING_ITEM_MAX_STR_LEN) != 0) {
                TEST_FAILED("Expected %s, got %s for %s", expected_str, got_str, key);
                return TEST_ERR;
            }
        }

        for (int i = 0; i < ARRAY_SIZE(invalid_test_parity); i++) {
            const char* invalid_str = invalid_test_parity[i];
            if (setting_items_save(key, invalid_str) == 0) {
                TEST_FAILED("Saved invalid %s \"%s\"", key, invalid_str);
                return TEST_ERR;
            }
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int databits_test(void)
{
    const char* valid_test_databits[] = {UART_DATA_5_BITS_STR, UART_DATA_6_BITS_STR,
                                         UART_DATA_7_BITS_STR, UART_DATA_8_BITS_STR};
    const char* invalid_test_databits[] = {"4", "9", "5b", "6b", "7b", "8b"};
    const char* keys[] = {"databits", "databits_2"};

    for (int k = 0; k < ARRAY_SIZE(keys); k++) {
        const char* key = keys[k];
        for (int i = 0; i < ARRAY_SIZE(valid_test_databits); i++) {
            const char* expected_str = valid_test_databits[i];
            if (setting_items_save(key, expected_str) != 0) {
                TEST_FAILED("Failed to save %s \"%s\"", key, expected_str);
                return TEST_ERR;
            }
            char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(key, got_str)) {
                TEST_FAILED("Failed to read %s", key);
                return TEST_ERR;
            }
            if (strncmp(expected_str, got_str, SETTING_ITEM_MAX_STR_LEN) != 0) {
                TEST_FAILED("Expected %s, got %s for %s", expected_str, got_str, key);
                return TEST_ERR;
            }
        }

        for (int i = 0; i < ARRAY_SIZE(invalid_test_databits); i++) {
            const char* invalid_str = invalid_test_databits[i];
            if (setting_items_save(key, invalid_str) == 0) {
                TEST_FAILED("Saved invalid %s \"%s\"", key, invalid_str);
                return TEST_ERR;
            }
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int stopbits_test(void)
{
    const char* valid_test_stopbits[] = {UART_STOP_BITS_1_STR, UART_STOP_BITS_1_5_STR,
                                         UART_STOP_BITS_2_STR};
    const char* invalid_test_stopbits[] = {"1", "1.5", "2", "1-b", "1.5-b", "2-b"};
    const char* keys[] = {"stopbits", "stopbits_2"};

    for (int k = 0; k < ARRAY_SIZE(keys); k++) {
        const char* key = keys[k];
        for (int i = 0; i < ARRAY_SIZE(valid_test_stopbits); i++) {
            const char* expected_str = valid_test_stopbits[i];
            if (setting_items_save(key, expected_str) != 0) {
                TEST_FAILED("Failed to save %s \"%s\"", key, expected_str);
                return TEST_ERR;
            }
            char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(key, got_str)) {
                TEST_FAILED("Failed to read %s", key);
                return TEST_ERR;
            }
            if (strncmp(expected_str, got_str, SETTING_ITEM_MAX_STR_LEN) != 0) {
                TEST_FAILED("Expected %s, got %s for %s", expected_str, got_str, key);
                return TEST_ERR;
            }
        }

        for (int i = 0; i < ARRAY_SIZE(invalid_test_stopbits); i++) {
            const char* invalid_str = invalid_test_stopbits[i];
            if (setting_items_save(key, invalid_str) == 0) {
                TEST_FAILED("Saved invalid %s \"%s\"", key, invalid_str);
                return TEST_ERR;
            }
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int ip_test(void)
{
    const char* valid_test_ip[] = {"192.168.111.12", "127.0.0.1"};
    const char* invalid_test_ip[] = {"192.168..111.255", "192.168.111.256", "192.168.111.25.1", "192.g.1",
        "99999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999999"};

    for (int i = 0; i < ARRAY_SIZE(valid_test_ip); i++) {
        const char* expected_str = valid_test_ip[i];
        if (setting_items_save("eth_ip_static", expected_str) != 0) {
            TEST_FAILED("Failed to save eth_ip \"%s\"", expected_str);
            return TEST_ERR;
        }
        char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read("eth_ip_static", got_str)) {
            TEST_FAILED("Failed to read eth_ip");
            return TEST_ERR;
        }
        if (strncmp(expected_str, got_str, SETTING_ITEM_MAX_STR_LEN) != 0) {
            TEST_FAILED("Expected %s, got %s", expected_str, got_str);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < ARRAY_SIZE(invalid_test_ip); i++) {
        const char* invalid_str = invalid_test_ip[i];
        if (setting_items_save("eth_ip", invalid_str) == 0) {
            TEST_FAILED("Saved invalid eth_ip \"%s\"", invalid_str);
            return TEST_ERR;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int bridge_mode_test(void)
{
    const char* valid_test_bridge_mode[] = {BRIDGE_MODE_SERVER_STR, BRIDGE_MODE_CLIENT_STR};
    const char* invalid_test_bridge_mode[] = {"server", "client", "serverr", "clientt"};
    const char* keys[] = {"bridge_mode", "bridge_mode_2"};

    for (int k = 0; k < ARRAY_SIZE(keys); k++) {
        const char* key = keys[k];
        for (int i = 0; i < ARRAY_SIZE(valid_test_bridge_mode); i++) {
            const char* expected_bridge_mode = valid_test_bridge_mode[i];
            if (setting_items_save(key, expected_bridge_mode) != 0) {
                TEST_FAILED("Failed to save %s %s", key, expected_bridge_mode);
                return TEST_ERR;
            }
            char got_bridge_mode[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(key, got_bridge_mode)) {
                TEST_FAILED("Failed to read %s", key);
                return TEST_ERR;
            }
            if (strncmp(expected_bridge_mode, got_bridge_mode, SETTING_ITEM_MAX_STR_LEN) != 0) {
                TEST_FAILED("Expected %s, got %s for %s", expected_bridge_mode, got_bridge_mode, key);
                return TEST_ERR;
            }
        }

        for (int i = 0; i < ARRAY_SIZE(invalid_test_bridge_mode); i++) {
            const char* invalid_bridge_mode = invalid_test_bridge_mode[i];
            if (setting_items_save(key, invalid_bridge_mode) == 0) {
                TEST_FAILED("Saved invalid %s \"%s\"", key, invalid_bridge_mode);
                return TEST_ERR;
            }
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int string_test(void) {
    const char* valid_test_string[] = {"string-----on------the------edge", "short-str"};
    const char* invalid_test_string[] = {"", "long-string long-string long-string long-string long-string"};

    // Для тестов строк выбран ключ KEY_HOSTNAME, т.к. при его сохранении строка проверяется на нулевую длину
    for (int i = 0; i < ARRAY_SIZE(valid_test_string); i++) {
        const char* expected_str = valid_test_string[i];
        if (setting_items_save(KEY_HOSTNAME, expected_str) != 0) {
            TEST_FAILED("Failed to save test_string \"%s\"", expected_str);
            return TEST_ERR;
        }
        char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(KEY_HOSTNAME, got_str)) {
            TEST_FAILED("Failed to read test_string");
            return TEST_ERR;
        }
        if (strncmp(expected_str, got_str, SETTING_ITEM_MAX_STR_LEN) != 0) {
            TEST_FAILED("Expected %s, got %s", expected_str, got_str);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < ARRAY_SIZE(invalid_test_string); i++) {
        const char* invalid_str = invalid_test_string[i];
        if (setting_items_save(KEY_HOSTNAME, invalid_str) == 0) {
            TEST_FAILED("Saved invalid test_string \"%s\"", invalid_str);
            return TEST_ERR;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int main(void)
{
    rams_init();
    setting_item_iface_t setting_item_iface = {
        .has_key = rams_has_key,
        .save_num = rams_write_u32,
        .save_str = rams_write_str,
        .save_bool = rams_write_u8,
        .read_num = rams_read_u32,
        .read_str = rams_read_str,
        .read_bool = rams_read_u8,
    };

    if (setting_items_init("WB-MGE", &setting_item_iface) != 0) {
        printf(PRINT_E("Failed to init setting items\n"));
        return -1;
    }

    if (setting_items_set_defaults() != 0) {
        printf(PRINT_E("Failed to set defaults\n"));
        return -1;
    }

    if (baudrate_test() != 0) {
        return -1;
    }
    if (parity_test() != 0) {
        return -1;
    }
    if (databits_test() != 0) {
        return -1;
    }
    if (stopbits_test() != 0) {
        return -1;
    }
    if (ip_test() != 0) {
        return -1;
    }
    if (bridge_mode_test() != 0) {
        return -1;
    }
    if (string_test() != 0) {
        return -1;
    }

    printf(PRINT_I("\nAll tests passed\n\n"));

    return 0;
}
