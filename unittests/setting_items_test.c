#include "setting_items.h"

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "ram_storage.h"

int baudrate_test(void)
{
    const uint32_t valid_test_baudrate[] = {9600, 115200, 460800};
    const uint32_t invalid_test_baudrate[] = {0, 299, 100, 1000000};

    for (int i = 0; i < (sizeof(valid_test_baudrate) / sizeof(valid_test_baudrate[0])); i++) {
        uint32_t expected_baudrate = valid_test_baudrate[i];
        if (setting_items_save("baudrate", &expected_baudrate) != 0) {
            TEST_FAILED("Failed to save baudrate %u", expected_baudrate);
            return TEST_ERR;
        }
        uint32_t got_baudrate = 0;
        if (setting_items_read("baudrate", &got_baudrate)) {
            TEST_FAILED("Failed to read baudrate");
            return TEST_ERR;
        }
        if (expected_baudrate != got_baudrate) {
            TEST_FAILED("Expected %u, got %u", expected_baudrate, got_baudrate);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < sizeof(invalid_test_baudrate) / sizeof(invalid_test_baudrate[0]); i++) {
        uint32_t invalid_baudrate = invalid_test_baudrate[i];
        if (setting_items_save("baudrate", &invalid_baudrate) == 0) {
            TEST_FAILED("Saved invalid baudrate %u", invalid_baudrate);
            return TEST_ERR;
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

    for (int i = 0; (i < (sizeof(valid_test_parity) / sizeof(valid_test_parity[0]))); i++) {
        const char* expected_str = valid_test_parity[i];
        if (setting_items_save("parity", expected_str) != 0) {
            TEST_FAILED("Failed to save parity \"%s\"", expected_str);
            return TEST_ERR;
        }
        char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read("parity", got_str)) {
            TEST_FAILED("Failed to read parity");
            return TEST_ERR;
        }
        if (strcmp(expected_str, got_str) != 0) {
            TEST_FAILED("Expected %s, got %s", expected_str, got_str);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < (sizeof(invalid_test_parity) / sizeof(invalid_test_parity[0])); i++) {
        const char* invalid_str = invalid_test_parity[i];
        if (setting_items_save("parity", invalid_str) == 0) {
            TEST_FAILED("Saved invalid parity \"%s\"", invalid_str);
            return TEST_ERR;
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

    for (int i = 0; i < (sizeof(valid_test_databits) / sizeof(valid_test_databits[0])); i++) {
        const char* expected_str = valid_test_databits[i];
        if (setting_items_save("databits", expected_str) != 0) {
            TEST_FAILED("Failed to save databits \"%s\"", expected_str);
            return TEST_ERR;
        }
        char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read("databits", got_str)) {
            TEST_FAILED("Failed to read databits");
            return TEST_ERR;
        }
        if (strcmp(expected_str, got_str) != 0) {
            TEST_FAILED("Expected %s, got %s", expected_str, got_str);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < (sizeof(invalid_test_databits) / sizeof(invalid_test_databits[0])); i++) {
        const char* invalid_str = invalid_test_databits[i];
        if (setting_items_save("databits", invalid_str) == 0) {
            TEST_FAILED("Saved invalid databits \"%s\"", invalid_str);
            return TEST_ERR;
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

    for (int i = 0; i < (sizeof(valid_test_stopbits) / sizeof(valid_test_stopbits[0])); i++) {
        const char* expected_str = valid_test_stopbits[i];
        if (setting_items_save("stopbits", expected_str) != 0) {
            TEST_FAILED("Failed to save stopbits \"%s\"", expected_str);
            return TEST_ERR;
        }
        char got_str[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read("stopbits", got_str)) {
            TEST_FAILED("Failed to read stopbits");
            return TEST_ERR;
        }
        if (strcmp(expected_str, got_str) != 0) {
            TEST_FAILED("Expected %s, got %s", expected_str, got_str);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < (sizeof(invalid_test_stopbits) / sizeof(invalid_test_stopbits[0])); i++) {
        const char* invalid_str = invalid_test_stopbits[i];
        if (setting_items_save("stopbits", invalid_str) == 0) {
            TEST_FAILED("Saved invalid stopbits \"%s\"", invalid_str);
            return TEST_ERR;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int ip_test(void)
{
    const char* valid_test_ip[] = {"192.168.111.12", "127.0.0.1"};
    const char* invalid_test_ip[] = {"192.168..111.255", "192.168.111.256", "192.168.111.25.1",
                                     "192..1", "192.168.1"};

    for (int i = 0; i < (sizeof(valid_test_ip) / sizeof(valid_test_ip[0])); i++) {
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
        if (strcmp(expected_str, got_str) != 0) {
            TEST_FAILED("Expected %s, got %s", expected_str, got_str);
            return TEST_ERR;
        }
    }

    for (int i = 0; i < (sizeof(invalid_test_ip) / sizeof(invalid_test_ip[0])); i++) {
        const char* invalid_str = invalid_test_ip[i];
        if (setting_items_save("eth_ip", invalid_str) == 0) {
            TEST_FAILED("Saved invalid eth_ip \"%s\"", invalid_str);
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
        .save_num = rams_write_u32,
        .save_str = rams_write_str,
        .save_bool = rams_write_u8,
        .read_num = rams_read_u32,
        .read_str = rams_read_str,
        .read_bool = rams_read_u8,
    };

    if (setting_items_init("WB-MGE", &setting_item_iface) != 0) {
        return -1;
    }

    int res = setting_items_set_defaults();
    if (res != 0) {
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

    printf(PRINT_I("\nAll tests passed\n\n"));

    return 0;
}
