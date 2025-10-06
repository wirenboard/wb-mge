#include "unity.h"
#include "console_log.h"

#include "config.h"
#include "wb_app_desc/wb_app_desc.h"

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#define MAGIC_WORD                      0xDACBBCAB

void setUp(void)
{

}

void tearDown(void)
{

}

void test_wb_app_desc_get_str_field(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test wb_app_desc_get_str_field function");
    LOG_MESSAGE();

    // Test normal string extraction
    const char test_src[] = "TestString";
    char dest[32];
    size_t len = wb_app_desc_get_str_field(test_src, strlen(test_src), dest);
    TEST_ASSERT_EQUAL_MESSAGE(strlen(test_src), len, "Length should match source string length");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_src, dest, "Destination string should match source string");

    // Test null-termination and buffer size
    const char test_src2[] = "1234567890";
    const size_t field_len = 5;
    char dest2[] = "Destination";
    len = wb_app_desc_get_str_field(test_src2, field_len, dest2);
    TEST_ASSERT_EQUAL_MESSAGE(field_len, len, "Length should match source string length");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("12345", dest2, "Destination string should match first 5 chars of source");

    // Test empty string
    const char test_src3[] = "";
    const size_t field_len2 = 3;
    char dest3[field_len2 + 1];
    len = wb_app_desc_get_str_field(test_src3, field_len2, dest3);
    TEST_ASSERT_EQUAL_MESSAGE(0, len, "Length should be 0 for empty source string");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_src3, dest3, "Destination string should match source string");
}

void test_wb_app_desc_struct_fields(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test wb_app_desc struct fields");
    LOG_MESSAGE();

    // Expected field sizes
    const size_t MAGIC_WORD_SIZE = 4;
    const size_t SIGNATURE_SIZE = 12;
    const size_t DEVICE_MODEL_SIZE = 20;
    const size_t FW_VERSION_SIZE = 16;
    const size_t FW_GIT_INFO_SIZE = 50;
    const size_t RESERVED_SIZE = 90;
    const size_t TOTAL_SIZE = 192;

    // Expected field offsets
    const size_t MAGIC_WORD_OFFSET = 0;
    const size_t SIGNATURE_OFFSET = 4;
    const size_t DEVICE_MODEL_OFFSET = 16;
    const size_t FW_VERSION_OFFSET = 36;
    const size_t FW_GIT_INFO_OFFSET = 52;
    const size_t RESERVED_OFFSET = 102;

    // Check field sizes
    TEST_ASSERT_EQUAL_MESSAGE(MAGIC_WORD_SIZE, sizeof(wb_app_desc.magic_word), "magic_word size should be 4 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(SIGNATURE_SIZE, sizeof(wb_app_desc.signature), "signature size should be 12 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_MODEL_SIZE, sizeof(wb_app_desc.device_model), "device_model size should be 20 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(FW_VERSION_SIZE, sizeof(wb_app_desc.fw_version), "fw_version size should be 16 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(FW_GIT_INFO_SIZE, sizeof(wb_app_desc.fw_git_info), "fw_git_info size should be 50 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(RESERVED_SIZE, sizeof(wb_app_desc.reserved), "reserved size should be 90 bytes");
    TEST_ASSERT_EQUAL_MESSAGE(TOTAL_SIZE, sizeof(wb_app_desc), "Total wb_app_desc size should be 192 bytes");

    // Check field offsets
    TEST_ASSERT_EQUAL_MESSAGE(MAGIC_WORD_OFFSET, offsetof(wb_app_desc_t, magic_word), "magic_word offset should be 0");
    TEST_ASSERT_EQUAL_MESSAGE(SIGNATURE_OFFSET, offsetof(wb_app_desc_t, signature), "signature offset should be 4");
    TEST_ASSERT_EQUAL_MESSAGE(DEVICE_MODEL_OFFSET, offsetof(wb_app_desc_t, device_model), "device_model offset should be 16");
    TEST_ASSERT_EQUAL_MESSAGE(FW_VERSION_OFFSET, offsetof(wb_app_desc_t, fw_version), "fw_version offset should be 36");
    TEST_ASSERT_EQUAL_MESSAGE(FW_GIT_INFO_OFFSET, offsetof(wb_app_desc_t, fw_git_info), "fw_git_info offset should be 52");
    TEST_ASSERT_EQUAL_MESSAGE(RESERVED_OFFSET, offsetof(wb_app_desc_t, reserved), "reserved offset should be 102");

    // Check magic word
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(MAGIC_WORD, wb_app_desc.magic_word, "Magic word should match");

    // Check signature, model, version, git info are not empty and null-terminated
    TEST_ASSERT_EQUAL_STRING_MESSAGE(wb_app_desc.signature, DEVICE_SIGNATURE, "Signature should match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(wb_app_desc.device_model, DEVICE_MODEL, "Device model should match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(wb_app_desc.fw_version, FIRMWARE_VERSION, "Firmware version should match");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(wb_app_desc.fw_git_info, FIRMWARE_GIT_INFO, "Firmware git info should match");

    for (size_t i = 0; i < WB_APP_DESC_RESERVED_LEN; ++i) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, wb_app_desc.reserved[i], "Reserved field should be 0");
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_wb_app_desc_get_str_field);
    RUN_TEST(test_wb_app_desc_struct_fields);

    return UNITY_END();
}
