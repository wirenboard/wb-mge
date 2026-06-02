#include "cache_modbus_server.h"
#include "cache_multimaster.h"
#include "mb_device.h"
#include "modbus_helpers.h"
#include "tcp_server.h"
#include "tcp_desc.h"
#include "setting_items.h"
#include "cache_modbus_server_internal.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stddef.h>
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "cache_mb_srv";

/* Single tcp_desc handle for the listener */
static tcp_desc_t *s_tcp_desc = NULL;

/* TCP port the server is currently listening on; 0 if not initialized */
static int s_port = 0;

/* ---- Modbus exception codes ---------------------------------------------- */

#define MB_EX_ILLEGAL_FUNCTION   0x01
#define MB_EX_ILLEGAL_ADDRESS    0x02
#define MB_EX_ILLEGAL_DATA_VALUE 0x03
#define MB_EX_GW_TARGET_FAILED   0x0B  /* Gateway target device failed to respond */

/* ---- Modbus function codes supported ------------------------------------- */

#define MB_FC_READ_COILS            0x01
#define MB_FC_READ_DISCRETE_INPUTS  0x02
#define MB_FC_READ_HOLDING_REGS     0x03
#define MB_FC_READ_INPUT_REGS       0x04

/* ---- Modbus limits (per spec) -------------------------------------------- */

#define MB_MAX_REGISTERS 125   /* FC03/FC04: max 125 registers per request  */
#define MB_MAX_COILS     2000  /* FC01/FC02: max 2000 coils per request     */

/* ---- Internal helpers ---------------------------------------------------- */

/**
 * @brief Send a Modbus TCP exception response to the client.
 *
 * Builds a complete MBAP header with error PDU and sends it over TCP.
 *
 * @param desc          tcp_desc for the current server instance.
 * @param client_sock   Client socket file descriptor.
 * @param transaction_id Transaction ID echoed from the request.
 * @param unit_id       Unit ID echoed from the request.
 * @param fc            Original function code from the request.
 * @param exception     Modbus exception code (e.g. MB_EX_ILLEGAL_ADDRESS).
 */
static void send_exception(tcp_desc_t *desc, int client_sock,
                            uint16_t transaction_id, uint8_t unit_id,
                            uint8_t fc, uint8_t exception)
{
    /* Exception PDU: [FC|0x80][exception_code] — 2 bytes.
     * MBAP length field = unit_id (1) + FC (1) + exception_code (1) = 3. */
    uint8_t buf[sizeof(mb_tcp_header_t) + 1]; /* header + 1 byte exception */
    mb_tcp_header_t *hdr = (mb_tcp_header_t *)buf;

    hdr->transaction_id = transaction_id;        /* already network byte order */
    hdr->protocol_id    = 0x0000;
    hdr->length         = htons(3);              /* unit_id + FC|0x80 + code  */
    hdr->unit_id        = unit_id;
    hdr->function       = (uint8_t)(fc | 0x80u);

    buf[sizeof(mb_tcp_header_t)] = exception;

    tcp_server_send(desc, client_sock, buf, sizeof(buf));
}

/* ---- Response builder functions ------------------------------------------ */

/**
 * @brief Build an FC03 or FC04 register response into resp_buf.
 *
 * Fills the MBAP header (transaction_id, protocol_id=0, unit_id, fc, length)
 * and the payload [byte_count][val0_hi][val0_lo]...[valN_hi][valN_lo].
 *
 * @param unit_id           Modbus unit ID (slave address) from the request.
 * @param fc                Function code: MB_FC_READ_HOLDING_REGS or MB_FC_READ_INPUT_REGS.
 * @param transaction_id    Transaction ID in network byte order, echoed from the request.
 * @param start_addr        Starting register address (0-based).
 * @param count             Number of registers to read (must be 1..125).
 * @param value_timeout_s   Age threshold in seconds passed to cache_multimaster_lookup().
 * @param resp_buf          Output buffer of at least (8 + 1 + count*2) bytes.
 * @param exception_code_out Output: set to 0x02 if the address range overflows the
 *                           16-bit space (start_addr + count > 0x10000), 0x02 if the
 *                           register is not found in cache, or 0x0B if the entry is
 *                           stale; not modified on success.
 * @return Total byte count to send on success; 0 on overflow or lookup failure.
 */
size_t cache_modbus_server_build_register_response(
    uint8_t unit_id, uint8_t fc, uint16_t transaction_id,
    uint16_t start_addr, uint16_t count, uint16_t value_timeout_s,
    uint8_t *resp_buf, uint8_t *exception_code_out)
{
    mb_tcp_header_t *resp_hdr = (mb_tcp_header_t *)resp_buf;

    /* Fill MBAP header fields */
    resp_hdr->transaction_id = transaction_id;
    resp_hdr->protocol_id    = 0x0000;
    resp_hdr->unit_id        = unit_id;
    resp_hdr->function       = fc;

    /* Defensive count guard: this is a public function whose resp_buf size
     * contract (and the uint8_t byte_count below) only holds for a protocol-legal
     * count. The caller validates count, but guard here too so the builder is
     * self-safe and byte_count = count*2 can never overflow a uint8_t. */
    if (count == 0 || count > MB_MAX_REGISTERS) {
        *exception_code_out = MB_EX_ILLEGAL_DATA_VALUE;
        return 0;
    }

    /* Reject requests where the address range overflows the 16-bit address space.
     * Per Modbus spec: start_addr + count must not exceed 0x10000. */
    if ((uint32_t)start_addr + (uint32_t)count > 0x10000u) {
        *exception_code_out = MB_EX_ILLEGAL_ADDRESS;
        return 0;
    }

    /* Payload: [byte_count][val0_hi][val0_lo]...[valN_hi][valN_lo] */
    uint8_t *payload    = resp_buf + sizeof(mb_tcp_header_t);
    uint8_t  byte_count = (uint8_t)(count * 2u);
    payload[0] = byte_count;

    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = 0;
        cache_lookup_result_t res = cache_multimaster_lookup(
            unit_id, fc, (uint16_t)(start_addr + i), &value, value_timeout_s);
        if (res == CACHE_LOOKUP_NOT_FOUND) {
            /* Register not in cache — return exception 0x02 (ILLEGAL DATA ADDRESS) */
            *exception_code_out = MB_EX_ILLEGAL_ADDRESS;
            return 0;
        }
        if (res == CACHE_LOOKUP_STALE) {
            /* Entry exists but is stale — return exception 0x0B (GW TARGET FAILED) */
            *exception_code_out = MB_EX_GW_TARGET_FAILED;
            return 0;
        }
        /* res == CACHE_LOOKUP_FOUND: pack value big-endian */
        payload[1 + i * 2]     = (uint8_t)(value >> 8);
        payload[1 + i * 2 + 1] = (uint8_t)(value & 0xFFu);
    }

    /* MBAP length = unit_id(1) + FC(1) + byte_count_field(1) + data(byte_count) */
    uint16_t pdu_len = (uint16_t)(1u + 1u + 1u + byte_count);
    resp_hdr->length = htons(pdu_len);

    /* Total wire bytes = sizeof(mb_tcp_header_t) + 1 (byte_count field) + byte_count */
    return sizeof(mb_tcp_header_t) + 1u + (size_t)byte_count;
}

/**
 * @brief Build an FC01 or FC02 coil/discrete-input response into resp_buf.
 *
 * Fills the MBAP header and payload [coil_bytes][byte0]...[byteN]
 * with LSB-first bit packing.
 *
 * @param unit_id           Modbus unit ID (slave address) from the request.
 * @param fc                Function code: MB_FC_READ_COILS or MB_FC_READ_DISCRETE_INPUTS.
 * @param transaction_id    Transaction ID in network byte order, echoed from the request.
 * @param start_addr        Starting coil address (0-based).
 * @param count             Number of coils to read (must be 1..2000).
 * @param value_timeout_s   Age threshold in seconds passed to cache_multimaster_lookup().
 * @param resp_buf          Output buffer of at least (8 + 1 + ceil(count/8)) bytes.
 * @param exception_code_out Output: set to 0x02 if the address range overflows the
 *                           16-bit space (start_addr + count > 0x10000), 0x02 if the
 *                           coil is not found in cache, or 0x0B if the entry is stale;
 *                           not modified on success.
 * @return Total byte count to send on success; 0 on overflow or lookup failure.
 */
size_t cache_modbus_server_build_coil_response(
    uint8_t unit_id, uint8_t fc, uint16_t transaction_id,
    uint16_t start_addr, uint16_t count, uint16_t value_timeout_s,
    uint8_t *resp_buf, uint8_t *exception_code_out)
{
    mb_tcp_header_t *resp_hdr = (mb_tcp_header_t *)resp_buf;

    /* Fill MBAP header fields */
    resp_hdr->transaction_id = transaction_id;
    resp_hdr->protocol_id    = 0x0000;
    resp_hdr->unit_id        = unit_id;
    resp_hdr->function       = fc;

    /* Defensive count guard — see build_register_response. Keeps the builder
     * self-safe (coil_bytes = ceil(count/8) cannot overflow a uint8_t and the
     * payload fits resp_buf) regardless of the caller's own validation. */
    if (count == 0 || count > MB_MAX_COILS) {
        *exception_code_out = MB_EX_ILLEGAL_DATA_VALUE;
        return 0;
    }

    /* Reject requests where the address range overflows the 16-bit address space.
     * Per Modbus spec: start_addr + count must not exceed 0x10000. */
    if ((uint32_t)start_addr + (uint32_t)count > 0x10000u) {
        *exception_code_out = MB_EX_ILLEGAL_ADDRESS;
        return 0;
    }

    /* Payload: [coil_bytes][byte0]...[byteN] (bits packed LSB first) */
    uint8_t  coil_bytes = (uint8_t)((count + 7u) / 8u);
    uint8_t *payload    = resp_buf + sizeof(mb_tcp_header_t);

    payload[0] = coil_bytes;
    memset(payload + 1, 0, coil_bytes); /* clear bit buffer */

    for (uint16_t i = 0; i < count; i++) {
        uint16_t value = 0;
        cache_lookup_result_t res = cache_multimaster_lookup(
            unit_id, fc, (uint16_t)(start_addr + i), &value, value_timeout_s);
        if (res == CACHE_LOOKUP_NOT_FOUND) {
            /* Coil not in cache — return exception 0x02 (ILLEGAL DATA ADDRESS) */
            *exception_code_out = MB_EX_ILLEGAL_ADDRESS;
            return 0;
        }
        if (res == CACHE_LOOKUP_STALE) {
            /* Entry exists but is stale — return exception 0x0B (GW TARGET FAILED) */
            *exception_code_out = MB_EX_GW_TARGET_FAILED;
            return 0;
        }
        /* res == CACHE_LOOKUP_FOUND: set bit i in the coil byte array (LSB first) */
        if (value) {
            payload[1 + i / 8] |= (uint8_t)(1u << (i % 8));
        }
    }

    /* MBAP length = unit_id(1) + FC(1) + byte_count_field(1) + coil_bytes */
    uint16_t pdu_len = (uint16_t)(1u + 1u + 1u + coil_bytes);
    resp_hdr->length = htons(pdu_len);

    /* Total wire bytes = sizeof(mb_tcp_header_t) + 1 (coil_bytes field) + coil_bytes */
    return sizeof(mb_tcp_header_t) + 1u + (size_t)coil_bytes;
}

/* ---- TCP receive callback ------------------------------------------------- */

/**
 * @brief Process one complete Modbus TCP ADU received from a client.
 *
 * Handles FC01/FC02/FC03/FC04 read requests by looking up values in the
 * in-memory register cache.  All other function codes receive a Modbus
 * exception 0x01 (ILLEGAL FUNCTION).  Missing cache entries result in
 * exception 0x02 (ILLEGAL DATA ADDRESS).
 *
 * This function expects exactly one complete ADU in data[0..len-1].
 * Stream reassembly (splitting/coalescing) is handled by process_data_from_tcp().
 */
static void process_one_frame(tcp_desc_t *desc, int client_sock,
                               uint8_t *data, size_t len)
{
    /* ---- 1. Basic length check ------------------------------------------ */
    if (len < sizeof(mb_tcp_header_t)) {
        ESP_LOGW(TAG, "sock=%d: request too short (%u bytes)", client_sock, (unsigned)len);
        return;
    }

    /* ---- 2. Validate Modbus TCP framing (protocol_id, length field) ------- */
    if (modbus_tcp_check_request(data, len) != ESP_OK) {
        ESP_LOGW(TAG, "sock=%d: invalid Modbus TCP framing", client_sock);
        return;
    }

    /* ---- 3. Parse MBAP header -------------------------------------------- */
    mb_tcp_header_t req_hdr;
    memcpy(&req_hdr, data, sizeof(req_hdr));

    uint16_t transaction_id = req_hdr.transaction_id; /* keep in network order */
    uint8_t  unit_id        = req_hdr.unit_id;
    uint8_t  fc             = req_hdr.function;

    /* Requests addressed to the gateway itself (Unit ID 0xFF) are served from the
     * built-in device-info register map, independently of the cache state. */
    if (mb_device_is_self(unit_id)) {
        /* Cache server task runs inside tcp_server's receiver_task; its stack is
         * TCP_SERVER_TASK_STACK_SIZE (see bridge/tcp_server.c). */
        #define CACHE_SRV_TASK_STACK_BYTES 4096u
        uint8_t dev_resp[260];
        size_t dev_len = mb_device_handle_self_request(data, len,
                                                       CACHE_SRV_TASK_STACK_BYTES, dev_resp);
        tcp_server_send(desc, client_sock, dev_resp, dev_len);
        return;
    }

    /* ---- 4. Check that the cache is enabled -------------------------------- */
    if (!cache_multimaster_is_enabled()) {
        ESP_LOGD(TAG, "sock=%d: cache disabled, returning exception 0x02", client_sock);
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_ADDRESS);
        return;
    }

    /* Read the configured value timeout once per request.
     * 0 means no timeout (always return cached value). */
    uint16_t value_timeout_s = (uint16_t)setting_items_read_int(KEY_CACHE_VALUE_TIMEOUT_S);

    /* ---- 5. Filter supported function codes -------------------------------- */
    if (fc != MB_FC_READ_COILS        &&
        fc != MB_FC_READ_DISCRETE_INPUTS &&
        fc != MB_FC_READ_HOLDING_REGS &&
        fc != MB_FC_READ_INPUT_REGS) {
        ESP_LOGD(TAG, "sock=%d: unsupported FC 0x%02X", client_sock, fc);
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_FUNCTION);
        return;
    }

    /* ---- 6. Require at least 4 PDU bytes after the MBAP header ------------ */
    /* PDU after MBAP header: [FC][start_hi][start_lo][count_hi][count_lo]
     * The header already consumed fc, so we need 4 more bytes in data.       */
    if (len < sizeof(mb_tcp_header_t) + 4) {
        ESP_LOGW(TAG, "sock=%d: PDU too short for FC 0x%02X", client_sock, fc);
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_FUNCTION);
        return;
    }

    /* Bytes immediately after the MBAP header (fc is already in header) */
    const uint8_t *pdu = data + sizeof(mb_tcp_header_t);
    /* pdu[0..1] = start address, pdu[2..3] = count                          */
    uint16_t start_addr = ((uint16_t)pdu[0] << 8) | pdu[1];
    uint16_t count      = ((uint16_t)pdu[2] << 8) | pdu[3];

    /* ---- 7. Validate count ------------------------------------------------- */
    if (fc == MB_FC_READ_HOLDING_REGS || fc == MB_FC_READ_INPUT_REGS) {
        if (count == 0 || count > MB_MAX_REGISTERS) {
            send_exception(desc, client_sock, transaction_id, unit_id, fc,
                           MB_EX_ILLEGAL_DATA_VALUE);
            return;
        }
    } else {
        /* FC01 / FC02 coils */
        if (count == 0 || count > MB_MAX_COILS) {
            send_exception(desc, client_sock, transaction_id, unit_id, fc,
                           MB_EX_ILLEGAL_DATA_VALUE);
            return;
        }
    }

    /* ---- 8. Build response ------------------------------------------------- */
    /* Maximum response buffer:
     *   MBAP header  = 8 bytes
     *   byte_count   = 1 byte
     *   data payload = max 250 bytes (125 regs × 2) or ceil(2000/8)=250 bytes
     * 512 bytes is more than enough.                                         */
    uint8_t resp_buf[512];

    if (fc == MB_FC_READ_HOLDING_REGS || fc == MB_FC_READ_INPUT_REGS) {
        uint8_t exception_code = MB_EX_ILLEGAL_ADDRESS; /* safe default if builder forgot to set */
        size_t resp_len = cache_modbus_server_build_register_response(
            unit_id, fc, transaction_id, start_addr, count, value_timeout_s,
            resp_buf, &exception_code);
        if (resp_len == 0) {
            send_exception(desc, client_sock, transaction_id, unit_id, fc, exception_code);
            return;
        }
        tcp_server_send(desc, client_sock, resp_buf, resp_len);
    } else {
        uint8_t exception_code = MB_EX_ILLEGAL_ADDRESS; /* safe default if builder forgot to set */
        size_t resp_len = cache_modbus_server_build_coil_response(
            unit_id, fc, transaction_id, start_addr, count, value_timeout_s,
            resp_buf, &exception_code);
        if (resp_len == 0) {
            send_exception(desc, client_sock, transaction_id, unit_id, fc, exception_code);
            return;
        }
        tcp_server_send(desc, client_sock, resp_buf, resp_len);
    }
}

/* ====================================================================== *
 * Bug 07 fix: TCP stream reassembly into whole Modbus frames.
 *
 * TCP is a byte stream: one recv() may deliver a partial frame OR several
 * frames coalesced. The old code assumed one recv() == one frame.
 * We keep a small per-connection accumulation buffer, parse the MBAP
 * length field to find frame boundaries, dispatch every complete frame,
 * and carry the remainder over to the next recv().
 * ====================================================================== */

#define CACHE_MB_MAX_CONNS  8
#define CACHE_MB_FRAME_MAX  300   /* > any Modbus TCP ADU we accept */

typedef struct {
    int     sock;                        /* -1 == free slot */
    size_t  len;
    uint8_t buf[CACHE_MB_FRAME_MAX];
} conn_reasm_t;

static conn_reasm_t      s_reasm[CACHE_MB_MAX_CONNS];
static SemaphoreHandle_t s_reasm_mutex = NULL;   /* guards slot alloc/free only */

/* Find (or allocate) the reassembly slot for a socket. */
static conn_reasm_t *reasm_get(int sock)
{
    conn_reasm_t *slot = NULL;
    if (s_reasm_mutex) { xSemaphoreTake(s_reasm_mutex, portMAX_DELAY); }
    conn_reasm_t *free_slot = NULL;
    for (int i = 0; i < CACHE_MB_MAX_CONNS; i++) {
        if (s_reasm[i].sock == sock) { slot = &s_reasm[i]; break; }
        if ((free_slot == NULL) && (s_reasm[i].sock == -1)) { free_slot = &s_reasm[i]; }
    }
    if ((slot == NULL) && free_slot) {
        free_slot->sock = sock;
        free_slot->len  = 0;
        slot = free_slot;
    }
    if (s_reasm_mutex) { xSemaphoreGive(s_reasm_mutex); }
    return slot;
}

static void reasm_free(int sock)
{
    if (s_reasm_mutex) { xSemaphoreTake(s_reasm_mutex, portMAX_DELAY); }
    for (int i = 0; i < CACHE_MB_MAX_CONNS; i++) {
        if (s_reasm[i].sock == sock) {
            s_reasm[i].sock = -1;
            s_reasm[i].len  = 0;
            break;
        }
    }
    if (s_reasm_mutex) { xSemaphoreGive(s_reasm_mutex); }
}

/* Total ADU length declared by the MBAP header (needs >= 6 bytes available). */
static size_t frame_total_len(const uint8_t *buf)
{
    uint16_t mbap_len = ((uint16_t)buf[4] << 8) | buf[5]; /* length field, big-endian */
    return (size_t)mbap_len + offsetof(mb_tcp_header_t, unit_id); /* +6 */
}

/* TCP receive callback: reassemble the byte stream into whole Modbus frames. */
static void process_data_from_tcp(tcp_desc_t *desc, int client_sock,
                                  uint8_t *data, size_t len)
{
    conn_reasm_t *c = reasm_get(client_sock);
    if (c == NULL) {
        /* Table full — best effort: process the buffer as a single frame. */
        process_one_frame(desc, client_sock, data, len);
        return;
    }

    size_t off = 0;
    while (off < len) {
        size_t space = CACHE_MB_FRAME_MAX - c->len;
        size_t chunk = len - off;
        if (chunk > space) { chunk = space; }
        memcpy(c->buf + c->len, data + off, chunk);
        c->len += chunk;
        off    += chunk;

        /* Dispatch every complete frame currently buffered. */
        size_t pos = 0;
        while ((c->len - pos) >= sizeof(mb_tcp_header_t)) {
            size_t flen = frame_total_len(c->buf + pos);
            if ((flen < sizeof(mb_tcp_header_t)) || (flen > CACHE_MB_FRAME_MAX)) {
                pos = c->len;   /* bogus length -> drop to resync */
                break;
            }
            if ((c->len - pos) < flen) { break; }   /* frame not complete yet */
            process_one_frame(desc, client_sock, c->buf + pos, flen);
            pos += flen;
        }
        if (pos > 0) {
            memmove(c->buf, c->buf + pos, c->len - pos);
            c->len -= pos;
        }
        /* Buffer full but no complete frame -> desynced/oversized; drop. */
        if (c->len == CACHE_MB_FRAME_MAX) {
            ESP_LOGW(TAG, "sock=%d: reassembly buffer full, resync (drop)", client_sock);
            c->len = 0;
        }
    }
}

/* Connection-close hook: release this socket's reassembly slot. */
static void on_conn_close(tcp_desc_t *desc, int client_sock)
{
    (void)desc;
    reasm_free(client_sock);
}

/* ---- Public API ---------------------------------------------------------- */

esp_err_t cache_modbus_server_init(int port)
{
    /* Idempotency guard (persist-1): a second init without an intervening
     * deinit would overwrite s_tcp_desc and orphan the first descriptor, its
     * acceptor task and listen socket (an unrecoverable leak — deinit only
     * frees the latest). This can happen at boot: the HTTP server is up before
     * port_manager_init(), so a POST /settings can call init(port) first and
     * port_manager_init() then calls it again. Make init idempotent. */
    if (s_tcp_desc != NULL) {
        if (port == s_port) {
            ESP_LOGD(TAG, "cache Modbus server already running on port %d — init is a no-op", port);
            return ESP_OK;
        }
        /* Port changed: tear down the running instance first so it is not
         * leaked, then fall through to start on the new port. */
        ESP_LOGW(TAG, "cache Modbus server re-init: port %d -> %d, restarting", s_port, port);
        esp_err_t de = cache_modbus_server_deinit();
        if (de != ESP_OK) {
            ESP_LOGE(TAG, "cache Modbus server re-init: deinit failed (%d), aborting", de);
            return de;
        }
    }

    ESP_LOGI(TAG, "Starting cache Modbus TCP server on port %d", port);

    /* Init per-connection reassembly state (bug 07 fix). */
    for (int i = 0; i < CACHE_MB_MAX_CONNS; i++) {
        s_reasm[i].sock = -1;
        s_reasm[i].len  = 0;
    }
    if (s_reasm_mutex == NULL) {
        s_reasm_mutex = xSemaphoreCreateMutex();
    }

    esp_err_t ret = tcp_server_init(port, process_data_from_tcp, &s_tcp_desc);
    if (ret == ESP_OK) {
        s_port = port;
        s_tcp_desc->close_handler = on_conn_close;   /* free reassembly slot on close */
    }
    return ret;
}

esp_err_t cache_modbus_server_deinit(void)
{
    if (s_tcp_desc == NULL) return ESP_OK;
    esp_err_t ret = tcp_server_deinit(s_tcp_desc);
    if (ret == ESP_OK) {
        s_tcp_desc = NULL;
        s_port = 0;
    }
    return ret;
}

int cache_modbus_server_get_port(void)
{
    return s_port;
}

#ifdef __unittest_env__
/* Thin shim exposing the static callback for unit tests. */
void cache_modbus_server_test_process(tcp_desc_t *desc, int client_sock,
                                       uint8_t *data, size_t len)
{
    process_data_from_tcp(desc, client_sock, data, len);
}

void cache_modbus_server_test_reset(void)
{
    for (int i = 0; i < CACHE_MB_MAX_CONNS; i++) {
        s_reasm[i].sock = -1;
        s_reasm[i].len  = 0;
    }
    s_reasm_mutex = NULL;
    s_tcp_desc    = NULL;
    s_port        = 0;
}
#endif /* __unittest_env__ */
