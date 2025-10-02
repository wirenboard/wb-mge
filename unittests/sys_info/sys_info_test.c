#include "unity.h"
#include "console_log.h"

#include "sys_info.h"
#include "esp_mac.h"
#include "esp_efuse.h"

#include <string.h>

// External mock control variables
extern bool mock_esp_read_mac_should_fail;

// Mock control functions
extern void mock_esp_efuse_set_signature(const char* signature);
extern void mock_esp_efuse_set_read_return(esp_err_t ret);

void setUp(void)
{
    // Reset mock state
    mock_esp_read_mac_should_fail = false;
    mock_esp_efuse_set_read_return(ESP_OK);
    mock_esp_efuse_set_signature("TEST_SIG");
    
    // Clear sys_info structure
    memset(&sys_info, 0, sizeof(sys_info));
}

void tearDown(void)
{
    // Clean up if needed
}

void test_sys_info_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - success case");
    LOG_MESSAGE();

    esp_err_t result = sys_info_init();
    
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Check serial number generation from MAC (existing mock uses MAC: 00:01:02:03:04:05)
    uint64_t expected_serial = 4328719365; // MAC 00:01:02:03:04:05 converted to uint64
    TEST_ASSERT_EQUAL_UINT64(expected_serial, sys_info.device_serial_num);
    
    // Check device signature
    TEST_ASSERT_EQUAL_STRING("TEST_SIG", sys_info.device_signature);
    
    // Check firmware info from wb_app_desc
    TEST_ASSERT_TRUE(strlen(sys_info.firmware_ver) > 0);
    TEST_ASSERT_TRUE(strlen(sys_info.firmware_git_info) > 0);
    TEST_ASSERT_TRUE(strlen(sys_info.device_name) > 0);
}

void test_sys_info_init_mac_read_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - MAC read failure");
    LOG_MESSAGE();

    mock_esp_read_mac_should_fail = true;
    
    esp_err_t result = sys_info_init();
    
    TEST_ASSERT_EQUAL(ESP_OK, result); // Init should still succeed
    TEST_ASSERT_EQUAL_UINT64(0, sys_info.device_serial_num); // Serial should be 0 on MAC failure
}

void test_sys_info_init_efuse_read_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - eFuse read failure");
    LOG_MESSAGE();

    mock_esp_efuse_set_read_return(ESP_FAIL);
    
    esp_err_t result = sys_info_init();
    
    TEST_ASSERT_EQUAL(ESP_OK, result); // Init should still succeed
    // Device signature should be empty or contain garbage, but function should handle it
}

void test_sys_info_init_empty_device_signature(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - empty device signature");
    LOG_MESSAGE();

    mock_esp_efuse_set_signature(""); // Empty signature
    
    esp_err_t result = sys_info_init();
    
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_EQUAL_STRING("", sys_info.device_signature);
}

void test_sys_info_init_mac_address_conversion(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - MAC address to serial conversion");
    LOG_MESSAGE();

    // Test that MAC address is properly converted to serial number
    // The existing mock provides MAC: 00:01:02:03:04:05
    esp_err_t result = sys_info_init();
    
    TEST_ASSERT_EQUAL(ESP_OK, result);
    
    // Verify the conversion logic: each byte shifted left by 8 bits
    // MAC 00:01:02:03:04:05 = 0x000102030405 = 4328719365 decimal
    uint64_t expected_serial = 4328719365;
    TEST_ASSERT_EQUAL_UINT64(expected_serial, sys_info.device_serial_num);
}

void test_sys_info_init_long_device_signature(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - long device signature");
    LOG_MESSAGE();

    // Test with maximum length signature (should be truncated properly)
    mock_esp_efuse_set_signature("MAXLEN_SIG12"); // Exactly DEVICE_SIGNATURE_LEN characters
    
    esp_err_t result = sys_info_init();
    
    TEST_ASSERT_EQUAL(ESP_OK, result);
    TEST_ASSERT_TRUE(strlen(sys_info.device_signature) <= DEVICE_SIGNATURE_LEN);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sys_info_init_success);
    RUN_TEST(test_sys_info_init_mac_read_failure);
    RUN_TEST(test_sys_info_init_efuse_read_failure);
    RUN_TEST(test_sys_info_init_empty_device_signature);
    RUN_TEST(test_sys_info_init_mac_address_conversion);
    RUN_TEST(test_sys_info_init_long_device_signature);

    return UNITY_END();
}
