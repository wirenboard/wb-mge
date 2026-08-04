// Unit tests for the WiFi settings guard in main/network/network.c.
//
// The case that brought these tests here: a device boots with wifi_perm_disable set, so
// network_init() never initializes the radio, and the flag is then cleared at runtime
// without a reboot (POST /cmd {"cmd":"set_default_settings"} does exactly that). From that
// moment NVS says WiFi is enabled while the hardware is down, and every settings write used
// to report changed WiFi settings, spawn the settings update task, and end in
// wifi_set_apsta_config() returning ESP_ERR_NOT_ALLOWED — logged as "Failed to update WiFi
// settings". Nothing was actually broken: the settings were stored and come up at the next
// boot.

#include "unity.h"
#include "console_log.h"

#include "esp_err.h"
#include "network.h"
#include "setting_items.h"
#include "setting_items_mock.h"
#include "sys_info.h"
#include "sys_info_mock.h"
#include "wifi_apsta_mock.h"


// A complete, valid WiFi + Ethernet configuration. network.c treats a missing key as a read
// failure and abandons the whole comparison, so every key it touches has to be present
// before the state under test can be reached at all.
static void seed_settings(void)
{
    mock_setting_items_set(KEY_HOSTNAME, "wb-mge-test");

    mock_setting_items_set_bool(KEY_ETH_DHCPC, true);
    mock_setting_items_set(KEY_ETH_IP_STATIC, "192.168.1.10");
    mock_setting_items_set(KEY_ETH_MASK_STATIC, "255.255.255.0");
    mock_setting_items_set(KEY_ETH_GW_STATIC, "192.168.1.1");

    mock_setting_items_set(KEY_WIFI_MODE, WIFI_MODE_AP_STR);
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-AP");
    mock_setting_items_set(KEY_AP_PASS, "apsecret1");
    mock_setting_items_set(KEY_WIFI_AUTH_AP, WIFI_AUTH_WPA2_PSK_STR);
    mock_setting_items_set(KEY_AP_IP_STATIC, "192.168.4.1");
    mock_setting_items_set(KEY_AP_MASK_STATIC, "255.255.255.0");
    mock_setting_items_set(KEY_AP_GW_STATIC, "192.168.4.1");

    mock_setting_items_set(KEY_STA_SSID, "HomeNet");
    mock_setting_items_set(KEY_STA_PASS, "stasecret1");
    mock_setting_items_set(KEY_WIFI_AUTH_STA, WIFI_AUTH_WPA2_PSK_STR);
    mock_setting_items_set_bool(KEY_STA_DHCPC, true);
    mock_setting_items_set(KEY_STA_IP_STATIC, "192.168.1.20");
    mock_setting_items_set(KEY_STA_MASK_STATIC, "255.255.255.0");
    mock_setting_items_set(KEY_STA_GW_STATIC, "192.168.1.1");
}

// Boot with WiFi permanently disabled: network_init() skips WiFi init entirely, exactly as
// the firmware does, so the mock radio stays uninitialized for the rest of the test.
static void boot_with_wifi_disabled(void)
{
    mock_setting_items_set_bool(KEY_WIFI_PERM_DISABLE, true);
    TEST_ASSERT_EQUAL(ESP_OK, network_init());
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_wifi_init_apsta_called,
                              "network_init must not initialize WiFi when it is permanently disabled");
    // Pins the cold start setUp() is supposed to give us, on both globals that survive a
    // test: a WiFi baseline left over from a previous test's boot would make the "nothing
    // changed" assertions below vacuous. A skipped WiFi init never calls
    // update_sys_info_wifi_state(), so anything in sys_info.wifi_mode came from elsewhere.
    TEST_ASSERT_EQUAL_MESSAGE(WIFI_MODE_NULL, network_get_wifi_mode(),
                              "a boot that skips WiFi init must leave no cached WiFi settings behind");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", sys_info.wifi_mode,
                                     "a boot that skips WiFi init must publish no WiFi mode");
    TEST_ASSERT_FALSE_MESSAGE(sys_info.wifi_enabled,
                              "a boot that skips WiFi init must not report WiFi as enabled");
}

// Normal boot: network_init() brings the radio up and caches the settings it applied.
static void boot_with_wifi_enabled(void)
{
    mock_setting_items_set_bool(KEY_WIFI_PERM_DISABLE, false);
    TEST_ASSERT_EQUAL(ESP_OK, network_init());
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_wifi_init_apsta_called,
                              "network_init must initialize WiFi on a normal boot");
}

// The runtime clearing of the flag that the fix is about: NVS now says WiFi is enabled
// while the radio of this boot is still down.
static void clear_perm_disable_without_reboot(void)
{
    mock_setting_items_set_bool(KEY_WIFI_PERM_DISABLE, false);
}


void setUp(void)
{
    // Two globals outlive a test and both have to go back to their boot state: network.c
    // caches the settings it last applied in a file-static, and it publishes what it did
    // into sys_info. Left alone, either one lets a later test read a value the previous
    // test wrote and pass for the wrong reason.
    network_test_reset();
    mock_sys_info_reset();
    mock_setting_items_reset();
    mock_wifi_apsta_reset();
    seed_settings();
}

void tearDown(void)
{
}


// ── network_wifi_settings_applicable() ───────────────────────────────────────

void test_network_wifi_not_applicable_when_permanently_disabled(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - WiFi settings cannot be applied when permanently disabled");

    boot_with_wifi_disabled();

    TEST_ASSERT_FALSE(network_wifi_settings_applicable());
}

void test_network_wifi_not_applicable_when_radio_never_initialized(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - WiFi settings cannot be applied while the radio is down");

    boot_with_wifi_disabled();
    clear_perm_disable_without_reboot();

    TEST_ASSERT_FALSE_MESSAGE(network_wifi_settings_applicable(),
                              "the flag is gone from NVS, but this boot has no radio to configure");
}

void test_network_wifi_applicable_after_normal_boot(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - WiFi settings can be applied after a normal boot");

    boot_with_wifi_enabled();

    TEST_ASSERT_TRUE(network_wifi_settings_applicable());
}

// The other direction of the same flag, and the only case where the two halves of the
// predicate disagree: the radio IS up, and wifi_perm_disable is set while the device runs.
// The radio keeps serving until the reboot that turns it off, so nothing is re-applied to
// it — the behaviour the wifi_perm_disable guard has always had.
void test_network_wifi_not_applicable_when_disabled_at_runtime(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - WiFi settings cannot be applied once disabled at runtime");

    boot_with_wifi_enabled();
    mock_setting_items_set_bool(KEY_WIFI_PERM_DISABLE, true);
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    TEST_ASSERT_FALSE(network_wifi_settings_applicable());
    TEST_ASSERT_FALSE(network_check_wifi_settings_changed());

    esp_err_t ret = network_update_wifi_settings();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_wifi_set_apsta_config_called,
                              "a radio that is being switched off must not be reconfigured");
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}


// ── network_check_wifi_settings_changed() ────────────────────────────────────

void test_network_check_wifi_no_change_while_radio_is_down(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - no WiFi change is reported while the radio is down");

    boot_with_wifi_disabled();
    clear_perm_disable_without_reboot();

    // A change that WOULD be reported on an initialized radio.
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    TEST_ASSERT_FALSE_MESSAGE(network_check_wifi_settings_changed(),
                              "the flag raised here is what spawns the settings update task, "
                              "and there is nothing for that task to do");
}

void test_network_check_wifi_no_change_when_permanently_disabled(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - no WiFi change is reported when permanently disabled");

    boot_with_wifi_disabled();
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    TEST_ASSERT_FALSE(network_check_wifi_settings_changed());
}

void test_network_check_wifi_change_on_initialized_radio(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - a WiFi change is reported on an initialized radio");

    boot_with_wifi_enabled();

    TEST_ASSERT_FALSE_MESSAGE(network_check_wifi_settings_changed(),
                              "nothing was written since the boot, so nothing has changed");

    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    TEST_ASSERT_TRUE(network_check_wifi_settings_changed());
}


// ── network_update_wifi_settings() ───────────────────────────────────────────

void test_network_update_wifi_skipped_while_radio_is_down(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - update succeeds without touching a radio that is down");

    boot_with_wifi_disabled();
    clear_perm_disable_without_reboot();
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    esp_err_t ret = network_update_wifi_settings();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_wifi_set_apsta_config_called,
                              "an uninitialized radio must not be configured at all: it answers "
                              "ESP_ERR_NOT_ALLOWED, which was reported as a hard error");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret,
                              "settings that only take effect after a reboot are not a failure");
}

void test_network_update_wifi_skipped_when_permanently_disabled(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - update succeeds without touching a disabled radio");

    boot_with_wifi_disabled();
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    esp_err_t ret = network_update_wifi_settings();
    TEST_ASSERT_EQUAL(0, mock_wifi_set_apsta_config_called);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

void test_network_update_wifi_applied_on_initialized_radio(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - update applies the new settings on an initialized radio");

    boot_with_wifi_enabled();
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");

    TEST_ASSERT_EQUAL(ESP_OK, network_update_wifi_settings());
    TEST_ASSERT_EQUAL(1, mock_wifi_set_apsta_config_called);

    TEST_ASSERT_FALSE_MESSAGE(network_check_wifi_settings_changed(),
                              "the applied settings must become the new baseline");
}

// The guard must not swallow real failures: an initialized radio that refuses the new
// configuration is still an error, and still has to be reported as one.
void test_network_update_wifi_reports_genuine_failure(void)
{
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test network - a genuine configuration failure is still an error");

    boot_with_wifi_enabled();
    mock_setting_items_set(KEY_AP_SSID, "WirenBoard-Renamed");
    mock_wifi_set_apsta_config_return_value = ESP_FAIL;

    esp_err_t ret = network_update_wifi_settings();
    TEST_ASSERT_EQUAL(1, mock_wifi_set_apsta_config_called);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret, "a radio that refuses its configuration is a real failure");
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_network_wifi_not_applicable_when_permanently_disabled);
    RUN_TEST(test_network_wifi_not_applicable_when_radio_never_initialized);
    RUN_TEST(test_network_wifi_applicable_after_normal_boot);
    RUN_TEST(test_network_wifi_not_applicable_when_disabled_at_runtime);

    RUN_TEST(test_network_check_wifi_no_change_while_radio_is_down);
    RUN_TEST(test_network_check_wifi_no_change_when_permanently_disabled);
    RUN_TEST(test_network_check_wifi_change_on_initialized_radio);

    RUN_TEST(test_network_update_wifi_skipped_while_radio_is_down);
    RUN_TEST(test_network_update_wifi_skipped_when_permanently_disabled);
    RUN_TEST(test_network_update_wifi_applied_on_initialized_radio);
    RUN_TEST(test_network_update_wifi_reports_genuine_failure);

    return UNITY_END();
}
