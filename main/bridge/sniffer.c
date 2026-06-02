#include "sniffer.h"
#include "modbus_helpers.h"
#include "fast_modbus.h"
#include "bridge.h"
#include "serial.h"
#include "stream_splitter.h"
#include "cache_multimaster.h"
#include "auth.h"

/* In unit test builds sniffer.h only provides httpd_handle_t; include the full
 * stub so that httpd_req_t, httpd_ws_frame_t, httpd_uri_t, etc. are available. */
#ifdef __unittest_env__
#include "esp_http_server.h"
#endif

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

/* Allow unit tests to access helper functions that are otherwise static */
#ifdef __unittest_env__
#define SNIFFER_STATIC
#else
#define SNIFFER_STATIC static
#endif

static const char *TAG = "sniffer";

#define SNIFFER_QUEUE_LEN       64
#define SNIFFER_RESP_TIMEOUT_MS 200
#define SNIFFER_WS_TASK_STACK   (1024 * 5)
#define SNIFFER_WS_TASK_PRIO    3

typedef enum {
    SNIFF_IDLE = 0,
    SNIFF_RES_WAIT,
} sniff_state_t;

/* In unit test builds sniff_packet_t is declared in sniffer.h under
 * #ifdef __unittest_env__ to allow tests to access it without duplication. */
#ifndef __unittest_env__
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
#endif

typedef struct {
    sniff_state_t  state;
    uint8_t        reasons;        /* bitmask of sniff_reason_t; sniffer runs when != 0 */
    uint8_t        req_buf[SNIFFER_MAX_PACKET_LEN];
    uint16_t       req_len;
    uint64_t       req_timestamp_us;
    TimerHandle_t  resp_timer;
    unsigned       port_index;
    serial_desc_t *serial_desc;
    /* Direction tracking for CRC-error recovery */
    bool           synchronized;    /* true after first packet with known direction */
    bool           last_was_master; /* direction of the most recently emitted packet */
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

SNIFFER_STATIC bool crc_check(const uint8_t *data, size_t len)
{
    if (len < 4) return false;
    uint16_t crc_calc = modbus_crc16(data, (uint16_t)(len - 2));
    /* modbus_crc16 returns big-endian value; RTU appends CRC low byte first */
    uint8_t crc_lo = (uint8_t)(crc_calc & 0xFF);
    uint8_t crc_hi = (uint8_t)(crc_calc >> 8);
    return (data[len - 2] == crc_lo) && (data[len - 1] == crc_hi);
}

SNIFFER_STATIC void bytes_to_hex(const uint8_t *data, uint16_t len, char *out, size_t out_size)
{
    size_t pos = 0;
    for (uint16_t i = 0; i < len && (pos + 2) < out_size; i++) {
        snprintf(out + pos, 3, "%02X", data[i]);
        pos += 2;
    }
    out[pos] = '\0';
}

/* Format a timeout JSON message into buf (at most buf_size bytes).
 * Returns the number of characters written (like snprintf). */
SNIFFER_STATIC int format_timeout_json(char *buf, size_t buf_size,
                                       uint32_t id, const sniff_packet_t *pkt)
{
    return snprintf(buf, buf_size,
        "{\"type\":\"timeout\",\"id\":%" PRIu32 ",\"port\":%u"
        ",\"timestamp_us\":%" PRIu64
        ",\"slave_id\":%u,\"function\":%u}",
        id, port_index_to_name(pkt->port), pkt->timestamp_us,
        pkt->slave_id, pkt->function);
}

/* Format a normal (non-timeout) packet JSON message into buf.
 * Returns the number of characters written (like snprintf). */
SNIFFER_STATIC int format_packet_json(char *buf, size_t buf_size,
                                       uint32_t id, const sniff_packet_t *pkt)
{
    char hex_str[SNIFFER_MAX_PACKET_LEN * 2 + 1];
    bytes_to_hex(pkt->data, pkt->data_len, hex_str, sizeof(hex_str));
    return snprintf(buf, buf_size,
        "{\"type\":\"packet\",\"id\":%" PRIu32 ",\"port\":%u"
        ",\"timestamp_us\":%" PRIu64
        ",\"sender\":\"%s\",\"slave_id\":%u,\"function\":%u"
        ",\"crc_valid\":%s,\"raw\":\"%s\",\"size\":%u}",
        id, port_index_to_name(pkt->port), pkt->timestamp_us,
        pkt->is_master ? "master" : "slave",
        pkt->slave_id, pkt->function,
        pkt->crc_valid ? "true" : "false",
        hex_str, pkt->data_len);
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

    /* sniff_packet_t (~280 bytes) on the Tmr Svc stack (2048) causes overflow under load.
     * All timer callbacks in Tmr Svc execute sequentially — static is safe here. */
    static sniff_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    bool do_enqueue = false;

    taskENTER_CRITICAL(&sniff_mux);
    if (ctx->req_len >= 2 && ctx->reasons != 0) {
        pkt.port         = (uint8_t)port_index;
        pkt.timestamp_us = ctx->req_timestamp_us + (uint64_t)SNIFFER_RESP_TIMEOUT_MS * 1000ULL;
        pkt.is_master    = true;
        pkt.crc_valid    = true;
        pkt.is_timeout   = true;
        pkt.slave_id     = ctx->req_buf[0];
        pkt.function     = ctx->req_buf[1];
        ctx->synchronized    = true;
        ctx->last_was_master = true;
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
SNIFFER_STATIC void strip_arbitration(const uint8_t *data, size_t len, const uint8_t **effective, size_t *effective_len)
{
    *effective = data;
    *effective_len = len;
    if (len == 0 || data[0] != 0xFF) return;

    /* fast_modbus_truncate_ff only advances the pointer; it does not write
     * through it, so casting away const here is safe. */
    uint8_t *t = (uint8_t *)(uintptr_t)data;
    size_t tlen = fast_modbus_truncate_ff(&t, len);
    if (tlen >= 4 && (t[1] == FAST_MODBUS_FUNC_1 || t[1] == FAST_MODBUS_FUNC_2)) {
        *effective = t;
        *effective_len = tlen;
    }
}

/* Determine if subcmd is a slave response (vs master request) for FM packets */
SNIFFER_STATIC bool fm_is_slave_subcmd(uint8_t subcmd)
{
    return (subcmd == 0x03 || subcmd == 0x04 ||
            subcmd == 0x09 || subcmd == 0x11 || subcmd == 0x12);
}

/* Direction classification result for standard Modbus RTU PDUs.
 * In __unittest_env__ this typedef is declared in sniffer.h to allow tests
 * to use symbolic names; in production builds it is defined here. */
#ifndef __unittest_env__
typedef enum {
    DIRECTION_REQUEST  = 0, /* Packet is unambiguously a master request */
    DIRECTION_RESPONSE = 1, /* Packet is unambiguously a slave response  */
    DIRECTION_UNKNOWN  = 2, /* Cannot determine direction from length/FC alone */
} pdu_direction_t;
#endif

/*
 * classify_direction — heuristic to decide whether a Modbus RTU packet is a
 * master request, a slave response, or ambiguous.
 *
 * Precondition: len >= 4 (guaranteed by the caller).
 * data[0] = slave address, data[1] = function code,
 * data[len-2..len-1] = CRC (already verified by caller when this is invoked).
 */
SNIFFER_STATIC pdu_direction_t classify_direction(const uint8_t *data, size_t len)
{
    uint8_t fc = data[1];

    switch (fc) {
    case 0x01: /* Read Coils */
    case 0x02: /* Read Discrete Inputs */
        /*
         * Request: fixed 8 bytes.
         * Response: 5 + data[2] bytes (bytecount field).
         * Ambiguous when len==8 AND data[2]==3: both formulas match simultaneously
         * (request len==8, response 5+3==8). Cannot determine direction — drop.
         */
        if (len == 8 && data[2] != 3) {
            return DIRECTION_REQUEST;
        }
        if (len >= 5 && len == (size_t)(5 + data[2]) && len != 8) {
            return DIRECTION_RESPONSE;
        }
        return DIRECTION_UNKNOWN; /* includes ambiguous len==8, data[2]==3 case */

    case 0x03: /* Read Holding Registers */
    case 0x04: /* Read Input Registers */
        /*
         * Request: fixed 8 bytes.
         * Response: 5 + data[2] bytes, data[2] must be even and > 0.
         * When len==8, a response would require data[2]==3 (odd) — impossible,
         * so len==8 is always a request.
         */
        if (len == 8) {
            return DIRECTION_REQUEST;
        }
        if (len >= 5 && len == (size_t)(5 + data[2]) &&
            (data[2] % 2 == 0) && (data[2] > 0)) {
            return DIRECTION_RESPONSE;
        }
        return DIRECTION_UNKNOWN;

    case 0x05: /* Write Single Coil  */
    case 0x06: /* Write Single Register */
    case 0x08: /* Diagnostics */
        /* Request and echo-response are both 8 bytes — indistinguishable. */
        return DIRECTION_UNKNOWN;

    case 0x07: /* Read Exception Status */
        /* Request: addr(1)+FC(1)+CRC(2) = 4 bytes.
         * Response: addr(1)+FC(1)+output_data(1)+CRC(2) = 5 bytes (Modbus spec §6.7). */
        if (len == 4) return DIRECTION_REQUEST;
        if (len == 5) return DIRECTION_RESPONSE;
        return DIRECTION_UNKNOWN;

    case 0x0B: /* Get Comm Event Counter */
        if (len == 4) return DIRECTION_REQUEST;
        if (len == 8) return DIRECTION_RESPONSE;
        return DIRECTION_UNKNOWN;

    case 0x0F: /* Write Multiple Coils  */
    case 0x10: /* Write Multiple Registers */
        /*
         * Response: fixed 8 bytes.
         * Request: 9 + data[6] bytes (bytecount at offset 6).
         */
        if (len == 8) return DIRECTION_RESPONSE;
        if (len >= 9 && len == (size_t)(9 + data[6])) return DIRECTION_REQUEST;
        return DIRECTION_UNKNOWN;

    case 0x11: /* Report Server ID */
        if (len == 4) return DIRECTION_REQUEST;
        if (len >= 5 && len == (size_t)(5 + data[2])) return DIRECTION_RESPONSE;
        return DIRECTION_UNKNOWN;

    default:
        return DIRECTION_UNKNOWN;
    }
}

static void sniffer_process(unsigned port_index, const uint8_t *data, size_t len)
{
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    /* Run only when at least one reason (display and/or cache) is active. */
    if (ctx->reasons == 0) return;
    if (len < 4) return;

    /* Strip leading 0xFF arbitration bytes unconditionally.
     * If the packet starts with 0xFF and after stripping has a valid FM header
     * (0xFD + FC46/60), effective points into data past the 0xFF bytes.
     * For all-0xFF packets or non-FM packets, effective == data. */
    const uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, len, &effective, &effective_len);

    /* When the effective buffer is longer than a single minimum-size Modbus frame
     * (4 bytes req + 4 bytes resp = 8 bytes minimum for two back-to-back frames),
     * its CRC is invalid (meaning it is not a single valid frame), and it is not
     * an FM arbitration or FM broadcast packet, attempt to split it into individual
     * frames and process each one separately. */
    bool is_fm = (effective[0] == 0xFD &&
                  (effective[1] == FAST_MODBUS_FUNC_1 || effective[1] == FAST_MODBUS_FUNC_2));
    bool is_arb = (effective[0] == 0xFF);

    /* NOTE: the recursive calls below must happen BEFORE taskENTER_CRITICAL.
     * Moving this block inside the critical section would cause a spinlock
     * deadlock because each recursive sniffer_process() call acquires the
     * same portMUX. */
    if (effective_len > 8 && !crc_check(effective, effective_len) && !is_fm && !is_arb) {
        stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];
        int nframes = stream_split(effective, effective_len,
                                   ctx->req_len >= 2 ? ctx->req_buf[0] : 0,
                                   ctx->req_len >= 2 ? ctx->req_buf[1] : 0,
                                   frames);
        if (nframes > 1) {
            /* Successfully split into multiple frames — process each individually */
            for (int fi = 0; fi < nframes; fi++) {
                sniffer_process(port_index, frames[fi].data, frames[fi].len);
            }
            return; /* original merged buffer fully handled */
        }
        /* nframes == 1: splitter found nothing useful, fall through to normal path */
    }

    bool should_start_timer = false;
    bool should_stop_timer = false;
    sniff_packet_t req_pkt = {0};
    sniff_packet_t res_pkt = {0};
    bool enqueue_req = false;
    bool enqueue_res = false;

    taskENTER_CRITICAL(&sniff_mux);
    if (ctx->state == SNIFF_IDLE) {
        bool valid_crc = crc_check(effective, effective_len);

        if (effective[0] == 0xFD &&
                   (effective[1] == FAST_MODBUS_FUNC_1 || effective[1] == FAST_MODBUS_FUNC_2)) {
            /* Fast Modbus packet (with or without stripped leading 0xFF bytes).
             * Subcmds 0x03/0x04/0x09/0x11/0x12 are slave responses; all others are master. */
            uint8_t subcmd = (effective_len >= 3) ? effective[2] : 0;
            bool is_slave_response = fm_is_slave_subcmd(subcmd);
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = !is_slave_response;
            req_pkt.crc_valid    = valid_crc;
            req_pkt.slave_id     = effective[0];
            req_pkt.function     = effective[1];
            size_t cpy           = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
            memcpy(req_pkt.data, effective, cpy);
            req_pkt.data_len     = (uint16_t)cpy;
            ctx->synchronized    = true;
            ctx->last_was_master = req_pkt.is_master;
            enqueue_req = true;
        } else if (!valid_crc) {
            if (!ctx->synchronized) {
                /* No known packet seen yet — cannot determine direction, drop silently. */
            } else {
                /* Alternate direction based on last known packet. */
                req_pkt.port         = (uint8_t)port_index;
                req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
                req_pkt.is_master    = !ctx->last_was_master;
                req_pkt.crc_valid    = false;
                req_pkt.slave_id     = effective[0];
                req_pkt.function     = effective[1];
                size_t cpy = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
                memcpy(req_pkt.data, effective, cpy);
                req_pkt.data_len     = (uint16_t)cpy;
                ctx->synchronized    = true;
                ctx->last_was_master = req_pkt.is_master;
                enqueue_req = true;
            }
        } else if (effective[0] == 0x00) {
            /* Broadcast: no response expected */
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = true;
            req_pkt.slave_id     = 0;
            req_pkt.function     = effective[1];
            size_t cpy           = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
            memcpy(req_pkt.data, effective, cpy);
            req_pkt.data_len     = (uint16_t)cpy;
            ctx->synchronized    = true;
            ctx->last_was_master = true;
            enqueue_req = true;
        } else {
            pdu_direction_t dir = classify_direction(effective, effective_len);
            if (dir == DIRECTION_RESPONSE) {
                /* Orphan response: sniffer started mid-exchange, the request was missed.
                 * Emit as a slave packet without buffering; stay in SNIFF_IDLE. */
                req_pkt.port         = (uint8_t)port_index;
                req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
                req_pkt.is_master    = false;
                req_pkt.crc_valid    = true;
                req_pkt.slave_id     = effective[0];
                req_pkt.function     = effective[1];
                size_t copy_len = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
                memcpy(req_pkt.data, effective, copy_len);
                req_pkt.data_len     = (uint16_t)copy_len;
                ctx->synchronized    = true;
                ctx->last_was_master = false;
                enqueue_req = true;
                /* State stays SNIFF_IDLE */
            } else if (dir == DIRECTION_REQUEST) {
                /* Emit master request immediately so it appears in the sniffer log right away. */
                req_pkt.port         = (uint8_t)port_index;
                req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
                req_pkt.is_master    = true;
                req_pkt.crc_valid    = true;
                req_pkt.is_timeout   = false;
                req_pkt.slave_id     = effective[0];
                req_pkt.function     = effective[1];
                size_t copy_len      = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
                memcpy(req_pkt.data, effective, copy_len);
                req_pkt.data_len     = (uint16_t)copy_len;
                ctx->synchronized    = true;
                ctx->last_was_master = true;
                enqueue_req          = true;
                /* Also buffer in req_buf so the timer and SNIFF_RES_WAIT branches can reference
                 * slave_id/function for the timeout event. */
                memcpy(ctx->req_buf, effective, copy_len);
                ctx->req_len          = (uint16_t)copy_len;
                ctx->req_timestamp_us = req_pkt.timestamp_us;
                ctx->state            = SNIFF_RES_WAIT;
                should_start_timer    = true;
            } else {
                /* DIRECTION_UNKNOWN: cannot determine direction — drop and stay in SNIFF_IDLE.
                 * Better to skip an ambiguous packet than to guess wrong and invert the stream. */
            }
        }
    } else { /* SNIFF_RES_WAIT */
        if (effective_len < 4) {
            /* Arbitration-only response (e.g. FF FF FF FF FF).
             * Master was already emitted immediately on receipt.
             * Emit only the slave (arbitration) packet here. */
            res_pkt.port         = (uint8_t)port_index;
            res_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            res_pkt.is_master    = false;
            res_pkt.crc_valid    = false;
            res_pkt.slave_id     = data[0];
            res_pkt.function     = data[1];
            size_t arb_cpy       = len < SNIFFER_MAX_PACKET_LEN ? len : SNIFFER_MAX_PACKET_LEN;
            memcpy(res_pkt.data, data, arb_cpy);
            res_pkt.data_len     = (uint16_t)arb_cpy;
            ctx->synchronized    = true;
            ctx->last_was_master = false;
            enqueue_res = true;

            ctx->state = SNIFF_IDLE;
            /* Defensive: stop the response timer even though this branch is currently
             * unreachable (effective_len is always >= 4 after strip_arbitration). */
            should_stop_timer = true;
            goto exit_critical;
        }

        should_stop_timer = true;

        /* If the packet arriving in RES_WAIT is a Fast Modbus packet (possibly with
         * stripped 0xFF prefix), the state machine is out of phase. Emit as standalone. */
        if (effective[0] == 0xFD &&
            (effective[1] == FAST_MODBUS_FUNC_1 || effective[1] == FAST_MODBUS_FUNC_2)) {
            uint8_t subcmd2       = (effective_len >= 3) ? effective[2] : 0;
            bool is_slave2        = fm_is_slave_subcmd(subcmd2);
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = !is_slave2;
            req_pkt.crc_valid    = crc_check(effective, effective_len);
            req_pkt.slave_id     = effective[0];
            req_pkt.function     = effective[1];
            size_t fm_cpy        = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
            memcpy(req_pkt.data, effective, fm_cpy);
            req_pkt.data_len     = (uint16_t)fm_cpy;
            ctx->synchronized    = true;
            ctx->last_was_master = req_pkt.is_master;
            enqueue_req = true;
            ctx->state = SNIFF_IDLE;
            goto exit_critical;
        }

        /* Before pairing, check if the arriving packet is actually a new master request
         * (second master starting a transaction while we were waiting for a response). */
        pdu_direction_t dir2 = classify_direction(effective, effective_len);
        if (dir2 == DIRECTION_REQUEST) {
            /* First master was already emitted on receipt.
             * Emit second master immediately and start waiting for its response. */
            req_pkt.port         = (uint8_t)port_index;
            req_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            req_pkt.is_master    = true;
            req_pkt.crc_valid    = true;
            req_pkt.is_timeout   = false;
            req_pkt.slave_id     = effective[0];
            req_pkt.function     = effective[1];
            size_t new_cpy       = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
            memcpy(req_pkt.data, effective, new_cpy);
            req_pkt.data_len     = (uint16_t)new_cpy;
            ctx->synchronized    = true;
            ctx->last_was_master = true;
            enqueue_req          = true;

            /* Buffer the new request; restart timer; stay in SNIFF_RES_WAIT. */
            memcpy(ctx->req_buf, effective, new_cpy);
            ctx->req_len          = (uint16_t)new_cpy;
            ctx->req_timestamp_us = req_pkt.timestamp_us;
            should_start_timer    = true;
            /* ctx->state stays SNIFF_RES_WAIT */
        } else {
            /* Normal response (DIRECTION_RESPONSE or DIRECTION_UNKNOWN):
             * master was already emitted immediately on receipt.
             * Emit only the slave response here. */
            res_pkt.port         = (uint8_t)port_index;
            res_pkt.timestamp_us = (uint64_t)esp_timer_get_time();
            res_pkt.is_master    = false;
            res_pkt.crc_valid    = crc_check(effective, effective_len);
            res_pkt.slave_id     = effective[0];
            res_pkt.function     = effective[1];
            size_t copy_len      = effective_len < SNIFFER_MAX_PACKET_LEN ? effective_len : SNIFFER_MAX_PACKET_LEN;
            memcpy(res_pkt.data, effective, copy_len);
            res_pkt.data_len     = (uint16_t)copy_len;
            ctx->synchronized    = true;
            ctx->last_was_master = false;
            enqueue_res          = true;

            ctx->state = SNIFF_IDLE;
        }
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


/* Process one packet from the sniffer queue: update the multimaster cache,
 * verify the saved WebSocket fd is still live, format JSON and send.
 * Extracted from sniffer_ws_task so that unit tests can call it directly
 * without blocking on xQueueReceive(portMAX_DELAY). */
SNIFFER_STATIC void sniffer_ws_dispatch(sniff_packet_t *pkt)
{
    static char json_buf[SNIFFER_JSON_BUF_SIZE];

    /* Feed packet to the caching multimaster regardless of WS client connection
     * state, but only for ports whose CACHE reason is active. Cache accumulates
     * data even when no WS client is connected. */
    bool cache_reason;
    taskENTER_CRITICAL(&sniff_mux);
    cache_reason = (pkt->port < BRIDGES_COUNT) && (sniff_ctx[pkt->port].reasons & SNIFF_REASON_CACHE);
    taskEXIT_CRITICAL(&sniff_mux);
    if (cache_multimaster_is_enabled() && cache_reason) {
        if (pkt->is_master && !pkt->is_timeout &&
            (pkt->function == 0x01 || pkt->function == 0x02 ||
             pkt->function == 0x03 || pkt->function == 0x04) &&
            pkt->crc_valid && pkt->data_len >= 8) {
            uint16_t start_reg = ((uint16_t)pkt->data[2] << 8) | pkt->data[3];
            uint16_t count     = ((uint16_t)pkt->data[4] << 8) | pkt->data[5];
            cache_multimaster_on_request(pkt->port, pkt->slave_id, pkt->function,
                                         start_reg, count);
        } else if (!pkt->is_master && !pkt->is_timeout &&
                   (pkt->function == 0x01 || pkt->function == 0x02 ||
                    pkt->function == 0x03 || pkt->function == 0x04) &&
                   pkt->crc_valid && pkt->data_len >= 5) {
            cache_multimaster_on_response(pkt->port, pkt->slave_id, pkt->function,
                                          pkt->data, pkt->data_len, pkt->timestamp_us);
        } else {
            /* Any other bus event ended the current transaction without a
             * cacheable response: a bus timeout, an exception reply (function
             * has the 0x80 error bit), a malformed/short slave frame, or a
             * non-read master request (e.g. an FC06/FC16 write). None of these
             * reach on_response(), so clear the pending request to stop it from
             * being matched against a later, unrelated response of the same
             * slave+FC (corr-7). On a half-duplex RS-485 bus nothing arrives
             * between a captured request and its response, so this never
             * spuriously clears a pending that is still awaiting its reply. */
            cache_multimaster_clear_pending(pkt->port);
        }
    }

    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    int fd = ws_client_fd;
    httpd_handle_t srv = ws_server;
    xSemaphoreGive(ws_mutex);

    if (fd == -1 || srv == NULL) return;

    /* When a WS client TCP-closes without an explicit cleanup the saved
     * fd can be recycled by the httpd layer for a subsequent plain-HTTP
     * connection. Sending a WS frame to such a recycled fd would dump
     * the WS bytes into an HTTP request stream — and httpd_ws_send_data
     * happily reports ESP_OK because the write succeeds. Verify the fd
     * still backs a WebSocket client before writing. */
    if (httpd_ws_get_fd_info(srv, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        if (ws_client_fd == fd) {
            ws_client_fd = -1;
        }
        xSemaphoreGive(ws_mutex);
        return;
    }

    packet_counter++;

    /* Timeout packets are no longer forwarded to the WebSocket client.
     * The master request is already visible as a standalone packet (emitted
     * immediately on receipt); an absent slave response is implicit from the
     * lack of a following slave packet.  The timeout event is still used
     * internally by cache_multimaster but carries no UI value. */
    if (pkt->is_timeout) {
        return;
    }

    format_packet_json(json_buf, SNIFFER_JSON_BUF_SIZE, packet_counter, pkt);

    httpd_ws_frame_t ws_frame = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json_buf,
        .len     = strlen(json_buf),
        .final   = true,
    };

    /* Use httpd_ws_send_data (blocking) instead of httpd_ws_send_frame_async:
     * the actual socket write runs in the httpd worker task, serialized with
     * the auto-PONG reply httpd sends for client PINGs. send_frame_async wrote
     * the frame (header + payload as two separate send() calls) directly from
     * this task, so an auto-PONG from the httpd task could interleave between
     * the two send()s and corrupt the WS byte stream. Routing through the worker
     * task removes the concurrency. Blocking variant lets us safely reuse json_buf. */
    esp_err_t ret = httpd_ws_send_data(srv, fd, &ws_frame);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS send failed (%d), dropping client", ret);
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        if (ws_client_fd == fd) {
            ws_client_fd = -1;
        }
        xSemaphoreGive(ws_mutex);
    }
}

static void sniffer_ws_task(void *arg)
{
    (void)arg;
    while (1) {
        sniff_packet_t pkt;
        xQueueReceive(sniff_queue, &pkt, portMAX_DELAY);
        sniffer_ws_dispatch(&pkt);
    }
}

#ifdef __unittest_env__
int sniffer_test_get_ws_client_fd(void) { return ws_client_fd; }
#endif

SNIFFER_STATIC esp_err_t sniffer_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Authenticate the upgrade request — cookie is available in the HTTP headers.
         * IDF v5.4 limitation: cannot reject WS upgrade from the handler (ESP_FAIL is ignored
         * by the WS layer, 101 is always sent). Instead, close the connection immediately
         * after the upgrade if auth fails — unauthenticated clients get 101 then FIN. */
        if (!auth_middleware_check(req)) {
            httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
            return ESP_OK;
        }
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        ws_server    = req->handle;
        ws_client_fd = httpd_req_to_sockfd(req);
        int new_fd   = ws_client_fd; // capture under mutex to avoid race in log
        xSemaphoreGive(ws_mutex);
        ESP_LOGI(TAG, "WS client connected, fd=%d", new_fd);
        return ESP_OK;
    }

    /* WebSocket frame message path — auth was verified during the HTTP GET upgrade above;
     * unauthenticated connections are closed immediately via httpd_sess_trigger_close(). */
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
            if (enable) sniffer_enable(idx, SNIFF_REASON_DISPLAY);
            else        sniffer_disable(idx, SNIFF_REASON_DISPLAY);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t sniffer_status_handler(httpd_req_t *req)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    char resp[64];
    /* Report the user-facing "live sniffer running" state, i.e. the DISPLAY reason. */
    snprintf(resp, sizeof(resp), "{\"port_%u\":%s,\"port_%u\":%s}",
        port_index_to_name(0), (sniff_ctx[0].reasons & SNIFF_REASON_DISPLAY) ? "true" : "false",
        port_index_to_name(1), (sniff_ctx[1].reasons & SNIFF_REASON_DISPLAY) ? "true" : "false");
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
        sniff_ctx[i].state           = SNIFF_IDLE;
        sniff_ctx[i].reasons         = 0;
        sniff_ctx[i].synchronized    = false;
        sniff_ctx[i].last_was_master = false;
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
    if (port_index >= BRIDGES_COUNT || serial_desc == NULL) return;
    serial_desc->sniff_handler = s_port_callbacks[port_index];
    sniff_ctx[port_index].serial_desc = serial_desc;
}

void sniffer_detach(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) return;
    /* Fully disable: clear all reasons so framing state and RX timeout are reset. */
    sniffer_disable(port_index, SNIFF_REASON_DISPLAY);
    sniffer_disable(port_index, SNIFF_REASON_CACHE);
    // Clear the callback pointer in the serial descriptor before releasing our reference,
    // to prevent the UART event task from calling a stale handler after detach.
    if (sniff_ctx[port_index].serial_desc) {
        sniff_ctx[port_index].serial_desc->sniff_handler = NULL;
    }
    sniff_ctx[port_index].serial_desc = NULL;
}

void sniffer_enable(unsigned port_index, sniff_reason_t reason)
{
    if (port_index >= BRIDGES_COUNT) return;
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    /* The reasons bitmask is shared with other tasks (e.g. sniffer_ws_dispatch and
     * sniffer_disable). Perform the read-modify-write under the spinlock so
     * concurrent writers cannot lose a bit.
     *
     * The overlay does NOT touch the serial RX inter-character timeout: that is
     * owned by the transport mode (set once at port init). The sniffer's frame
     * splitting (stream_splitter) works regardless of the RX timeout. */
    taskENTER_CRITICAL(&sniff_mux);
    ctx->reasons = (uint8_t)(ctx->reasons | (uint8_t)reason);
    taskEXIT_CRITICAL(&sniff_mux);

    ESP_LOGI(TAG, "Sniffer reason 0x%02X enabled on port %u (reasons=0x%02X)",
             (unsigned)reason, port_index, ctx->reasons);
}

void sniffer_disable(unsigned port_index, sniff_reason_t reason)
{
    if (port_index >= BRIDGES_COUNT) return;
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    /* Perform the read-modify-write and non-zero -> 0 edge detection under the
     * spinlock so concurrent writers cannot lose a bit. The framing-state reset is
     * safe here (sniffer_process mutates these fields under sniff_mux too). */
    taskENTER_CRITICAL(&sniff_mux);
    uint8_t prev = ctx->reasons;
    ctx->reasons = (uint8_t)(prev & ~(uint8_t)reason);
    bool became_idle = (prev != 0) && (ctx->reasons == 0);
    if (became_idle) {
        ctx->req_len = 0;   /* Prevent stale timer CB from emitting a packet */
        ctx->state   = SNIFF_IDLE;
    }
    taskEXIT_CRITICAL(&sniff_mux);

    if (!became_idle) {
        /* Still running for another reason — keep framing state. */
        ESP_LOGI(TAG, "Sniffer reason 0x%02X disabled on port %u (reasons=0x%02X)",
                 (unsigned)reason, port_index, ctx->reasons);
        return;
    }
    /* No reasons left — fully quiesce the pipeline. The RX timeout is owned by the
     * transport mode and is intentionally left untouched. xTimerStop() is not
     * spinlock-safe, so it runs after releasing the lock. */
    xTimerStop(ctx->resp_timer, 0);
    ESP_LOGI(TAG, "Sniffer fully disabled on port %u", port_index);
}

esp_err_t sniffer_register_handlers(httpd_handle_t server)
{
    esp_err_t ret = httpd_register_uri_handler(server, &sniffer_ws_uri);
    if (ret != ESP_OK) return ret;
    return httpd_register_uri_handler(server, &sniffer_status_uri);
}
