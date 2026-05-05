#ifdef __unittest_env__
    #define malloc test_malloc
    #define free test_free
#endif

#include "serial.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_bit_defs.h"


// Buffer must be larger than the maximum Modbus packet size + fast Modbus arbitration bytes
// However, since the device can work in "transparent" gateway mode, the buffer size should be chosen with a margin
// When the buffer overflows, UART_BUFFER_FULL event will occur
#define SERIAL_BUF_SIZE                 (1000)
#define SERIAL_TASK_STACK_SIZE          (1024 * 4)
#define SERIAL_TASK_PRIORITY            12
#define SERIAL_QUEUE_SIZE               20          // UART event queue size

#define EVENT_TASK_STARTED              BIT0
#define EVENT_TASK_FINISHED             BIT1
#define EVENT_TASK_EXIT_REQ             BIT8

#define SERIAL_EVENT_WAIT_TIMEOUT_MS    50


static const char *TAG = "serial";


typedef struct {
    uint8_t *data;
    size_t data_len;
} buffer_ctx_t;


static void handle_uart_event(serial_desc_t *desc, uart_event_t event, buffer_ctx_t *buffer_ctx)
{
    switch (event.type) {
        case UART_DATA:
            int free_space = (int)SERIAL_BUF_SIZE - (int)buffer_ctx->data_len;
            if (free_space < (int)event.size) {
                ESP_LOGE(TAG, "UART[%d] receive buffer overflow, free: %d, expected: >= %zu", desc->port_num, free_space, event.size);
                uart_flush_input(desc->port_num);
                xQueueReset(desc->uart_queue);
                buffer_ctx->data_len = 0;
                break;
            }
            ESP_LOGD(TAG, "UART[%d] DATA: %zu, TIMEOUT: %u", desc->port_num, event.size, (unsigned)event.timeout_flag);
            uart_read_bytes(desc->port_num, &buffer_ctx->data[buffer_ctx->data_len], event.size, portMAX_DELAY);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, &buffer_ctx->data[buffer_ctx->data_len], event.size, ESP_LOG_DEBUG);
            buffer_ctx->data_len += event.size;
            if (event.timeout_flag) {
                if (desc->receive_handler) {
                    desc->receive_handler(desc, buffer_ctx->data, buffer_ctx->data_len);
                }
                if (desc->sniff_handler) {
                    desc->sniff_handler(desc, buffer_ctx->data, buffer_ctx->data_len);
                }
                buffer_ctx->data_len = 0;
            }
            break;
        case UART_FIFO_OVF:
            ESP_LOGW(TAG, "UART[%d] HW fifo overflow", desc->port_num);
            uart_flush_input(desc->port_num);
            xQueueReset(desc->uart_queue);
            buffer_ctx->data_len = 0;
            break;
        case UART_BUFFER_FULL:
            ESP_LOGW(TAG, "UART[%d] ring buffer full", desc->port_num);
            uart_flush_input(desc->port_num);
            xQueueReset(desc->uart_queue);
            buffer_ctx->data_len = 0;
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

// This task must not be blocked for long in the callback
// Otherwise the UART event queue may overflow,
// causing packets to merge and partially drop
static void uart_event_task(void *pvParameters)
{
    serial_desc_t *desc = (serial_desc_t *)pvParameters;
    xEventGroupSetBits(desc->event_group, EVENT_TASK_STARTED);
    ESP_LOGD(TAG, "UART[%d] event task started", desc->port_num);

    uint8_t *dtmp = (uint8_t *)malloc(SERIAL_BUF_SIZE);
    buffer_ctx_t buffer_ctx = {
        .data = dtmp,
        .data_len = 0
    };

    uart_flush_input(desc->port_num);
    xQueueReset(desc->uart_queue);

    while(1) {
        uart_event_t event;
        BaseType_t result = xQueueReceive(desc->uart_queue, (void *)&event, pdMS_TO_TICKS(SERIAL_EVENT_WAIT_TIMEOUT_MS));
        if (result == pdPASS) {
            handle_uart_event(desc, event, &buffer_ctx);
        }

        EventBits_t bits = xEventGroupWaitBits(desc->event_group, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, 0);
        if (bits & EVENT_TASK_EXIT_REQ) {
            break;
        }
    }

    free(dtmp);
    ESP_LOGI(TAG, "UART[%d] event task finished", desc->port_num);
    xEventGroupSetBits(desc->event_group, EVENT_TASK_FINISHED);
    desc->task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t configure_uart_parameters(serial_config_t *serial_config)
{
    uart_config_t uart_config = {
        .baud_rate = serial_config->baudrate,
        .data_bits = serial_config->databits,
        .parity = serial_config->parity,
        .stop_bits = serial_config->stopbits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(serial_config->port_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART parameters configuring");
        return err;
    }

    err = uart_set_pin(serial_config->port_num, serial_config->tx_pin, serial_config->rx_pin, serial_config->dir_pin, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART pin set");
        return err;
    }

    err = uart_set_mode(serial_config->port_num, UART_MODE_RS485_HALF_DUPLEX);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART mode set");
        return err;
    }

    // Force enable Rx timeout events generation to be able to detect
    // the end of a packet if its length is equal to the UART receive buffer size
    uart_set_always_rx_timeout(serial_config->port_num, true);

    err = uart_set_rx_timeout(serial_config->port_num, SERIAL_RX_TOUT_SNIFFER);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART receive timeout set");
        return err;
    }

    return ESP_OK;
}

serial_desc_t* serial_init(serial_config_t *serial_config, serial_receive_handler_t serial_receive_handler)
{
    if (serial_config == NULL) {
        ESP_LOGE(TAG, "Serial config pointer is NULL");
        return NULL;
    }
    ESP_LOGD(TAG, "UART[%d] initializing...", serial_config->port_num);

    serial_desc_t *desc = malloc(sizeof(serial_desc_t));
    if (!desc) {
        ESP_LOGE(TAG, "Unable to allocate memory for serial_desc_t");
        return NULL;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (event_group == NULL) {
        ESP_LOGE(TAG, "Unable to create event group");
        free(desc);
        return NULL;
    }

    desc->port_num = serial_config->port_num;
    desc->receive_handler = serial_receive_handler;
    desc->sniff_handler = NULL;
    desc->uart_queue = NULL;
    desc->task_handle = NULL;
    desc->event_group = event_group;

    esp_err_t err = uart_driver_install(serial_config->port_num, SERIAL_BUF_SIZE, SERIAL_BUF_SIZE, SERIAL_QUEUE_SIZE, &desc->uart_queue, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error during UART driver installation");
        free(desc);
        vEventGroupDelete(event_group);
        return NULL;
    }

    err = configure_uart_parameters(serial_config);
    if (err != ESP_OK) {
        uart_driver_delete(serial_config->port_num);
        vEventGroupDelete(event_group);
        free(desc);
        return NULL;
    }

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(uart_event_task, "uart_event_task", SERIAL_TASK_STACK_SIZE, desc, SERIAL_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Unable to create UART event task");
        uart_driver_delete(serial_config->port_num);
        vEventGroupDelete(event_group);
        free(desc);
        return NULL;
    }

    desc->task_handle = task_handle;
    xEventGroupWaitBits(desc->event_group, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);

    ESP_LOGD(TAG, "UART[%d] initialized", serial_config->port_num);
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

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols)
{
    esp_err_t err = uart_set_rx_timeout(desc->port_num, tout_symbols);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART[%d] failed to set RX timeout to %u symbols", desc->port_num, tout_symbols);
        return err;
    }
    ESP_LOGD(TAG, "UART[%d] RX timeout set to %u symbols", desc->port_num, tout_symbols);
    return ESP_OK;
}

esp_err_t serial_deinit(serial_desc_t *desc)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (desc->task_handle == NULL || desc->event_group == NULL) {
        ESP_LOGE(TAG, "UART[%d] not initialized", desc->port_num);
        return ESP_ERR_NOT_ALLOWED;
    }

    ESP_LOGD(TAG, "UART[%d] deinitializing...", desc->port_num);

    xEventGroupSetBits(desc->event_group, EVENT_TASK_EXIT_REQ);
    EventBits_t bits = xEventGroupWaitBits(desc->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);
    if (!(bits & EVENT_TASK_FINISHED)) {
        return ESP_FAIL;
    }

    uart_driver_delete(desc->port_num);
    vEventGroupDelete(desc->event_group);

    ESP_LOGD(TAG, "UART[%d] deinitialized", desc->port_num);
    free(desc);

    return ESP_OK;
}
