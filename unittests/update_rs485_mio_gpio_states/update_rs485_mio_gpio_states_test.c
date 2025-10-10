#include "unity.h"
#include "console_log.h"

#include "update_rs485_mio_gpio_states.h"
#include "setting_items.h"
#include "rs485_control.h"
#include "mio_control.h"

#include <string.h>

extern int mock_rs485_pupd_on_off_called;
extern rs485_port_t mock_rs485_pupd_on_off_ports[MAX_CALLS];
extern bool mock_rs485_pupd_on_off_on_values[MAX_CALLS];

extern int mock_rs485_term_on_off_called;
extern rs485_port_t mock_rs485_term_on_off_ports[MAX_CALLS];
extern bool mock_rs485_term_on_off_on_values[MAX_CALLS];

extern int mock_rs485_bus_vout_on_off_called;
extern bool mock_rs485_bus_vout_on_off_on_values[MAX_CALLS];

extern int mock_mio_control_io_bus_onoff_called;
extern bool mock_mio_control_io_bus_onoff_on_values[MAX_CALLS];

extern int mock_setting_items_read_bool_called;
extern char mock_setting_items_read_bool_keys[MAX_CALLS][64];

void mock_setting_items_set_bool(const char *key, bool value);
void mock_setting_items_reset(void);

void setUp(void)
{
    mock_rs485_pupd_on_off_called = 0;
    memset(mock_rs485_pupd_on_off_ports, 0, sizeof(mock_rs485_pupd_on_off_ports));
    memset(mock_rs485_pupd_on_off_on_values, 0, sizeof(mock_rs485_pupd_on_off_on_values));

    mock_rs485_term_on_off_called = 0;
    memset(mock_rs485_term_on_off_ports, 0, sizeof(mock_rs485_term_on_off_ports));
    memset(mock_rs485_term_on_off_on_values, 0, sizeof(mock_rs485_term_on_off_on_values));

    mock_rs485_bus_vout_on_off_called = 0;
    memset(mock_rs485_bus_vout_on_off_on_values, 0, sizeof(mock_rs485_bus_vout_on_off_on_values));

    mock_mio_control_io_bus_onoff_called = 0;
    memset(mock_mio_control_io_bus_onoff_on_values, 0, sizeof(mock_mio_control_io_bus_onoff_on_values));

    mock_setting_items_reset();
}

void tearDown(void)
{

}

// Test update_rs485_control with all settings enabled
void test_update_rs485_control_all_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - all settings enabled");
    LOG_MESSAGE();

    // Set all settings to true
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    // Verify setting_items_read_bool was called 5 times
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, mock_setting_items_read_bool_called,
        "setting_items_read_bool should be called 5 times");

    // Verify the correct keys were read
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_FAIL_SAFE_1, mock_setting_items_read_bool_keys[0],
        "First read should be KEY_485_FAIL_SAFE_1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_FAIL_SAFE_2, mock_setting_items_read_bool_keys[1],
        "Second read should be KEY_485_FAIL_SAFE_2");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_TERM_1, mock_setting_items_read_bool_keys[2],
        "Third read should be KEY_485_TERM_1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_TERM_2, mock_setting_items_read_bool_keys[3],
        "Fourth read should be KEY_485_TERM_2");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_VOUT, mock_setting_items_read_bool_keys[4],
        "Fifth read should be KEY_485_VOUT");

    // Verify rs485_pupd_on_off was called twice
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_pupd_on_off_called,
        "rs485_pupd_on_off should be called twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_1, mock_rs485_pupd_on_off_ports[0],
        "First pullup call should be for RS485_1");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_pupd_on_off_on_values[0],
        "First pullup should be enabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_2, mock_rs485_pupd_on_off_ports[1],
        "Second pullup call should be for RS485_2");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_pupd_on_off_on_values[1],
        "Second pullup should be enabled");

    // Verify rs485_term_on_off was called twice
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_term_on_off_called,
        "rs485_term_on_off should be called twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_1, mock_rs485_term_on_off_ports[0],
        "First terminator call should be for RS485_1");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_term_on_off_on_values[0],
        "First terminator should be enabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_2, mock_rs485_term_on_off_ports[1],
        "Second terminator call should be for RS485_2");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_term_on_off_on_values[1],
        "Second terminator should be enabled");

    // Verify rs485_bus_vout_on_off was called once
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_rs485_bus_vout_on_off_called,
        "rs485_bus_vout_on_off should be called once");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_bus_vout_on_off_on_values[0],
        "VOUT should be enabled");
}

// Test update_rs485_control with all settings disabled
void test_update_rs485_control_all_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - all settings disabled");
    LOG_MESSAGE();

    // Set all settings to false (default is false, so no need to set explicitly)
    
    update_rs485_control();

    // Verify rs485_pupd_on_off was called twice with disabled values
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_pupd_on_off_called,
        "rs485_pupd_on_off should be called twice");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_pupd_on_off_on_values[0],
        "First pullup should be disabled");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_pupd_on_off_on_values[1],
        "Second pullup should be disabled");

    // Verify rs485_term_on_off was called twice with disabled values
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_term_on_off_called,
        "rs485_term_on_off should be called twice");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_term_on_off_on_values[0],
        "First terminator should be disabled");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_term_on_off_on_values[1],
        "Second terminator should be disabled");

    // Verify rs485_bus_vout_on_off was called once with disabled value
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_rs485_bus_vout_on_off_called,
        "rs485_bus_vout_on_off should be called once");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_bus_vout_on_off_on_values[0],
        "VOUT should be disabled");
}

// Test update_rs485_control with mixed settings
void test_update_rs485_control_mixed_settings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - mixed settings");
    LOG_MESSAGE();

    // Set some settings to true, others to false
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    // Verify rs485_pupd_on_off calls
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_pupd_on_off_called,
        "rs485_pupd_on_off should be called twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_1, mock_rs485_pupd_on_off_ports[0],
        "First pullup call should be for RS485_1");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_pupd_on_off_on_values[0],
        "Pullup 1 should be enabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_2, mock_rs485_pupd_on_off_ports[1],
        "Second pullup call should be for RS485_2");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_pupd_on_off_on_values[1],
        "Pullup 2 should be disabled");

    // Verify rs485_term_on_off calls
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_term_on_off_called,
        "rs485_term_on_off should be called twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_1, mock_rs485_term_on_off_ports[0],
        "First terminator call should be for RS485_1");
    TEST_ASSERT_FALSE_MESSAGE(mock_rs485_term_on_off_on_values[0],
        "Terminator 1 should be disabled");
    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_2, mock_rs485_term_on_off_ports[1],
        "Second terminator call should be for RS485_2");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_term_on_off_on_values[1],
        "Terminator 2 should be enabled");

    // Verify rs485_bus_vout_on_off call
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_rs485_bus_vout_on_off_called,
        "rs485_bus_vout_on_off should be called once");
    TEST_ASSERT_TRUE_MESSAGE(mock_rs485_bus_vout_on_off_on_values[0],
        "VOUT should be enabled");
}

// Test update_io_bus_control with IO bus enabled
void test_update_io_bus_control_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_io_bus_control - IO bus enabled");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_IO_BUS_ENABLED, true);

    update_io_bus_control();

    // Verify setting_items_read_bool was called once
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_setting_items_read_bool_called,
        "setting_items_read_bool should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_IO_BUS_ENABLED, mock_setting_items_read_bool_keys[0],
        "Read should be for KEY_IO_BUS_ENABLED");

    // Verify mio_control_io_bus_onoff was called with true
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mio_control_io_bus_onoff_called,
        "mio_control_io_bus_onoff should be called once");
    TEST_ASSERT_TRUE_MESSAGE(mock_mio_control_io_bus_onoff_on_values[0],
        "IO bus should be enabled");
}

// Test update_io_bus_control with IO bus disabled
void test_update_io_bus_control_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_io_bus_control - IO bus disabled");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_IO_BUS_ENABLED, false);

    update_io_bus_control();

    // Verify setting_items_read_bool was called once
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_setting_items_read_bool_called,
        "setting_items_read_bool should be called once");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_IO_BUS_ENABLED, mock_setting_items_read_bool_keys[0],
        "Read should be for KEY_IO_BUS_ENABLED");

    // Verify mio_control_io_bus_onoff was called with false
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mio_control_io_bus_onoff_called,
        "mio_control_io_bus_onoff should be called once");
    TEST_ASSERT_FALSE_MESSAGE(mock_mio_control_io_bus_onoff_on_values[0],
        "IO bus should be disabled");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_update_rs485_control_all_enabled);
    RUN_TEST(test_update_rs485_control_all_disabled);
    RUN_TEST(test_update_rs485_control_mixed_settings);
    RUN_TEST(test_update_io_bus_control_enabled);
    RUN_TEST(test_update_io_bus_control_disabled);

    return UNITY_END();
}
