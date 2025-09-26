#include "unity.h"
#include "console_log.h"

#include "esp_log.h"
#include "ram_storage.h"
#include "setting_items.h"

#include <string.h>

setting_storage_iface_t test_storage = {
    .has_key = rams_has_key,
    .write_str = rams_write_str,
    .read_str = rams_read_str,
};

extern bool mock_esp_read_mac_should_fail;

void setUp(void)
{

}

void tearDown(void)
{

}

// Тестируем get_dynamic_ap_pass_default и get_dynamic_hostname_default в случае, когда esp_read_mac возвращает ошибку
void test_dynamic_defaults_generation_edge_cases(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test dynamic defaults generation edge cases");
    LOG_MESSAGE();

    mock_esp_read_mac_should_fail = true;

    rams_init();
    setting_items_init_with_storage(&test_storage);

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        ESP_OK,
        setting_items_read(KEY_AP_PASS, buffer),
        "Should return ESP_OK for successful read"
    );

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "wirenboard",
        buffer,
        "Should return fallback password when MAC read fails"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_dynamic_defaults_generation_edge_cases);

    return UNITY_END();
}
