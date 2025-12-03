#include "transparent_tcp.h"

#include <string.h>
#include <esp_log.h>
#include "serial.h"
#include "tcp_server.h"
#include "tcp_client.h"
#include "rs485_stats.h"
#include "bridge.h"


#define TRANSPARENT_TCP_MAX_TASK_COUNT      BRIDGES_COUNT   // Maximum number of tasks (ports)


typedef esp_err_t (*tcp_send_func_t)(tcp_desc_t *desc, uint8_t *data, size_t len);
typedef esp_err_t (*tcp_connected_func_t)(tcp_desc_t *desc);
typedef esp_err_t (*tcp_deinit_func_t)(tcp_desc_t *desc);

typedef struct {
    bool initialized;
    unsigned index;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bridge_mode_t mode;
    tcp_send_func_t tcp_send_func;
    tcp_connected_func_t tcp_connected_func;
    tcp_deinit_func_t tcp_deinit_func;
} transp_tcp_task_ctx_t;


static const char *TAG = "transparent_tcp";

static transp_tcp_task_ctx_t transp_tcp_task_ctx[TRANSPARENT_TCP_MAX_TASK_COUNT] = {0};


// Find context by serial_desc_t descriptor
static transp_tcp_task_ctx_t* find_ctx_by_serial_desc(const serial_desc_t* serial_desc)
{
    for (unsigned i = 0; i < TRANSPARENT_TCP_MAX_TASK_COUNT; i++) {
        if (transp_tcp_task_ctx[i].serial_desc == serial_desc) {
            return &transp_tcp_task_ctx[i];
        }
    }
    return 0;
}


// Find context by tcp_desc_t descriptor
static transp_tcp_task_ctx_t* find_ctx_by_tcp_desc(const tcp_desc_t* tcp_desc)
{
    for (unsigned i = 0; i < TRANSPARENT_TCP_MAX_TASK_COUNT; i++) {
        if (transp_tcp_task_ctx[i].tcp_desc == tcp_desc) {
            return &transp_tcp_task_ctx[i];
        }
    }
    return 0;
}


// Callback function for receiving data from serial port
static void process_data_from_serial(serial_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "Received data from serial, length: %u", len);

    transp_tcp_task_ctx_t* ctx = find_ctx_by_serial_desc(desc);
    if (!ctx) {
        ESP_LOGE(TAG, "Unknown serial_desc in process_data_from_serial()");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not initialized, skipping packet");
        return;
    }

    ESP_LOGD(TAG, "Port[%u]: Received %u bytes from serial, sending them to TCP", ctx->index + 1, len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t conn = ctx->tcp_connected_func(ctx->tcp_desc);
    if (conn != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: No TCP connection, packet from serial will be skipped", ctx->index + 1);
        return;
    }

    esp_err_t err = ctx->tcp_send_func(ctx->tcp_desc, data, len);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to send data to TCP from serial", ctx->index + 1);
    }
}


// Callback function for receiving data from TCP socket
static void process_data_from_tcp(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "Received data from TCP, length: %u", len);

    transp_tcp_task_ctx_t* ctx = find_ctx_by_tcp_desc(desc);
    if (!ctx) {
        ESP_LOGE(TAG, "Unknown tcp_desc in process_data_from_tcp()");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not initialized, skipping packet");
        return;
    }

    if (desc->client_sock < 0) {
        ESP_LOGE(TAG, "Port[%u]: no client connected", ctx->index + 1);
        return;
    }

    ESP_LOGD(TAG, "Port[%u]: Received %u bytes from TCP, sending them to serial", ctx->index + 1, len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t err = serial_send(ctx->serial_desc, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to send data to serial from TCP", ctx->index + 1);
    }
}


esp_err_t transparent_tcp_init_port(unsigned index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (index >= TRANSPARENT_TCP_MAX_TASK_COUNT) {
        ESP_LOGE(TAG, "Port[%u]: Port number out of range", index + 1);
        return ESP_ERR_INVALID_ARG;
    }

    transp_tcp_task_ctx_t* ctx = &transp_tcp_task_ctx[index];
    if (ctx->initialized) {
        ESP_LOGW(TAG, "Port[%u]: Already initialized", index + 1);
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Port[%u]: Initializing in transparent TCP mode...", index + 1);

    esp_err_t err = ESP_OK;
    tcp_send_func_t tcp_send_func = 0;
    tcp_connected_func_t tcp_connected_func = 0;
    tcp_deinit_func_t tcp_deinit_func = 0;

    switch (mode) {
        case BRIDGE_MODE_SERVER:
            err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
            tcp_send_func = tcp_server_send;
            tcp_connected_func = tcp_server_connected;
            tcp_deinit_func = tcp_server_deinit;
            break;
        case BRIDGE_MODE_CLIENT:
            err = tcp_client_init(ip, port, process_data_from_tcp, tcp_desc);
            tcp_send_func = tcp_client_send;
            tcp_connected_func = tcp_client_connected;
            tcp_deinit_func = tcp_client_deinit;
            break;
        default:
            ESP_LOGE(TAG, "Port[%u]: Unknown bridge mode: %d", index + 1, mode);
            return ESP_FAIL;
    }

    if (err != ESP_OK) {
        return err;
    }

    *serial_desc = serial_init(config, process_data_from_serial);

    if (!*serial_desc) {
        ESP_LOGE(TAG, "Port[%u]: Failed to initialize serial port", index + 1);
        return ESP_FAIL;
    }

    ctx->index = index;
    ctx->serial_desc = *serial_desc;
    ctx->tcp_desc = *tcp_desc;
    ctx->mode = mode;
    ctx->tcp_send_func = tcp_send_func;
    ctx->tcp_connected_func = tcp_connected_func;
    ctx->tcp_deinit_func = tcp_deinit_func;

    ctx->initialized = 1;

    ESP_LOGD(TAG, "Port[%u]: Initialized", index + 1);
    return ESP_OK;
}

esp_err_t transparent_tcp_deinit_port(unsigned index)
{
    if (index >= TRANSPARENT_TCP_MAX_TASK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Port[%u]: Deinitializing...", index + 1);

    transp_tcp_task_ctx_t* ctx = &transp_tcp_task_ctx[index];
    ctx->initialized = 0;

    ctx->tcp_deinit_func(ctx->tcp_desc);
    serial_deinit(ctx->serial_desc);

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}
