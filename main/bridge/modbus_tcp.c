#include "modbus_tcp.h"
#include <string.h>
#include <esp_log.h>
#include "serial.h"
#include "tcp_server.h"
#include "modbus_helpers.h"
#include "packet_queue.h"
#include "rs485_stats.h"
#include "bridge.h"


#define MODBUS_TCP_TASK_STACK_SIZE          3072            // Размер стека каждой задачи
#define MODBUS_TCP_TASK_PRIORITY            4               // Приоритет задач
#define MODBUS_TCP_MAX_TASK_COUNT           BRIDGES_COUNT   // Максимальное количество задач (портов)

#define MODBUS_TCP_QUEUE_LEN                10              // Длина очереди запросов от клиентов

#define MODBUS_TCP_SEND_BUFFER_SIZE         1024            // Размер буфера передачи для TCP и RTU пакетов

#define EVENT_SERIAL_RESPONSE_RECEIVED      BIT0            // Флаг события: serial-порт получил пакет с ответом
#define EVENT_TASK_STARTED                  BIT8            // Флаг события: задача запустилась
#define EVENT_TASK_FINISHED                 BIT9            // Флаг события: задача завершила работу
#define EVENT_TASK_EXIT_REQ                 BIT16           // Флаг события: запрос завершения работы задачи

#define MODBUS_RTU_MAX_PACKET_LEN           256             // Максимальная длина пакета Modbus RTU (кадров)
#define MODBUS_RTU_RECV_RESERVE_LEN         10              // Запас на прием пакета с учетом интервала тишины и арбитража быстрого Modbus (кадров)
#define RS485_BITS_PER_FRAME                11              // Количество бит в кадре UART (8 бит данных + старт-бит + 2 стоп-бита)

#define WAIT_LOOP_DELAY_MS                  100             // Задержка в циклах ожидания, нужна чтобы периодически проверять флаг запроса выхода


typedef struct {
    bool initialized;
    unsigned index;
    serial_desc_t* serial_desc;
    tcp_desc_t* tcp_desc;
    bridge_mode_t mode;
    packet_queue_handle tcp_queue;
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    uint16_t pending_tid;
    uint8_t pending_slave_id;
    unsigned resp_timeout_ticks;
} mb_tcp_task_ctx_t;


static const char *TAG = "modbus_tcp";

static mb_tcp_task_ctx_t mb_tcp_task_ctx[MODBUS_TCP_MAX_TASK_COUNT] = {0};


static inline bool check_task_exit_req(const mb_tcp_task_ctx_t *ctx)
{
    EventBits_t bits = xEventGroupWaitBits(ctx->event_group, EVENT_TASK_EXIT_REQ, pdFALSE, pdTRUE, 0);
    if (bits & EVENT_TASK_EXIT_REQ) {
        return true;
    }
    return false;
}


// Поиск контекста по дескриптору serial_desc_t
static mb_tcp_task_ctx_t* find_ctx_by_serial_desc(const serial_desc_t* serial_desc)
{
    for (unsigned i = 0; i < MODBUS_TCP_MAX_TASK_COUNT; i++) {
        if (mb_tcp_task_ctx[i].serial_desc == serial_desc) {
            return &mb_tcp_task_ctx[i];
        }
    }
    return 0;
}

// Поиск контекста по дескриптору tcp_desc_t
static mb_tcp_task_ctx_t* find_ctx_by_tcp_desc(const tcp_desc_t* tcp_desc)
{
    for (unsigned i = 0; i < MODBUS_TCP_MAX_TASK_COUNT; i++) {
        if (mb_tcp_task_ctx[i].tcp_desc == tcp_desc) {
            return &mb_tcp_task_ctx[i];
        }
    }
    return 0;
}


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
            ESP_LOGW(TAG, "Port[%u]: TCP packet with incorrect length will be skipped", ctx->index + 1);
            break;
        }
        if (modbus_tcp_check_request(req_data, req_len) != ESP_OK) {
            ESP_LOGW(TAG, "Port[%d]: Incorrect TCP packet will be skipped", ctx->index + 1);
            break;
        }
        esp_err_t queue_res = packet_queue_push(ctx->tcp_queue, req_data, req_len);
        if (queue_res != ESP_OK) {
            break;
        }
        pos += req_len;
        count++;
    }
    if (pos < len) {
        ESP_LOGW(TAG, "Port[%u]: Not all data in the TCP packet was processed", ctx->index + 1);
    }
    return count;
}


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
        ESP_LOGW(TAG, "Context is not initialized, skipping RTU packet");
        return;
    }

    // TODO: Add fast modbus support (0xFF truncation)

    ESP_LOGD(TAG, "Port[%u]: Processing data from serial port", ctx->index + 1);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t check_res = modbus_rtu_check_response(data, len, NULL);
    if (check_res != ESP_OK) {
        return;
    }

    if (tcp_server_connected(ctx->tcp_desc) != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: No TCP client connected, skipping RTU packet", ctx->index + 1);
        return;
    }

    uint8_t* tcp_resp_buf = malloc(MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_buf) {
        ESP_LOGE(TAG, "Port[%u]: Failed to create TCP send buffer", ctx->index + 1);
        return;
    }

    size_t tcp_resp_len = modbus_tcp_from_rtu(ctx->pending_tid, data, len, tcp_resp_buf, MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_len) {
        free(tcp_resp_buf);
        return;
    }

    xEventGroupSetBits(ctx->event_group, EVENT_SERIAL_RESPONSE_RECEIVED);

    ESP_LOGD(TAG, "Port[%u]: Sending TCP response, length: %u", ctx->index + 1, tcp_resp_len);
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
        ESP_LOGW(TAG, "Context is not initialized, skipping TCP packet");
        return;
    }

    unsigned count = separate_and_push_requests_from_tcp(ctx, data, len);
    if (!count) {
        rs485_stats_update(ctx->index, false);
    }
}


// Ожидание активного TCP подключения
static void wait_tcp_connection(const mb_tcp_task_ctx_t* ctx)
{
    bool wait_conn = false;

    while ((tcp_server_connected(ctx->tcp_desc) != ESP_OK) &&
            !check_task_exit_req(ctx)) {
        if (!wait_conn) {
            ESP_LOGI(TAG, "Waiting for TCP connection...");
            packet_queue_clear(ctx->tcp_queue);
            wait_conn = true;
        }
        vTaskDelay(pdMS_TO_TICKS(WAIT_LOOP_DELAY_MS));  // Задержка, чтобы не вешать систему
    }
}


// Получение TCP запроса из очереди пакетов
// Возвращает размер полученного пакета и устанавливает указатель tcp_req_buf на данные пакета
// В случае отсутствия пакета в очереди возвращает 0
// После использования буфер tcp_req_buf необходимо освободить через free(tcp_req_buf)
static size_t fetch_tcp_request(mb_tcp_task_ctx_t* ctx, uint8_t** tcp_req_buf)
{
    size_t len = 0;
    do {
        if (check_task_exit_req(ctx)) {
            return 0;
        }
        len = packet_queue_pop(ctx->tcp_queue, tcp_req_buf, pdMS_TO_TICKS(WAIT_LOOP_DELAY_MS));
    } while (len == 0);

    ESP_LOGD(TAG, "Port[%u]: Fetch TCP request from queue, length: %u", ctx->index + 1, len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, *tcp_req_buf, len, ESP_LOG_DEBUG);
    return len;
}


// Создание RTU запроса из TCP запроса
// Возвращает размер RTU запроса и устанавливает указатель rtu_req_buf на данные RTU пакета
// В случае ошибки возвращает 0
// После использования буфер rtu_req_buf необходимо освободить через free(rtu_req_buf)
static size_t make_rtu_request_from_tcp(mb_tcp_task_ctx_t* ctx, uint8_t* tcp_req_buf, uint8_t** rtu_req_buf)
{
    *rtu_req_buf = malloc(MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!*rtu_req_buf) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create TCP send buffer", ctx->index + 1);
        return 0;
    }
    size_t rtu_req_len = modbus_rtu_from_tcp(tcp_req_buf, *rtu_req_buf, MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!rtu_req_len) {
        ESP_LOGE(TAG, "Port[%u]: Failed to create RTU request from TCP", ctx->index + 1);
        free(*rtu_req_buf);
        *rtu_req_buf = NULL;
        return 0;
    }
    ctx->pending_tid = modbus_swap16(((mb_tcp_header_t*)tcp_req_buf)->transaction_id);
    ctx->pending_slave_id = ((mb_tcp_header_t*)tcp_req_buf)->unit_id;
    return rtu_req_len;
}


// Отправка RTU пакета с запросом
static esp_err_t send_rtu_request(mb_tcp_task_ctx_t* ctx, uint8_t* rtu_req_buf, size_t rtu_req_len)
{
    ESP_LOGD(TAG, "Port[%u]: Sending RTU request, length: %u", ctx->index + 1, rtu_req_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, rtu_req_buf, rtu_req_len, ESP_LOG_DEBUG);

    esp_err_t send_result = serial_send(ctx->serial_desc, rtu_req_buf, rtu_req_len);
    if (send_result != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to send serial data", ctx->index + 1);
    }

    return send_result;
}


// Ожидание отправки RTU запроса и получения ответа
// Возвращает результат ожидания ответа (true - ответ получен)
static bool wait_rtu_send_receive(mb_tcp_task_ctx_t* ctx)
{
    xEventGroupClearBits(ctx->event_group, EVENT_SERIAL_RESPONSE_RECEIVED);

    // Waiting for end of serial transmission
    serial_wait_tx_done(ctx->serial_desc, portMAX_DELAY);

    EventBits_t bits_to_wait = EVENT_SERIAL_RESPONSE_RECEIVED | EVENT_TASK_EXIT_REQ;
    EventBits_t bits = xEventGroupWaitBits(ctx->event_group, bits_to_wait, pdFALSE, pdFALSE, ctx->resp_timeout_ticks);
    if (bits & EVENT_SERIAL_RESPONSE_RECEIVED) {
        return true;
    }
    if (bits & EVENT_TASK_EXIT_REQ) {
        ESP_LOGD(TAG, "Port[%u]: Response waiting aborted by task exit request", ctx->index + 1);
        return false;
    }
    ESP_LOGW(TAG, "Port[%u]: No response from device with slave ID: %u", ctx->index + 1, ctx->pending_slave_id);
    return false;
}


// Задача для работы в режиме Modbus TCP сервера
static void modbus_tcp_server_task(void *arg)
{
    mb_tcp_task_ctx_t* ctx = (mb_tcp_task_ctx_t*)arg;
    xEventGroupSetBits(ctx->event_group, EVENT_TASK_STARTED);
    ESP_LOGD(TAG, "Port[%u]: Started Modbus TCP Server task", ctx->index + 1);

    while (1)
    {
        wait_tcp_connection(ctx);
        if (check_task_exit_req(ctx)) {
            break;
        }

        uint8_t* tcp_req_buf = 0;
        size_t tcp_req_len = fetch_tcp_request(ctx, &tcp_req_buf);
        if (!tcp_req_len) {
            continue;
        }

        // Принятый пакет с запросом уже проверен в колбэке process_data_from_tcp()
        // TODO: Add special commands detection and response (e.g. fast modbus support probe)

        uint8_t* rtu_req_buf = 0;
        size_t rtu_req_len = make_rtu_request_from_tcp(ctx, tcp_req_buf, &rtu_req_buf);
        free(tcp_req_buf);

        if (!rtu_req_len) {
            continue;
        }

        esp_err_t send_result = send_rtu_request(ctx, rtu_req_buf, rtu_req_len);
        free(rtu_req_buf);

        if (send_result != ESP_OK) {
            continue;
        }

        rs485_busy_monitor_update_activity(ctx->index);

        bool wait_res = wait_rtu_send_receive(ctx);
        rs485_stats_update(ctx->index, wait_res);
    }

    ESP_LOGI(TAG, "Port[%u]: Modbus TCP Server task finished", ctx->index);
    xEventGroupSetBits(ctx->event_group, EVENT_TASK_FINISHED);
    vTaskDelete(NULL);
}


// Расчет тайм-аута получения RTU-ответа в зависимости от скорости порта.
// Расчет производится из времени, необходимого на получение пакета Modbus RTU максимальной
// длины (256 байт) + запас на интервал тишины и арбитраж быстрого Modbus (10 байт).
// Размер кадра считается максимальным и наиболее веротяным (11 бит)
// Возвращает значение тайм-аута в тиках FreeRTOS.
static unsigned calc_response_timeout_ticks(unsigned baudrate)
{
    static const unsigned max_resp_len = MODBUS_RTU_MAX_PACKET_LEN + MODBUS_RTU_RECV_RESERVE_LEN;

    unsigned bytes_rate = baudrate / RS485_BITS_PER_FRAME;
    unsigned timeout_ms = ((1000 * max_resp_len) + bytes_rate - 1) / bytes_rate;
    unsigned timeout_ticks = (timeout_ms * configTICK_RATE_HZ + 999) / 1000;

    return timeout_ticks;
}


esp_err_t modbus_tcp_init_port(unsigned index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc)
{
    if (mode != BRIDGE_MODE_SERVER) {
        ESP_LOGE(TAG, "Port[%u]: Unsupported mode: %d, only Modbus TCP Server mode (%d) is suppurted", index + 1, mode, BRIDGE_MODE_SERVER);
        return ESP_ERR_INVALID_ARG;
    }

    if (index >= MODBUS_TCP_MAX_TASK_COUNT) {
        ESP_LOGE(TAG, "Port[%u]: Port number out of range", index + 1);
        return ESP_ERR_INVALID_ARG;
    }

    mb_tcp_task_ctx_t* ctx = &mb_tcp_task_ctx[index];
    if (ctx->initialized) {
        ESP_LOGW(TAG, "Port[%u]: Already initialized", index + 1);
        return ESP_ERR_NOT_ALLOWED;
    }

    ESP_LOGD(TAG, "Port[%u]: Initializing in Modbus TCP mode...", index + 1);

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (!event_group) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create event group", index + 1);
        return ESP_FAIL;
    }

    *serial_desc = serial_init(config, process_data_from_serial);
    if (!*serial_desc) {
        ESP_LOGE(TAG, "Port[%u]: Error while initializing serial port", index + 1);
        vEventGroupDelete(event_group);
        return ESP_FAIL;
    }

    packet_queue_handle queue_handle = packet_queue_create(MODBUS_TCP_QUEUE_LEN);
    if (!queue_handle) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create TCP packets queue", index + 1);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        return ESP_FAIL;
    }

    esp_err_t err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Error while initializing TCP server", index + 1);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        packet_queue_delete(queue_handle);
        return err;
    }

    ctx->index = index;
    ctx->serial_desc = *serial_desc;
    ctx->tcp_desc = *tcp_desc;
    ctx->mode = mode;
    ctx->tcp_queue = queue_handle;
    ctx->event_group = event_group;
    ctx->task_handle = NULL;
    ctx->pending_tid = 0;
    ctx->pending_slave_id = 0;
    ctx->resp_timeout_ticks = calc_response_timeout_ticks(config->baudrate);

    ESP_LOGD(TAG, "Port[%u] response timeout: %u ms", index + 1, (unsigned)pdTICKS_TO_MS(ctx->resp_timeout_ticks));

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(modbus_tcp_server_task, "modbus_tcp_server_task", MODBUS_TCP_TASK_STACK_SIZE, ctx, MODBUS_TCP_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create Modbus TCP Server task", index + 1);
        tcp_server_deinit(*tcp_desc);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        packet_queue_delete(queue_handle);
        return ESP_FAIL;
    }

    ctx->task_handle = task_handle;
    xEventGroupWaitBits(ctx->event_group, EVENT_TASK_STARTED, pdFALSE, pdTRUE, portMAX_DELAY);

    ctx->initialized = true;

    ESP_LOGD(TAG, "Port[%u]: Initialized", index + 1);
    return ESP_OK;
}


esp_err_t modbus_tcp_deinit_port(unsigned index)
{
    if (index >= MODBUS_TCP_MAX_TASK_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    mb_tcp_task_ctx_t* ctx = &mb_tcp_task_ctx[index];

    if (!ctx->initialized || (ctx->task_handle == NULL)) {
        ESP_LOGW(TAG, "Port[%u]: Not initialized", index + 1);
        return ESP_ERR_NOT_ALLOWED;
    }

    ESP_LOGD(TAG, "Port[%u]: Deinitializing...", index + 1);

    ctx->initialized = false;

    xEventGroupSetBits(ctx->event_group, EVENT_TASK_EXIT_REQ);
    ESP_LOGD(TAG, "Waiting for Modbus TCP Server task finished...");
    xEventGroupWaitBits(ctx->event_group, EVENT_TASK_FINISHED, pdFALSE, pdTRUE, portMAX_DELAY);

    tcp_server_deinit(ctx->tcp_desc);
    serial_deinit(ctx->serial_desc);
    vEventGroupDelete(ctx->event_group);
    packet_queue_delete(ctx->tcp_queue);

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}
