#include "transparent_tcp.h"

#include <string.h>
#include <esp_log.h>
#include "serial.h"
#include "tcp_server.h"
#include "tcp_client.h"
#include "rs485_busy_monitor.h"

//------------------------------------------------------------------------------

#define TRANSPARENT_TCP_DEBUG_LOG_ENABLE        1

#define TRANSPARENT_TCP_MAX_TASK_COUNT          2           // Максимальное количество задач (портов)

//------------------------------------------------------------------------------

typedef esp_err_t (*tcp_send_func_t)(tcp_desc_t *desc, uint8_t *data, size_t len);

typedef struct {
    bool initialized;
    int index;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bridge_mode_t mode;
    tcp_send_func_t tcp_send_func;
} transp_tcp_task_ctx_t;

//------------------------------------------------------------------------------

static const char *TAG = "transparent_tcp";

static transp_tcp_task_ctx_t transp_tcp_task_ctx[TRANSPARENT_TCP_MAX_TASK_COUNT] = {0};
static int transp_tcp_task_count = 0;

//------------------------------------------------------------------------------

// Поиск контекста по дескриптору serial_desc_t
static transp_tcp_task_ctx_t* find_ctx_by_serial_desc(const serial_desc_t* serial_desc)
{
    for (int i = 0; i < transp_tcp_task_count; i++) {
        if (transp_tcp_task_ctx[i].serial_desc == serial_desc) {
            return &transp_tcp_task_ctx[i];
        }
    }
    return 0;
}

// Поиск контекста по дескриптору tcp_desc_t
static transp_tcp_task_ctx_t* find_ctx_by_tcp_desc(const tcp_desc_t* tcp_desc)
{
    for (int i = 0; i < transp_tcp_task_count; i++) {
        if (transp_tcp_task_ctx[i].tcp_desc == tcp_desc) {
            return &transp_tcp_task_ctx[i];
        }
    }
    return 0;
}

//------------------------------------------------------------------------------

// Callback-функция приема данных из последовательного порта
static void process_data_from_serial(serial_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "Received data from serial, length: %u", len);

    transp_tcp_task_ctx_t* ctx = find_ctx_by_serial_desc(desc);
    if (!ctx) {
        ESP_LOGE(TAG, "Unknown serial_desc in process_data_from_serial()");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not fully initialized, skipping packet");
        return;
    }

    ESP_LOGD(TAG, "Received %d bytes from serial port %d, sending them to TCP", len, desc->port_num);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t err = ctx->tcp_send_func(ctx->tcp_desc, data, len);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data to TCP from serial port %d", desc->port_num);
    }
}

//------------------------------------------------------------------------------

// Callback-функция приема данных из TCP-сокета
static void process_data_from_tcp(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "Received data from TCP, length: %u", len);

    transp_tcp_task_ctx_t* ctx = find_ctx_by_tcp_desc(desc);
    if (!ctx) {
        ESP_LOGE(TAG, "Unknown tcp_desc in process_data_from_tcp()");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not fully initialized, skipping packet");
        return;
    }

    if (desc->client_sock < 0) {
        ESP_LOGE(TAG, "%s: no client connected", __func__);
        return;
    }

    ESP_LOGD(TAG, "Received %d bytes from TCP, sending them to serial port %d", len, ctx->serial_desc->port_num);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t err = serial_send(ctx->serial_desc, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send data to serial port %d from TCP", ctx->serial_desc->port_num);
    }
}

//------------------------------------------------------------------------------

esp_err_t transparent_tcp_init_port(int index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (TRANSPARENT_TCP_DEBUG_LOG_ENABLE) {
        esp_log_level_set(TAG, ESP_LOG_DEBUG);
    }

    if (transp_tcp_task_count >= TRANSPARENT_TCP_MAX_TASK_COUNT) {
        ESP_LOGE(TAG, "Task count limit reached");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    tcp_send_func_t tcp_send_func = 0;

    switch (mode) {
        case BRIDGE_MODE_SERVER:
            err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
            tcp_send_func = tcp_server_send;
            break;
        case BRIDGE_MODE_CLIENT:
            err = tcp_client_init(ip, port, process_data_from_tcp, tcp_desc);
            tcp_send_func = tcp_client_send;
            break;
        default:
            ESP_LOGE(TAG, "Unknown bridge mode: %d", mode);
            return ESP_FAIL;
    }

    if (err != ESP_OK) {
        return err;
    }

    *serial_desc = serial_init(config, process_data_from_serial);

    if (!*serial_desc) {
        ESP_LOGE(TAG, "Failed to initialize serial port");
        return ESP_FAIL;
    }

    transp_tcp_task_ctx_t* ctx = &transp_tcp_task_ctx[transp_tcp_task_count];
    ctx->index = index;
    ctx->serial_desc = *serial_desc;
    ctx->tcp_desc = *tcp_desc;
    ctx->mode = mode;
    ctx->tcp_send_func = tcp_send_func;

    transp_tcp_task_count++;
    ctx->initialized = 1;

    return ESP_OK;
}

//------------------------------------------------------------------------------
