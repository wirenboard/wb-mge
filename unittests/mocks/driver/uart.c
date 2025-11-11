#include "unity.h"

#include "uart.h"
#include <string.h>
#include <stdlib.h>

mock_uart_calls_t mock_uart_calls = {0};
mock_uart_data_t mock_uart_data = {0};

esp_err_t uart_flush_input(uart_port_t uart_num)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_flush_input should be called with correct port number");

    mock_uart_calls.flush_input_called++;
    return ESP_OK;
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int event_queue_size,
                              QueueHandle_t *uart_queue, int intr_alloc_flags)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_driver_install should be called with correct port number");

    mock_uart_calls.driver_install_called++;
    mock_uart_data.rx_buffer_size = rx_buffer_size;
    mock_uart_data.tx_buffer_size = tx_buffer_size;
    mock_uart_data.event_queue_size = event_queue_size;
    mock_uart_data.uart_queue = uart_queue;
    mock_uart_data.intr_alloc_flags = intr_alloc_flags;

    if (mock_uart_calls.driver_install_result != ESP_OK) {
        return mock_uart_calls.driver_install_result;
    }

    if (uart_queue != NULL) {
        *uart_queue = xQueueCreate(event_queue_size, sizeof(uart_event_t));
        if (*uart_queue == NULL) {
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t uart_num)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_driver_delete should be called with correct port number");

    mock_uart_calls.driver_delete_called++;

    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_param_config should be called with correct port number");
    TEST_ASSERT_NOT_NULL_MESSAGE(uart_config, "uart_param_config called with NULL uart_config");

    mock_uart_calls.param_config_called++;
    memcpy(&mock_uart_data.config, uart_config, sizeof(uart_config_t));

    return mock_uart_calls.param_config_result;
}

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num, int cts_io_num)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_set_pin should be called with correct port number");

    mock_uart_calls.set_pin_called++;
    mock_uart_data.tx_pin = tx_io_num;
    mock_uart_data.rx_pin = rx_io_num;
    mock_uart_data.dir_pin = rts_io_num;
    mock_uart_data.cts_pin = cts_io_num;
    return mock_uart_calls.set_pin_result;
}

esp_err_t uart_set_mode(uart_port_t uart_num, uart_mode_t mode)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_set_mode should be called with correct port number");

    mock_uart_calls.set_mode_called++;
    mock_uart_data.mode = mode;
    return mock_uart_calls.set_mode_result;
}

esp_err_t uart_set_rx_timeout(uart_port_t uart_num, const uint8_t tout_thresh)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_set_rx_timeout should be called with correct port number");

    mock_uart_calls.set_rx_timeout_called++;
    mock_uart_data.rx_timeout = tout_thresh;
    return mock_uart_calls.set_rx_timeout_result;
}

esp_err_t uart_wait_tx_done(uart_port_t uart_num, TickType_t ticks_to_wait)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_wait_tx_done should be called with correct port number");

    mock_uart_calls.wait_tx_done_called++;
    (void)ticks_to_wait;
    return ESP_OK;
}

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_read_bytes should be called with correct port number");

    mock_uart_calls.read_bytes_called++;
    (void)ticks_to_wait;

    if (buf != NULL && length > 0) {
        memset(buf, 0, length);
    }
    return length;
}

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size)
{
    TEST_ASSERT_EQUAL_MESSAGE(MOCK_PORT_NUM_UART1, uart_num, "uart_write_bytes should be called with correct port number");

    mock_uart_calls.write_bytes_called++;
    (void)src;
    return (int)size;
}

void mock_uart_reset(void)
{
    memset(&mock_uart_calls, 0, sizeof(mock_uart_calls));
    memset(&mock_uart_data, 0, sizeof(mock_uart_data));
    mock_uart_calls.driver_install_result = ESP_OK;
    mock_uart_calls.param_config_result = ESP_OK;
    mock_uart_calls.set_pin_result = ESP_OK;
    mock_uart_calls.set_mode_result = ESP_OK;
    mock_uart_calls.set_rx_timeout_result = ESP_OK;
}
