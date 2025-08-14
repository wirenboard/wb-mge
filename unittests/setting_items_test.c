#include "setting_items.h"  // Use new string-based implementation
#include "ram_storage.h"     // Mock storage for testing

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define TEST_OK 0
#define TEST_FAIL 1

#define TEST_PASSED() do { printf("✅ TEST PASSED: %s\n", __func__); } while(0)
#define TEST_FAILED(fmt, ...) do { printf("❌ TEST FAILED in %s: " fmt "\n", __func__, ##__VA_ARGS__); } while(0)

int baudrate_test(void)
{
    const char* valid_test_baudrate[] = {"9600", "115200"};
    const char* invalid_test_baudrate[] = {"0", "299", "100", "1000000"};
    const char* keys[] = {KEY_BAUDRATE1, KEY_BAUDRATE2};

    for (int k = 0; k < 2; k++) {
        printf("Testing key: %s\n", keys[k]);

        // Test valid values
        for (int i = 0; i < 3; i++) {
            if (setting_items_save(keys[k], valid_test_baudrate[i]) != ESP_OK) {
                TEST_FAILED("Failed to save valid baudrate: %s", valid_test_baudrate[i]);
                return TEST_FAIL;
            }

            char got_baudrate_str[SETTING_ITEM_MAX_STR_LEN] = {0};
            if (setting_items_read(keys[k], got_baudrate_str) != ESP_OK) {
                TEST_FAILED("Failed to read baudrate");
                return TEST_FAIL;
            }

            if (strcmp(got_baudrate_str, valid_test_baudrate[i]) != 0) {
                TEST_FAILED("Baudrate mismatch: expected %s, got %s",
                           valid_test_baudrate[i], got_baudrate_str);
                return TEST_FAIL;
            }
        }

        // Test invalid values
        for (int i = 0; i < 4; i++) {
            if (setting_items_save(keys[k], invalid_test_baudrate[i]) == ESP_OK) {
                TEST_FAILED("Invalid baudrate %s was accepted", invalid_test_baudrate[i]);
                return TEST_FAIL;
            }
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int hostname_test(void)
{
    const char* valid_hostnames[] = {"WB-MGE", "device-123", "test"};
    const char* invalid_hostnames[] = {"", "device with spaces", "very-long-hostname-that-exceeds-maximum-length"};

    for (int i = 0; i < 3; i++) {
        if (setting_items_save(KEY_HOSTNAME, valid_hostnames[i]) != ESP_OK) {
            TEST_FAILED("Failed to save valid hostname: %s", valid_hostnames[i]);
            return TEST_FAIL;
        }

        char got_hostname[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(KEY_HOSTNAME, got_hostname) != ESP_OK) {
            TEST_FAILED("Failed to read hostname");
            return TEST_FAIL;
        }

        if (strcmp(got_hostname, valid_hostnames[i]) != 0) {
            TEST_FAILED("Hostname mismatch: expected %s, got %s", valid_hostnames[i], got_hostname);
            return TEST_FAIL;
        }
    }

    // Test invalid hostnames
    for (int i = 0; i < 3; i++) {
        if (setting_items_save(KEY_HOSTNAME, invalid_hostnames[i]) == ESP_OK) {
            TEST_FAILED("Invalid hostname %s was accepted", invalid_hostnames[i]);
            return TEST_FAIL;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int bool_test(void)
{
    const char* keys[] = {KEY_IO_BUS_ENABLED, KEY_485_VOUT, KEY_ETH_DHCPC};

    for (int k = 0; k < 3; k++) {
        // Test valid boolean values
        if (setting_items_save(keys[k], "true") != ESP_OK) {
            TEST_FAILED("Failed to save 'true' for %s", keys[k]);
            return TEST_FAIL;
        }

        char value[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(keys[k], value) != ESP_OK) {
            TEST_FAILED("Failed to read %s", keys[k]);
            return TEST_FAIL;
        }

        if (strcmp(value, "true") != 0) {
            TEST_FAILED("Expected 'true', got '%s' for %s", value, keys[k]);
            return TEST_FAIL;
        }

        // Test false
        if (setting_items_save(keys[k], "false") != ESP_OK) {
            TEST_FAILED("Failed to save 'false' for %s", keys[k]);
            return TEST_FAIL;
        }

        if (setting_items_read(keys[k], value) != ESP_OK) {
            TEST_FAILED("Failed to read %s", keys[k]);
            return TEST_FAIL;
        }

        if (strcmp(value, "false") != 0) {
            TEST_FAILED("Expected 'false', got '%s' for %s", value, keys[k]);
            return TEST_FAIL;
        }

        // Test invalid boolean
        if (setting_items_save(keys[k], "invalid") == ESP_OK) {
            TEST_FAILED("Invalid boolean 'invalid' was accepted for %s", keys[k]);
            return TEST_FAIL;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int wifi_test(void)
{
    // Test WiFi mode
    const char* valid_modes[] = {"ap", "sta", "apsta"};
    const char* invalid_modes[] = {"invalid", "", "AP", "Station"};

    for (int i = 0; i < 3; i++) {
        if (setting_items_save(KEY_WIFI_MODE, valid_modes[i]) != ESP_OK) {
            TEST_FAILED("Failed to save valid WiFi mode: %s", valid_modes[i]);
            return TEST_FAIL;
        }

        char value[SETTING_ITEM_MAX_STR_LEN] = {0};
        if (setting_items_read(KEY_WIFI_MODE, value) != ESP_OK) {
            TEST_FAILED("Failed to read WiFi mode");
            return TEST_FAIL;
        }

        if (strcmp(value, valid_modes[i]) != 0) {
            TEST_FAILED("WiFi mode mismatch: expected %s, got %s", valid_modes[i], value);
            return TEST_FAIL;
        }
    }

    // Test invalid modes
    for (int i = 0; i < 4; i++) {
        if (setting_items_save(KEY_WIFI_MODE, invalid_modes[i]) == ESP_OK) {
            TEST_FAILED("Invalid WiFi mode %s was accepted", invalid_modes[i]);
            return TEST_FAIL;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int rs485_test(void)
{
    // Test termination and fail-safe booleans
    const char* bool_keys[] = {KEY_485_TERM_1, KEY_485_FAIL_SAFE_1, KEY_485_TERM_2, KEY_485_FAIL_SAFE_2};

    for (int k = 0; k < 4; k++) {
        // Test true/false
        if (setting_items_save(bool_keys[k], "true") != ESP_OK) {
            TEST_FAILED("Failed to save 'true' for %s", bool_keys[k]);
            return TEST_FAIL;
        }

        if (setting_items_save(bool_keys[k], "false") != ESP_OK) {
            TEST_FAILED("Failed to save 'false' for %s", bool_keys[k]);
            return TEST_FAIL;
        }

        // Test invalid boolean
        if (setting_items_save(bool_keys[k], "invalid") == ESP_OK) {
            TEST_FAILED("Invalid boolean value accepted for %s", bool_keys[k]);
            return TEST_FAIL;
        }
    }

    TEST_PASSED();
    return TEST_OK;
}

int main(void)
{
    printf("=== Running String-Based Setting Items Tests ===\n");

    // Initialize RAM storage for testing
    rams_init();

    // Create storage interface for testing
    setting_storage_iface_t test_storage = {
        .has_key = rams_has_key,
        .write_str = rams_write_str,
        .read_str = rams_read_str,
    };

    // Initialize setting items with test storage
    if (setting_items_init_with_storage(&test_storage) != ESP_OK) {
        printf("❌ Failed to initialize setting items\n");
        return 1;
    }

    // Run tests
    if (baudrate_test() != TEST_OK) {
        printf("❌ baudrate_test FAILED\n");
        return 1;
    }

    if (hostname_test() != TEST_OK) {
        printf("❌ hostname_test FAILED\n");
        return 1;
    }

    if (bool_test() != TEST_OK) {
        printf("❌ bool_test FAILED\n");
        return 1;
    }

    if (wifi_test() != TEST_OK) {
        printf("❌ wifi_test FAILED\n");
        return 1;
    }

    if (rs485_test() != TEST_OK) {
        printf("❌ rs485_test FAILED\n");
        return 1;
    }

    printf("✅ ALL TESTS PASSED!\n");
    printf("=== String-Based Settings Migration Complete ===\n");
    return 0;
}