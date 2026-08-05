#include "unity.h"
#include "console_log.h"

#include "update_rs485_mio_gpio_states.h"
#include "setting_items.h"
#include "rs485_control.h"
#include "mio_control.h"

extern int mock_port_manager_set_tx_disabled_called;
extern unsigned mock_port_manager_set_tx_disabled_port[];
extern bool mock_port_manager_set_tx_disabled_value[];
extern void mock_port_manager_reset(void);

void setUp(void)
{
    mock_rs485_control_reset();
    mock_mio_control_reset();
    mock_setting_items_reset();
    mock_port_manager_reset();
}

void tearDown(void)
{

}

static void verify_rs485_pupd_calls(bool expected_1, bool expected_2)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_pupd_on_off_called,
        "rs485_pupd_on_off should be called twice");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_1, mock_rs485_pupd_on_off_ports[0],
        "First pullup call should be for RS485_1");
    TEST_ASSERT_EQUAL_MESSAGE(expected_1, mock_rs485_pupd_on_off_on_values[0],
        expected_1 ? "Pullup 1 should be enabled" : "Pullup 1 should be disabled");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_2, mock_rs485_pupd_on_off_ports[1],
        "Second pullup call should be for RS485_2");
    TEST_ASSERT_EQUAL_MESSAGE(expected_2, mock_rs485_pupd_on_off_on_values[1],
        expected_2 ? "Pullup 2 should be enabled" : "Pullup 2 should be disabled");
}

static void verify_rs485_term_calls(bool expected_1, bool expected_2)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_rs485_term_on_off_called,
        "rs485_term_on_off should be called twice");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_1, mock_rs485_term_on_off_ports[0],
        "First terminator call should be for RS485_1");
    TEST_ASSERT_EQUAL_MESSAGE(expected_1, mock_rs485_term_on_off_on_values[0],
        expected_1 ? "Terminator 1 should be enabled" : "Terminator 1 should be disabled");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_2, mock_rs485_term_on_off_ports[1],
        "Second terminator call should be for RS485_2");
    TEST_ASSERT_EQUAL_MESSAGE(expected_2, mock_rs485_term_on_off_on_values[1],
        expected_2 ? "Terminator 2 should be enabled" : "Terminator 2 should be disabled");
}

static void verify_rs485_vout_call(bool expected)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_rs485_bus_vout_on_off_called,
        "rs485_bus_vout_on_off should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(expected, mock_rs485_bus_vout_on_off_on_values[0],
        expected ? "VOUT should be enabled" : "VOUT should be disabled");
}

static void verify_io_bus_call(bool expected)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mio_control_io_bus_onoff_called,
        "mio_control_io_bus_onoff should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(expected, mock_mio_control_io_bus_onoff_on_values[0],
        expected ? "IO bus should be enabled" : "IO bus should be disabled");
}

static void verify_rs485_function_not_called(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_rs485_pupd_on_off_called, "rs485_pupd_on_off should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_rs485_term_on_off_called, "rs485_term_on_off should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_rs485_bus_vout_on_off_called, "rs485_bus_vout_on_off should not be called");
}

// Test update_rs485_control with all settings enabled
void test_update_rs485_control_all_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - all settings enabled");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    // Verify setting_items_read_bool was called 5 times (GPIO settings only; TX disabled is handled by update_serial_tx_disabled)
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

    verify_rs485_pupd_calls(true, true);
    verify_rs485_term_calls(true, true);
    verify_rs485_vout_call(true);

    // Verify mio_control_io_bus_onoff is not called
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mio_control_io_bus_onoff_called,
        "mio_control_io_bus_onoff should not be called");
}

// Test update_rs485_control with all settings disabled
void test_update_rs485_control_all_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - all settings disabled");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, false);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, false);
    mock_setting_items_set_bool(KEY_485_VOUT, false);

    update_rs485_control();

    verify_rs485_pupd_calls(false, false);
    verify_rs485_term_calls(false, false);
    verify_rs485_vout_call(false);

    // Verify mio_control_io_bus_onoff is not called
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mio_control_io_bus_onoff_called,
        "mio_control_io_bus_onoff should not be called");
}

// Test "walking one" — only one setting enabled at a time
void test_update_rs485_control_walking_one_failsafe_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking one: FAIL_SAFE_1");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, false);
    mock_setting_items_set_bool(KEY_485_VOUT, false);

    update_rs485_control();

    verify_rs485_pupd_calls(true, false);
    verify_rs485_term_calls(false, false);
    verify_rs485_vout_call(false);
}

void test_update_rs485_control_walking_one_failsafe_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking one: FAIL_SAFE_2");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, false);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, false);
    mock_setting_items_set_bool(KEY_485_VOUT, false);

    update_rs485_control();

    verify_rs485_pupd_calls(false, true);
    verify_rs485_term_calls(false, false);
    verify_rs485_vout_call(false);
}

void test_update_rs485_control_walking_one_term_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking one: TERM_1");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, false);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, false);
    mock_setting_items_set_bool(KEY_485_VOUT, false);

    update_rs485_control();

    verify_rs485_pupd_calls(false, false);
    verify_rs485_term_calls(true, false);
    verify_rs485_vout_call(false);
}

void test_update_rs485_control_walking_one_term_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking one: TERM_2");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, false);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, false);

    update_rs485_control();

    verify_rs485_pupd_calls(false, false);
    verify_rs485_term_calls(false, true);
    verify_rs485_vout_call(false);
}

void test_update_rs485_control_walking_one_vout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking one: VOUT");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, false);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, false);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    verify_rs485_pupd_calls(false, false);
    verify_rs485_term_calls(false, false);
    verify_rs485_vout_call(true);
}

// Test "walking zero" — only one setting disabled at a time
void test_update_rs485_control_walking_zero_failsafe_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking zero: FAIL_SAFE_1");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, false);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    verify_rs485_pupd_calls(false, true);
    verify_rs485_term_calls(true, true);
    verify_rs485_vout_call(true);
}

void test_update_rs485_control_walking_zero_failsafe_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking zero: FAIL_SAFE_2");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, false);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    verify_rs485_pupd_calls(true, false);
    verify_rs485_term_calls(true, true);
    verify_rs485_vout_call(true);
}

void test_update_rs485_control_walking_zero_term_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking zero: TERM_1");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, false);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    verify_rs485_pupd_calls(true, true);
    verify_rs485_term_calls(false, true);
    verify_rs485_vout_call(true);
}

void test_update_rs485_control_walking_zero_term_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking zero: TERM_2");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, false);
    mock_setting_items_set_bool(KEY_485_VOUT, true);

    update_rs485_control();

    verify_rs485_pupd_calls(true, true);
    verify_rs485_term_calls(true, false);
    verify_rs485_vout_call(true);
}

void test_update_rs485_control_walking_zero_vout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_rs485_control - walking zero: VOUT");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_1, true);
    mock_setting_items_set_bool(KEY_485_FAIL_SAFE_2, true);
    mock_setting_items_set_bool(KEY_485_TERM_1, true);
    mock_setting_items_set_bool(KEY_485_TERM_2, true);
    mock_setting_items_set_bool(KEY_485_VOUT, false);

    update_rs485_control();

    verify_rs485_pupd_calls(true, true);
    verify_rs485_term_calls(true, true);
    verify_rs485_vout_call(false);
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

    verify_io_bus_call(true);
    verify_rs485_function_not_called();
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

    verify_io_bus_call(false);
    verify_rs485_function_not_called();
}

// Tests for update_serial_tx_disabled()
void test_update_serial_tx_disabled_both_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_serial_tx_disabled - both ports tx_disabled=false");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_TX_DISABLED_1, false);
    mock_setting_items_set_bool(KEY_485_TX_DISABLED_2, false);

    update_serial_tx_disabled();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_setting_items_read_bool_called,
        "setting_items_read_bool should be called twice");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_TX_DISABLED_1, mock_setting_items_read_bool_keys[0],
        "First read should be KEY_485_TX_DISABLED_1");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(KEY_485_TX_DISABLED_2, mock_setting_items_read_bool_keys[1],
        "Second read should be KEY_485_TX_DISABLED_2");

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_port_manager_set_tx_disabled_called,
        "port_manager_set_tx_disabled should be called twice");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_port_manager_set_tx_disabled_port[0],
        "First call should be for port 0");
    TEST_ASSERT_EQUAL_MESSAGE(false, mock_port_manager_set_tx_disabled_value[0],
        "Port 0 tx_disabled should be false");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_port_manager_set_tx_disabled_port[1],
        "Second call should be for port 1");
    TEST_ASSERT_EQUAL_MESSAGE(false, mock_port_manager_set_tx_disabled_value[1],
        "Port 1 tx_disabled should be false");
}

void test_update_serial_tx_disabled_both_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_serial_tx_disabled - both ports tx_disabled=true");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_TX_DISABLED_1, true);
    mock_setting_items_set_bool(KEY_485_TX_DISABLED_2, true);

    update_serial_tx_disabled();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_port_manager_set_tx_disabled_called,
        "port_manager_set_tx_disabled should be called twice");
    TEST_ASSERT_EQUAL_MESSAGE(true, mock_port_manager_set_tx_disabled_value[0],
        "Port 0 tx_disabled should be true");
    TEST_ASSERT_EQUAL_MESSAGE(true, mock_port_manager_set_tx_disabled_value[1],
        "Port 1 tx_disabled should be true");
}

void test_update_serial_tx_disabled_mixed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test update_serial_tx_disabled - port 0 disabled, port 1 enabled");
    LOG_MESSAGE();

    mock_setting_items_set_bool(KEY_485_TX_DISABLED_1, true);
    mock_setting_items_set_bool(KEY_485_TX_DISABLED_2, false);

    update_serial_tx_disabled();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_port_manager_set_tx_disabled_called,
        "port_manager_set_tx_disabled should be called twice");
    TEST_ASSERT_EQUAL_MESSAGE(true, mock_port_manager_set_tx_disabled_value[0],
        "Port 0 tx_disabled should be true");
    TEST_ASSERT_EQUAL_MESSAGE(false, mock_port_manager_set_tx_disabled_value[1],
        "Port 1 tx_disabled should be false");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_update_rs485_control_all_enabled);
    RUN_TEST(test_update_rs485_control_all_disabled);

    RUN_TEST(test_update_rs485_control_walking_one_failsafe_1);
    RUN_TEST(test_update_rs485_control_walking_one_failsafe_2);
    RUN_TEST(test_update_rs485_control_walking_one_term_1);
    RUN_TEST(test_update_rs485_control_walking_one_term_2);
    RUN_TEST(test_update_rs485_control_walking_one_vout);

    RUN_TEST(test_update_rs485_control_walking_zero_failsafe_1);
    RUN_TEST(test_update_rs485_control_walking_zero_failsafe_2);
    RUN_TEST(test_update_rs485_control_walking_zero_term_1);
    RUN_TEST(test_update_rs485_control_walking_zero_term_2);
    RUN_TEST(test_update_rs485_control_walking_zero_vout);

    RUN_TEST(test_update_io_bus_control_enabled);
    RUN_TEST(test_update_io_bus_control_disabled);

    RUN_TEST(test_update_serial_tx_disabled_both_disabled);
    RUN_TEST(test_update_serial_tx_disabled_both_enabled);
    RUN_TEST(test_update_serial_tx_disabled_mixed);

    return UNITY_END();
}
