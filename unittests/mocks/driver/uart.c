#include "uart.h"
#include <string.h>

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    (void)uart_num;
    (void)ticks_to_wait;

    if (buf != NULL && length > 0) {
        memset(buf, 0, length);
    }
    return length;
}

esp_err_t uart_flush_input(uart_port_t uart_num)
{
    (void)uart_num;
    return ESP_OK;
}

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size,
                              int tx_buffer_size, int event_queue_size,
                              QueueHandle_t *uart_queue, int intr_alloc_flags)
{
    (void)uart_num;
    (void)rx_buffer_size;
    (void)tx_buffer_size;
    (void)event_queue_size;
    (void)intr_alloc_flags;

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
    (void)uart_num;
    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config)
{
    (void)uart_num;
    (void)uart_config;
    return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num, int cts_io_num)
{
    (void)uart_num;
    (void)tx_io_num;
    (void)rx_io_num;
    (void)rts_io_num;
    (void)cts_io_num;
    return ESP_OK;
}

esp_err_t uart_set_mode(uart_port_t uart_num, uart_mode_t mode)
{
    (void)uart_num;
    (void)mode;
    return ESP_OK;
}

esp_err_t uart_set_rx_timeout(uart_port_t uart_num, const uint8_t tout_thresh)
{
    (void)uart_num;
    (void)tout_thresh;
    return ESP_OK;
}

esp_err_t uart_wait_tx_done(uart_port_t uart_num, TickType_t ticks_to_wait)
{
    (void)uart_num;
    (void)ticks_to_wait;
    return ESP_OK;
}

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size)
{
    (void)uart_num;
    (void)src;
    return (int)size;
}
