#include "unity.h"
#include "console_log.h"

#include "esp_mac.h"
#include "ram_storage.h"
#include "setting_items.h"

#include <string.h>

setting_storage_iface_t test_storage = {
    .has_key = rams_has_key,
    .write_str = rams_write_str,
    .read_str = rams_read_str,
};

void setUp(void)
{

}

void tearDown(void)
{

}

// Тестируем генерацию пароля на основе MAC-адреса, когда MAC-адрес короткий
void test_generate_mac_based_password_short_mac(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test generate_mac_based_password - short MAC address");
    LOG_MESSAGE();

    mock_mac_address[MAC_ADDRESS_SIZE - 1] = 1;

    rams_init();
    setting_items_init_with_storage(&test_storage);

    char buffer[SETTING_ITEM_MAX_STR_LEN];
    memset(buffer, 0, sizeof(buffer));

    esp_err_t ret = setting_items_read(KEY_AP_PASS, buffer);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Reading generated AP password should succeed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0010000001", buffer, "Generated password should match expected value");
    TEST_ASSERT_EQUAL_MESSAGE(10, strlen(buffer), "Generated password length should be 10");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_generate_mac_based_password_short_mac);

    return UNITY_END();
}
