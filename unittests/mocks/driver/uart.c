#include "unity.h"

#include "uart.h"
#include <string.h>
#include <stdlib.h>

mock_uart_flush_input_t mock_uart_flush_input_data = {0};
mock_uart_driver_install_t mock_uart_driver_install_data = {0};
mock_uart_driver_delete_t mock_uart_driver_delete_data = {0};
mock_uart_param_config_t mock_uart_param_config_data = {0};
mock_uart_set_pin_t mock_uart_set_pin_data = {0};
mock_uart_set_mode_t mock_uart_set_mode_data = {0};
mock_uart_set_rx_timeout_t mock_uart_set_rx_timeout_data = {0};
mock_uart_wait_tx_done_t mock_uart_wait_tx_done_data = {0};
mock_uart_read_bytes_t mock_uart_read_bytes_data = {0};
mock_uart_write_bytes_t mock_uart_write_bytes_data = {0};

esp_err_t uart_flush_input(uart_port_t uart_num)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_flush_input called with invalid uart_num");

    mock_uart_flush_input_data.called++;
    mock_uart_flush_input_data.uart_num = uart_num;

    return ESP_OK;
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int event_queue_size,
                              QueueHandle_t *uart_queue, int intr_alloc_flags)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_driver_install called with invalid uart_num");

    mock_uart_driver_install_data.called++;
    mock_uart_driver_install_data.uart_num = uart_num;
    mock_uart_driver_install_data.rx_buffer_size = rx_buffer_size;
    mock_uart_driver_install_data.tx_buffer_size = tx_buffer_size;
    mock_uart_driver_install_data.event_queue_size = event_queue_size;
    mock_uart_driver_install_data.uart_queue = uart_queue;
    mock_uart_driver_install_data.intr_alloc_flags = intr_alloc_flags;

    if (mock_uart_driver_install_data.result != ESP_OK) {
        return mock_uart_driver_install_data.result;
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
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_driver_delete called with invalid uart_num");

    mock_uart_driver_delete_data.called++;
    mock_uart_driver_delete_data.uart_num = uart_num;

    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_param_config called with invalid uart_num");
    TEST_ASSERT_NOT_NULL_MESSAGE(uart_config, "uart_param_config called with NULL uart_config");

    mock_uart_param_config_data.called++;
    mock_uart_param_config_data.uart_num = uart_num;
    memcpy(&mock_uart_param_config_data.config, uart_config, sizeof(uart_config_t));

    return mock_uart_param_config_data.result;
}

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num, int cts_io_num)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_set_pin called with invalid uart_num");

    mock_uart_set_pin_data.called++;
    mock_uart_set_pin_data.uart_num = uart_num;
    mock_uart_set_pin_data.tx_pin = tx_io_num;
    mock_uart_set_pin_data.rx_pin = rx_io_num;
    mock_uart_set_pin_data.dir_pin = rts_io_num;
    mock_uart_set_pin_data.cts_pin = cts_io_num;

    return mock_uart_set_pin_data.result;
}

esp_err_t uart_set_mode(uart_port_t uart_num, uart_mode_t mode)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_set_mode called with invalid uart_num");

    mock_uart_set_mode_data.called++;
    mock_uart_set_mode_data.uart_num = uart_num;
    mock_uart_set_mode_data.mode = mode;

    return mock_uart_set_mode_data.result;
}

esp_err_t uart_set_rx_timeout(uart_port_t uart_num, const uint8_t tout_thresh)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_set_rx_timeout called with invalid uart_num");

    mock_uart_set_rx_timeout_data.called++;
    mock_uart_set_rx_timeout_data.uart_num = uart_num;
    mock_uart_set_rx_timeout_data.rx_timeout = tout_thresh;

    return mock_uart_set_rx_timeout_data.result;
}

esp_err_t uart_wait_tx_done(uart_port_t uart_num, TickType_t ticks_to_wait)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_wait_tx_done called with invalid uart_num");

    mock_uart_wait_tx_done_data.called++;
    mock_uart_wait_tx_done_data.uart_num = uart_num;
    mock_uart_wait_tx_done_data.ticks_to_wait = ticks_to_wait;

    return mock_uart_wait_tx_done_data.result;
}

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_read_bytes called with invalid uart_num");
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, "uart_read_bytes called with NULL buffer");

    mock_uart_read_bytes_data.called++;
    mock_uart_read_bytes_data.uart_num = uart_num;
    mock_uart_read_bytes_data.length = length;
    mock_uart_read_bytes_data.ticks_to_wait = ticks_to_wait;
    strncpy((char *)buf, MOCK_DATA_FROM_UART_READ, length);

    return (int)length;
}

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size)
{
    TEST_ASSERT_TRUE_MESSAGE(uart_num < UART_NUM_MAX, "uart_write_bytes called with invalid uart_num");
    TEST_ASSERT_NOT_NULL_MESSAGE(src, "uart_write_bytes called with NULL source buffer");

    mock_uart_write_bytes_data.called++;
    mock_uart_write_bytes_data.uart_num = uart_num;
    mock_uart_write_bytes_data.src = (void *)src;
    mock_uart_write_bytes_data.size = size;

    if (mock_uart_write_bytes_data.return_value != 0) {
        return mock_uart_write_bytes_data.return_value;
    }

    return (int)size;
}

void mock_uart_reset(void)
{
    memset(&mock_uart_flush_input_data, 0, sizeof(mock_uart_flush_input_data));
    memset(&mock_uart_driver_install_data, 0, sizeof(mock_uart_driver_install_data));
    memset(&mock_uart_driver_delete_data, 0, sizeof(mock_uart_driver_delete_data));
    memset(&mock_uart_param_config_data, 0, sizeof(mock_uart_param_config_data));
    memset(&mock_uart_set_pin_data, 0, sizeof(mock_uart_set_pin_data));
    memset(&mock_uart_set_mode_data, 0, sizeof(mock_uart_set_mode_data));
    memset(&mock_uart_set_rx_timeout_data, 0, sizeof(mock_uart_set_rx_timeout_data));
    memset(&mock_uart_wait_tx_done_data, 0, sizeof(mock_uart_wait_tx_done_data));
    memset(&mock_uart_read_bytes_data, 0, sizeof(mock_uart_read_bytes_data));
    memset(&mock_uart_write_bytes_data, 0, sizeof(mock_uart_write_bytes_data));
}
