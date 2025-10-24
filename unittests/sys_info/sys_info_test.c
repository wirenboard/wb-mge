#include "unity.h"
#include "console_log.h"

#include "config.h"
#include "sys_info.h"
#include "esp_mac.h"
#include "esp_efuse.h"

#include <string.h>

#define SERIAL_FROM_MAC                         4328719365UL // MAC 00:01:02:03:04:05 converted to uint64
#define TEST_DEVICE_SIGNATURE                   "TEST_SIG"
#define TEST_SECURITY_CODE                      {0xAC, 0x57, 0x21, 0xF0, 0xAA, 0x8B, 0x37, 0x61, 0x93, 0xC5, 0x72, 0xEF}
#define TEST_SECURITY_CODE_LEN                  12

extern bool mock_esp_read_mac_should_fail;
extern esp_efuse_block_t mock_read_block;
extern size_t mock_read_offset;
extern esp_err_t mock_esp_efuse_read_block_return;

extern void mock_esp_efuse_set_signature(const char* signature);

void setUp(void)
{
    mock_esp_read_mac_should_fail = false;
    mock_esp_efuse_read_block_return = ESP_OK;
    mock_esp_efuse_set_signature(TEST_DEVICE_SIGNATURE);
    mock_esp_efuse_set_security_code((uint8_t[])TEST_SECURITY_CODE);

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

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TEST_DEVICE_SIGNATURE, sys_info.device_signature, "Device signature should match mock value");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        (uint8_t[])TEST_SECURITY_CODE, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should match mock value"
    );
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

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TEST_DEVICE_SIGNATURE, sys_info.device_signature, "Device signature should match mock value");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        (uint8_t[])TEST_SECURITY_CODE, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should match mock value"
    );
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

    const uint8_t zero_buf[TEST_SECURITY_CODE_LEN] = {0};
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        zero_buf, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should be zero when eFuse read fails"
    );

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "Serial number should still be populated"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
}

// Тестируем инициализацию sys_info с пустой сигнатурой устройства
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

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        (uint8_t[])TEST_SECURITY_CODE, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should match mock value"
    );
}

// Тестируем инициализацию sys_info с максимально длинной сигнатурой устройства
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

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        (uint8_t[])TEST_SECURITY_CODE, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should match mock value"
    );
}

// Тестируем инициализацию sys_info с обрезкой сигнатуры устройства
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
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        (uint8_t[])TEST_SECURITY_CODE, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should match mock value"
    );
}

// Тестируем инициализацию sys_info с пустым (нулевым) кодом безопасности
void test_sys_info_init_zero_security_code(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - zero security code");
    LOG_MESSAGE();

    const uint8_t zero_sec_code[TEST_SECURITY_CODE_LEN] = {0};
    mock_esp_efuse_set_security_code(zero_sec_code);

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK with zero security code");

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        zero_sec_code, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should be zero"
    );

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "Device serial number should match MAC address conversion"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TEST_DEVICE_SIGNATURE, sys_info.device_signature, "Device signature should match mock value");
}

void test_sys_info_init_security_code_with_zeros(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info_init - security code with zeros");
    LOG_MESSAGE();

    const uint8_t sec_code[TEST_SECURITY_CODE_LEN] = {0x00, 0x57, 0x21, 0x00, 0xAA, 0x8B, 0x37, 0x00, 0x93, 0xC5, 0x72, 0x00};
    mock_esp_efuse_set_security_code(sec_code);

    esp_err_t result = sys_info_init();

    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "sys_info_init should return ESP_OK with zero security code");

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        sec_code, sys_info.security_code, TEST_SECURITY_CODE_LEN, "Device security code should match the value specified in the test"
    );

    TEST_ASSERT_EQUAL_UINT64_MESSAGE(
        SERIAL_FROM_MAC, sys_info.device_serial_num, "Device serial number should match MAC address conversion"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_VERSION, sys_info.firmware_ver, "Firmware version should be FIRMWARE_VERSION");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(FIRMWARE_GIT_INFO, sys_info.firmware_git_info, "Firmware git info should be FIRMWARE_GIT_INFO");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(DEVICE_MODEL, sys_info.device_name, "Device name should be DEVICE_MODEL");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TEST_DEVICE_SIGNATURE, sys_info.device_signature, "Device signature should match mock value");
}

void test_sys_info_constants(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sys_info - constants definitions");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL_MESSAGE(64, SYS_INFO_MAX_STR_LEN, "SYS_INFO_MAX_STR_LEN must be 64");
    TEST_ASSERT_EQUAL_MESSAGE(12, DEVICE_SIGNATURE_LEN, "DEVICE_SIGNATURE_LEN must be 12");
    TEST_ASSERT_EQUAL_MESSAGE(12, SECURITY_CODE_LEN, "SECURITY_CODE_LEN must be 12");
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
    RUN_TEST(test_sys_info_init_zero_security_code);
    RUN_TEST(test_sys_info_init_security_code_with_zeros);
    RUN_TEST(test_sys_info_constants);

    return UNITY_END();
}
