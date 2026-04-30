#include "sniffer.h"
#include "modbus_helpers.h"
#include "fast_modbus.h"
#include "bridge.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "sniffer";

#define SNIFFER_MAX_PACKET_LEN  256
#define SNIFFER_QUEUE_LEN       64
#define SNIFFER_RESP_TIMEOUT_MS 200
#define SNIFFER_WS_TASK_STACK   (1024 * 5)
#define SNIFFER_WS_TASK_PRIO    3
#define SNIFFER_JSON_BUF_SIZE   1200

typedef enum {
    SNIFF_IDLE = 0,
    SNIFF_RES_WAIT,
} sniff_state_t;

typedef struct {
    uint8_t  port;
    uint64_t timestamp_us;
    bool     is_master;
    bool     crc_valid;
    bool     is_timeout;
    uint8_t  slave_id;
    uint8_t  function;
    uint8_t  data[SNIFFER_MAX_PACKET_LEN];
    uint16_t data_len;
} sniff_packet_t;

typedef struct {
    sniff_state_t  state;
    bool           enabled;
    bool           last_was_fast_modbus;
    uint8_t        req_buf[SNIFFER_MAX_PACKET_LEN];
    uint16_t       req_len;
    uint64_t       req_timestamp_us;
    TimerHandle_t  resp_timer;
    unsigned       port_index;
} sniff_ctx_t;

static sniff_ctx_t sniff_ctx[BRIDGES_COUNT];
static portMUX_TYPE sniff_mux = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t sniff_queue;
static int ws_client_fd = -1;
static httpd_handle_t ws_server = NULL;
static SemaphoreHandle_t ws_mutex = NULL;
static uint32_t packet_counter = 0;


/* Convert internal 0-based port index to external 1-based port name */
static unsigned port_index_to_name(unsigned index) { return index + 1; }

/* Convert external 1-based port name to internal 0-based index.
 * Returns BRIDGES_COUNT if the name is out of range. */
static unsigned port_name_to_index(unsigned name)
{
    if (name < 1 || name > BRIDGES_COUNT) return BRIDGES_COUNT;
    return name - 1;
}

static bool crc_check(const uint8_t *data, size_t len)
{
    if (len < 4) return false;
    uint16_t crc_calc = modbus_crc16(data, (uint16_t)(len - 2));
    /* modbus_crc16 returns big-endian value; RTU appends CRC low byte first */
    uint8_t crc_lo = (uint8_t)(crc_calc & 0xFF);
    uint8_t crc_hi = (uint8_t)(crc_calc >> 8);
    return (data[len - 2] == crc_lo) && (data[len - 1] == crc_hi);
}

static void bytes_to_hex(const uint8_t *data, uint16_t len, char *out, size_t out_size)
{
    size_t pos = 0;
    for (uint16_t i = 0; i < len && (pos + 2) < out_size; i++) {
        snprintf(out + pos, 3, "%02X", data[i]);
        pos += 2;
    }
    out[pos] = '\0';
}

/* Try to enqueue packet; on failure log and reset port state to IDLE */
static void try_enqueue(unsigned port_index, sniff_packet_t *pkt)
{
    if (xQueueSend(sniff_queue, pkt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "sniff queue full, port %u reset to IDLE", port_index);
        xTimerStop(sniff_ctx[port_index].resp_timer, 0);
        sniff_ctx[port_index].state = SNIFF_IDLE;
    }
}

static void resp_timer_cb(TimerHandle_t timer)
{
    unsigned port_index = (unsigned)(uintptr_t)pvTimerGetTimerID(timer);
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    sniff_packet_t pkt = {0};
    bool do_enqueue = false;

    taskENTER_CRITICAL(&sniff_mux);
    if (ctx->req_len >= 2) {
        pkt.port         = (uint8_t)port_index;
        pkt.timestamp_us = ctx->req_timestamp_us + (uint64_t)SNIFFER_RESP_TIMEOUT_MS * 1000ULL;
        pkt.is_master    = true;
        pkt.crc_valid    = true;
        pkt.is_timeout   = true;
        pkt.slave_id     = ctx->req_buf[0];
        pkt.function     = ctx->req_buf[1];
        do_enqueue = true;
    }
    ctx->state = SNIFF_IDLE;
    taskEXIT_CRITICAL(&sniff_mux);

    if (do_enqueue) {
        if (xQueueSend(sniff_queue, &pkt, 0) != pdTRUE) {
            ESP_LOGW(TAG, "sniff queue full, dropping timeout pkt port %u", port_index);
        }
    }
}

#define FAST_MODBUS_FUNC_1  0x46
#define FAST_MODBUS_FUNC_2  0x60

/* Strip leading 0xFF arbitration bytes only when the pattern matches Fast Modbus:
 * there are leading 0xFF bytes AND after stripping the function code is 0x46 or 0x60.
 * Raw data (with 0xFF) is preserved for display; stripped data is used for CRC/fields. */
static void strip_arbitration(uint8_t *data, size_t len, uint8_t **effective, size_t *effective_len)
{
    *effective = data;
    *effective_len = len;
    if (len == 0 || data[0] != 0xFF) return;

    uint8_t *t = data;
    size_t tlen = fast_modbus_truncate_ff(&t, len);
    if (tlen >= 4 && (t[1] == FAST_MODBUS_FUNC_1 || t[1] == FAST_MODBUS_FUNC_2)) {
        *effective = t;
        *effective_len = tlen;
    }
}

static void sniffer_process(unsigned port_index, uint8_t *data, size_t len)
{
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    if (!ctx->enabled) return;
    if (len < 4) return;

    bool should_start_timer = false;
    bool should_stop_timer = false;
    sniff_packet_t req_pkt = {0};
    sniff_packet_t res_pkt = {0};
    bool enqueue_req = false;
    bool enqueue_res = false;

    taskENTER_CRITICAL(&sniff_mux);
    if (ctx->state == SNIFF_IDLE) {
        bool valid_crc = crc_check(data, len);

        /* Check if this is a Fast Modbus arbitration response (all 0xFF bytes)
         * following a FD 46 command — it has no CRC and should be shown as SLAVE OK */
        bool is_arbitration = false;
        if (!valid_crc && ctx->last_was_fast_modbus) {
            bool all_ff = true;
            for (size_t i = 0; i < len; i++) {
                if (data[i] != 0xFF) { all_ff = false; break; }
            }
            is_arbitration = all_ff;
        }

        if (is_arbitration) {
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = false;
            req_pkt.crc_valid    = true;
            req_pkt.slave_id     = data[0];
            req_pkt.function     = data[1];
            memcpy(req_pkt.data, data, len);
            req_pkt.data_len     = (uint16_t)len;
            enqueue_req = true;
            ctx->last_was_fast_modbus = false;
        } else if (!valid_crc) {
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = false;
            req_pkt.slave_id     = data[0];
            req_pkt.function     = data[1];
            memcpy(req_pkt.data, data, len);
            req_pkt.data_len     = (uint16_t)len;
            enqueue_req = true;
            ctx->last_was_fast_modbus = false;
        } else if (data[0] == 0x00) {
            /* broadcast */
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = true;
            req_pkt.slave_id     = 0;
            req_pkt.function     = data[1];
            memcpy(req_pkt.data, data, len);
            req_pkt.data_len     = (uint16_t)len;
            enqueue_req = true;
            ctx->last_was_fast_modbus = false;
        } else if (data[0] == 0xFD &&
                   (data[1] == FAST_MODBUS_FUNC_1 || data[1] == FAST_MODBUS_FUNC_2)) {
            /* Fast Modbus broadcast to 0xFD — master sends, no slave response expected.
             * Mark flag so the next all-FF packet is recognized as arbitration. */
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = true;
            req_pkt.slave_id     = data[0];
            req_pkt.function     = data[1];
            memcpy(req_pkt.data, data, len);
            req_pkt.data_len     = (uint16_t)len;
            enqueue_req = true;
            ctx->last_was_fast_modbus = true;
        } else {
            /* Normal unicast request: save and wait for response */
            size_t copy_len = len < SNIFFER_MAX_PACKET_LEN ? len : SNIFFER_MAX_PACKET_LEN;
            memcpy(ctx->req_buf, data, copy_len);
            ctx->req_len          = (uint16_t)copy_len;
            ctx->req_timestamp_us = (uint64_t)esp_timer_get_time();
            ctx->state            = SNIFF_RES_WAIT;
            should_start_timer    = true;
            ctx->last_was_fast_modbus = false;
        }
    } else { /* SNIFF_RES_WAIT */
        /* Slave responses may be preceded by Fast Modbus arbitration 0xFF bytes.
         * Strip them only when the pattern is recognized (leading 0xFF + Fast Modbus func code). */
        uint8_t *effective;
        size_t effective_len;
        strip_arbitration(data, len, &effective, &effective_len);

        if (effective_len < 4) {
            /* Arbitration-only response (e.g. FF FF FF FF FF) — flush the
             * buffered request so the state machine stays in phase. */
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = ctx->req_timestamp_us;
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = true;
            req_pkt.slave_id     = ctx->req_buf[0];
            req_pkt.function     = ctx->req_buf[1];
            memcpy(req_pkt.data, ctx->req_buf, ctx->req_len);
            req_pkt.data_len     = ctx->req_len;
            enqueue_req = true;

            res_pkt.port         = (uint8_t)port_index;
            res_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            res_pkt.is_master    = false;
            res_pkt.crc_valid    = false;
            res_pkt.slave_id     = data[0];
            res_pkt.function     = data[1];
            size_t arb_cpy       = len < SNIFFER_MAX_PACKET_LEN ? len : SNIFFER_MAX_PACKET_LEN;
            memcpy(res_pkt.data, data, arb_cpy);
            res_pkt.data_len     = (uint16_t)arb_cpy;
            enqueue_res = true;

            ctx->state = SNIFF_IDLE;
            goto exit_critical;
        }

        should_stop_timer = true;

        /* If the packet arriving in RES_WAIT is itself a Fast Modbus master broadcast
         * (0xFD + FC46/60), we caught the state machine out of phase. Discard the
         * buffered request, emit the FD 46 as a standalone MASTER packet and mark the
         * flag so the following all-FF arbitration is recognized. */
        if (effective[0] == 0xFD &&
            (effective[1] == FAST_MODBUS_FUNC_1 || effective[1] == FAST_MODBUS_FUNC_2)) {
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = crc_check(effective, effective_len);
            req_pkt.slave_id     = effective[0];
            req_pkt.function     = effective[1];
            size_t fm_cpy        = len < SNIFFER_MAX_PACKET_LEN ? len : SNIFFER_MAX_PACKET_LEN;
            memcpy(req_pkt.data, data, fm_cpy);
            req_pkt.data_len     = (uint16_t)fm_cpy;
            enqueue_req = true;
            ctx->last_was_fast_modbus = true;
            ctx->state = SNIFF_IDLE;
            goto exit_critical;
        }

        req_pkt.port         = (uint8_t)port_index;
        req_pkt.timestamp_us = ctx->req_timestamp_us;
        req_pkt.is_master    = true;
        req_pkt.crc_valid    = true;
        req_pkt.slave_id     = ctx->req_buf[0];
        req_pkt.function     = ctx->req_buf[1];
        memcpy(req_pkt.data, ctx->req_buf, ctx->req_len);
        req_pkt.data_len     = ctx->req_len;
        enqueue_req = true;

        res_pkt.port         = (uint8_t)port_index;
        res_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
        res_pkt.is_master    = false;
        res_pkt.crc_valid    = crc_check(effective, effective_len);
        res_pkt.slave_id     = effective[0];
        res_pkt.function     = effective[1];
        size_t copy_len      = len < SNIFFER_MAX_PACKET_LEN ? len : SNIFFER_MAX_PACKET_LEN;
        memcpy(res_pkt.data, data, copy_len);
        res_pkt.data_len     = (uint16_t)copy_len;
        enqueue_res = true;

        ctx->last_was_fast_modbus = false;
        ctx->state = SNIFF_IDLE;
    }
exit_critical:
    taskEXIT_CRITICAL(&sniff_mux);

    if (should_stop_timer) xTimerStop(ctx->resp_timer, 0);
    if (enqueue_req) try_enqueue(port_index, &req_pkt);
    if (enqueue_res) try_enqueue(port_index, &res_pkt);
    if (should_start_timer) xTimerStart(ctx->resp_timer, 0);
}

static void sniffer_receive_cb_0(serial_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    sniffer_process(0, data, len);
}

static void sniffer_receive_cb_1(serial_desc_t *desc, uint8_t *data, size_t len)
{
    (void)desc;
    sniffer_process(1, data, len);
}

static const serial_receive_handler_t s_port_callbacks[BRIDGES_COUNT] = {
    sniffer_receive_cb_0,
    sniffer_receive_cb_1,
};


static void sniffer_ws_task(void *arg)
{
    (void)arg;
    sniff_packet_t pkt;
    char hex_str[SNIFFER_MAX_PACKET_LEN * 2 + 1];
    char *json_buf = malloc(SNIFFER_JSON_BUF_SIZE);

    if (!json_buf) {
        ESP_LOGE(TAG, "Failed to allocate WS JSON buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        xQueueReceive(sniff_queue, &pkt, portMAX_DELAY);

        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        int fd = ws_client_fd;
        httpd_handle_t srv = ws_server;
        xSemaphoreGive(ws_mutex);

        if (fd == -1 || srv == NULL) continue;

        packet_counter++;

        if (pkt.is_timeout) {
            snprintf(json_buf, SNIFFER_JSON_BUF_SIZE,
                "{\"type\":\"timeout\",\"id\":%" PRIu32 ",\"port\":%u"
                ",\"timestamp_us\":%" PRIu64
                ",\"slave_id\":%u,\"function\":%u}",
                packet_counter, port_index_to_name(pkt.port), pkt.timestamp_us,
                pkt.slave_id, pkt.function);
        } else {
            bytes_to_hex(pkt.data, pkt.data_len, hex_str, sizeof(hex_str));
            snprintf(json_buf, SNIFFER_JSON_BUF_SIZE,
                "{\"type\":\"packet\",\"id\":%" PRIu32 ",\"port\":%u"
                ",\"timestamp_us\":%" PRIu64
                ",\"dir\":\"%s\",\"slave_id\":%u,\"function\":%u"
                ",\"crc_valid\":%s,\"raw\":\"%s\",\"size\":%u}",
                packet_counter, port_index_to_name(pkt.port), pkt.timestamp_us,
                pkt.is_master ? "master" : "slave",
                pkt.slave_id, pkt.function,
                pkt.crc_valid ? "true" : "false",
                hex_str, pkt.data_len);
        }

        httpd_ws_frame_t ws_frame = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json_buf,
            .len     = strlen(json_buf),
            .final   = true,
        };

        esp_err_t ret = httpd_ws_send_frame_async(srv, fd, &ws_frame);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WS send failed (%d), dropping client", ret);
            xSemaphoreTake(ws_mutex, portMAX_DELAY);
            ws_client_fd = -1;
            xSemaphoreGive(ws_mutex);
        }
    }
}

static esp_err_t sniffer_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        ws_server    = req->handle;
        ws_client_fd = httpd_req_to_sockfd(req);
        xSemaphoreGive(ws_mutex);
        ESP_LOGI(TAG, "WS client connected, fd=%d", ws_client_fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len == 0 || ws_pkt.type != HTTPD_WS_TYPE_TEXT) return ESP_OK;

    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    buf[ws_pkt.len] = '\0';

    cJSON *root = cJSON_Parse((char *)buf);
    free(buf);

    if (!root) return ESP_OK;

    cJSON *cmd  = cJSON_GetObjectItem(root, "cmd");
    cJSON *port = cJSON_GetObjectItem(root, "port");

    if (cmd && cJSON_IsString(cmd) && cJSON_IsNumber(port)) {
        bool enable = (strcmp(cmd->valuestring, "start") == 0);
        unsigned idx = port_name_to_index((unsigned)port->valuedouble);
        if (idx < BRIDGES_COUNT) {
            if (enable) sniffer_enable(idx);
            else        sniffer_disable(idx);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t sniffer_status_handler(httpd_req_t *req)
{
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"port_%u\":%s,\"port_%u\":%s}",
        port_index_to_name(0), sniff_ctx[0].enabled ? "true" : "false",
        port_index_to_name(1), sniff_ctx[1].enabled ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, (ssize_t)strlen(resp));
    return ESP_OK;
}

static const httpd_uri_t sniffer_ws_uri = {
    .uri          = "/sniffer/ws",
    .method       = HTTP_GET,
    .handler      = sniffer_ws_handler,
    .is_websocket = true,
};

static const httpd_uri_t sniffer_status_uri = {
    .uri     = "/sniffer/status",
    .method  = HTTP_GET,
    .handler = sniffer_status_handler,
};


esp_err_t sniffer_init(void)
{
    ws_mutex = xSemaphoreCreateMutex();
    if (!ws_mutex) {
        ESP_LOGE(TAG, "Failed to create WS mutex");
        return ESP_ERR_NO_MEM;
    }

    sniff_queue = xQueueCreate(SNIFFER_QUEUE_LEN, sizeof(sniff_packet_t));
    if (!sniff_queue) {
        ESP_LOGE(TAG, "Failed to create sniffer queue");
        return ESP_ERR_NO_MEM;
    }

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        sniff_ctx[i].state      = SNIFF_IDLE;
        sniff_ctx[i].enabled    = false;
        sniff_ctx[i].port_index = i;
        sniff_ctx[i].resp_timer = xTimerCreate(
            "sniff_resp",
            pdMS_TO_TICKS(SNIFFER_RESP_TIMEOUT_MS),
            pdFALSE,
            (void *)(uintptr_t)i,
            resp_timer_cb);
        if (!sniff_ctx[i].resp_timer) {
            ESP_LOGE(TAG, "Failed to create resp_timer for port %u", i);
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t res = xTaskCreate(sniffer_ws_task, "sniffer_ws",
        SNIFFER_WS_TASK_STACK, NULL, SNIFFER_WS_TASK_PRIO, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sniffer WS task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sniffer initialized");
    return ESP_OK;
}

void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc)
{
    if (port_index >= BRIDGES_COUNT) return;
    serial_desc->sniff_handler = s_port_callbacks[port_index];
}

void sniffer_enable(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) return;
    sniff_ctx[port_index].enabled = true;
    ESP_LOGI(TAG, "Sniffer enabled on port %u", port_index);
}

void sniffer_disable(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) return;
    sniff_ctx[port_index].enabled = false;
    xTimerStop(sniff_ctx[port_index].resp_timer, 0);
    sniff_ctx[port_index].state = SNIFF_IDLE;
    ESP_LOGI(TAG, "Sniffer disabled on port %u", port_index);
}

esp_err_t sniffer_register_handlers(httpd_handle_t server)
{
    esp_err_t ret = httpd_register_uri_handler(server, &sniffer_ws_uri);
    if (ret != ESP_OK) return ret;
    return httpd_register_uri_handler(server, &sniffer_status_uri);
}
