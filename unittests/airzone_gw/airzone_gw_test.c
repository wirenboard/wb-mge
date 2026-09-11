// Unit tests for airzone_gw.c — the store behind the register block served on unit 0xFF.
// Covers the three properties the block depends on: the four installer settings are answered from
// RAM and never from storage, a counter reaches the board only once the flash has taken it, and
// the mirror's age is measured in whole seconds.

#include "unity.h"
#include "console_log.h"

#include "airzone_gw.h"
#include "setting_items.h"
#include "esp_timer.h"

#include <string.h>

// -------------------------------------------------------------------
// Symbols exported by mocks
// -------------------------------------------------------------------
extern int       mock_setting_items_read_count;
extern int       mock_setting_items_save_count;
extern esp_err_t mock_setting_items_save_error;
void             mock_setting_items_reset(void);
void             mock_setting_items_set_int(const char *key, int value);
int              mock_setting_items_get_int(const char *key);

#define US_PER_S   1000000ULL

// -------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------

// Put a device that has been in service into storage and load it into the module.
static void init_with(int address, int zone, int speed, int product, int incl_cnt, int set_cnt)
{
    mock_setting_items_set_int(KEY_AIRZONE_ADDRESS,  address);
    mock_setting_items_set_int(KEY_AIRZONE_ZONE,     zone);
    mock_setting_items_set_int(KEY_AIRZONE_SPEED,    speed);
    mock_setting_items_set_int(KEY_AIRZONE_PRODUCT,  product);
    mock_setting_items_set_int(KEY_AIRZONE_INCL_CNT, incl_cnt);
    mock_setting_items_set_int(KEY_AIRZONE_SET_CNT,  set_cnt);
    airzone_gw_init();
}

void setUp(void)
{
    mock_setting_items_reset();
    mock_esp_timer_reset();
}

void tearDown(void)
{
}

// ===================================================================
// The four installer settings
// ===================================================================

// airzone_gw_init() is the only place allowed to reach storage for them.
void test_init_loads_the_four_settings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "init: the four stored settings are loaded into the cache");
    LOG_MESSAGE();

    init_with(5, 7, 48, 2, 0, 0);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, airzone_gw_get_address(), "az_address must be loaded");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(7, airzone_gw_get_zone(), "az_zone must be loaded");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(48, airzone_gw_get_speed_code(), "az_speed must be loaded");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(2, airzone_gw_get_product_type(), "az_product must be loaded");
}

// The getters are reached from the UART event task, on every ordinary poll of the block by the
// Z-Wave board. serial.c documents that task as one that must not block, and an NVS read both
// blocks and takes the global NVS mutex the HTTP task holds across a settings save — so a poll
// must reach storage zero times.
void test_getters_never_read_storage(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "get: repeated polls of the four settings reach storage zero times");
    LOG_MESSAGE();

    init_with(5, 7, 48, 2, 4, 9);
    mock_setting_items_read_count = 0;

    for (int poll = 0; poll < 3; poll++) {
        TEST_ASSERT_EQUAL_UINT16(5, airzone_gw_get_address());
        TEST_ASSERT_EQUAL_UINT16(7, airzone_gw_get_zone());
        TEST_ASSERT_EQUAL_UINT16(48, airzone_gw_get_speed_code());
        TEST_ASSERT_EQUAL_UINT16(2, airzone_gw_get_product_type());
        TEST_ASSERT_EQUAL_UINT16(4, airzone_gw_get_inclusion_counter());
        TEST_ASSERT_EQUAL_UINT16(9, airzone_gw_get_settings_counter());
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_setting_items_read_count,
        "reading the register block must not reach storage — that read runs in the UART task");
}

// The flip side of the cache: a value written behind the module's back is not visible until the
// writer says so, which is why every writer of these four calls the reload.
void test_reload_publishes_the_stored_values(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "reload: a stored write becomes visible only once the cache is refreshed");
    LOG_MESSAGE();

    init_with(5, 7, 48, 2, 0, 0);

    mock_setting_items_set_int(KEY_AIRZONE_ADDRESS, 9);
    mock_setting_items_set_int(KEY_AIRZONE_ZONE,    3);
    mock_setting_items_set_int(KEY_AIRZONE_SPEED,   96);
    mock_setting_items_set_int(KEY_AIRZONE_PRODUCT, 0);

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, airzone_gw_get_address(),
        "a write that skipped the reload must not be visible yet");

    airzone_gw_reload_settings();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(9, airzone_gw_get_address(), "az_address must be republished");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, airzone_gw_get_zone(), "az_zone must be republished");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(96, airzone_gw_get_speed_code(), "az_speed must be republished");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, airzone_gw_get_product_type(), "az_product must be republished");
}

// ===================================================================
// The two event counters
// ===================================================================

void test_inc_counter_persists_and_publishes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inc: a successful persist moves both the stored value and the published one");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 7, 3);

    airzone_gw_inc_inclusion_counter();
    airzone_gw_inc_settings_counter();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(8, airzone_gw_get_inclusion_counter(),
        "the board must see the incremented inclusion counter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, mock_setting_items_get_int(KEY_AIRZONE_INCL_CNT),
        "the incremented inclusion counter must be in storage");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(4, airzone_gw_get_settings_counter(),
        "the board must see the incremented settings counter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, mock_setting_items_get_int(KEY_AIRZONE_SET_CNT),
        "the incremented settings counter must be in storage");
}

// The persist runs before the RAM copy is published, so a write that failed leaves the board
// seeing exactly what it saw before. The alternative — publish first — would hand the board an
// event the flash never recorded, and the next reboot would deliver it a second time.
void test_failed_persist_leaves_the_published_counter_unchanged(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inc: a failed persist publishes nothing — the event is lost, never repeated");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 7, 3);
    mock_setting_items_save_error = ESP_FAIL;

    airzone_gw_inc_inclusion_counter();
    airzone_gw_inc_settings_counter();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_setting_items_save_count,
        "both increments must have attempted a write");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(7, airzone_gw_get_inclusion_counter(),
        "a failed write must not let the board see the inclusion event");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(3, airzone_gw_get_settings_counter(),
        "a failed write must not let the board see the settings event");
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, mock_setting_items_get_int(KEY_AIRZONE_INCL_CNT),
        "a failed write must leave the stored inclusion counter alone");
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, mock_setting_items_get_int(KEY_AIRZONE_SET_CNT),
        "a failed write must leave the stored settings counter alone");
}

// The installer presses again, and that press must count as one event — not two, and not a jump
// of two, which the board would still read as one event but from the wrong base.
void test_press_after_a_failed_persist_moves_the_counter_by_one(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inc: after a failed persist, the next successful one moves the counter by exactly one");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 7, 3);

    mock_setting_items_save_error = ESP_FAIL;
    airzone_gw_inc_inclusion_counter();

    mock_setting_items_save_error = ESP_OK;
    airzone_gw_inc_inclusion_counter();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(8, airzone_gw_get_inclusion_counter(),
        "the retry must continue from the stored value, not from the one the failure computed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, mock_setting_items_get_int(KEY_AIRZONE_INCL_CNT),
        "storage and the published value must agree after the retry");
}

// The board compares for difference, not for order, so the wrap needs no special case — but it
// must be a wrap and not a clamp, or the counter would stop moving at 65535.
void test_counter_wraps_at_the_register_width(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "inc: a counter at 65535 wraps to 0 instead of sticking");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 0xFFFF, 0);

    airzone_gw_inc_inclusion_counter();

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, airzone_gw_get_inclusion_counter(),
        "the counter must wrap, so the board still sees a difference");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_setting_items_get_int(KEY_AIRZONE_INCL_CNT),
        "the wrapped value must be stored");
}

// ===================================================================
// The mirror block
// ===================================================================

void test_mirror_is_invalid_until_the_board_writes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "mirror: invalid with age 0 until the board has written the block");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 0, 0);

    airzone_gw_mirror_t snapshot;
    airzone_gw_mirror_get(&snapshot);

    TEST_ASSERT_FALSE_MESSAGE(snapshot.valid, "a block the board never wrote must read as invalid");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, snapshot.age_s, "an invalid block has no age to report");
}

// The write timestamp is kept as a uint32_t count of seconds: it is written by the UART task and
// read by the HTTP task, and on a 32-bit core a 64-bit read is two words, so a carry between them
// would show the page an age of ~4295 s for a board that never stopped reporting.
void test_mirror_age_counts_whole_seconds_since_the_write(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "mirror: age is whole seconds since the last write");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 0, 0);

    mock_esp_timer_get_time_value = 10 * US_PER_S;
    airzone_gw_mirror_set_reg(0, 0x1234);

    mock_esp_timer_get_time_value = (10 * US_PER_S) + (7 * US_PER_S) + (US_PER_S / 2);

    airzone_gw_mirror_t snapshot;
    airzone_gw_mirror_get(&snapshot);

    TEST_ASSERT_TRUE_MESSAGE(snapshot.valid, "a written block must read as valid");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7, snapshot.age_s,
        "the age must be the whole seconds since the write");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0x1234, snapshot.values[0],
        "the snapshot must carry what the board wrote");
}

// A fresh write restarts the clock: the age is about the LAST write, not the first.
void test_mirror_age_restarts_on_every_write(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "mirror: a later write restarts the age");
    LOG_MESSAGE();

    init_with(1, 1, 16, 1, 0, 0);

    mock_esp_timer_get_time_value = 10 * US_PER_S;
    airzone_gw_mirror_set_reg(0, 1);

    mock_esp_timer_get_time_value = 100 * US_PER_S;
    airzone_gw_mirror_set_reg(1, 2);

    mock_esp_timer_get_time_value = 103 * US_PER_S;

    airzone_gw_mirror_t snapshot;
    airzone_gw_mirror_get(&snapshot);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, snapshot.age_s,
        "the age must be measured from the most recent write");
}

// -------------------------------------------------------------------
// Test runner
// -------------------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_loads_the_four_settings);
    RUN_TEST(test_getters_never_read_storage);
    RUN_TEST(test_reload_publishes_the_stored_values);
    RUN_TEST(test_inc_counter_persists_and_publishes);
    RUN_TEST(test_failed_persist_leaves_the_published_counter_unchanged);
    RUN_TEST(test_press_after_a_failed_persist_moves_the_counter_by_one);
    RUN_TEST(test_counter_wraps_at_the_register_width);
    RUN_TEST(test_mirror_is_invalid_until_the_board_writes);
    RUN_TEST(test_mirror_age_counts_whole_seconds_since_the_write);
    RUN_TEST(test_mirror_age_restarts_on_every_write);

    return UNITY_END();
}
