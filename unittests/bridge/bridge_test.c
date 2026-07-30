#include "unity.h"
#include "console_log.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "bridge.h"
#include "config.h"
#include "setting_items.h"
#include "modbus_tcp.h"
#include "transparent_tcp.h"
#include "rs485_stats.h"

#include <string.h>

#define MOCK_DEFAULT_BRIDGE_IP_BINARY              33925312

#define SETTING_ITEMS_READ_INT_MAX_CALLS           2
#define SETTING_ITEMS_READ_MAX_CALLS               5
#define SETTING_ITEMS_READ_BOOL_MAX_CALLS          1

void bridge_reset(void);

// Declared here because serial mock header is not exposed; the symbol is defined in mocks/serial.c
extern bool mock_serial_init_should_succeed;
void mock_serial_reset(void);

static void mocks_reset(void)
{
    mock_setting_items_reset();
    mock_modbus_tcp_reset();
    mock_transparent_tcp_reset();
    mock_rs485_stats_reset();
    mock_serial_reset();
}

void setUp(void)
{
    mocks_reset();
    bridge_reset();
}

void tearDown(void)
{

}

static void configure_port_modes(void)
{
    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;
}


static void verify_setting_items_read_calls(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_setting_items_read_calls called with invalid index");

    TEST_ASSERT_EQUAL_MESSAGE(
        SETTING_ITEMS_READ_INT_MAX_CALLS,
        mock_setting_items_calls[index].read_int_called,
        "setting_items_read_int should be called 2 times for each index"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SETTING_ITEMS_READ_MAX_CALLS,
        mock_setting_items_calls[index].read_called,
        "setting_items_read should be called 5 times for each index"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SETTING_ITEMS_READ_BOOL_MAX_CALLS,
        mock_setting_items_calls[index].read_bool_called,
        "setting_items_read_bool should be called once for each index"
    );
}

static void verify_setting_items_not_called(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_setting_items_not_called called with invalid index");

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_setting_items_calls[index].read_int_called,
        "setting_items_read_int should not be called when port is disabled"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_setting_items_calls[index].read_called,
        "setting_items_read should not be called when port is disabled"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_setting_items_calls[index].read_bool_called,
        "setting_items_read_bool should not be called when port is disabled"
    );
}

static_assert(sizeof(mock_modbus_tcp_t) == sizeof(mock_transparent_tcp_t), "Struct sizes do not match");

// Structures mock_modbus_tcp_t and mock_transparent_tcp_t are identical, so we use one function to verify both types
static void verify_mode_tcp_init_port(unsigned index, void *mode_tcp_struct)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_mode_tcp_init_port called with invalid index");
    TEST_ASSERT_NOT_NULL_MESSAGE(mode_tcp_struct, "verify_mode_tcp_init_port called with NULL mode_tcp_struct");

    mock_modbus_tcp_t *mode_tcp = (mock_modbus_tcp_t *)mode_tcp_struct;

    if (index == 0) {
        TEST_ASSERT_EQUAL_MESSAGE(
            UART_NUM_1,
            mode_tcp->config->port_num,
            "tcp_init_port should be called with correct port number"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            GPIO_NUM_10,
            mode_tcp->config->tx_pin,
            "tcp_init_port should be called with correct tx pin"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            GPIO_NUM_9,
            mode_tcp->config->rx_pin,
            "tcp_init_port should be called with correct rx pin"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            GPIO_NUM_4,
            mode_tcp->config->dir_pin,
            "tcp_init_port should be called with correct dir pin"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            MOCK_DEFAULT_BRIDGE_PORT,
            mode_tcp->port,
            "tcp_init_port should be called with 502 bridge port"
        );
    } else {
        TEST_ASSERT_EQUAL_MESSAGE(
            UART_NUM_2,
            mode_tcp->config->port_num,
            "tcp_init_port should be called with correct port number"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            GPIO_NUM_14,
            mode_tcp->config->tx_pin,
            "tcp_init_port should be called with correct tx pin"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            GPIO_NUM_12,
            mode_tcp->config->rx_pin,
            "tcp_init_port should be called with correct rx pin"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            GPIO_NUM_15,
            mode_tcp->config->dir_pin,
            "tcp_init_port should be called with correct dir pin"
        );
        TEST_ASSERT_EQUAL_MESSAGE(
            MOCK_DEFAULT_BRIDGE_PORT2,
            mode_tcp->port,
            "tcp_init_port should be called with 503 bridge port"
        );
    }

    TEST_ASSERT_EQUAL_MESSAGE(
        MOCK_DEFAULT_BAUDRATE,
        mode_tcp->config->baudrate,
        "tcp_init_port should be called with 9600 baudrate"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        MOCK_DEFAULT_BRIDGE_IP_BINARY,
        mode_tcp->ip,
        "tcp_init_port should be called with correct bridge IP"
    );

    TEST_ASSERT_NOT_NULL_MESSAGE(
        mode_tcp->serial_desc,
        "tcp_init_port should be called with non-NULL serial_desc"
    );

    TEST_ASSERT_NOT_NULL_MESSAGE(
        mode_tcp->tcp_desc,
        "tcp_init_port should be called with non-NULL tcp_desc"
    );
}

static void verify_serial_config_and_mode(unsigned index, void *mode_tcp_struct,
                                          uart_parity_t parity, uart_stop_bits_t stopbits,
                                          uart_word_length_t databits, bridge_mode_t mode)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_serial_config_and_mode called with invalid index");
    TEST_ASSERT_NOT_NULL_MESSAGE(mode_tcp_struct, "verify_serial_config_and_mode called with NULL mode_tcp_struct");

    mock_modbus_tcp_t *mode_tcp = (mock_modbus_tcp_t *)mode_tcp_struct;

    TEST_ASSERT_EQUAL_MESSAGE(
        parity,
        mode_tcp->config->parity,
        "tcp_init_port should be called with expected parity"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        stopbits,
        mode_tcp->config->stopbits,
        "tcp_init_port should be called with expected stopbits"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        databits,
        mode_tcp->config->databits,
        "tcp_init_port should be called with expected databits"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        mode,
        mode_tcp->mode,
        "tcp_init_port should be called with expected mode"
    );
}

static void verify_mode_tcp_init_port_calls(unsigned index, int expected_modbus_calls, int expected_transparent_calls)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_mode_tcp_init_port_calls called with invalid index");

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_modbus_calls,
        mock_modbus_tcp_calls[index].init_port_called,
        "modbus_tcp_init_port call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_transparent_calls,
        mock_transparent_tcp_calls[index].init_port_called,
        "transparent_tcp_init_port call count mismatch"
    );
}

static void verify_rs485_reset_calls(unsigned index, int expected_busy_monitor_reset_calls, int expected_stats_reset_calls)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_rs485_reset_calls called with invalid index");

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_busy_monitor_reset_calls,
        mock_rs485_stats.busy_monitor_reset_called[index],
        "rs485_busy_monitor_reset call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_stats_reset_calls,
        mock_rs485_stats.stats_reset_called[index],
        "rs485_stats_reset call count mismatch"
    );
}

static void verify_mode_tcp_deinit_port_calls(unsigned index, int expected_modbus_calls, int expected_transparent_calls)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "verify_mode_tcp_deinit_port_calls called with invalid index");

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_modbus_calls,
        mock_modbus_tcp_calls[index].deinit_port_called,
        "modbus_tcp_deinit_port call count mismatch"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_transparent_calls,
        mock_transparent_tcp_calls[index].deinit_port_called,
        "transparent_tcp_deinit_port call count mismatch"
    );
}

// Test bridge_init
void test_bridge_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_init");
    LOG_MESSAGE();

    esp_err_t result = bridge_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_init should return ESP_OK");

    // rs485_busy_monitor_init() and rs485_stats_init() have been moved to
    // port_manager_init(), so they are NOT called from bridge_init() any more.
    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_rs485_stats.busy_monitor_init_called,
        "rs485_busy_monitor_init should not be called from bridge_init"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_rs485_stats.stats_init_called,
        "rs485_stats_init should not be called from bridge_init"
    );

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Test bridge_port_init initialization in Modbus TCP mode with various serial port settings
void test_bridge_port_init_server_modbus_tcp_various_settings_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with various serial settings in server Modbus TCP mode - 1");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);
        mock_settings_items_bridge_cfg[index].bridge_mb = true;
    }

    strcpy(mock_settings_items_bridge_cfg[0].serial_config.parity, UART_PARITY_DISABLE_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_2_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.databits, UART_DATA_8_BITS_STR);

    strcpy(mock_settings_items_bridge_cfg[1].serial_config.parity, UART_PARITY_EVEN_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.stopbits, UART_STOP_BITS_1_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.databits, UART_DATA_5_BITS_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port(index, &mock_modbus_tcp[index]);
        verify_mode_tcp_init_port_calls(index, 1, 0);
    }

    verify_serial_config_and_mode(
        0, &mock_modbus_tcp[0], UART_PARITY_DISABLE, UART_STOP_BITS_2, UART_DATA_8_BITS, BRIDGE_MODE_SERVER
    );
    verify_serial_config_and_mode(
        1, &mock_modbus_tcp[1], UART_PARITY_EVEN, UART_STOP_BITS_1, UART_DATA_5_BITS, BRIDGE_MODE_SERVER
    );
}

void test_bridge_port_init_server_modbus_tcp_various_settings_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with various serial settings in server Modbus TCP mode - 2");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);
        mock_settings_items_bridge_cfg[index].bridge_mb = true;
    }

    strcpy(mock_settings_items_bridge_cfg[0].serial_config.parity, UART_PARITY_ODD_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_1_5_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.databits, UART_DATA_6_BITS_STR);

    strcpy(mock_settings_items_bridge_cfg[1].serial_config.parity, UART_PARITY_DISABLE_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.stopbits, UART_STOP_BITS_2_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.databits, UART_DATA_7_BITS_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port(index, &mock_modbus_tcp[index]);
        verify_mode_tcp_init_port_calls(index, 1, 0);
    }

    verify_serial_config_and_mode(
        0, &mock_modbus_tcp[0], UART_PARITY_ODD, UART_STOP_BITS_1_5, UART_DATA_6_BITS, BRIDGE_MODE_SERVER
    );
    verify_serial_config_and_mode(
        1, &mock_modbus_tcp[1], UART_PARITY_DISABLE, UART_STOP_BITS_2, UART_DATA_7_BITS, BRIDGE_MODE_SERVER
    );
}

void test_bridge_port_init_server_modbus_tcp_invalid_settings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with invalid serial settings in server Modbus TCP mode");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        mock_settings_items_bridge_cfg[index].bridge_mb = true;

        strcpy(mock_settings_items_bridge_cfg[index].serial_config.parity, "INVALID_PARITY");
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.stopbits, "INVALID_STOPBITS");
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.databits, "INVALID_DATABITS");
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port(index, &mock_modbus_tcp[index]);
        verify_mode_tcp_init_port_calls(index, 1, 0);
        verify_serial_config_and_mode(
            index, &mock_modbus_tcp[index], UART_PARITY_DISABLE, UART_STOP_BITS_2, UART_DATA_8_BITS, BRIDGE_MODE_SERVER
        );
    }
}

// Test bridge_port_init initialization in Modbus TCP mode when modbus_tcp_init_port fails
void test_bridge_port_init_modbus_tcp_mode_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init in Modbus TCP mode when modbus_tcp_init_port fails");
    LOG_MESSAGE();

    mock_modbus_tcp_init_port_should_fail = true;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_CLIENT_STR);
        mock_settings_items_bridge_cfg[index].bridge_mb = true;

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL");

        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 1, 0);
    }
}

// Test bridge_port_init initialization in transparent gateway mode with various serial port settings and bridge modes
void test_bridge_port_init_transparent_mode_various_settings_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(
        CONS_COLOR_LIGHT_BLUE,
        "Test bridge_port_init with various serial settings and bridge modes in transparent mode - 1"
    );
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        mock_settings_items_bridge_cfg[index].bridge_mb = false;
    }

    strcpy(mock_settings_items_bridge_cfg[0].serial_config.parity, UART_PARITY_DISABLE_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_2_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.databits, UART_DATA_8_BITS_STR);
    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);

    strcpy(mock_settings_items_bridge_cfg[1].serial_config.parity, UART_PARITY_EVEN_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.stopbits, UART_STOP_BITS_1_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.databits, UART_DATA_5_BITS_STR);
    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port(index, &mock_transparent_tcp[index]);
        verify_mode_tcp_init_port_calls(index, 0, 1);
    }

    verify_serial_config_and_mode(
        0, &mock_transparent_tcp[0], UART_PARITY_DISABLE, UART_STOP_BITS_2, UART_DATA_8_BITS, BRIDGE_MODE_SERVER
    );

    verify_serial_config_and_mode(
        1, &mock_transparent_tcp[1], UART_PARITY_EVEN, UART_STOP_BITS_1, UART_DATA_5_BITS, BRIDGE_MODE_CLIENT
    );
}

void test_bridge_port_init_transparent_mode_various_settings_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(
        CONS_COLOR_LIGHT_BLUE,
        "Test bridge_port_init with various serial settings and bridge modes in transparent mode - 2"
    );
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        mock_settings_items_bridge_cfg[index].bridge_mb = false;
    }

    strcpy(mock_settings_items_bridge_cfg[0].serial_config.parity, UART_PARITY_EVEN_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_1_STR);
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.databits, UART_DATA_6_BITS_STR);
    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_CLIENT_STR);

    strcpy(mock_settings_items_bridge_cfg[1].serial_config.parity, UART_PARITY_ODD_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.stopbits, UART_STOP_BITS_1_5_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.databits, UART_DATA_7_BITS_STR);
    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_SERVER_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port(index, &mock_transparent_tcp[index]);
        verify_mode_tcp_init_port_calls(index, 0, 1);
    }

    verify_serial_config_and_mode(
        0, &mock_transparent_tcp[0], UART_PARITY_EVEN, UART_STOP_BITS_1, UART_DATA_6_BITS, BRIDGE_MODE_CLIENT
    );

    verify_serial_config_and_mode(
        1, &mock_transparent_tcp[1], UART_PARITY_ODD, UART_STOP_BITS_1_5, UART_DATA_7_BITS, BRIDGE_MODE_SERVER
    );
}

void test_bridge_port_init_transparent_mode_invalid_settings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with invalid serial settings in transparent mode");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        mock_settings_items_bridge_cfg[index].bridge_mb = false;

        strcpy(mock_settings_items_bridge_cfg[index].serial_config.parity, "INVALID_PARITY");
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.stopbits, "INVALID_STOPBITS");
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.databits, "INVALID_DATABITS");
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");

        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port(index, &mock_transparent_tcp[index]);
        verify_mode_tcp_init_port_calls(index, 0, 1);
        verify_serial_config_and_mode(
            index,
            &mock_transparent_tcp[index],
            UART_PARITY_DISABLE,
            UART_STOP_BITS_2,
            UART_DATA_8_BITS,
            BRIDGE_MODE_SERVER
        );
    }
}

// Test bridge_port_init initialization in transparent gateway mode when transparent_tcp_init_port fails
void test_bridge_port_init_transparent_mode_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init in transparent mode when transparent_tcp_init_port fails");
    LOG_MESSAGE();

    mock_transparent_tcp_init_port_should_fail = true;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_CLIENT_STR);
        mock_settings_items_bridge_cfg[index].bridge_mb = false;

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL");

        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 0, 1);
    }
}

// Test bridge_port_init initialization with setting read errors
void test_bridge_port_init_setting_read_errors_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with setting read errors 1");
    LOG_MESSAGE();

    mock_setting_items_calls[0].read_result.parity = ESP_FAIL;
    mock_setting_items_calls[1].read_result.stopbits = ESP_FAIL;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL");
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

void test_bridge_port_init_setting_read_errors_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with setting read errors 2");
    LOG_MESSAGE();

    mock_setting_items_calls[0].read_result.databits = ESP_FAIL;
    mock_setting_items_calls[1].read_result.bridge_mode = ESP_FAIL;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL");
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

void test_bridge_port_init_setting_read_errors_3(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with setting read errors 3");
    LOG_MESSAGE();

    mock_setting_items_calls[0].read_result.bridge_ip = ESP_FAIL;
    mock_setting_items_calls[1].read_result.parity = ESP_FAIL;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL");
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

void test_bridge_port_init_setting_read_errors_4(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with setting read errors 4");
    LOG_MESSAGE();

    mock_settings_items_bridge_cfg[0].serial_config.baudrate = 0;
    mock_settings_items_bridge_cfg[1].bridge_port = 0;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL when reading baudrate fails");
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Test repeated bridge_port_init initialization with different port modes
void test_bridge_port_init_already_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init - already initialized");
    LOG_MESSAGE();

    mock_settings_items_bridge_cfg[0].bridge_mb = true;
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK on first initialization");
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, index == 0 ? 1 : 0, index == 1 ? 1 : 0);
    }

    verify_mode_tcp_init_port(0, &mock_modbus_tcp[0]);
    verify_mode_tcp_init_port(1, &mock_transparent_tcp[1]);

    verify_serial_config_and_mode(
        0, &mock_modbus_tcp[0], UART_PARITY_DISABLE, UART_STOP_BITS_2, UART_DATA_8_BITS, BRIDGE_MODE_SERVER
    );

    verify_serial_config_and_mode(
        1, &mock_transparent_tcp[1], UART_PARITY_DISABLE, UART_STOP_BITS_2, UART_DATA_8_BITS, BRIDGE_MODE_SERVER
    );

    mocks_reset();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_ERR_NOT_ALLOWED, result, "bridge_port_init should return ESP_ERR_NOT_ALLOWED when already initialized"
        );

        verify_setting_items_not_called(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Test bridge_port_init with an invalid/legacy bridge_mode in settings.
// port_mode is the authoritative on/off axis now: a tcp_bridge port whose bridge_mode
// maps to BRIDGE_MODE_DISABLED (corrupt/legacy value) must NOT be half-initialized.
// bridge_port_init() returns an error so port_manager_set_mode() rolls the port back
// instead of leaving a zombie tcp_bridge. The port must stay uninitialized.
void test_bridge_port_init_invalid_bridge_mode_in_settings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with invalid/legacy bridge_mode in settings");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.parity, UART_PARITY_DISABLE_STR);
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.stopbits, UART_STOP_BITS_2_STR);
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.databits, UART_DATA_8_BITS_STR);
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, "disabled");

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_ERR_INVALID_STATE, result,
            "bridge_port_init should return ESP_ERR_INVALID_STATE for invalid/legacy bridge_mode"
        );

        // Config was read, but no TCP transport was brought up.
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Test bridge_port_init and bridge_port_deinit with an invalid port number
void test_bridge_port_init_deinit_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init - invalid index");
    LOG_MESSAGE();

    esp_err_t result = bridge_port_init(BRIDGES_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, result, "bridge_port_init should return ESP_ERR_INVALID_ARG for invalid index"
    );

    result = bridge_port_deinit(BRIDGES_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, result, "bridge_port_deinit should return ESP_ERR_INVALID_ARG for invalid index"
    );
}

// Test bridge_port_deinit with uninitialized ports
void test_bridge_port_deinit_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_deinit - not initialized ports");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_deinit(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_deinit should return ESP_OK for not initialized port");
        verify_rs485_reset_calls(index, 0, 0);
        verify_mode_tcp_deinit_port_calls(index, 0, 0);
    }
}

// Test successful bridge_port_deinit deinitialization
void test_bridge_port_deinit_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_deinit - success");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");

        result = bridge_port_deinit(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_deinit should return ESP_OK");

        // rs485_busy_monitor_reset() and rs485_stats_reset() have been moved to
        // port_manager, so they are NOT called from bridge_port_deinit() any more.
        verify_rs485_reset_calls(index, 0, 0);
        verify_mode_tcp_deinit_port_calls(index, index == 0 ? 1 : 0, index == 1 ? 1 : 0);
    }
}

// Test tcp_server_active_connections with an invalid server number
void test_tcp_server_active_connections_invalid_server_num(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - invalid server number");
    LOG_MESSAGE();

    int result = tcp_server_active_connections(-1);
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 for negative server_num");

    result = tcp_server_active_connections(TCP_SERVER_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 for server_num >= TCP_SERVER_COUNT");
}

// Test tcp_server_active_connections on a port that was never brought up.
// Named for the old mode-based guard; since the guard moved to bridge_ctx[].initialized the
// function does not look at bridge_mode at all, so what this pins now is the un-initialized
// port, not the DISABLED mode.
void test_tcp_server_active_connections_disabled_mode(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - disabled mode");
    LOG_MESSAGE();

    for (unsigned index = 0; index < TCP_SERVER_COUNT; index++) {
        int result = tcp_server_active_connections(index);
        TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 for a port that was never initialized");
    }
}

// Test tcp_server_active_connections with a NULL tcp_desc
void test_tcp_server_active_connections_null_tcp_desc(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - null tcp_desc");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_SERVER_STR);
        mock_settings_items_bridge_cfg[index].bridge_mb = true;

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    for (unsigned index = 0; index < TCP_SERVER_COUNT; index++) {
        *(mock_modbus_tcp[index].tcp_desc) = NULL;
        int result = tcp_server_active_connections(index);
        TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 when tcp_desc is NULL");
    }
}

// Test tcp_server_active_connections with active connections
void test_tcp_server_active_connections_exist(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - active connections exist");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    (*(mock_modbus_tcp[0].tcp_desc))->active_connections = 1;
    (*(mock_transparent_tcp[1].tcp_desc))->active_connections = 2;

    for (unsigned index = 0; index < TCP_SERVER_COUNT; index++) {
        int result = tcp_server_active_connections(index);
        TEST_ASSERT_EQUAL_MESSAGE(
            (index == 0) ? 1 : 2,
            result,
            "tcp_server_active_connections should return correct number of active connections"
        );
    }
}

// Regression C7: GET /info read a freed tcp_desc after a port was torn down.
//
// bridge_port_deinit() used to clear only serial_desc and initialized, never tcp_desc,
// while the descriptor itself had already been free()d inside
// modbus_tcp_deinit_port()/transparent_tcp_deinit_port(). tcp_server_active_connections()
// is called unconditionally for both ports on every GET /info, and its old guard
// (bridge_current_cfg[].bridge_mode != DISABLED, a field deinit does not clear either) let
// the call through to ->active_connections on freed memory.
//
// The invariant asserted here is not "the number is 0" but "the descriptor is no longer
// reachable from bridge_ctx": a NULL pointer is what makes it impossible for the reader to
// touch the freed block at all.
void test_tcp_server_active_connections_after_deinit(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - after deinit (C7)");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    // Non-zero on purpose: a port that reports 0 only because the count happened to be 0
    // would prove nothing about the guard.
    (*(mock_modbus_tcp[0].tcp_desc))->active_connections = 3;
    (*(mock_transparent_tcp[1].tcp_desc))->active_connections = 4;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_deinit(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_deinit should return ESP_OK");
    }

    TEST_ASSERT_NULL_MESSAGE(
        *(mock_modbus_tcp[0].tcp_desc),
        "bridge_port_deinit should clear tcp_desc so the freed descriptor is unreachable"
    );
    TEST_ASSERT_NULL_MESSAGE(
        *(mock_transparent_tcp[1].tcp_desc),
        "bridge_port_deinit should clear tcp_desc so the freed descriptor is unreachable"
    );

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        TEST_ASSERT_NULL_MESSAGE(
            bridge_get_serial_desc(index),
            "bridge_port_deinit should clear serial_desc"
        );
        int result = tcp_server_active_connections(index);
        TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 after deinit");
    }
}

// Regression C7, ordering half: the clear must happen BEFORE the descriptors are freed.
//
// bridge_port_deinit() used to unpublish the context only after
// modbus_tcp_deinit_port()/transparent_tcp_deinit_port() had returned, leaving a window
// that spans the whole teardown (which joins the TCP receiver tasks and the UART event
// task) during which the httpd task could still reach the descriptor being freed.
//
// The mocks stand where the real modules free the descriptors and sample both bridge_ctx
// readers there; both must already come up empty.
void test_bridge_ctx_unpublished_before_descriptors_are_freed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_deinit - context unpublished before the free (C7)");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    (*(mock_modbus_tcp[0].tcp_desc))->active_connections = 3;
    (*(mock_transparent_tcp[1].tcp_desc))->active_connections = 4;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_deinit(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_deinit should return ESP_OK");
    }

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_modbus_tcp_calls[0].deinit_observed_active_conns,
        "tcp_server_active_connections must already report 0 when modbus_tcp_deinit_port frees the descriptor"
    );
    TEST_ASSERT_NULL_MESSAGE(
        mock_modbus_tcp_calls[0].deinit_observed_serial_desc,
        "bridge_get_serial_desc must already be NULL when modbus_tcp_deinit_port frees the descriptor"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_transparent_tcp_calls[1].deinit_observed_active_conns,
        "tcp_server_active_connections must already report 0 when transparent_tcp_deinit_port frees the descriptor"
    );
    TEST_ASSERT_NULL_MESSAGE(
        mock_transparent_tcp_calls[1].deinit_observed_serial_desc,
        "bridge_get_serial_desc must already be NULL when transparent_tcp_deinit_port frees the descriptor"
    );
}

// Regression C7, second path: a FAILED bridge_port_init() left the same dangling pointers.
//
// The modules' late failure paths free the descriptors they had already written to the
// out-parameters; modbus_tcp.c's task-creation branch used to leave both pointers behind,
// and clearing them there is part of this same change. bridge_port_init() propagated the
// error with bridge_ctx[index].initialized still false, so bridge_port_deinit() would
// early-return "not initialized" and never clean up — the stale pointers survived
// indefinitely.
//
// The mock deliberately leaves both out-parameters set on its late-failure path: the clear
// under test is bridge.c's, and it must hold whatever the callee does.
void test_bridge_ctx_cleared_after_failed_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init - context cleared after a late failure (C7)");
    LOG_MESSAGE();

    configure_port_modes();
    mock_modbus_tcp_init_port_should_fail_late = true;
    mock_transparent_tcp_init_port_should_fail_late = true;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL on a late module failure");
    }

    TEST_ASSERT_NULL_MESSAGE(
        *(mock_modbus_tcp[0].tcp_desc),
        "bridge_port_init should clear tcp_desc when modbus_tcp_init_port fails after creating it"
    );
    TEST_ASSERT_NULL_MESSAGE(
        *(mock_transparent_tcp[1].tcp_desc),
        "bridge_port_init should clear tcp_desc when transparent_tcp_init_port fails after creating it"
    );

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        TEST_ASSERT_NULL_MESSAGE(
            bridge_get_serial_desc(index),
            "bridge_port_init should clear serial_desc when the module fails after creating it"
        );
        int result = tcp_server_active_connections(index);
        TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 after a failed init");
    }
}

// Regression C7, guard half: tcp_server_active_connections() must not depend on
// bridge_current_cfg[].bridge_mode.
//
// This is the one state the caller-side clear cannot cover, because bridge.c has not
// regained control yet: inside a failing module init, bridge_ctx[index].tcp_desc points at
// the descriptor the module is about to free and bridge_current_cfg[index].bridge_mode
// already says SERVER/CLIENT. The old mode-based guard therefore let a concurrent GET /info
// through; only a guard that consults `initialized` reports 0 here.
void test_tcp_server_active_connections_during_failed_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - during a failing init (C7)");
    LOG_MESSAGE();

    configure_port_modes();
    mock_modbus_tcp_init_port_should_fail_late = true;
    mock_transparent_tcp_init_port_should_fail_late = true;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL on a late module failure");
    }

    // 0, not MOCK_TCP_FAIL_LATE_ACTIVE_CONNS: the port is not initialized, whatever the
    // stale bridge_mode says. -1 would mean the mock never reached the observation point.
    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_modbus_tcp_calls[0].init_fail_observed_active_conns,
        "tcp_server_active_connections must report 0 while modbus_tcp_init_port is failing"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_transparent_tcp_calls[1].init_fail_observed_active_conns,
        "tcp_server_active_connections must report 0 while transparent_tcp_init_port is failing"
    );
}

// Test bridge_port_check_settings_changed with an invalid index
void test_bridge_port_check_settings_changed_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - invalid index");
    LOG_MESSAGE();

    bool result = bridge_port_check_settings_changed(BRIDGES_COUNT);
    TEST_ASSERT_FALSE_MESSAGE(result, "bridge_port_check_settings_changed should return false for invalid index");
}

// Test bridge_port_check_settings_changed with setting read errors
void test_bridge_port_check_settings_changed_read_errors(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - setting read errors");
    LOG_MESSAGE();

    mock_setting_items_calls[0].read_result.parity = ESP_FAIL;
    bool result = bridge_port_check_settings_changed(0);
    TEST_ASSERT_FALSE_MESSAGE(result, "bridge_port_check_settings_changed should return false when serial config read fails");

    mock_setting_items_calls[1].read_result.bridge_mode = ESP_FAIL;
    result = bridge_port_check_settings_changed(1);
    TEST_ASSERT_FALSE_MESSAGE(result, "bridge_port_check_settings_changed should return false when bridge config read fails");
}

// Test bridge_port_check_settings_changed for an uninitialized port with disabled mode
void test_bridge_port_check_settings_changed_not_initialized_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(
        CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - not initialized port with disabled mode"
    );
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, "disabled");

        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_FALSE_MESSAGE(
            result, "bridge_port_check_settings_changed should return false for uninitialized port in disabled mode"
        );
    }
}

// Test bridge_port_check_settings_changed for an uninitialized port with enabled mode
void test_bridge_port_check_settings_changed_not_initialized_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(
        CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - not initialized port with enabled mode"
    );
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_TRUE_MESSAGE(
            result, "bridge_port_check_settings_changed should return true for uninitialized port with enabled mode"
        );
    }
}

// Test bridge_port_check_settings_changed for an initialized port with no changes
void test_bridge_port_check_settings_changed_initialized_no_changes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with no changes");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_FALSE_MESSAGE(result, "bridge_port_check_settings_changed should return false when settings haven't changed");
    }
}

// Test bridge_port_check_settings_changed for an initialized port with changes
void test_bridge_port_check_settings_changed_initialized_changes_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 1");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    mock_settings_items_bridge_cfg[0].serial_config.baudrate = 115200;
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.parity, UART_PARITY_EVEN_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_TRUE_MESSAGE(result, "bridge_port_check_settings_changed should return true when serial config changed");
    }
}

// Test bridge_port_check_settings_changed for an initialized port with changes
void test_bridge_port_check_settings_changed_initialized_changes_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 2");
    LOG_MESSAGE();

    configure_port_modes();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, "disabled");
    strcpy(mock_settings_items_bridge_cfg[1].bridge_ip, "192.168.11.7");

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_TRUE_MESSAGE(result, "bridge_port_check_settings_changed should return true when bridge config changed");
    }
}

// Test bridge_port_check_settings_changed for an initialized port with changes
void test_bridge_port_check_settings_changed_initialized_changes_3(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 3");
    LOG_MESSAGE();

    configure_port_modes();

    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_1_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.databits, UART_DATA_8_BITS_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_2_STR);
    strcpy(mock_settings_items_bridge_cfg[1].serial_config.databits, UART_DATA_7_BITS_STR);

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_TRUE_MESSAGE(result, "bridge_port_check_settings_changed should return true when serial config changed");
    }
}

// Test bridge_port_check_settings_changed for an initialized port with changes
void test_bridge_port_check_settings_changed_initialized_changes_4(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 4");
    LOG_MESSAGE();

    configure_port_modes();

    mock_settings_items_bridge_cfg[0].bridge_port = MOCK_DEFAULT_BRIDGE_PORT;
    mock_settings_items_bridge_cfg[1].bridge_port = MOCK_DEFAULT_BRIDGE_PORT2;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    mock_settings_items_bridge_cfg[0].bridge_port = 8080;
    mock_settings_items_bridge_cfg[1].bridge_mb = true;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_TRUE_MESSAGE(result, "bridge_port_check_settings_changed should return true when bridge config changed");
    }
}

// Tests for bridge_port_init_serial_only

void test_bridge_port_init_serial_only_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init_serial_only - invalid index");
    LOG_MESSAGE();

    serial_desc_t *desc = NULL;
    esp_err_t result = bridge_port_init_serial_only(BRIDGES_COUNT, &desc);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, result, "bridge_port_init_serial_only should return ESP_ERR_INVALID_ARG for index >= BRIDGES_COUNT");
}

void test_bridge_port_init_serial_only_null_out(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init_serial_only - NULL serial_desc_out");
    LOG_MESSAGE();

    esp_err_t result = bridge_port_init_serial_only(0, NULL);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, result, "bridge_port_init_serial_only should return ESP_ERR_INVALID_ARG for NULL serial_desc_out");
}

void test_bridge_port_init_serial_only_read_config_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init_serial_only - read_serial_port_config fails");
    LOG_MESSAGE();

    /* Cause read_serial_port_config to fail by making parity read return an error */
    mock_setting_items_calls[0].read_result.parity = ESP_FAIL;

    serial_desc_t *desc = NULL;
    esp_err_t result = bridge_port_init_serial_only(0, &desc);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result,
        "bridge_port_init_serial_only should return ESP_FAIL when read_serial_port_config fails");
    TEST_ASSERT_NULL_MESSAGE(desc, "serial_desc_out should remain NULL when config read fails");
}

void test_bridge_port_init_serial_only_serial_init_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init_serial_only - serial_init returns NULL");
    LOG_MESSAGE();

    // mock_serial_init_should_succeed is false by default (set in setUp via mocks_reset)
    serial_desc_t *desc = NULL;
    esp_err_t result = bridge_port_init_serial_only(0, &desc);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init_serial_only should return ESP_FAIL when serial_init returns NULL");
    TEST_ASSERT_NULL_MESSAGE(desc, "serial_desc_out should remain NULL on failure");
}

void test_bridge_port_init_serial_only_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init_serial_only - success");
    LOG_MESSAGE();

    mock_serial_init_should_succeed = true;
    serial_desc_t *desc = NULL;
    esp_err_t result = bridge_port_init_serial_only(0, &desc);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init_serial_only should return ESP_OK on success");
    TEST_ASSERT_NOT_NULL_MESSAGE(desc, "serial_desc_out should point to a valid descriptor on success");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bridge_init);

    RUN_TEST(test_bridge_port_init_server_modbus_tcp_various_settings_1);
    RUN_TEST(test_bridge_port_init_server_modbus_tcp_various_settings_2);
    RUN_TEST(test_bridge_port_init_server_modbus_tcp_invalid_settings);
    RUN_TEST(test_bridge_port_init_modbus_tcp_mode_fail);

    RUN_TEST(test_bridge_port_init_transparent_mode_various_settings_1);
    RUN_TEST(test_bridge_port_init_transparent_mode_various_settings_2);
    RUN_TEST(test_bridge_port_init_transparent_mode_invalid_settings);
    RUN_TEST(test_bridge_port_init_transparent_mode_fail);

    RUN_TEST(test_bridge_port_init_setting_read_errors_1);
    RUN_TEST(test_bridge_port_init_setting_read_errors_2);
    RUN_TEST(test_bridge_port_init_setting_read_errors_3);
    RUN_TEST(test_bridge_port_init_setting_read_errors_4);

    RUN_TEST(test_bridge_port_init_already_initialized);
    RUN_TEST(test_bridge_port_init_invalid_bridge_mode_in_settings);
    RUN_TEST(test_bridge_port_init_deinit_invalid_index);

    RUN_TEST(test_bridge_port_deinit_not_initialized);
    RUN_TEST(test_bridge_port_deinit_success);

    RUN_TEST(test_tcp_server_active_connections_invalid_server_num);
    RUN_TEST(test_tcp_server_active_connections_disabled_mode);
    RUN_TEST(test_tcp_server_active_connections_null_tcp_desc);
    RUN_TEST(test_tcp_server_active_connections_exist);
    RUN_TEST(test_tcp_server_active_connections_after_deinit);
    RUN_TEST(test_bridge_ctx_unpublished_before_descriptors_are_freed);
    RUN_TEST(test_bridge_ctx_cleared_after_failed_init);
    RUN_TEST(test_tcp_server_active_connections_during_failed_init);

    RUN_TEST(test_bridge_port_check_settings_changed_invalid_index);
    RUN_TEST(test_bridge_port_check_settings_changed_read_errors);
    RUN_TEST(test_bridge_port_check_settings_changed_not_initialized_disabled);
    RUN_TEST(test_bridge_port_check_settings_changed_not_initialized_enabled);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_no_changes);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_1);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_2);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_3);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_4);

    RUN_TEST(test_bridge_port_init_serial_only_invalid_index);
    RUN_TEST(test_bridge_port_init_serial_only_null_out);
    RUN_TEST(test_bridge_port_init_serial_only_read_config_fail);
    RUN_TEST(test_bridge_port_init_serial_only_serial_init_fail);
    RUN_TEST(test_bridge_port_init_serial_only_success);

    return UNITY_END();
}
