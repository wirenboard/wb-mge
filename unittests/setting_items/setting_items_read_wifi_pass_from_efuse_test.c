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

void test_read_wifi_pass_from_efuse_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test read_wifi_pass_from_efuse - success case");
    LOG_MESSAGE();

    mock_esp_efuse_set_wifi_password("testpass123");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_read_wifi_pass_from_efuse_success);

    return UNITY_END();
}

