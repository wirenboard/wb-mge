#include "unity.h"
#include "console_log.h"

#include "sys_info.h"
#include "esp_mac.h"
#include "esp_efuse.h"

#include <string.h>

#define SERIAL_FROM_MAC                                 4328719365UL // MAC 00:01:02:03:04:05 converted to uint64

extern bool mock_esp_read_mac_should_fail;
extern esp_err_t mock_esp_efuse_read_block_return;

extern void mock_esp_efuse_set_signature(const char* signature);

void setUp(void)
{
    mock_esp_read_mac_should_fail = false;
    mock_esp_efuse_read_block_return = ESP_OK;
    mock_esp_efuse_set_signature(TEST_DEVICE_SIGNATURE);

    memset(&sys_info, 0, sizeof(sys_info));
}

void tearDown(void)
{

}

// Тестируем успешную инициализацию sys_info
void test_sys_info_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - success case");
    LOG_MESSAGE();

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK");

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "Device serial number should match MAC address conversion"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE("TEST_SIG", sys_info.device_signature, "Device signature should match mock value");

    TEST_ASSERT_TRUE_MESSAGE(strlen(sys_info.firmware_ver) > 0, "Firmware version should not be empty");
    TEST_ASSERT_TRUE_MESSAGE(strlen(sys_info.firmware_git_info) > 0, "Firmware git info should not be empty");
    TEST_ASSERT_TRUE_MESSAGE(strlen(sys_info.device_name) > 0, "Device name should not be empty");
}

// Тестируем инициализацию sys_info при ошибке чтения MAC
void test_sys_info_init_mac_read_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - MAC read failure");
    LOG_MESSAGE();

    mock_esp_read_mac_should_fail = true;

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK even when MAC read fails");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, sys_info.device_serial_num, "Device serial number should be 0 when MAC read fails");
}

// Тестируем инициализацию sys_info при ошибке чтения eFuse
void test_sys_info_init_efuse_read_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - eFuse read failure");
    LOG_MESSAGE();

    mock_esp_efuse_read_block_return = ESP_FAIL;

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK even when eFuse read fails");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "", sys_info.device_signature, "Device signature should be empty when eFuse read fails"
    );
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "Serial number should still be populated"
    );
}

// Тестируем инициализацию sys_info с пустой подписью устройства
void test_sys_info_init_empty_device_signature(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - empty device signature");
    LOG_MESSAGE();

    const char* empty_signature = "";
    mock_esp_efuse_set_signature(empty_signature);

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK with empty signature");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(empty_signature, sys_info.device_signature, "Device signature should be empty string");
}

// Тестируем инициализацию sys_info с максимально длинной подписью устройства
void test_sys_info_init_long_device_signature(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - long device signature");
    LOG_MESSAGE();

    const char* test_signature = "MAXLEN_SIG12";
    mock_esp_efuse_set_signature(test_signature);

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK with long signature");
    TEST_ASSERT_EQUAL_MESSAGE(
        DEVICE_SIGNATURE_LEN, strlen(sys_info.device_signature), "Device signature should not exceed maximum length"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        test_signature, sys_info.device_signature, "Device signature should match exactly"
    );
}

// Тестируем инициализацию sys_info с обрезкой подписи устройства
void test_sys_info_init_signature_truncation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - truncation of device signature");
    LOG_MESSAGE();

    mock_esp_efuse_set_signature("TOOLONGSIGNATURE123456");

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK with long signature");
    TEST_ASSERT_EQUAL_MESSAGE(
        DEVICE_SIGNATURE_LEN, strlen(sys_info.device_signature), "Device signature should be truncated"
    );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "TOOLONGSIGNA", sys_info.device_signature, "Device signature should be truncated to first 12 chars"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_sys_info_init_success);
    RUN_TEST(test_sys_info_init_mac_read_failure);
    RUN_TEST(test_sys_info_init_efuse_read_failure);
    RUN_TEST(test_sys_info_init_empty_device_signature);
    RUN_TEST(test_sys_info_init_long_device_signature);
    RUN_TEST(test_sys_info_init_signature_truncation);

    return UNITY_END();
}
