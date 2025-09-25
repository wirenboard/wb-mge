#include "unity.h"
#include "console_log.h"

#include "config.h"
#include "wb_app_desc/wb_app_desc.h"

#include <string.h>
#include <stdint.h>

#define WB_APP_DESC_MAGIC_WORD                  0xDACBBCAB

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
    TEST_ASSERT_EQUAL(strlen(test_src), len);
    TEST_ASSERT_EQUAL_STRING(test_src, dest);

    // Test null-termination and buffer size
    const char test_src2[] = "1234567890";
    char dest2[6];
    len = wb_app_desc_get_str_field(test_src2, 5, dest2);
    TEST_ASSERT_EQUAL(5, len);
    dest2[5] = 0; // ensure null-termination
    TEST_ASSERT_EQUAL_STRING("12345", dest2);

    // Test empty string
    char dest3[4];
    len = wb_app_desc_get_str_field("", 3, dest3);
    TEST_ASSERT_EQUAL(0, len);
    TEST_ASSERT_EQUAL_STRING("", dest3);
}

void test_wb_app_desc_struct_fields(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test wb_app_desc struct fields");
    LOG_MESSAGE();

    // Check magic word
    TEST_ASSERT_EQUAL_HEX32(WB_APP_DESC_MAGIC_WORD, wb_app_desc.magic_word);

    // Check signature, model, version, git info are not empty and null-terminated
    TEST_ASSERT_EQUAL_STRING(wb_app_desc.signature, DEVICE_SIGNATURE);
    TEST_ASSERT_EQUAL_STRING(wb_app_desc.device_model, DEVICE_MODEL);
    TEST_ASSERT_EQUAL_STRING(wb_app_desc.fw_version, FIRMWARE_VERSION);
    TEST_ASSERT_EQUAL_STRING(wb_app_desc.fw_git_info, FIRMWARE_GIT_INFO);

    for (size_t i = 0; i < sizeof(wb_app_desc.reserved); ++i) {
        TEST_ASSERT_EQUAL_HEX8(0, wb_app_desc.reserved[i]);
    }
}

int main(void)
{
    UNITY_BEGIN();


    RUN_TEST(test_wb_app_desc_get_str_field);
    RUN_TEST(test_wb_app_desc_struct_fields);

    return UNITY_END();
}
