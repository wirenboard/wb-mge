#include "modbus_tcp.h"
#include "mbtcp_reasm.h"
#include <string.h>
#include <esp_log.h>
#include "tcp_server.h"
#include "modbus_helpers.h"
#include "rs485_stats.h"
#include "fast_modbus.h"
#include "mb_device.h"
#include "freertos/semphr.h"
#include <stddef.h>



#define MODBUS_TCP_TASK_STACK_SIZE          3072            // Stack size for each task
#define MODBUS_TCP_TASK_PRIORITY            4               // Task priority
#define MODBUS_TCP_MAX_TASK_COUNT           BRIDGES_COUNT   // Maximum number of tasks (ports)

#define MODBUS_TCP_QUEUE_LEN                10              // Client request queue length

#define MODBUS_TCP_SEND_BUFFER_SIZE         1024            // Transmit buffer size for TCP and RTU packets

#define EVENT_SERIAL_RESPONSE_RECEIVED      BIT0            // Event flag: serial port received response packet
#define EVENT_TASK_STARTED                  BIT8            // Event flag: task started
#define EVENT_TASK_FINISHED                 BIT9            // Event flag: task finished
#define EVENT_TASK_EXIT_REQ                 BIT16           // Event flag: task exit request

#define WAIT_LOOP_DELAY_MS                  100             // Delay in wait loops, needed to periodically check exit request flag

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
    int pending_client_sock;    // client socket that sent the current pending RTU request
    /* Per-connection TCP stream reassembly (bridge/mbtcp_reasm). */
    mbtcp_reasm_t reasm;
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

// Find context by serial_desc_t descriptor
static mb_tcp_task_ctx_t* find_ctx_by_serial_desc(const serial_desc_t* serial_desc)
{
    for (unsigned i = 0; i < MODBUS_TCP_MAX_TASK_COUNT; i++) {
        if (mb_tcp_task_ctx[i].serial_desc == serial_desc) {
            return &mb_tcp_task_ctx[i];
        }
    }
    return 0;
}

// Find context by tcp_desc_t descriptor
static mb_tcp_task_ctx_t* find_ctx_by_tcp_desc(const tcp_desc_t* tcp_desc)
{
    for (unsigned i = 0; i < MODBUS_TCP_MAX_TASK_COUNT; i++) {
        if (mb_tcp_task_ctx[i].tcp_desc == tcp_desc) {
            return &mb_tcp_task_ctx[i];
        }
    }
    return 0;
}



// Callback function for receiving data from serial port
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

    size_t truncated_len = fast_modbus_truncate_ff(&data, len);
    if (truncated_len == 0) {
        return;
    }

    ESP_LOGD(TAG, "Port[%d]: Processing data from serial port, truncated length: %zu", ctx->index + 1, truncated_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, truncated_len, ESP_LOG_DEBUG);

    rs485_busy_monitor_update_activity(ctx->index);

    esp_err_t check_res = modbus_rtu_check_response(data, truncated_len, NULL);
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

    size_t tcp_resp_len = modbus_tcp_from_rtu(ctx->pending_tid, data, truncated_len, tcp_resp_buf, MODBUS_TCP_SEND_BUFFER_SIZE);
    if (!tcp_resp_len) {
        free(tcp_resp_buf);
        return;
    }

    xEventGroupSetBits(ctx->event_group, EVENT_SERIAL_RESPONSE_RECEIVED);

    ESP_LOGD(TAG, "Port[%u]: Sending TCP response to client_sock=%d, length: %u", ctx->index + 1, ctx->pending_client_sock, tcp_resp_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, tcp_resp_buf, tcp_resp_len, ESP_LOG_DEBUG);

    // Send response to the specific client that originated the request.
    // If the client disconnected while waiting for RTU response, tcp_server_send() will
    // return an error - log it but do not treat it as fatal.
    esp_err_t send_res = tcp_server_send(ctx->tcp_desc, ctx->pending_client_sock, tcp_resp_buf, tcp_resp_len);
    if (send_res != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: Failed to send TCP response (client may have disconnected)", ctx->index + 1);
    }
    free(tcp_resp_buf);
}


/* Single-pass frame separation — used as fallback when the reassembly table is full.
 * Processes only frames that fit entirely within [data, data+len). */
static unsigned separate_and_push_one_pass(
    mb_tcp_task_ctx_t *ctx, int client_sock, const uint8_t *data, size_t len)
{
    unsigned count = 0;
    size_t   pos   = 0;

    while (pos < len) {
        // A short trailing fragment cannot contain a full MBAP length field: the
        // length lives within the first offsetof(mb_tcp_header_t, unit_id) bytes
        // (transaction_id[2] + protocol_id[2] + length[2] = 6). Bail out before
        // casting to the header to avoid reading past the buffer (OOB read).
        if ((len - pos) < offsetof(mb_tcp_header_t, unit_id)) {
            break;
        }
        const uint8_t   *req_data = &data[pos];
        mb_tcp_header_t *header   = (mb_tcp_header_t *)req_data;
        size_t req_len = modbus_swap16(header->length) + offsetof(mb_tcp_header_t, unit_id);
        if ((req_len + pos) > len) {
            ESP_LOGW(TAG, "Port[%u]: TCP packet with incorrect length will be skipped",
                     ctx->index + 1);
            break;
        }
        if (modbus_tcp_check_request(req_data, req_len) != ESP_OK) {
            ESP_LOGW(TAG, "Port[%u]: Incorrect TCP packet will be skipped", ctx->index + 1);
            break;
        }
        esp_err_t queue_res = packet_queue_push_with_client(
            ctx->tcp_queue, req_data, req_len, client_sock);
        if (queue_res != ESP_OK) { break; }
        pos += req_len;
        count++;
    }
    if (pos < len) {
        ESP_LOGW(TAG, "Port[%u]: Not all data in the TCP packet was processed", ctx->index + 1);
    }
    return count;
}

/* Reassembler callback: one complete Modbus TCP ADU from a client.
 * Validates the framing and queues it for the RS-485 side. Returns true when the
 * frame made it onto the queue — mbtcp_reasm_feed() counts those. */
static bool on_reasm_frame(void *user_ctx, int sock, const uint8_t *frame, size_t len)
{
    mb_tcp_task_ctx_t *ctx = (mb_tcp_task_ctx_t *)user_ctx;

    if (modbus_tcp_check_request(frame, len) != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: sock=%d: invalid Modbus TCP framing, dropping",
                 ctx->index + 1, sock);
        return false;
    }

    return packet_queue_push_with_client(ctx->tcp_queue, frame, len, sock) == ESP_OK;
}

/* TCP stream reassembly for the gateway: accumulates bytes per connection,
 * dispatches each complete Modbus TCP ADU to the packet queue, and carries
 * any partial tail over to the next recv() call. */
static unsigned separate_and_push_requests_from_tcp_with_client(
    mb_tcp_task_ctx_t *ctx, int client_sock, const uint8_t *data, size_t len)
{
    int pushed = mbtcp_reasm_feed(&ctx->reasm, client_sock, data, len,
                                  on_reasm_frame, ctx);

    if (pushed == MBTCP_REASM_NO_SLOT) {
        /* Reassembly table full — fall back to a single unbuffered pass. */
        ESP_LOGW(TAG, "Port[%u]: reasm table full for sock=%d, fallback mode",
                 ctx->index + 1, client_sock);
        return separate_and_push_one_pass(ctx, client_sock, data, len);
    }

    if (pushed == 0) {
        ESP_LOGD(TAG, "Port[%u]: sock=%d: no complete frames yet (%zu bytes buffered)",
                 ctx->index + 1, client_sock, mbtcp_reasm_pending(&ctx->reasm, client_sock));
    }
    return (unsigned)pushed;
}

/* Connection-close hook: release this socket's reassembly slot. */
static void on_tcp_conn_close(tcp_desc_t *desc, int client_sock)
{
    mb_tcp_task_ctx_t *ctx = find_ctx_by_tcp_desc(desc);
    if (ctx) {
        mbtcp_reasm_close(&ctx->reasm, client_sock);
        /* Clear stale pending state if the disconnected client was the one
         * whose RTU request is currently in flight. Without this, the next
         * client receives a response with the disconnected client's TID.
         * Also clear pending_client_sock to prevent fd-reuse aliasing: if a new
         * client gets the same fd before process_data_from_serial() fires, the
         * RTU response would be sent to the wrong client. */
        if (ctx->pending_client_sock == client_sock) {
            ctx->pending_tid         = 0;
            ctx->pending_slave_id    = 0;
            ctx->pending_client_sock = -1;
        }
    }
}


// Callback function for receiving data from TCP socket
static void process_data_from_tcp(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len)
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

    unsigned count = separate_and_push_requests_from_tcp_with_client(ctx, client_sock, data, len);
    if (!count) {
        /* A zero count is not necessarily a failure: it is normal when the first
         * segment of a split frame arrives and gets buffered for reassembly.
         * Only treat it as a failure when the reassembly slot has no pending bytes
         * (i.e. data was truly rejected, not merely accumulated). */
        bool has_pending = (mbtcp_reasm_pending(&ctx->reasm, client_sock) > 0);
        if (!has_pending) {
            rs485_stats_update(ctx->index, false);
        }
    }
}


// Wait for active TCP connection.
//
// Returns immediately when at least one client is connected OR the queue has
// data to drain. The queue may still hold a request from a client that
// connected briefly and disconnected before this task woke up from its
// vTaskDelay — without the queue-non-empty check, the previous version of
// this function would packet_queue_clear() those bytes and the request would
// be silently lost (UART never sees the RTU frame).
static void wait_tcp_connection(const mb_tcp_task_ctx_t* ctx)
{
    bool logged = false;

    while ((tcp_server_connected(ctx->tcp_desc) != ESP_OK) &&
           (packet_queue_count(ctx->tcp_queue) == 0) &&
           !check_task_exit_req(ctx)) {
        if (!logged) {
            ESP_LOGI(TAG, "Waiting for TCP connection...");
            logged = true;
        }
        vTaskDelay(pdMS_TO_TICKS(WAIT_LOOP_DELAY_MS));  // Delay to avoid hanging the system
    }
}


// Get TCP request from packet queue together with the originating client socket
// Returns received packet size, sets tcp_req_buf pointer to packet data, and writes client_sock
// Returns 0 if no packet in queue
// Buffer tcp_req_buf must be freed with free(tcp_req_buf) after use
static size_t fetch_tcp_request(mb_tcp_task_ctx_t* ctx, uint8_t** tcp_req_buf, int* client_sock)
{
    size_t len = 0;
    do {
        if (check_task_exit_req(ctx)) {
            return 0;
        }
        len = packet_queue_pop_with_client(ctx->tcp_queue, tcp_req_buf, pdMS_TO_TICKS(WAIT_LOOP_DELAY_MS), client_sock);
    } while (len == 0);

    ESP_LOGD(TAG, "Port[%u]: Fetch TCP request from queue, length: %u, client_sock: %d", ctx->index + 1, len, *client_sock);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, *tcp_req_buf, len, ESP_LOG_DEBUG);
    return len;
}


// Create RTU request from TCP request
// Returns RTU request size and sets rtu_req_buf pointer to RTU packet data
// Returns 0 on error
// Buffer rtu_req_buf must be freed with free(rtu_req_buf) after use
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
    // pending_client_sock is set by the caller (modbus_tcp_server_task) before this function
    return rtu_req_len;
}


// Send RTU request packet
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


// Wait for RTU request transmission and response reception
// Returns result of waiting for response (true - response received)
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


// Answer a request addressed to the gateway itself (Unit ID 0xFF) from the
// built-in device-info register map, without forwarding to RS485.
static void handle_self_device_request(mb_tcp_task_ctx_t *ctx, int client_sock,
                                       uint8_t *tcp_req_buf, size_t tcp_req_len)
{
    uint8_t resp[MODBUS_TCP_MAX_ADU_LEN];
    size_t rlen = mb_device_handle_self_request(tcp_req_buf, tcp_req_len,
                                                MODBUS_TCP_TASK_STACK_SIZE, resp);
    tcp_server_send(ctx->tcp_desc, client_sock, resp, rlen);
}


// Task for Modbus TCP server mode operation
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
        int client_sock = -1;
        size_t tcp_req_len = fetch_tcp_request(ctx, &tcp_req_buf, &client_sock);
        if (!tcp_req_len) {
            continue;
        }

        // Store the client socket so process_data_from_serial() can reply to the correct client
        ctx->pending_client_sock = client_sock;

        // Received request packet is already validated in process_data_from_tcp() callback
        // Check if request is a Fast Modbus support probe
        enum fast_modbus_probe_result probe_result = fast_modbus_send_probe_response(
            ctx->index + 1, ctx->tcp_desc, client_sock, tcp_req_buf
        );
        if (probe_result != FAST_MODBUS_NOT_PROBE) {
            free(tcp_req_buf);
            continue;
        }

        /* Requests addressed to the gateway itself (Unit ID 0xFF) are answered locally
         * from the built-in device-info register map, not forwarded to RS485. */
        if (mb_device_is_self(((mb_tcp_header_t*)tcp_req_buf)->unit_id)) {
            handle_self_device_request(ctx, client_sock, tcp_req_buf, tcp_req_len);
            free(tcp_req_buf);
            continue;
        }

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

    /* Initialize per-connection reassembly state. */
    if (!mbtcp_reasm_init(&ctx->reasm, TAG)) {
        ESP_LOGE(TAG, "Port[%u]: Failed to create reassembly mutex", index + 1);
        return ESP_ERR_NO_MEM;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (!event_group) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create event group", index + 1);
        mbtcp_reasm_deinit(&ctx->reasm);
        return ESP_FAIL;
    }

    *serial_desc = serial_init(config, process_data_from_serial);
    if (!*serial_desc) {
        ESP_LOGE(TAG, "Port[%u]: Error while initializing serial port", index + 1);
        vEventGroupDelete(event_group);
        mbtcp_reasm_deinit(&ctx->reasm);
        return ESP_FAIL;
    }
    (*serial_desc)->wait_for_idle = true;  // Modbus gateway must assemble complete RTU frames before forwarding

    packet_queue_handle queue_handle = packet_queue_create(MODBUS_TCP_QUEUE_LEN);
    if (!queue_handle) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create TCP packets queue", index + 1);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        mbtcp_reasm_deinit(&ctx->reasm);
        return ESP_FAIL;
    }

    esp_err_t err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Error while initializing TCP server", index + 1);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        packet_queue_delete(queue_handle);
        mbtcp_reasm_deinit(&ctx->reasm);
        return err;
    }

    (*tcp_desc)->close_handler = on_tcp_conn_close;

    ctx->index = index;
    ctx->serial_desc = *serial_desc;
    ctx->tcp_desc = *tcp_desc;
    ctx->mode = mode;
    ctx->tcp_queue = queue_handle;
    ctx->event_group = event_group;
    ctx->task_handle = NULL;
    ctx->pending_tid = 0;
    ctx->pending_slave_id = 0;
    ctx->pending_client_sock = -1;
    ctx->resp_timeout_ticks = modbus_rtu_response_timeout_ticks(config->baudrate);

    ESP_LOGD(TAG, "Port[%u] response timeout: %u ms", index + 1, (unsigned)pdTICKS_TO_MS(ctx->resp_timeout_ticks));

    TaskHandle_t task_handle = NULL;
    BaseType_t ret = xTaskCreate(modbus_tcp_server_task, "modbus_tcp_server_task", MODBUS_TCP_TASK_STACK_SIZE, ctx, MODBUS_TCP_TASK_PRIORITY, &task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create Modbus TCP Server task", index + 1);
        tcp_server_deinit(*tcp_desc);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        packet_queue_delete(queue_handle);
        mbtcp_reasm_deinit(&ctx->reasm);
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

    tcp_server_deinit(ctx->tcp_desc);   /* waits for all receiver tasks to finish */
    serial_deinit(ctx->serial_desc);
    vEventGroupDelete(ctx->event_group);
    packet_queue_delete(ctx->tcp_queue);

    /* Safe to delete now: all receiver tasks have exited and no one takes the mutex. */
    mbtcp_reasm_deinit(&ctx->reasm);

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}
