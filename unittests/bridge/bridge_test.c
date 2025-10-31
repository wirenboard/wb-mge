#include "unity.h"
#include "console_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "bridge.h"

#include "setting_items.h"
#include "modbus_tcp.h"
#include "transparent_tcp.h"
#include "rs485_stats.h"

#include <string.h>

void setUp(void)
{
    mock_setting_items_reset();
    mock_modbus_tcp_reset();
    mock_transparent_tcp_reset();
    mock_rs485_stats_reset();
}

void tearDown(void)
{

}

// Тестируем успешную инициализацию bridge с отключенными портами
void test_bridge_init_success_disabled_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_init - success with both ports disabled");
    LOG_MESSAGE();

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bridge_disable_port(index);
    }

    esp_err_t result = bridge_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_init should return ESP_OK");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_rs485_busy_monitor_init_called, "rs485_busy_monitor_init should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_rs485_stats_init_called, "rs485_stats_init should be called once");
}

// Тестируем инициализацию bridge_port_init с невалидным номером порта
void test_bridge_port_init_invalid_index(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init - invalid index");
    LOG_MESSAGE();

    esp_err_t result = bridge_port_init(BRIDGES_COUNT);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_ERR_INVALID_ARG, result, "bridge_port_init should return ESP_ERR_INVALID_ARG for invalid index");
}

// Тестируем инициализацию bridge_port_init с ошибкой чтения baudrate
void test_bridge_port_init_baudrate_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_port_init - read baudrate failure");
    LOG_MESSAGE();

    mock_setting_items_read_int_should_fail = true;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        bridge_enable_port(index);
        esp_err_t result = bridge_port_init(index);
        TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "bridge_port_init should return ESP_FAIL when reading baudrate fails");
    }
}

// Тестируем инициализацию bridge_port_init со включенными портами
// void test_bridge_port_init(void)
// {
//     LOG_MESSAGE();
//     LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bridge_init - success");
//     LOG_MESSAGE();

//     strncpy(mock_setting_items_read_value_to_return_parity, UART_PARITY_DISABLE_STR, sizeof(UART_PARITY_DISABLE_STR) - 1);
//     strncpy(mock_setting_items_read_value_to_return_stopbits, UART_STOP_BITS_1_STR, sizeof(UART_STOP_BITS_1_STR) - 1);
//     strncpy(mock_setting_items_read_value_to_return_databits, UART_DATA_5_BITS_STR, sizeof(UART_DATA_5_BITS_STR) - 1);
//     strncpy(mock_setting_items_read_value_to_return_bridge_mode, BRIDGE_MODE_SERVER_STR, sizeof(BRIDGE_MODE_SERVER_STR) - 1);
//     strncpy(mock_setting_items_read_value_to_return_bridge_ip, "192.168.1.1", sizeof("192.168.1.1") - 1);

//     for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
//         bridge_enable_port(index);
//         esp_err_t result = bridge_port_init(index);

//         TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "bridge_port_init should return ESP_OK");

//     }

    // TEST_ASSERT_EQUAL_MESSAGE(10, mock_setting_items_read_int_called, "setting_items_read should be called 10 times");
    // TEST_ASSERT_EQUAL_MESSAGE(10, mock_setting_items_read_called, "setting_items_read should be called 10 times");

//     TEST_ASSERT_EQUAL_MESSAGE(2, mock_transparent_tcp_init_port_called, "transparent_tcp_init_port should be called twice (BRIDGES_COUNT)");
//     TEST_ASSERT_EQUAL_MESSAGE(0, mock_transparent_tcp_init_port_indices[0], "First call should be for index 0");
//     TEST_ASSERT_EQUAL_MESSAGE(1, mock_transparent_tcp_init_port_indices[1], "Second call should be for index 1");

//     TEST_ASSERT_EQUAL_MESSAGE(0, mock_modbus_tcp_init_port_called, "modbus_tcp_init_port should not be called when bridge_modbus is false");

//     TEST_ASSERT_MESSAGE(mock_setting_items_read_called >= 6, "setting_items_read should be called at least 6 times (parity, stopbits, databits, bridge_mode, bridge_ip for 2 ports)");
//     TEST_ASSERT_MESSAGE(mock_setting_items_read_int_called >= 4, "setting_items_read_int should be called at least 4 times (baudrate, bridge_port for 2 ports)");
//     TEST_ASSERT_MESSAGE(mock_setting_items_read_bool_called >= 2, "setting_items_read_bool should be called at least 2 times (bridge_modbus for 2 ports)");
// }


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_bridge_init_success_disabled_ports);
    RUN_TEST(test_bridge_port_init_invalid_index);
    RUN_TEST(test_bridge_port_init_baudrate_failure);
    // RUN_TEST(test_bridge_port_init);

    return UNITY_END();
}
