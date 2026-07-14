#include "cache_modbus_server.h"
#include "cache_multimaster.h"
#include "mb_device.h"
#include "modbus_helpers.h"
#include "tcp_server.h"
#include "tcp_desc.h"
#include "mbtcp_reasm.h"
#include "setting_items.h"
#include "cache_modbus_server_priv.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "cache_mb_srv";

/* Single tcp_desc handle for the listener */
static tcp_desc_t *s_tcp_desc = NULL;

/* TCP port the server is currently listening on; 0 if not initialized */
static int s_port = 0;

/* ---- Modbus exception codes (aliases of the canonical modbus_helpers set) - */

#define MB_EX_ILLEGAL_FUNCTION   MODBUS_EXC_ILLEGAL_FUNCTION
#define MB_EX_ILLEGAL_ADDRESS    MODBUS_EXC_ILLEGAL_ADDRESS
#define MB_EX_ILLEGAL_DATA_VALUE MODBUS_EXC_ILLEGAL_DATA_VALUE
#define MB_EX_GW_TARGET_FAILED   MODBUS_EXC_GW_TARGET_FAILED  /* Gateway target device failed to respond */

/* ---- Modbus function codes supported (aliases of the canonical set) ------- */

#define MB_FC_READ_COILS            MODBUS_FC_READ_COILS
#define MB_FC_READ_DISCRETE_INPUTS  MODBUS_FC_READ_DISCRETE_INPUTS
#define MB_FC_READ_HOLDING_REGS     MODBUS_FC_READ_HOLDING_REGS
#define MB_FC_READ_INPUT_REGS       MODBUS_FC_READ_INPUT_REGS

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

    /* transaction_id is already in network byte order, echoed verbatim. */
    size_t len = modbus_pdu_build_exception(buf, transaction_id, unit_id, fc, exception);

    tcp_server_send(desc, client_sock, buf, len);
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
    resp_hdr->protocol_id    = MODBUS_TCP_PROTOCOL_ID;
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
    resp_hdr->protocol_id    = MODBUS_TCP_PROTOCOL_ID;
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
    /* ---- 1. Validate Modbus TCP framing ----------------------------------
     * modbus_tcp_check_request() starts with the very length check that used to
     * be duplicated here (len >= sizeof(mb_tcp_header_t)), then validates the
     * protocol id and the MBAP length field. One call covers all of it. */
    if (modbus_tcp_check_request(data, len) != ESP_OK) {
        ESP_LOGW(TAG, "sock=%d: invalid Modbus TCP framing (%u bytes)",
                 client_sock, (unsigned)len);
        return;
    }

    /* ---- 2. Parse MBAP header -------------------------------------------- */
    mb_tcp_header_t req_hdr;
    memcpy(&req_hdr, data, sizeof(req_hdr));

    uint16_t transaction_id = req_hdr.transaction_id; /* keep in network order */
    uint8_t  unit_id        = req_hdr.unit_id;
    uint8_t  fc             = req_hdr.function;

    /* Requests addressed to the gateway itself (Unit ID 0xFF) are served from the
     * built-in device-info register map, independently of the cache state. */
    if (mb_device_is_self(unit_id)) {
        /* This callback runs inside tcp_server's per-connection receiver task, so
         * the stack the device registers report is tcp_server's own — take it from
         * tcp_server.h rather than keeping a copy that can drift out of sync and
         * make REG_STACK_SIZE / REG_MAX_STACK_USED lie. */
        uint8_t dev_resp[MODBUS_TCP_MAX_ADU_LEN];
        size_t dev_len = mb_device_handle_self_request(data, len,
                                                       TCP_SERVER_TASK_STACK_SIZE, dev_resp);
        tcp_server_send(desc, client_sock, dev_resp, dev_len);
        return;
    }

    /* ---- 3. Check that the cache is enabled -------------------------------- */
    if (!cache_multimaster_is_enabled()) {
        ESP_LOGD(TAG, "sock=%d: cache disabled, returning exception 0x02", client_sock);
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_ADDRESS);
        return;
    }

    /* Read the configured value timeout once per request.
     * 0 means no timeout (always return cached value). */
    uint16_t value_timeout_s = (uint16_t)setting_items_read_int(KEY_CACHE_VALUE_TIMEOUT_S);

    /* ---- 4. Filter supported function codes -------------------------------- */
    if (fc != MB_FC_READ_COILS        &&
        fc != MB_FC_READ_DISCRETE_INPUTS &&
        fc != MB_FC_READ_HOLDING_REGS &&
        fc != MB_FC_READ_INPUT_REGS) {
        ESP_LOGD(TAG, "sock=%d: unsupported FC 0x%02X", client_sock, fc);
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_FUNCTION);
        return;
    }

    /* ---- 5. Require at least 4 PDU bytes after the MBAP header ------------ */
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
    uint16_t start_addr = 0;
    uint16_t count      = 0;
    modbus_pdu_parse_read_request(pdu, &start_addr, &count);

    /* The register and coil paths differ only in their count limit and in which
     * builder assembles the response — everything around that is identical, so
     * select both here and run a single validate-build-send path below. */
    const bool is_reg = (fc == MB_FC_READ_HOLDING_REGS || fc == MB_FC_READ_INPUT_REGS);
    const uint16_t max_count = is_reg ? MB_MAX_REGISTERS : MB_MAX_COILS;
    const cache_mb_response_builder_t build = is_reg
                                              ? cache_modbus_server_build_register_response
                                              : cache_modbus_server_build_coil_response;

    /* ---- 6. Validate count ------------------------------------------------- */
    if (count == 0 || count > max_count) {
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_DATA_VALUE);
        return;
    }

    /* ---- 7. Build response ------------------------------------------------- */
    /* Maximum response buffer:
     *   MBAP header  = 8 bytes
     *   byte_count   = 1 byte
     *   data payload = max 250 bytes (125 regs × 2) or ceil(2000/8)=250 bytes
     * 512 bytes is more than enough.                                         */
    uint8_t resp_buf[512];
    uint8_t exception_code = MB_EX_ILLEGAL_ADDRESS; /* safe default if builder forgot to set */

    size_t resp_len = build(unit_id, fc, transaction_id, start_addr, count,
                            value_timeout_s, resp_buf, &exception_code);
    if (resp_len == 0) {
        send_exception(desc, client_sock, transaction_id, unit_id, fc, exception_code);
        return;
    }
    tcp_server_send(desc, client_sock, resp_buf, resp_len);
}

/* ---- TCP receive path ----------------------------------------------------- *
 * TCP is a byte stream: one recv() may deliver a partial frame or several
 * coalesced. Stream reassembly lives in bridge/mbtcp_reasm — the same module the
 * Modbus TCP gateway uses, so both servers frame identically.
 * ========================================================================== */

static mbtcp_reasm_t s_reasm;

/* Reassembler callback: one complete Modbus TCP ADU. user_ctx is the tcp_desc. */
static bool on_reasm_frame(void *user_ctx, int sock, const uint8_t *frame, size_t len)
{
    process_one_frame((tcp_desc_t *)user_ctx, sock, (uint8_t *)frame, len);
    return true;
}

/* TCP receive callback: reassemble the byte stream into whole Modbus frames. */
static void process_data_from_tcp(tcp_desc_t *desc, int client_sock,
                                  uint8_t *data, size_t len)
{
    int rc = mbtcp_reasm_feed(&s_reasm, client_sock, data, len, on_reasm_frame, desc);

    if (rc == MBTCP_REASM_NO_SLOT) {
        /* More simultaneous connections than the reassembler can track. Best
         * effort: treat this recv() as one whole frame. A frame that arrives
         * complete still works; one split across recvs on this connection does
         * not. mbtcp_reasm counts the condition (cache-mb-framing-2). */
        process_one_frame(desc, client_sock, data, len);
    }
}

/* Connection-close hook: release this socket's reassembly slot. */
static void on_conn_close(tcp_desc_t *desc, int client_sock)
{
    (void)desc;
    mbtcp_reasm_close(&s_reasm, client_sock);
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

    if (!mbtcp_reasm_init(&s_reasm, TAG)) {
        ESP_LOGE(TAG, "Failed to create the reassembly mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = tcp_server_init(port, process_data_from_tcp, &s_tcp_desc);
    if (ret != ESP_OK) {
        mbtcp_reasm_deinit(&s_reasm);
        return ret;
    }
    s_port = port;
    s_tcp_desc->close_handler = on_conn_close;   /* free reassembly slot on close */
    return ESP_OK;
}

esp_err_t cache_modbus_server_deinit(void)
{
    if (s_tcp_desc == NULL) return ESP_OK;
    esp_err_t ret = tcp_server_deinit(s_tcp_desc);   /* waits for the receiver tasks to exit */
    if (ret == ESP_OK) {
        /* Safe now: no task can still be inside mbtcp_reasm_feed(). */
        mbtcp_reasm_deinit(&s_reasm);
        s_tcp_desc = NULL;
        s_port = 0;
    }
    return ret;
}

int cache_modbus_server_get_port(void)
{
    return s_port;
}
