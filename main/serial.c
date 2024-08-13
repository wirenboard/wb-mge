#include "serial.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "serial";

#define SERIAL_PORT_NUM 2
#if CONFIG_IDF_TARGET_ESP32
#define SERIAL_INPUT_PIN GPIO_NUM_22
#define SERIAL_OUTPUT_PIN GPIO_NUM_23
#define SERIAL_IO_PIN GPIO_NUM_12
#elif CONFIG_IDF_TARGET_ESP32S3
#define SERIAL_INPUT_PIN GPIO_NUM_18
#define SERIAL_OUTPUT_PIN GPIO_NUM_17
#define SERIAL_IO_PIN GPIO_NUM_8
#endif

#define SERIAL_BUF_SIZE (1000)
#define ECHO_READ_TOUT (30) // (3) 3.5T * 8 = 28 ticks, TOUT=3 -> ~24..33 ticks

serial_receive_handler_t receive_handler = NULL;
static QueueHandle_t uart_queue = NULL;

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t *dtmp = (uint8_t *)malloc(SERIAL_BUF_SIZE);
    uart_flush_input(SERIAL_PORT_NUM);

    for (;;)
    {
        if (xQueueReceive(uart_queue, (void *)&event, (TickType_t)portMAX_DELAY))
        {
            bzero(dtmp, SERIAL_BUF_SIZE);
            ESP_LOGD(TAG, "uart[%d] event:", SERIAL_PORT_NUM);
            switch (event.type)
            {
            case UART_DATA:
                ESP_LOGD(TAG, "[UART DATA]: %d", event.size);
                uart_read_bytes(SERIAL_PORT_NUM, dtmp, event.size, portMAX_DELAY);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, dtmp, event.size, ESP_LOG_DEBUG);
                receive_handler(dtmp, event.size);
                break;
            case UART_FIFO_OVF:
                ESP_LOGD(TAG, "hw fifo overflow");
                uart_flush_input(SERIAL_PORT_NUM);
                xQueueReset(uart_queue);
                break;
            case UART_BUFFER_FULL:
                ESP_LOGD(TAG, "ring buffer full");
                uart_flush_input(SERIAL_PORT_NUM);
                xQueueReset(uart_queue);
                break;
            case UART_BREAK:
                ESP_LOGD(TAG, "uart rx break");
                break;
            case UART_PARITY_ERR:
                ESP_LOGD(TAG, "uart parity error");
                break;
            case UART_FRAME_ERR:
                ESP_LOGD(TAG, "uart frame error");
                break;
            default:
                ESP_LOGD(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
    free(dtmp);
    dtmp = NULL;
    vTaskDelete(NULL);
}

esp_err_t serial_init(uart_config_t *uart_config, serial_receive_handler_t serial_receive_handler)
{
    esp_err_t err = ESP_OK;
    if (serial_receive_handler == NULL)
    {
        ESP_LOGE(TAG, "Serial receive handler is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    receive_handler = serial_receive_handler;
    err = uart_driver_install(SERIAL_PORT_NUM, SERIAL_BUF_SIZE * 2, SERIAL_BUF_SIZE * 2, 20, &uart_queue, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(SERIAL_PORT_NUM, uart_config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(SERIAL_PORT_NUM, SERIAL_OUTPUT_PIN, SERIAL_INPUT_PIN, SERIAL_IO_PIN, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_mode(SERIAL_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_rx_timeout(SERIAL_PORT_NUM, ECHO_READ_TOUT);
    if (err != ESP_OK) {
        return err;
    }

    xTaskCreate(uart_event_task, "uart_event_task", 1024 * 4, NULL, 12, NULL);

    return err;
}

esp_err_t serial_send(uint8_t *data, uint8_t len){
    return uart_write_bytes(SERIAL_PORT_NUM, (const char*) data, len);
}
