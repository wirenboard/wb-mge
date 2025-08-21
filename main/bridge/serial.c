#include "serial.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// буфер должен быть больше, чем максимальный размер пакета modbus + байты арбитража быстрого modbus
// но, так как устройство может работать в режиме "прозрачного" шлюза, то размер буфера
// стоит выбирать с запасом
// при переполнении буфера возникнет событие UART_BUFFER_FULL
#define SERIAL_BUF_SIZE             (1000)
#define SERIAL_READ_TOUT            5           // Задержка приема UART в символах (1 символ ~= 11 бит), с запасом
#define SERIAL_TASK_STACK_SIZE      (1024 * 4)  // TODO: check stack size
#define SERIAL_TASK_PRIORITY        12

static const char *TAG = "serial";

// Нельзя эту задачу надолго блокировать в колбэке
// Иначе может переполниться очередь событий UART,
// из-за чего пакеты начнут склеиваться и частично дропаться
static void uart_event_task(void *pvParameters)
{
    serial_desc_t *desc = (serial_desc_t *)pvParameters;
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *)malloc(SERIAL_BUF_SIZE);
    uart_flush_input(desc->port_num);

    for (;;) {
        if (xQueueReceive(desc->uart_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            memset(dtmp, 0, SERIAL_BUF_SIZE);

            switch (event.type) {
                case UART_DATA:
                    ESP_LOGD(TAG, "[UART[%d] DATA]: %d", desc->port_num, event.size);
                    uart_read_bytes(desc->port_num, dtmp, event.size, portMAX_DELAY);
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, dtmp, event.size, ESP_LOG_DEBUG);
                    desc->receive_handler(desc, dtmp, event.size);
                    break;
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART[%d] HW fifo overflow", desc->port_num);
                    uart_flush_input(desc->port_num);
                    xQueueReset(desc->uart_queue);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART[%d] ring buffer full", desc->port_num);
                    uart_flush_input(desc->port_num);
                    xQueueReset(desc->uart_queue);
                    break;
                case UART_BREAK:
                    ESP_LOGD(TAG, "UART[%d] rx break", desc->port_num);
                    break;
                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "UART[%d] parity error", desc->port_num);
                    break;
                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART[%d] frame error", desc->port_num);
                    break;
                default:
                    ESP_LOGW(TAG, "UART[%d] not handled event type: %d", desc->port_num, event.type);
                    break;
            }
        }
    }

    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

serial_desc_t* serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler)
{
    if (serial_receive_handler == NULL) {
        ESP_LOGE(TAG, "Serial receive handler is NULL");
        return NULL;
    }

    serial_desc_t *desc = malloc(sizeof(serial_desc_t));

    if (!desc) {
        ESP_LOGE(TAG, "Unable to allocate memory for serial_desc_t");
        return NULL;
    }

    desc->port_num = serial_config->port_num;
    desc->receive_handler = serial_receive_handler;
    desc->uart_queue = NULL;
    esp_err_t err = uart_driver_install(serial_config->port_num, SERIAL_BUF_SIZE, SERIAL_BUF_SIZE, 20, &desc->uart_queue, 0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART driver installation");
        free(desc); return NULL;
    }

    uart_config_t uart_config = {
        .baud_rate = serial_config->baudrate,
        .data_bits = serial_config->databits,
        .parity = serial_config->parity,
        .stop_bits = serial_config->stopbits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_param_config(serial_config->port_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART parameters configuring");
        free(desc); return NULL;
    }
    err = uart_set_pin(serial_config->port_num, serial_config->tx_pin, serial_config->rx_pin, serial_config->dir_pin, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART pin set");
        free(desc); return NULL;
    }
    err = uart_set_mode(serial_config->port_num, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART mode set");
        free(desc); return NULL;
    }
    err = uart_set_rx_timeout(serial_config->port_num, SERIAL_READ_TOUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART receive timeout set");
        free(desc); return NULL;
    }

    xTaskCreate(uart_event_task, "uart_event_task", SERIAL_TASK_STACK_SIZE, desc, SERIAL_TASK_PRIORITY, NULL);
    return desc;
}

esp_err_t serial_send(serial_desc_t *desc, uint8_t *data, size_t len)
{
    int written = uart_write_bytes(desc->port_num, (const char *)data, len);

    if (written != len) {
        ESP_LOGE(TAG, "Error sending data to serial port %d", desc->port_num);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t serial_wait_tx_done(serial_desc_t *desc, TickType_t timeout_ticks)
{
    return uart_wait_tx_done(desc->port_num, timeout_ticks);
}
