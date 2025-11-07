#include "unity.h"
#include "console_log.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "bridge.h"
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
static void mocks_reset(void);

void setUp(void)
{
    mocks_reset();
    bridge_reset();
}

void tearDown(void)
{

}

static void mocks_reset(void)
{
    mock_setting_items_reset();
    mock_modbus_tcp_reset();
    mock_transparent_tcp_reset();
    mock_rs485_stats_reset();
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

// Структуры mock_modbus_tcp_t и mock_transparent_tcp_t идентичны, поэтому используем одну функцию для проверки обоих типов
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

// Тестируем bridge_init
void test_bridge_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_init");
    LOG_MESSAGE();

    esp_err_t result = bridge_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_init should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_rs485_stats.busy_monitor_init_called,
        "rs485_busy_monitor_init should be called once"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_rs485_stats.stats_init_called,
        "rs485_stats_init should be called once"
    );

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Тестируем инициализацию bridge_port_init в режиме Modbus TCP и разными настройками последовательного порта
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

void test_bridge_port_init_client_modbus_tcp(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init in client Modbus TCP mode");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, BRIDGE_MODE_CLIENT_STR);
        mock_settings_items_bridge_cfg[index].bridge_mb = true;

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_ERR_INVALID_ARG, result, "bridge_port_init should return ESP_ERR_INVALID_ARG in client mode"
        );

        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 1, 0);
    }
}

// Тестируем инициализацию bridge_port_init в режиме прозрачного шлюза и разными настройками последовательного порта и режимами моста
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

// Тестируем инициализацию bridge_port_init в режиме прозрачного шлюза с ошибкой инициализации прозрачного TCP порта
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

// Тестируем инициализацию bridge_port_init с ошибками чтения настроек
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

// Тестируем повторную инициализацию bridge_port_init с разными режимами портов
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
    }

    verify_mode_tcp_init_port(0, &mock_modbus_tcp[0]);
    verify_mode_tcp_init_port_calls(0, 1, 0);

    verify_mode_tcp_init_port(1, &mock_transparent_tcp[1]);
    verify_mode_tcp_init_port_calls(1, 0, 1);

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

// Тестируем инициализацию bridge_port_init с отключенными портами
void test_bridge_port_init_disabled_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with both ports disabled");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_disable_port(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_disable_port should return ESP_OK");

        result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK when port is disabled");

        verify_setting_items_not_called(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Тестируем инициализацию bridge_port_init, отключив порты в настройках
void test_bridge_port_init_disabled_ports_in_settings(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init with both ports disabled in settings");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.parity, UART_PARITY_DISABLE_STR);
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.stopbits, UART_STOP_BITS_2_STR);
        strcpy(mock_settings_items_bridge_cfg[index].serial_config.databits, UART_DATA_8_BITS_STR);
        strcpy(mock_settings_items_bridge_cfg[index].bridge_mode, "disabled");

        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK when port is disabled in settings");

        verify_setting_items_read_calls(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Тестируем bridge_port_init и bridge_port_deinit с невалидным номером порта
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

// Тестируем bridge_port_deinit с неинициализированными портами
void test_bridge_port_deinit_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_deinit - not initialized ports");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_deinit(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_deinit should return ESP_OK for not initialized port");
    }
}

// Тестируем успешную деинициализацию bridge_port_deinit
void test_bridge_port_deinit_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_deinit - success");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");

        result = bridge_port_deinit(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_deinit should return ESP_OK for disabled port");

        TEST_ASSERT_EQUAL_MESSAGE(
            1,
            mock_rs485_stats.busy_monitor_reset_called[index],
            "rs485_stats_busy_monitor_reset should be called once"
        );

        TEST_ASSERT_EQUAL_MESSAGE(
            1,
            mock_rs485_stats.stats_reset_called[index],
            "rs485_stats_reset should be called once"
        );
    }

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_modbus_tcp_calls[0].deinit_port_called,
        "modbus_tcp_deinit_port should be called once"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_transparent_tcp_calls[1].deinit_port_called,
        "transparent_tcp_deinit_port should be called once"
    );
}

// Тестируем функции bridge_disable_port и bridge_enable_port с невалидным индексом
void test_bridge_disable_enable_port_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_disable_port and bridge_enable_port - invalid index");
    LOG_MESSAGE();

    esp_err_t result = bridge_disable_port(BRIDGES_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, result, "bridge_disable_port should return ESP_ERR_INVALID_ARG for invalid index"
    );

    result = bridge_enable_port(BRIDGES_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, result, "bridge_enable_port should return ESP_ERR_INVALID_ARG for invalid index"
    );
}

// Тестируем функцию bridge_disable_port с инициализированным bridge_port_init
void test_bridge_disable_port_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_disable_port - initialized port");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");

        result = bridge_disable_port(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_disable_port should return ESP_OK for initialized port");

        TEST_ASSERT_EQUAL_MESSAGE(
            1,
            mock_rs485_stats.busy_monitor_reset_called[index],
            "rs485_stats_busy_monitor_reset should be called once"
        );

        TEST_ASSERT_EQUAL_MESSAGE(
            1,
            mock_rs485_stats.stats_reset_called[index],
            "rs485_stats_reset should be called once"
        );
    }

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_modbus_tcp_calls[0].deinit_port_called,
        "modbus_tcp_deinit_port should be called once"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_transparent_tcp_calls[1].deinit_port_called,
        "transparent_tcp_deinit_port should be called once"
    );
}

// Тестируем функцию bridge_enable_port с неинициализированным портом без запроса инициализации
void test_bridge_enable_port_not_initialized_no_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_enable_port - not initialized port without init request");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_enable_port(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_enable_port should return ESP_OK");

        verify_setting_items_not_called(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }
}

// Тестируем функцию bridge_enable_port с неинициализированным портом, который был отключен при попытке инициализации
void test_bridge_enable_port_disabled_during_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_enable_port - port disabled during initialization");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_disable_port(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_disable_port should return ESP_OK");

        // Try to initialize disabled port - should set init_request flag
        result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK for disabled port");

        // Verify port was not actually initialized
        verify_setting_items_not_called(index);
        verify_mode_tcp_init_port_calls(index, 0, 0);
    }

    // Now enable the ports - should trigger actual initialization
    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_enable_port(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_enable_port should return ESP_OK");
        verify_setting_items_read_calls(index);
    }

    verify_mode_tcp_init_port(0, &mock_modbus_tcp[0]);
    verify_mode_tcp_init_port_calls(0, 1, 0);

    verify_mode_tcp_init_port(1, &mock_transparent_tcp[1]);
    verify_mode_tcp_init_port_calls(1, 0, 1);
}

// Тестируем tcp_server_active_connections с невалидным номером сервера
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

// Тестируем tcp_server_active_connections с отключенным портом
void test_tcp_server_active_connections_disabled_mode(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - disabled mode");
    LOG_MESSAGE();

    for (unsigned index = 0; index < TCP_SERVER_COUNT; index++) {
        int result = tcp_server_active_connections(index);
        TEST_ASSERT_EQUAL_MESSAGE(0, result, "tcp_server_active_connections should return 0 when bridge mode is DISABLED");
    }
}

// Тестируем tcp_server_active_connections с нулевым tcp_desc
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

// Тестируем tcp_server_active_connections с активными соединениями
void test_tcp_server_active_connections_exist(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test tcp_server_active_connections - active connections exist");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

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

// Тестируем bridge_port_check_settings_changed с невалидным индексом
void test_bridge_port_check_settings_changed_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - invalid index");
    LOG_MESSAGE();

    bool result = bridge_port_check_settings_changed(BRIDGES_COUNT);
    TEST_ASSERT_FALSE_MESSAGE(result, "bridge_port_check_settings_changed should return false for invalid index");
}

// Тестируем bridge_port_check_settings_changed с ошибками чтения настроек
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

// Тестируем bridge_port_check_settings_changed для неинициализированного порта с отключенным режимом
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

// Тестируем bridge_port_check_settings_changed для неинициализированного порта с включенным режимом
void test_bridge_port_check_settings_changed_not_initialized_enabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(
        CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - not initialized port with enabled mode"
    );
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_TRUE_MESSAGE(
            result, "bridge_port_check_settings_changed should return true for uninitialized port with enabled mode"
        );
    }
}

// Тестируем bridge_port_check_settings_changed для инициализированного порта без изменений
void test_bridge_port_check_settings_changed_initialized_no_changes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with no changes");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");
    }

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bool result = bridge_port_check_settings_changed(index);
        TEST_ASSERT_FALSE_MESSAGE(result, "bridge_port_check_settings_changed should return false when settings haven't changed");
    }
}

// Тестируем bridge_port_check_settings_changed для инициализированного порта с изменениями
void test_bridge_port_check_settings_changed_initialized_changes_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 1");
    LOG_MESSAGE();

    mock_settings_items_bridge_cfg[0].serial_config.baudrate = MOCK_DEFAULT_BAUDRATE;
    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].serial_config.parity, UART_PARITY_DISABLE_STR);
    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

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

// Тестируем bridge_port_check_settings_changed для инициализированного порта с изменениями
void test_bridge_port_check_settings_changed_initialized_changes_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 2");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;

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

// Тестируем bridge_port_check_settings_changed для инициализированного порта с изменениями
void test_bridge_port_check_settings_changed_initialized_changes_3(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 3");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;
    strcpy(mock_settings_items_bridge_cfg[0].serial_config.stopbits, UART_STOP_BITS_1_STR);

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;
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

// Тестируем bridge_port_check_settings_changed для инициализированного порта с изменениями
void test_bridge_port_check_settings_changed_initialized_changes_4(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_check_settings_changed - initialized port with changes - 4");
    LOG_MESSAGE();

    strcpy(mock_settings_items_bridge_cfg[0].bridge_mode, BRIDGE_MODE_SERVER_STR);
    mock_settings_items_bridge_cfg[0].bridge_mb = true;
    mock_settings_items_bridge_cfg[0].bridge_port = MOCK_DEFAULT_BRIDGE_PORT;

    strcpy(mock_settings_items_bridge_cfg[1].bridge_mode, BRIDGE_MODE_CLIENT_STR);
    mock_settings_items_bridge_cfg[1].bridge_mb = false;
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

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bridge_init);

    RUN_TEST(test_bridge_port_init_server_modbus_tcp_various_settings_1);
    RUN_TEST(test_bridge_port_init_server_modbus_tcp_various_settings_2);
    RUN_TEST(test_bridge_port_init_server_modbus_tcp_invalid_settings);
    RUN_TEST(test_bridge_port_init_client_modbus_tcp);

    RUN_TEST(test_bridge_port_init_transparent_mode_various_settings_1);
    RUN_TEST(test_bridge_port_init_transparent_mode_various_settings_2);
    RUN_TEST(test_bridge_port_init_transparent_mode_invalid_settings);
    RUN_TEST(test_bridge_port_init_transparent_mode_fail);

    RUN_TEST(test_bridge_port_init_setting_read_errors_1);
    RUN_TEST(test_bridge_port_init_setting_read_errors_2);
    RUN_TEST(test_bridge_port_init_setting_read_errors_3);
    RUN_TEST(test_bridge_port_init_setting_read_errors_4);

    RUN_TEST(test_bridge_port_init_already_initialized);
    RUN_TEST(test_bridge_port_init_disabled_ports);
    RUN_TEST(test_bridge_port_init_disabled_ports_in_settings);
    RUN_TEST(test_bridge_port_init_deinit_invalid_index);

    RUN_TEST(test_bridge_port_deinit_not_initialized);
    RUN_TEST(test_bridge_port_deinit_success);

    RUN_TEST(test_bridge_disable_enable_port_invalid_index);
    RUN_TEST(test_bridge_disable_port_initialized);

    RUN_TEST(test_bridge_enable_port_not_initialized_no_request);
    RUN_TEST(test_bridge_enable_port_disabled_during_init);

    RUN_TEST(test_tcp_server_active_connections_invalid_server_num);
    RUN_TEST(test_tcp_server_active_connections_disabled_mode);
    RUN_TEST(test_tcp_server_active_connections_null_tcp_desc);
    RUN_TEST(test_tcp_server_active_connections_exist);

    RUN_TEST(test_bridge_port_check_settings_changed_invalid_index);
    RUN_TEST(test_bridge_port_check_settings_changed_read_errors);
    RUN_TEST(test_bridge_port_check_settings_changed_not_initialized_disabled);
    RUN_TEST(test_bridge_port_check_settings_changed_not_initialized_enabled);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_no_changes);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_1);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_2);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_3);
    RUN_TEST(test_bridge_port_check_settings_changed_initialized_changes_4);

    return UNITY_END();
}
