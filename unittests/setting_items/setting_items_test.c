#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "ram_storage.h"
#include "setting_items.h"

void setUp(void)
{

}

void tearDown(void)
{

}

void test_baudrate(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test baudrate");
    LOG_MESSAGE();

    const char* valid_test_baudrate[] = {"9600", "115200"};
    const char* invalid_test_baudrate[] = {"0", "299", "100", "1000000"};
    const char* keys[] = {KEY_BAUDRATE1, KEY_BAUDRATE2};

    for (size_t k = 0; k < ARRAY_SIZE(keys); k++) {
        for (size_t i = 0; i < ARRAY_SIZE(valid_test_baudrate); i++) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                ESP_OK,
                setting_items_save(keys[k], valid_test_baudrate[i]),
                "Failed to save valid baudrate"
            );

            char got_baudrate_str[SETTING_ITEM_MAX_STR_LEN] = {0};
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                ESP_OK,
                setting_items_read(keys[k], got_baudrate_str),
                "Failed to read baudrate"
            );

            TEST_ASSERT_EQUAL_STRING_MESSAGE(
                valid_test_baudrate[i],
                got_baudrate_str,
                "Baudrate mismatch"
            );
        }

        for (size_t i = 0; i < ARRAY_SIZE(invalid_test_baudrate); i++) {
            TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(
                ESP_OK,
                setting_items_save(keys[k], invalid_test_baudrate[i]),
                "Invalid baudrate was accepted"
            );
        }
    }
}

void test_hostname(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test hostname");
    LOG_MESSAGE();

    const char* valid_hostnames[] = {"WB-MGE", "device-123", "test"};
    const char* invalid_hostnames[] = {"", "device with spaces", "very-long-hostname-that-exceeds-maximum-length"};

    for (size_t i = 0; i < ARRAY_SIZE(valid_hostnames); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(KEY_HOSTNAME, valid_hostnames[i]),
            "Failed to save valid hostname"
        );

        char got_hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_read(KEY_HOSTNAME, got_hostname),
            "Failed to read hostname"
        );

        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            valid_hostnames[i],
            got_hostname,
            "Hostname mismatch"
        );
    }

    for (size_t i = 0; i < ARRAY_SIZE(invalid_hostnames); i++) {
        TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(KEY_HOSTNAME, invalid_hostnames[i]),
            "Invalid hostname was accepted"
        );
    }
}

void test_bool(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test boolean settings");
    LOG_MESSAGE();

    const char* keys[] = {KEY_IO_BUS_ENABLED, KEY_485_VOUT, KEY_ETH_DHCPC};

    for (size_t k = 0; k < ARRAY_SIZE(keys); k++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(keys[k], "true"),
            "Failed to save 'true'"
        );

        char value[SETTING_ITEM_MAX_STR_LEN] = {0};
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_read(keys[k], value),
            "Failed to read value"
        );

        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            "true",
            value,
            "Expected 'true'"
        );

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(keys[k], "false"),
            "Failed to save 'false'"
        );

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_read(keys[k], value),
            "Failed to read value"
        );

        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            "false",
            value,
            "Expected 'false'"
        );

        TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(keys[k], "invalid"),
            "Invalid boolean 'invalid' was accepted"
        );
    }
}

void test_wifi(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test wifi");
    LOG_MESSAGE();

    const char* valid_modes[] = {"ap", "sta", "apsta"};
    const char* invalid_modes[] = {"invalid", "", "AP", "Station"};

    for (size_t i = 0; i < ARRAY_SIZE(valid_modes); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(KEY_WIFI_MODE, valid_modes[i]),
            "Failed to save valid WiFi mode"
        );

        char value[SETTING_ITEM_MAX_STR_LEN] = {0};
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_read(KEY_WIFI_MODE, value),
            "Failed to read WiFi mode"
        );

        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            valid_modes[i],
            value,
            "WiFi mode mismatch"
        );
    }

    for (size_t i = 0; i < ARRAY_SIZE(invalid_modes); i++) {
        TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(KEY_WIFI_MODE, invalid_modes[i]),
            "Invalid WiFi mode was accepted"
        );
    }
}

void test_rs485(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test RS485");
    LOG_MESSAGE();

    const char* bool_keys[] = {KEY_485_TERM_1, KEY_485_FAIL_SAFE_1, KEY_485_TERM_2, KEY_485_FAIL_SAFE_2};

    for (size_t k = 0; k < ARRAY_SIZE(bool_keys); k++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(bool_keys[k], "true"),
            "Failed to save 'true'"
        );

        TEST_ASSERT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(bool_keys[k], "false"),
            "Failed to save 'false'"
        );

        // Test invalid boolean
        TEST_ASSERT_NOT_EQUAL_INT_MESSAGE(
            ESP_OK,
            setting_items_save(bool_keys[k], "invalid"),
            "Invalid boolean value accepted"
        );
    }
}

int main(void)
{
    UNITY_BEGIN();

    LOG_COLORED_MESSAGE(CONS_COLOR_ORANGE, "Initializing RAM, storage interface and setting items");
    LOG_MESSAGE();

    // Initialize RAM storage for testing
    rams_init();

    // Create storage interface for testing
    setting_storage_iface_t test_storage = {
        .has_key = rams_has_key,
        .write_str = rams_write_str,
        .read_str = rams_read_str,
    };

    // Initialize setting items with test storage
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_init_with_storage(&test_storage),
        "Failed to initialize setting items"
    );

    RUN_TEST(test_baudrate);
    RUN_TEST(test_hostname);
    RUN_TEST(test_bool);
    RUN_TEST(test_wifi);
    RUN_TEST(test_rs485);

    return UNITY_END();
}
