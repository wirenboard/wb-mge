#include "modbus_tcp.h"
#include <string.h>
#include <esp_log.h>
#include "serial.h"
#include "tcp_server.h"
#include "modbus_helpers.h"
#include "packet_queue.h"
#include "rs485_stats.h"

//------------------------------------------------------------------------------

#define MODBUS_TCP_DEBUG_LOG_ENABLE         0           // TODO: Возможно, вынести в настройки

#define MODBUS_TCP_TASK_STACK_SIZE          3072        // Размер стека каждой задачи
#define MODBUS_TCP_TASK_PRIORITY            4           // Приоритет задач
#define MODBUS_TCP_MAX_TASK_COUNT           2           // Максимальное количество задач (портов)

#define MODBUS_TCP_QUEUE_LEN                10          // Длина очереди запросов от клиентов

#define MODBUS_TCP_SEND_BUFFER_SIZE         1024        // Размер буфера передачи для TCP и RTU пакетов

#define EVENT_SERIAL_RESPONSE_RECEIVED      (1 << 0)    // Флаг события: serial-порт получил пакет с ответом

//------------------------------------------------------------------------------

typedef struct {
    bool initialized;
    int index;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bridge_mode_t mode;
    packet_queue_handle tcp_queue;
    EventGroupHandle_t event_group;
    uint16_t pending_tid;
    unsigned resp_timeout_ticks;
} mb_tcp_task_ctx_t;

//------------------------------------------------------------------------------

static const char *TAG = "modbus_tcp";

static mb_tcp_task_ctx_t mb_tcp_task_ctx[MODBUS_TCP_MAX_TASK_COUNT] = {0};
static int mb_tcp_task_count = 0;

//------------------------------------------------------------------------------

// Поиск контекста по дескриптору serial_desc_t
static mb_tcp_task_ctx_t* find_ctx_by_serial_desc(const serial_desc_t* serial_desc)
{
    for (int i = 0; i < mb_tcp_task_count; i++) {
        if (mb_tcp_task_ctx[i].serial_desc == serial_desc) {
            return &mb_tcp_task_ctx[i];
        }
    }
    return 0;
}

// Поиск контекста по дескриптору tcp_desc_t
static mb_tcp_task_ctx_t* find_ctx_by_tcp_desc(const tcp_desc_t* tcp_desc)
{
    for (int i = 0; i < mb_tcp_task_count; i++) {
        if (mb_tcp_task_ctx[i].tcp_desc == tcp_desc) {
            return &mb_tcp_task_ctx[i];
        }
    }
    return 0;
}

//------------------------------------------------------------------------------

// Разделение Modbus TCP запросов в TCP-потоке данных и помещение их в очередь
// Возвращает количество запросов, помещенных в очередь
static unsigned separate_and_push_requests_from_tcp(mb_tcp_task_ctx_t* ctx, const uint8_t* data, size_t len)
{
    unsigned count = 0;
    size_t pos = 0;

    while (pos < len) {
        const uint8_t* req_data = &data[pos];
        mb_tcp_header_t* header = (mb_tcp_header_t*)req_data;
        size_t req_len = modbus_swap16(header->length) + offsetof(mb_tcp_header_t, unit_id);
        if ((req_len + pos) > len) {
            ESP_LOGW(TAG, "Port[%d]: TCP packet with incorrect length will be skipped", ctx->index + 1);
            break;
        }
        if (modbus_tcp_check_request(req_data, req_len) != 0) {
            ESP_LOGW(TAG, "Port[%d]: Incorrect TCP packet will be skipped", ctx->index + 1);
            break;
        }
        int queue_res = packet_queue_push(ctx->tcp_queue, req_data, req_len);
        if (queue_res != 0) {
            break;
        }
        pos += req_len;
        count++;
    }
    if (pos < len) {
        ESP_LOGW(TAG, "Port[%d]: Not all data in the TCP packet was processed", ctx->index + 1);
    }
    return count;
}

//------------------------------------------------------------------------------

// Callback-функция приема данных из последовательного порта
static void process_data_from_serial(serial_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "Received data from serial, length: %u", len);

    mb_tcp_task_ctx_t* ctx = find_ctx_by_serial_desc(desc);
    if (!ctx) {
        ESP_LOGE(TAG, "Unknown serial_desc in process_data_from_serial()");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not fully initialized, skipping RTU packet");
        return;
    }

    // TODO: Add fast modbus support (0xFF truncation)

    ESP_LOGD(TAG, "Port[%d]: Processing data from serial port", ctx->index + 1);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    int check_res = modbus_rtu_check_response(data, len, 0);
    if (check_res != 0) {
        return;
    }

    if (tcp_server_connected(ctx->tcp_desc) != ESP_OK) {
        ESP_LOGW(TAG, "Port[%d]: No TCP client connected, skipping RTU packet", ctx->index + 1);
        return;
    }

    uint8_t* tcp_resp_buf = malloc(MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_buf) {
        ESP_LOGE(TAG, "Port[%d]: Failed to create TCP send buffer", ctx->index + 1);
        return;
    }

    size_t tcp_resp_len = modbus_tcp_from_rtu(ctx->pending_tid, data, len, tcp_resp_buf, MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_len) {
        return;
    }

    xEventGroupSetBits(ctx->event_group, EVENT_SERIAL_RESPONSE_RECEIVED);

    ESP_LOGD(TAG, "Port[%d]: Sending TCP response, length: %u", ctx->index + 1, tcp_resp_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, tcp_resp_buf, tcp_resp_len, ESP_LOG_DEBUG);

    tcp_server_send(ctx->tcp_desc, tcp_resp_buf, tcp_resp_len);
    free(tcp_resp_buf);
}

// Callback-функция приема данных из TCP-сокета
static void process_data_from_tcp(tcp_desc_t *desc, uint8_t *data, size_t len)
{
    ESP_LOGD(TAG, "Received data from TCP, length: %u", len);

    mb_tcp_task_ctx_t* ctx = find_ctx_by_tcp_desc(desc);
    if (!ctx) {
        ESP_LOGE(TAG, "Unknown tcp_desc in process_data_from_tcp()");
        return;
    }
    if (!ctx->initialized) {
        ESP_LOGW(TAG, "Context is not fully initialized, skipping TCP packet");
        return;
    }

    unsigned count = separate_and_push_requests_from_tcp(ctx, data, len);
    if (!count) {
        rs485_stats_update(ctx->index, 1, 0);
    }
}

//------------------------------------------------------------------------------

// Ожидание активного TCP подключения
static void wait_tcp_connection(const mb_tcp_task_ctx_t* ctx)
{
    bool wait_conn = 0;

    while (tcp_server_connected(ctx->tcp_desc) != ESP_OK) {
        if (!wait_conn) {
            ESP_LOGI(TAG, "Waiting for TCP connection...");
            packet_queue_clear(ctx->tcp_queue);
            wait_conn = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Задержка, чтобы не вешать систему
    }
}

//------------------------------------------------------------------------------

// Задача для работы в режиме Modbus TCP сервера
static void modbus_tcp_server_task(void *arg)
{
    mb_tcp_task_ctx_t* ctx = (mb_tcp_task_ctx_t*)arg;

    ESP_LOGD(TAG, "Port[%d]: Started modbus_tcp_server_task()", ctx->index + 1);

    while (1)
    {
        wait_tcp_connection(ctx);

        uint8_t* tcp_req_buf = 0;
        size_t tcp_req_len = packet_queue_pop(ctx->tcp_queue, &tcp_req_buf, portMAX_DELAY);
        if (!tcp_req_len) {
            continue;
        }

        // Принятый пакет уже проверен в колбэке process_data_from_tcp()

        ESP_LOGD(TAG, "Port[%d]: Fetch TCP request from queue, length: %u", ctx->index + 1, tcp_req_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, tcp_req_buf, tcp_req_len, ESP_LOG_DEBUG);

        // TODO: Add special commands detection and response (e.g. fast modbus support probe)

        uint8_t* rtu_req_buf = malloc(MODBUS_TCP_SEND_BUFFER_SIZE);
        if (!rtu_req_buf) {
            ESP_LOGE(TAG, "Port[%d]: Unable to create TCP send buffer", ctx->index + 1);
            free(tcp_req_buf);
            continue;
        }

        size_t rtu_req_len = modbus_rtu_from_tcp(tcp_req_buf, tcp_req_len, rtu_req_buf, MODBUS_TCP_SEND_BUFFER_SIZE);

        ctx->pending_tid = modbus_swap16(((mb_tcp_header_t*)tcp_req_buf)->transaction_id);
        free(tcp_req_buf);

        if (!rtu_req_len) {
            ESP_LOGE(TAG, "Port[%d]: Failed to create RTU request from TCP", ctx->index + 1);
            continue;
        }

        ESP_LOGD(TAG, "Port[%d]: Sending RTU request, length: %u", ctx->index + 1, rtu_req_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, rtu_req_buf, rtu_req_len, ESP_LOG_DEBUG);

        rs485_busy_monitor_update_activity(ctx->index);

        esp_err_t send_result = serial_send(ctx->serial_desc, rtu_req_buf, rtu_req_len);
        free(rtu_req_buf);
        if (send_result != ESP_OK) {
            ESP_LOGE(TAG, "Port[%d]: Failed to send serial data", ctx->index + 1);
            continue;
        }

        xEventGroupClearBits(ctx->event_group, EVENT_SERIAL_RESPONSE_RECEIVED);

        // Waiting for end of serial transmission
        serial_wait_tx_done(ctx->serial_desc, portMAX_DELAY);

        EventBits_t bits = xEventGroupWaitBits(ctx->event_group, EVENT_SERIAL_RESPONSE_RECEIVED,
                                                pdTRUE, pdTRUE, ctx->resp_timeout_ticks);
        if (bits & EVENT_SERIAL_RESPONSE_RECEIVED) {
            rs485_stats_update(ctx->index, 1, 1);
        } else {
            rs485_stats_update(ctx->index, 1, 0);
        }

    } // while (1)
}

//------------------------------------------------------------------------------

static unsigned calc_response_timeout_ticks(unsigned baudrate)
{
    static const unsigned max_resp_len = 256 + 5;
    static const unsigned bit_count = 10;

    unsigned bytes_rate = baudrate / bit_count;
    unsigned timeout_ms = ((1000 * max_resp_len) + bytes_rate - 1) / bytes_rate;

    return pdMS_TO_TICKS(timeout_ms);
}

//------------------------------------------------------------------------------

esp_err_t modbus_tcp_init_port(int index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (mode != BRIDGE_MODE_SERVER) {
        ESP_LOGE(TAG, "Port[%d]: Unsupported mode: %d, only Modbus TCP server mode (%d) is suppurted", index + 1, mode, BRIDGE_MODE_SERVER);
        return ESP_FAIL;
    }

    if (MODBUS_TCP_DEBUG_LOG_ENABLE) {
        esp_log_level_set(TAG, ESP_LOG_DEBUG);
        esp_log_level_set("modbus_helpers", ESP_LOG_DEBUG);
    }

    if (mb_tcp_task_count >= MODBUS_TCP_MAX_TASK_COUNT) {
        ESP_LOGE(TAG, "Port[%d]: Task count limit reached", index + 1);
        return ESP_FAIL;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (!event_group) {
        ESP_LOGE(TAG, "Port[%d]: Unable to create event group", index + 1);
        return ESP_FAIL;
    }

    *serial_desc = serial_init(config, process_data_from_serial);
    if (!*serial_desc) {
        ESP_LOGE(TAG, "Port[%d]: Error while initializing serial port", index + 1);
        vEventGroupDelete(event_group);
        return ESP_FAIL;
    }

    packet_queue_handle queue_handle = packet_queue_create(MODBUS_TCP_QUEUE_LEN);
    if (!queue_handle) {
        ESP_LOGE(TAG, "Port[%d]: Unable to create TCP packets queue", index + 1);
        // TODO: Add de-init Serial port
        vEventGroupDelete(event_group);
        return ESP_FAIL;
    }

    esp_err_t err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
    if (err != ESP_OK) {
        // TODO: Add de-init Serial port
        vEventGroupDelete(event_group);
        packet_queue_delete(queue_handle);
        return err;
    }

    mb_tcp_task_ctx_t* ctx = &mb_tcp_task_ctx[mb_tcp_task_count];
    ctx->index = index;
    ctx->serial_desc = *serial_desc;
    ctx->tcp_desc = *tcp_desc;
    ctx->mode = mode;
    ctx->tcp_queue = queue_handle;
    ctx->event_group = event_group;
    ctx->pending_tid = 0;
    ctx->resp_timeout_ticks = calc_response_timeout_ticks(config->baudrate);

    ESP_LOGD(TAG, "Port[%d] response timeout: %u ms", index + 1, (unsigned)pdTICKS_TO_MS(ctx->resp_timeout_ticks));

    xTaskCreate(modbus_tcp_server_task, "modbus_tcp_server_task", MODBUS_TCP_TASK_STACK_SIZE, ctx, MODBUS_TCP_TASK_PRIORITY, NULL);

    mb_tcp_task_count++;
    ctx->initialized = 1;

    return ESP_OK;
}

//------------------------------------------------------------------------------
