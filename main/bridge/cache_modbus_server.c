#include "cache_modbus_server.h"
#include "cache_multimaster.h"
#include "modbus_helpers.h"
#include "tcp_server.h"
#include "tcp_desc.h"

#include "esp_log.h"

#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "cache_mb_srv";

/* Single tcp_desc handle for the listener */
static tcp_desc_t *s_tcp_desc = NULL;

/* ---- Modbus exception codes ---------------------------------------------- */

#define MB_EX_ILLEGAL_FUNCTION   0x01
#define MB_EX_ILLEGAL_ADDRESS    0x02
#define MB_EX_ILLEGAL_DATA_VALUE 0x03

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

/* ---- TCP receive callback ------------------------------------------------- */

/**
 * @brief Process one Modbus TCP request received from a client.
 *
 * Handles FC01/FC02/FC03/FC04 read requests by looking up values in the
 * in-memory register cache.  All other function codes receive a Modbus
 * exception 0x01 (ILLEGAL FUNCTION).  Missing cache entries result in
 * exception 0x02 (ILLEGAL DATA ADDRESS).
 *
 * This callback is invoked synchronously from the tcp_server receiver task,
 * so multiple clients are handled concurrently without any additional locking.
 */
static void process_data_from_tcp(tcp_desc_t *desc, int client_sock,
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

    /* ---- 4. Check that the cache is enabled -------------------------------- */
    if (!cache_multimaster_is_enabled()) {
        ESP_LOGD(TAG, "sock=%d: cache disabled, returning exception 0x02", client_sock);
        send_exception(desc, client_sock, transaction_id, unit_id, fc,
                       MB_EX_ILLEGAL_ADDRESS);
        return;
    }

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
    mb_tcp_header_t *resp_hdr = (mb_tcp_header_t *)resp_buf;

    resp_hdr->transaction_id = transaction_id;
    resp_hdr->protocol_id    = 0x0000;
    resp_hdr->unit_id        = unit_id;
    resp_hdr->function       = fc;
    /* resp_hdr->length is filled in after payload is assembled */

    if (fc == MB_FC_READ_HOLDING_REGS || fc == MB_FC_READ_INPUT_REGS) {
        /* ---- FC03 / FC04: holding or input registers --------------------- */
        /* Payload: [byte_count][val0_hi][val0_lo]...[valN_hi][valN_lo]      */
        uint8_t *payload     = resp_buf + sizeof(mb_tcp_header_t);
        uint8_t  byte_count  = (uint8_t)(count * 2u);
        payload[0] = byte_count;

        for (uint16_t i = 0; i < count; i++) {
            uint16_t value = 0;
            if (!cache_multimaster_lookup(unit_id, fc, (uint16_t)(start_addr + i), &value)) {
                /* At least one register not in cache — return exception 0x02 */
                send_exception(desc, client_sock, transaction_id, unit_id, fc,
                               MB_EX_ILLEGAL_ADDRESS);
                return;
            }
            payload[1 + i * 2]     = (uint8_t)(value >> 8);
            payload[1 + i * 2 + 1] = (uint8_t)(value & 0xFFu);
        }

        /* MBAP length = unit_id(1) + FC(1) + byte_count_field(1) + data(byte_count)
         * resp_buf layout: [transaction_id:2][protocol_id:2][length:2]
         *                  [unit_id:1][function:1][byte_count:1][data:byte_count]
         * Total wire bytes = 8 + 1 + byte_count                             */
        uint16_t pdu_len = (uint16_t)(1u + 1u + 1u + byte_count);
        resp_hdr->length = htons(pdu_len);

        size_t resp_len = sizeof(mb_tcp_header_t) + 1u + (size_t)byte_count;
        tcp_server_send(desc, client_sock, resp_buf, resp_len);

    } else {
        /* ---- FC01 / FC02: coils or discrete inputs ----------------------- */
        /* Payload: [byte_count][byte0]...[byteN]  (bits packed LSB first)   */
        uint8_t  coil_bytes = (uint8_t)((count + 7u) / 8u);
        uint8_t *payload    = resp_buf + sizeof(mb_tcp_header_t);

        payload[0] = coil_bytes;
        memset(payload + 1, 0, coil_bytes); /* clear bit buffer */

        for (uint16_t i = 0; i < count; i++) {
            uint16_t value = 0;
            if (!cache_multimaster_lookup(unit_id, fc, (uint16_t)(start_addr + i), &value)) {
                send_exception(desc, client_sock, transaction_id, unit_id, fc,
                               MB_EX_ILLEGAL_ADDRESS);
                return;
            }
            if (value) {
                /* Set bit i in the coil byte array (LSB first) */
                payload[1 + i / 8] |= (uint8_t)(1u << (i % 8));
            }
        }

        /* MBAP length = unit_id(1) + FC(1) + byte_count_field(1) + coil_bytes */
        uint16_t pdu_len = (uint16_t)(1u + 1u + 1u + coil_bytes);
        resp_hdr->length = htons(pdu_len);

        size_t resp_len = sizeof(mb_tcp_header_t) + 1u + (size_t)coil_bytes;
        tcp_server_send(desc, client_sock, resp_buf, resp_len);
    }
}

/* ---- Public API ---------------------------------------------------------- */

esp_err_t cache_modbus_server_init(int port)
{
    ESP_LOGI(TAG, "Starting cache Modbus TCP server on port %d", port);
    return tcp_server_init(port, process_data_from_tcp, &s_tcp_desc);
}

esp_err_t cache_modbus_server_deinit(void)
{
    if (s_tcp_desc == NULL) return ESP_OK;
    esp_err_t ret = tcp_server_deinit(s_tcp_desc);
    s_tcp_desc = NULL;
    return ret;
}
