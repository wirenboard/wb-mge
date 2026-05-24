#include "modbus_tcp.h"
#include "modbus_tcp_internal.h"
#include <string.h>
#include <esp_log.h>
#include "tcp_server.h"
#include "modbus_helpers.h"
#include "rs485_stats.h"
#include "fast_modbus.h"
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

#define MODBUS_RTU_MAX_PACKET_LEN           256             // Maximum Modbus RTU packet length (frames)
#define MODBUS_RTU_RECV_RESERVE_LEN         10              // Reserve for packet reception with silence interval and Fast Modbus arbitration (frames)
#define RS485_BITS_PER_FRAME                11              // Number of bits in UART frame (8 data bits + start bit + 2 stop bits)

#define MODBUS_RTU_RECV_TOUT_RESERVE_MS     30              // Extra reserve for packet reception waiting timeout (compensate FreeRTOS and logs lag)

#define WAIT_LOOP_DELAY_MS                  100             // Delay in wait loops, needed to periodically check exit request flag

/* ---- TCP stream reassembly (bug 07 fix) ---------------------------------- */
/* Maximum concurrent TCP clients per gateway port; matches tcp_server.c limit. */
#define MBTCP_REASM_MAX_CONNS  8
/* Maximum Modbus TCP ADU size: 6 (MBAP) + 1 (unit) + 1 (FC) + 252 (data) = 260.
 * Round up to 300 for safety margin. */
#define MBTCP_REASM_FRAME_MAX  300

typedef struct {
    int     sock;                          /* -1 = free slot */
    size_t  len;
    uint8_t buf[MBTCP_REASM_FRAME_MAX];
} mbtcp_reasm_t;

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
    /* Per-connection TCP stream reassembly buffers (bug 07 fix). */
    mbtcp_reasm_t reasm[MBTCP_REASM_MAX_CONNS];
    SemaphoreHandle_t reasm_mutex;        /* guards reasm[] slot alloc/free only */
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


/* Find (or allocate) the reassembly slot for a client socket within a port context.
 * The mutex protects slot alloc/free; per-connection buf/len is single-task after
 * allocation (one receiver_task per connection). Returns NULL if table is full. */
static mbtcp_reasm_t *mbtcp_reasm_get(mb_tcp_task_ctx_t *ctx, int sock)
{
    /* Reject invalid socket descriptors: -1 is used as the "free slot" sentinel
     * and must never match a real client socket. */
    if (sock < 0) { return NULL; }
    mbtcp_reasm_t *slot = NULL;
    if (ctx->reasm_mutex) { xSemaphoreTake(ctx->reasm_mutex, portMAX_DELAY); }
    mbtcp_reasm_t *free_slot = NULL;
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (ctx->reasm[i].sock == sock) { slot = &ctx->reasm[i]; break; }
        if ((free_slot == NULL) && (ctx->reasm[i].sock == -1)) { free_slot = &ctx->reasm[i]; }
    }
    if ((slot == NULL) && free_slot) {
        free_slot->sock = sock;
        free_slot->len  = 0;
        slot = free_slot;
    }
    if (ctx->reasm_mutex) { xSemaphoreGive(ctx->reasm_mutex); }
    return slot;
}

/* Look up an existing reassembly slot for sock without allocating a new one.
 * Returns NULL if no slot exists for this socket. Safe to call from any task
 * that holds no other lock. */
static mbtcp_reasm_t *mbtcp_reasm_find(mb_tcp_task_ctx_t *ctx, int sock)
{
    mbtcp_reasm_t *slot = NULL;
    if (ctx->reasm_mutex) { xSemaphoreTake(ctx->reasm_mutex, portMAX_DELAY); }
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (ctx->reasm[i].sock == sock) { slot = &ctx->reasm[i]; break; }
    }
    if (ctx->reasm_mutex) { xSemaphoreGive(ctx->reasm_mutex); }
    return slot;
}

static void mbtcp_reasm_free(mb_tcp_task_ctx_t *ctx, int sock)
{
    if (ctx->reasm_mutex) { xSemaphoreTake(ctx->reasm_mutex, portMAX_DELAY); }
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (ctx->reasm[i].sock == sock) {
            ctx->reasm[i].sock = -1;
            ctx->reasm[i].len  = 0;
            break;
        }
    }
    if (ctx->reasm_mutex) { xSemaphoreGive(ctx->reasm_mutex); }
}

/* Total ADU length declared by the MBAP header (requires >= 6 bytes in buf). */
static size_t mbtcp_frame_total_len(const uint8_t *buf)
{
    uint16_t mbap_len = ((uint16_t)buf[4] << 8) | buf[5]; /* big-endian length field */
    return (size_t)mbap_len + offsetof(mb_tcp_header_t, unit_id); /* + 6 */
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

/* TCP stream reassembly for the gateway: accumulates bytes per connection,
 * dispatches each complete Modbus TCP ADU to the packet queue, and carries
 * any partial tail over to the next recv() call. */
static unsigned separate_and_push_requests_from_tcp_with_client(
    mb_tcp_task_ctx_t *ctx, int client_sock, const uint8_t *data, size_t len)
{
    mbtcp_reasm_t *c = mbtcp_reasm_get(ctx, client_sock);
    if (c == NULL) {
        /* Reassembly table full — fall back to single-pass (old behaviour). */
        ESP_LOGW(TAG, "Port[%u]: reasm table full for sock=%d, fallback mode",
                 ctx->index + 1, client_sock);
        return separate_and_push_one_pass(ctx, client_sock, data, len);
    }

    unsigned total_count = 0;
    size_t   off         = 0;

    while (off < len) {
        /* Copy as many bytes as fit into the accumulation buffer. */
        size_t space = MBTCP_REASM_FRAME_MAX - c->len;
        size_t chunk = len - off;
        if (chunk > space) { chunk = space; }
        memcpy(c->buf + c->len, data + off, chunk);
        c->len += chunk;
        off    += chunk;

        /* Dispatch every complete frame currently in the buffer. */
        size_t pos = 0;
        while ((c->len - pos) >= sizeof(mb_tcp_header_t)) {
            size_t flen = mbtcp_frame_total_len(c->buf + pos);
            if ((flen < sizeof(mb_tcp_header_t)) || (flen > MBTCP_REASM_FRAME_MAX)) {
                /* Bogus length field — resync by dropping buffered bytes. */
                pos = c->len;
                break;
            }
            if ((c->len - pos) < flen) { break; }   /* frame not yet complete */

            /* Validate and push the complete frame. */
            const uint8_t *frame = c->buf + pos;
            if (modbus_tcp_check_request(frame, flen) != ESP_OK) {
                ESP_LOGW(TAG, "Port[%u]: sock=%d: invalid Modbus TCP framing, dropping",
                         ctx->index + 1, client_sock);
            } else {
                esp_err_t qres = packet_queue_push_with_client(
                    ctx->tcp_queue, frame, flen, client_sock);
                if (qres == ESP_OK) {
                    total_count++;
                }
            }
            pos += flen;
        }

        /* Shift any remaining partial frame to the front of the buffer. */
        if (pos > 0) {
            memmove(c->buf, c->buf + pos, c->len - pos);
            c->len -= pos;
        }

        /* If the buffer is full but no complete frame was found, the stream is
         * desynced or the ADU is oversized — drop and resync. */
        if (c->len == MBTCP_REASM_FRAME_MAX) {
            ESP_LOGW(TAG, "Port[%u]: sock=%d: reasm buffer full, resync (drop)",
                     ctx->index + 1, client_sock);
            c->len = 0;
        }
    }

    if (total_count == 0) {
        ESP_LOGD(TAG, "Port[%u]: sock=%d: no complete frames yet (%zu bytes buffered)",
                 ctx->index + 1, client_sock, c->len);
    }
    return total_count;
}

/* Connection-close hook: release this socket's reassembly slot. */
static void on_tcp_conn_close(tcp_desc_t *desc, int client_sock)
{
    mb_tcp_task_ctx_t *ctx = find_ctx_by_tcp_desc(desc);
    if (ctx) {
        mbtcp_reasm_free(ctx, client_sock);
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
        mbtcp_reasm_t *slot = mbtcp_reasm_find(ctx, client_sock);
        bool has_pending = (slot != NULL) && (slot->len > 0);
        if (!has_pending) {
            rs485_stats_update(ctx->index, false);
        }
    }
}


// Wait for active TCP connection
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


// Calculate RTU response timeout based on port speed.
// Calculation is based on time required to receive maximum length Modbus RTU packet
// (256 bytes) + reserve for silence interval and Fast Modbus arbitration (10 bytes).
// Frame size is considered maximum and most likely (11 bits)
// Returns timeout value in FreeRTOS ticks.
static unsigned calc_response_timeout_ticks(unsigned baudrate)
{
    static const unsigned max_resp_len = MODBUS_RTU_MAX_PACKET_LEN + MODBUS_RTU_RECV_RESERVE_LEN;

    unsigned bytes_rate = baudrate / RS485_BITS_PER_FRAME;
    unsigned timeout_ms = ((1000 * max_resp_len) + bytes_rate - 1) / bytes_rate;
    timeout_ms += MODBUS_RTU_RECV_TOUT_RESERVE_MS;
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

    /* Initialize per-connection reassembly state. */
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        ctx->reasm[i].sock = -1;
        ctx->reasm[i].len  = 0;
    }
    ctx->reasm_mutex = xSemaphoreCreateMutex();
    if (ctx->reasm_mutex == NULL) {
        ESP_LOGE(TAG, "Port[%u]: Failed to create reassembly mutex", index + 1);
        return ESP_ERR_NO_MEM;
    }

    EventGroupHandle_t event_group = xEventGroupCreate();
    if (!event_group) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create event group", index + 1);
        vSemaphoreDelete(ctx->reasm_mutex);
        ctx->reasm_mutex = NULL;
        return ESP_FAIL;
    }

    *serial_desc = serial_init(config, process_data_from_serial);
    if (!*serial_desc) {
        ESP_LOGE(TAG, "Port[%u]: Error while initializing serial port", index + 1);
        vEventGroupDelete(event_group);
        vSemaphoreDelete(ctx->reasm_mutex);
        ctx->reasm_mutex = NULL;
        return ESP_FAIL;
    }

    packet_queue_handle queue_handle = packet_queue_create(MODBUS_TCP_QUEUE_LEN);
    if (!queue_handle) {
        ESP_LOGE(TAG, "Port[%u]: Unable to create TCP packets queue", index + 1);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        vSemaphoreDelete(ctx->reasm_mutex);
        ctx->reasm_mutex = NULL;
        return ESP_FAIL;
    }

    esp_err_t err = tcp_server_init(port, process_data_from_tcp, tcp_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Error while initializing TCP server", index + 1);
        serial_deinit(*serial_desc);
        vEventGroupDelete(event_group);
        packet_queue_delete(queue_handle);
        vSemaphoreDelete(ctx->reasm_mutex);
        ctx->reasm_mutex = NULL;
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
        vSemaphoreDelete(ctx->reasm_mutex);
        ctx->reasm_mutex = NULL;
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
    if (ctx->reasm_mutex) {
        vSemaphoreDelete(ctx->reasm_mutex);
        ctx->reasm_mutex = NULL;
    }

    ESP_LOGD(TAG, "Port[%u]: Deinitialized", index + 1);
    return ESP_OK;
}


#ifdef __unittest_env__

void modbus_tcp_test_init_ctx(unsigned ctx_idx, packet_queue_handle queue, tcp_desc_t *tcp_desc)
{
    mb_tcp_task_ctx_t *ctx = &mb_tcp_task_ctx[ctx_idx];
    memset(ctx, 0, sizeof(*ctx));
    ctx->index       = ctx_idx;
    ctx->tcp_queue   = queue;
    ctx->tcp_desc    = tcp_desc;
    ctx->reasm_mutex = NULL;   /* no mutex in unit tests: single-threaded */
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        ctx->reasm[i].sock = -1;
        ctx->reasm[i].len  = 0;
    }
}

int modbus_tcp_test_reasm_get(unsigned ctx_idx, int sock)
{
    return (mbtcp_reasm_get(&mb_tcp_task_ctx[ctx_idx], sock) != NULL) ? 1 : 0;
}

void modbus_tcp_test_reasm_free(unsigned ctx_idx, int sock)
{
    mbtcp_reasm_free(&mb_tcp_task_ctx[ctx_idx], sock);
}

int modbus_tcp_test_reasm_has_slot(unsigned ctx_idx, int sock)
{
    mb_tcp_task_ctx_t *ctx = &mb_tcp_task_ctx[ctx_idx];
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (ctx->reasm[i].sock == sock) { return 1; }
    }
    return 0;
}

size_t modbus_tcp_test_reasm_pending_bytes(unsigned ctx_idx, int sock)
{
    mb_tcp_task_ctx_t *ctx = &mb_tcp_task_ctx[ctx_idx];
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        if (ctx->reasm[i].sock == sock) { return ctx->reasm[i].len; }
    }
    return 0;
}

size_t modbus_tcp_test_frame_total_len(const uint8_t *buf)
{
    return mbtcp_frame_total_len(buf);
}

unsigned modbus_tcp_test_push_data(unsigned ctx_idx, int client_sock,
                                    const uint8_t *data, size_t len)
{
    return separate_and_push_requests_from_tcp_with_client(
        &mb_tcp_task_ctx[ctx_idx], client_sock, data, len);
}

void modbus_tcp_test_conn_close(unsigned ctx_idx, int client_sock)
{
    on_tcp_conn_close(mb_tcp_task_ctx[ctx_idx].tcp_desc, client_sock);
}

#endif /* __unittest_env__ */
