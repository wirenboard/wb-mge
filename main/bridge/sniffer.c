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

/* sniff_packet_t, pdu_direction_t and sniff_state_t are declared in sniffer.h —
 * one definition shared by the production and the unit-test builds. */

typedef struct {
    sniff_state_t  state;
    uint8_t        reasons;        /* bitmask of sniff_reason_t; sniffer runs when != 0 */
    /* The pending master request. Only the slave address and the function code are ever
     * read back (by the timeout event and by the stream splitter's frame hints), so only
     * those are kept — buffering the whole ADU copied bytes nobody looked at. */
    bool           req_valid;      /* a master request is currently pending */
    uint8_t        req_slave;      /* pending request: slave address */
    uint8_t        req_fc;         /* pending request: function code */
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

/*
 * READINESS. Whether the sniffer is up is read straight off these handles — the same
 * discipline cache_multimaster.c applies to s_cache_mutex — and every public entry
 * point that hands a handle to FreeRTOS checks that handle first: sniffer_attach() and
 * sniffer_enable() up front (both gate a path that leads to the queue), sniffer_process()
 * — the RX callback attach publishes — at its entry, sniffer_ws_handler() before it takes
 * ws_mutex, and sniffer_disable() right at the xTimerStop() it ends on. The entry points
 * without a check are the ones with no handle to check: sniffer_detach() only delegates
 * to sniffer_disable(), sniffer_status_handler() reads nothing but the static
 * sniff_ctx[] (see its own note), and sniffer_register_handlers() hands its argument to
 * httpd, not to FreeRTOS.
 *
 * There is no separate "initialised" flag and there should not be one: sniffer_init()
 * is all-or-nothing. Every early exit taken after the first handle exists runs
 * sniffer_init_cleanup(), which deletes AND NULLs every handle created so far; the one
 * exit before that (the WS mutex itself failing to allocate) has nothing to tear down
 * and returns straight away. Either way nothing is left behind, so there is no
 * half-built sniffer to probe for — a NULL handle is the whole truth about the state it
 * protects, and a flag would only add a second source of truth that can disagree with it.
 *
 * Why the checks are needed at all: http_server_init() registers /sniffer/ws and
 * /sniffer/status, and it can run before this module is up — either because a request
 * beats the boot sequence, or because sniffer_init() itself failed on a low-memory
 * device and main.c deliberately carries on without a sniffer rather than boot-looping.
 * FreeRTOS configASSERTs on a NULL queue/timer/semaphore handle, and this firmware is
 * built with assertions on: reaching one of these handles too early is a panic and a
 * reboot, not a bad packet. The call order set up in main.c is still the invariant —
 * these checks are what stops it from being the ONLY thing standing between a stray
 * HTTP request and a reboot loop.
 */


/* Convert internal 0-based port index to external 1-based port name */
static unsigned port_index_to_name(unsigned index) { return index + 1; }

/* Convert external 1-based port name to internal 0-based index.
 * Returns BRIDGES_COUNT if the name is out of range. */
static unsigned port_name_to_index(unsigned name)
{
    if (name < 1 || name > BRIDGES_COUNT) {
        return BRIDGES_COUNT;
    }
    return name - 1;
}

SNIFFER_STATIC bool crc_check(const uint8_t *data, size_t len)
{
    if (len < 4) {
        return false;
    }
    uint16_t crc_calc = modbus_crc16(data, (uint16_t)(len - 2));
    /* modbus_crc16 returns big-endian value; RTU appends CRC low byte first */
    uint8_t crc_lo = (uint8_t)(crc_calc & 0xFF);
    uint8_t crc_hi = (uint8_t)(crc_calc >> 8);
    return (data[len - 2] == crc_lo) && (data[len - 1] == crc_hi);
}

SNIFFER_STATIC void bytes_to_hex(const uint8_t *data, uint16_t len, char *out, size_t out_size)
{
    /* Nibble lookup instead of snprintf("%02X"): the previous version ran a full
     * vfprintf per byte (one per packet byte, on every packet) on the sniffer WS task.
     * Semantics are unchanged: uppercase hex, same (pos + 2) < out_size truncation
     * boundary, always NUL-terminated. */
    static const char hx[] = "0123456789ABCDEF";
    size_t pos = 0;
    for (uint16_t i = 0; i < len && (pos + 2) < out_size; i++) {
        out[pos]     = hx[data[i] >> 4];
        out[pos + 1] = hx[data[i] & 0x0F];
        pos += 2;
    }
    out[pos] = '\0';
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

/* Try to enqueue a packet. Returns false when the queue is full (the packet is dropped).
 * Deliberately touches no port state: sniff_ctx[].state is owned by the sniff_mux critical
 * section (resp_timer_cb() and sniffer_disable() mutate it from other tasks/cores), and
 * this runs outside it. The caller applies the state transition under the spinlock. */
static bool try_enqueue(unsigned port_index, sniff_packet_t *pkt)
{
    if (xQueueSend(sniff_queue, pkt, 0) == pdTRUE) {
        return true;
    }
    ESP_LOGW(TAG, "sniff queue full, dropping packet on port %u", port_index);
    return false;
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
    if (ctx->req_valid && ctx->reasons != 0) {
        pkt.port         = (uint8_t)port_index;
        pkt.timestamp_us = ctx->req_timestamp_us + (uint64_t)SNIFFER_RESP_TIMEOUT_MS * 1000ULL;
        pkt.is_master    = true;
        pkt.crc_valid    = true;
        pkt.is_timeout   = true;
        pkt.slave_id     = ctx->req_slave;
        pkt.function     = ctx->req_fc;
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
    if (len == 0 || data[0] != 0xFF) {
        return;
    }

    /* fast_modbus_truncate_ff only advances the pointer; it does not write
     * through it, so casting away const here is safe. */
    uint8_t *t = (uint8_t *)(uintptr_t)data;
    size_t tlen = fast_modbus_truncate_ff(&t, len);
    if (tlen >= 4 && (t[1] == FAST_MODBUS_FUNC_1 || t[1] == FAST_MODBUS_FUNC_2)) {
        *effective = t;
        *effective_len = tlen;
    }
}

/* Determine if subcmd is a slave response (vs master request) for FM packets.
 * NOTE: 0x18 (event config) reuses the SAME subcmd for request and response, so a
 * 0x18 device response is labeled as master. This is a known display-only limitation:
 * direction cannot be derived from the subcmd alone for 0x18. */
SNIFFER_STATIC bool fm_is_slave_subcmd(uint8_t subcmd)
{
    return (subcmd == 0x03 || subcmd == 0x04 ||
            subcmd == 0x09 || subcmd == 0x11 || subcmd == 0x12);
}

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

    /* Exception reply: addr(1) + (fc|0x80)(1) + exception code(1) + CRC(2) = 5 bytes.
     * The high bit is set by the server and by nothing else, so the shape is
     * unambiguous — checked before the switch, which knows only the request-side
     * function codes and would otherwise send every exception to `default:`. The stream
     * splitter frames these by the same rule (stream_splitter.c, frame_expected_len()).
     * Fast Modbus command bytes (0x46 / 0x60) have the high bit clear and are not
     * affected. */
    if (fc & 0x80) {
        return (len == 5) ? DIRECTION_RESPONSE : DIRECTION_UNKNOWN;
    }

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
        /* Request and echo-response are both 8 bytes — indistinguishable.
         * The frame is still displayed: sniffer_decide() infers a direction for it. */
        return DIRECTION_UNKNOWN;

    case 0x07: /* Read Exception Status */
        /* Request: addr(1)+FC(1)+CRC(2) = 4 bytes.
         * Response: addr(1)+FC(1)+output_data(1)+CRC(2) = 5 bytes (Modbus spec §6.7). */
        if (len == 4) {
            return DIRECTION_REQUEST;
        }
        if (len == 5) {
            return DIRECTION_RESPONSE;
        }
        return DIRECTION_UNKNOWN;

    case 0x0B: /* Get Comm Event Counter */
        if (len == 4) {
            return DIRECTION_REQUEST;
        }
        if (len == 8) {
            return DIRECTION_RESPONSE;
        }
        return DIRECTION_UNKNOWN;

    case 0x0F: /* Write Multiple Coils  */
    case 0x10: /* Write Multiple Registers */
        /*
         * Response: fixed 8 bytes.
         * Request: 9 + data[6] bytes (bytecount at offset 6).
         */
        if (len == 8) {
            return DIRECTION_RESPONSE;
        }
        if (len >= 9 && len == (size_t)(9 + data[6])) {
            return DIRECTION_REQUEST;
        }
        return DIRECTION_UNKNOWN;

    case 0x11: /* Report Server ID */
        if (len == 4) {
            return DIRECTION_REQUEST;
        }
        if (len >= 5 && len == (size_t)(5 + data[2])) {
            return DIRECTION_RESPONSE;
        }
        return DIRECTION_UNKNOWN;

    default:
        return DIRECTION_UNKNOWN;
    }
}

/*
 * sniffer_decide — the whole request/response state machine, as a pure function.
 *
 * Takes the port's framing state plus the properties of one already-delimited frame,
 * and returns what to do about it. No locks, no clock, no queue, no memcpy: it can be
 * exercised directly from a unit test, and the caller can do all the expensive work
 * (CRC, classification, packet copy) outside the spinlock.
 *
 * Exactly one packet is emitted per frame at most; the emitted direction is also what
 * the port re-synchronises on (in->last_was_master for the next frame).
 */
SNIFFER_STATIC sniff_decision_t sniffer_decide(const sniff_input_t *in)
{
    sniff_decision_t d = {0};
    d.new_state = in->state;

    if (in->state == SNIFF_IDLE) {
        if (in->is_fm) {
            /* Fast Modbus frame (leading 0xFF arbitration bytes already stripped).
             * Recognised by the command byte at any address — event-transfer/config
             * frames carry the device server_id, not the 0xFD broadcast address.
             * The subcommand decides the direction. */
            d.emit      = true;
            d.is_master = !in->fm_slave_subcmd;
            d.crc_valid = in->crc_valid;
        } else if (!in->crc_valid) {
            if (!in->synchronized) {
                /* Nothing known yet — direction is unguessable. Drop silently. */
                return d;
            }
            /* Alternate from the direction of the last known packet. */
            d.emit      = true;
            d.is_master = !in->last_was_master;
            d.crc_valid = false;
        } else if (in->broadcast) {
            /* Broadcast: no response is expected, so no timer and no RES_WAIT. */
            d.emit      = true;
            d.is_master = true;
            d.crc_valid = true;
        } else if (in->dir == DIRECTION_RESPONSE) {
            /* Orphan response: the sniffer started mid-exchange and missed the
             * request. Emit it as a slave packet and stay in SNIFF_IDLE. */
            d.emit      = true;
            d.is_master = false;
            d.crc_valid = true;
        } else if (in->dir == DIRECTION_REQUEST) {
            /* Emit the master request immediately so it shows up in the log right
             * away, then wait for the matching slave response. */
            d.emit          = true;
            d.is_master     = true;
            d.crc_valid     = true;
            d.latch_request = true;
            d.new_state     = SNIFF_RES_WAIT;
            d.start_timer   = true;
        } else {
            /* DIRECTION_UNKNOWN with a valid CRC: a frame whose shape does not pin down
             * a direction. That is not a short list — but it is not everything
             * classify_direction() gives up on either. The branches above claim a Fast
             * Modbus frame and a broadcast whatever their dir, so an unresolved dir on
             * those two never reaches this point. What does: FC05/FC06/FC08, whose
             * request and echo-response are both 8 bytes; an FC01/FC02 frame with len==8
             * and bytecount==3, which satisfies the request and the response formula at
             * once; FC03/FC04, FC0F/FC10 and exception frames whose length fits neither
             * formula; every vendor-specific or unknown function code, which reaches
             * `default:`; and corrupted frames whose CRC happened to check out.
             *
             * The frame is shown anyway, always as a master. Skipping it is worse than
             * mislabelling it: a master that only ever writes with FC06 to devices that
             * do not answer leaves the port in SNIFF_IDLE forever, so every one of its
             * frames hits this branch and the sniffer stays empty with nothing to
             * explain why.
             *
             * Master is unconditional, NOT alternated from in->last_was_master the way
             * the invalid-CRC branch above does it. Note what SNIFF_IDLE actually
             * asserts: that THE SNIFFER is not tracking an open exchange — not that the
             * bus has none. Of the paths that reach this state, only two really close
             * one: sniffer_init(), and a SNIFF_RES_WAIT that consumed its response (or
             * an arbitration-only frame). The other four leave a reply perfectly
             * possible:
             *   - resp_timer_cb(): SNIFFER_RESP_TIMEOUT_MS is this module's display
             *     policy, not a rule of the bus, and a slow device answers after it;
             *   - the "queue full" path in sniffer_process_frame(), which drops the
             *     pending request because the consumer lost the packet, not because the
             *     exchange ended;
             *   - a Fast Modbus frame arriving in SNIFF_RES_WAIT, which resynchronises
             *     to SNIFF_IDLE while the standard request is still unanswered;
             *   - sniffer_enable() after sniffer_disable(), i.e. switching the sniffer
             *     on in the middle of somebody else's transaction.
             * So the guess is a guess, and a late echo of a real FC05/FC06 write does
             * reach this branch and is labelled master. Alternation would be worse
             * rather than better: resp_timer_cb() leaves last_was_master = true, so on
             * repeated unanswered FC06 writes — precisely the case this branch exists
             * for — it would call every second write a slave, and the broadcast branch
             * above likewise emits a master that nobody will ever answer.
             *
             * A wrong guess costs the direction label and nothing else: the bytes, the
             * length, the CRC verdict and the timestamp are all still right. Its reach is
             * bounded, but it is not one packet. The next CRC-valid frame of an
             * unambiguous shape does clear it, being labelled from that shape in both
             * states (a request is a master in IDLE and in RES_WAIT alike). A run of
             * ambiguous frames sharing one (address, FC) does not: guessing master on an
             * echo latches that pair, the real write that follows matches the latch and is
             * therefore reported as the reply, which returns the port to SNIFF_IDLE, where
             * the next echo is guessed master again. The whole run comes out inverted, and
             * the phase is recovered only on the first inter-transaction gap long enough
             * for resp_timer_cb() to run — i.e. longer than SNIFFER_RESP_TIMEOUT_MS. The
             * guess can also outlive frames that are themselves unclassifiable: a corrupt
             * frame in SNIFF_IDLE is labelled by alternation, without its shape being
             * consulted at all.
             *
             * Latched like a real request, so that the echo-response of an FC05/FC06
             * write is consumed by the RES_WAIT branch below and displayed as the slave
             * half of the pair, and so that a write that goes unanswered ends its
             * transaction on the response timeout instead of leaving a pending request
             * latched forever. Latching is also what makes the guess self-limiting: of the
             * frames that reach the RES_WAIT response branch, an AMBIGUOUS one is taken as
             * this reply only if it carries the same address and function code. That is a
             * condition on ambiguous frames alone, not on the branch — it also accepts any
             * frame unambiguously shaped as a response, from any address, and any corrupt
             * frame (see its own comment). What the condition buys is the case at hand: a
             * second unanswered write — to another slave, or to the same one before the
             * timeout gets a chance to run — is recognised as a new request rather than
             * swallowed as this one's reply. */
            d.emit          = true;
            d.crc_valid     = true;
            d.is_master     = true;
            d.latch_request = true;
            d.new_state     = SNIFF_RES_WAIT;
            d.start_timer   = true;
        }
        return d;
    }

    /* SNIFF_RES_WAIT — a master request is pending. Nearly every path below concludes the
     * wait for the current request, so the response timer stops by default. The one
     * exception is the broadcast branch, which leaves the pending request untouched and
     * clears this flag again. */
    d.stop_timer = true;

    if (in->short_frame) {
        /* Arbitration-only response (e.g. FF FF FF FF FF). The master was already
         * emitted on receipt, so only the arbitration packet is emitted here.
         * Defensive: unreachable today, strip_arbitration() never yields < 4 bytes. */
        d.emit      = true;
        d.is_master = false;
        d.crc_valid = false;
        d.from_raw  = true;
        d.new_state = SNIFF_IDLE;
        return d;
    }

    if (in->is_fm) {
        /* A Fast Modbus frame where a response was expected: the state machine is out
         * of phase. Emit it standalone and resynchronise. */
        d.emit      = true;
        d.is_master = !in->fm_slave_subcmd;
        d.crc_valid = in->crc_valid;
        d.new_state = SNIFF_IDLE;
        return d;
    }

    if (in->crc_valid && in->broadcast) {
        /* A broadcast arriving mid-transaction. It gets its own branch here for exactly
         * the reason it has one in SNIFF_IDLE: address 0x00 obliges no device to answer,
         * so there is nothing to wait for and nothing to latch.
         *
         * Without it a broadcast write fell through to the re-latch branch below — a
         * broadcast FC05/FC06 is DIRECTION_UNKNOWN, CRC-valid, and can never match the
         * latched (address, FC) — and became the pending request with req_slave = 0x00.
         * The NEXT broadcast then matched that latch and was reported as its reply, so a
         * run of broadcasts came out master, slave, master, slave..., which is the very
         * artefact the re-latch branch exists to prevent. It also armed the response
         * timer on a frame nobody will ever answer.
         *
         * stop_timer is cleared again: the blanket d.stop_timer = true above holds only
         * because every path under it concludes the pending request's wait, and this one
         * does not. The earlier request is still latched and still owed a reply, so its
         * timer has to keep running to its own deadline — disarming it would strand that
         * request in SNIFF_RES_WAIT with no timeout event left to end it. Nothing else
         * needs undoing: this branch neither latches nor starts a timer, and new_state
         * stays SNIFF_RES_WAIT, so the port comes out of the broadcast waiting for the
         * same reply it was waiting for before. */
        d.emit       = true;
        d.is_master  = true;
        d.crc_valid  = true;
        d.stop_timer = false;
        return d;
    }

    /* Two kinds of frame that cannot be the reply this port is waiting for. Both are
     * handled identically — emit as a master, re-latch as the pending request, restart
     * the timer and keep waiting:
     *
     *   - DIRECTION_REQUEST: not a response at all but a second master starting a new
     *     transaction while we were waiting. The first master was already emitted.
     *
     *   - an ambiguous frame addressed elsewhere: a Modbus RTU reply always carries the
     *     address the request was sent to, and echoes its function code unless it is an
     *     exception reply — that one answers with fc | 0x80 and therefore never matches
     *     the latch. The test is still sound, because an exception reply does not reach
     *     it: the spec gives it exactly one shape (5 bytes), classify_direction() reads
     *     the high bit and calls it DIRECTION_RESPONSE, and this branch only ever sees
     *     DIRECTION_UNKNOWN. So among the frames that DO reach it, one whose (address, FC)
     *     pair is not the latched one cannot be the awaited reply, however unclassifiable
     *     its shape. The premise is scoped to that spec-defined shape and nothing wider: a
     *     CRC-valid frame with the high bit set and a length other than 5 is
     *     DIRECTION_UNKNOWN, does arrive here, and is re-latched as a phantom request with
     *     fc = 0x86. No conforming device emits such a frame, and there is no honest rule
     *     to apply to one that would not be invented along with the frame.
     *
     *     Without this test the "an ambiguous frame in SNIFF_IDLE is a master" rule above
     *     holds only for as long as resp_timer_cb() manages to run between frames: a
     *     second unanswered write arriving inside the 200 ms window would be consumed
     *     here as the first one's response, and in a gap-less buffer — every frame of
     *     which is dispatched from a single sniffer_process() call — the timer cannot
     *     possibly intervene, so a run of writes came out labelled master, slave, master,
     *     slave...
     *     Restricted to CRC-valid frames: the address and function code of a corrupt
     *     frame are not evidence of anything, and this branch stamps crc_valid = true.
     *     A corrupt frame therefore still falls through to the response branch below,
     *     which reports the real CRC verdict.
     *     What this does NOT catch, and nothing can: a master retrying the very same
     *     write to the very same slave inside the response window. Those bytes are
     *     byte-for-byte what the echo of the first attempt would be, so the retry is
     *     reported as that echo. It is one wrong label on one frame; the retry after
     *     the timeout — the shape TC-14d walks — is labelled correctly. */
    if (in->dir == DIRECTION_REQUEST ||
        (in->dir == DIRECTION_UNKNOWN && in->crc_valid && !in->matches_pending)) {
        d.emit          = true;
        d.is_master     = true;
        d.crc_valid     = true;
        d.latch_request = true;
        d.start_timer   = true;
        /* new_state stays SNIFF_RES_WAIT */
        return d;
    }

    /* The awaited slave response: either unambiguously shaped as one, or ambiguous but
     * consistent with the pending request (the FC05/FC06 echo case, where the reply is
     * byte-for-byte the request), or corrupt — a frame whose CRC failed is reported as
     * the response because in this state that is the likeliest thing it was. The master
     * was already emitted on receipt, so only the slave packet is emitted here. */
    d.emit      = true;
    d.is_master = false;
    d.crc_valid = in->crc_valid;
    d.new_state = SNIFF_IDLE;
    return d;
}

/* sniffer_process_frame — run the request/response state machine for exactly ONE
 * already-delimited Modbus / Fast Modbus frame.
 *
 * This function never calls stream_split() and never recurses. Splitting a merged,
 * gap-less buffer into individual frames is done iteratively by sniffer_process()
 * below, which dispatches each sub-frame here. Keeping the per-frame work in a leaf
 * function bounds the UART task stack usage regardless of how many back-to-back
 * frames arrive in a single buffer. */
static void sniffer_process_frame(unsigned port_index, const uint8_t *data, size_t len)
{
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    /* Run only when at least one reason (display and/or cache) is active. */
    if (ctx->reasons == 0) {
        return;
    }
    if (len < 4) {
        return;
    }

    /* Strip leading 0xFF arbitration bytes unconditionally.
     * If the packet starts with 0xFF and after stripping has a valid FM header
     * (0xFD + FC46/60), effective points into data past the 0xFF bytes.
     * For all-0xFF packets or non-FM packets, effective == data. */
    const uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, len, &effective, &effective_len);

    /* Everything below is a pure function of the frame, and the frame belongs to the
     * calling task alone — nothing reachable from the spinlock can mutate it. So it all
     * runs BEFORE taskENTER_CRITICAL. That matters: sniff_mux is a portMUX_TYPE, i.e.
     * the critical section masks interrupts on this core. modbus_crc16() is bitwise
     * (8 iterations per byte — ~2200 for a full-length frame), and running it with
     * interrupts masked stalls every ISR on the core, up to and including a FIFO
     * overrun on the neighbouring UART. */
    sniff_input_t in = {
        .is_fm           = (effective[1] == FAST_MODBUS_FUNC_1 ||
                            effective[1] == FAST_MODBUS_FUNC_2),
        .fm_slave_subcmd = fm_is_slave_subcmd(effective_len >= 3 ? effective[2] : 0),
        .crc_valid       = crc_check(effective, effective_len),
        .short_frame     = (effective_len < 4),
        .broadcast       = (effective[0] == 0x00),
        .dir             = (effective_len >= 4)
                               ? classify_direction(effective, effective_len)
                               : DIRECTION_UNKNOWN,
    };

    /* Read the clock once, before the lock: this is the arrival timestamp of the frame,
     * shared by the emitted packet and (when latched) the pending request. */
    uint64_t now_us = (uint64_t)esp_timer_get_time();

    /* Under the spinlock: read the framing state, decide, apply the transition.
     * Nothing else. No CRC, no classification, no memcpy, no timer calls. */
    taskENTER_CRITICAL(&sniff_mux);
    in.state           = ctx->state;
    in.synchronized    = ctx->synchronized;
    in.last_was_master = ctx->last_was_master;
    /* Compared against the same two bytes the latch stores below, and on the same
     * (stripped) view of the frame the classification used. Both indices are in range:
     * the caller rejected len < 4, and strip_arbitration() either leaves the buffer
     * alone or hands back at least 4 bytes. */
    in.matches_pending = ctx->req_valid &&
                         effective[0] == ctx->req_slave &&
                         effective[1] == ctx->req_fc;

    sniff_decision_t d = sniffer_decide(&in);

    ctx->state = d.new_state;
    if (d.emit) {
        /* The emitted packet is what the port resynchronises on. */
        ctx->synchronized    = true;
        ctx->last_was_master = d.is_master;
    }
    if (d.latch_request) {
        ctx->req_slave        = effective[0];
        ctx->req_fc           = effective[1];
        ctx->req_valid        = true;
        ctx->req_timestamp_us = now_us;
    }
    taskEXIT_CRITICAL(&sniff_mux);

    /* Outside the lock: the packet copy, the queue and the timers. xTimerStop/Start are
     * not spinlock-safe anyway. Order is stop -> enqueue -> start, as before: a re-latched
     * request must end up with a freshly armed timer. */
    if (d.stop_timer) {
        xTimerStop(ctx->resp_timer, 0);
    }

    bool enqueued = true;
    if (d.emit) {
        /* The arbitration-only branch reports the raw bytes; every other branch reports
         * the frame with the 0xFF arbitration prefix stripped. */
        const uint8_t *src     = d.from_raw ? data : effective;
        size_t         src_len = d.from_raw ? len  : effective_len;
        size_t         cpy     = src_len < SNIFFER_MAX_PACKET_LEN ? src_len
                                                                  : SNIFFER_MAX_PACKET_LEN;
        sniff_packet_t pkt = {0};
        pkt.port         = (uint8_t)port_index;
        pkt.timestamp_us = now_us;
        pkt.is_master    = d.is_master;
        pkt.crc_valid    = d.crc_valid;
        pkt.slave_id     = src[0];
        pkt.function     = src[1];
        memcpy(pkt.data, src, cpy);
        pkt.data_len     = (uint16_t)cpy;

        enqueued = try_enqueue(port_index, &pkt);
    }

    if (!enqueued) {
        /* The packet never reached the consumer, so the exchange it belongs to cannot be
         * followed any further: abandon the pending request and return the port to IDLE.
         * The state write goes through the spinlock (it is shared with resp_timer_cb() and
         * sniffer_disable()), and the response timer is deliberately NOT started — an armed
         * timer on a request whose master packet was dropped would fire a phantom timeout
         * packet for an exchange no consumer ever saw. Any previously armed timer was
         * already stopped above by all RES_WAIT decisions but one: the broadcast branch
         * leaves the earlier request's timer running on purpose. If it is a broadcast that
         * gets dropped here, that timer stays armed while req_valid is cleared — harmless,
         * because resp_timer_cb() re-reads req_valid under this same spinlock and emits
         * nothing when it is false, having set the state to SNIFF_IDLE regardless. */
        taskENTER_CRITICAL(&sniff_mux);
        ctx->state     = SNIFF_IDLE;
        ctx->req_valid = false;
        taskEXIT_CRITICAL(&sniff_mux);
        return;
    }

    /*
     * The return value is checked, and a lost start is not survivable in SNIFF_RES_WAIT.
     *
     * xTimerStart() does not arm anything itself: it posts a command to the FreeRTOS timer
     * command queue, CONFIG_FREERTOS_TIMER_QUEUE_LENGTH (10) entries deep, for the Tmr Svc
     * task to execute. That task runs at CONFIG_FREERTOS_TIMER_TASK_PRIORITY (1) and this
     * one at SERIAL_TASK_PRIORITY (12), so it cannot preempt us to drain the queue while we
     * are walking a buffer — and with xTicksToWait = 0 (mandatory: a serial task must not
     * block on the bus's behalf) a full queue means the command is simply dropped.
     *
     * A gap-less buffer makes that reachable rather than theoretical. Every re-latching
     * frame issues a stop and a start, so a single stream_split() pass of 15 write frames
     * posts 1 + 14 * 2 = 29 commands with no chance for Tmr Svc to run in between: the
     * first ten are queued and the rest are lost. The 10th is a stop, and the start that
     * belonged with it is the first casualty.
     *
     * Only the stop-honoured-start-lost combination is dangerous, which is why this one
     * call is the only one checked. If the stop is lost too, the timer stays armed on the
     * previous deadline and still fires: it expires early for the freshly latched request,
     * emits its timeout packet and returns the port to SNIFF_IDLE — a wrong timeout
     * instant, not a wedged port. But a stop that lands with no start behind it leaves the
     * port in SNIFF_RES_WAIT with req_valid = true and nothing armed to end it: no timeout
     * packet is ever emitted, and the first frame after the silence that happens to carry
     * the latched (address, FC) is reported as a slave reply to a request from minutes ago.
     *
     * So the port is returned to SNIFF_IDLE instead, exactly as the dropped-packet path
     * above does it and for the same reason: the exchange can no longer be followed. The
     * whole cost is one missing timeout event — the WS client is never shown those anyway
     * (sniffer_ws_dispatch drops is_timeout packets), and the cache overlay only uses one
     * to clear its pending entry, which the next packet from this port then clears or
     * replaces regardless. The state write goes through the spinlock, after the timer call,
     * keeping the established order: decide and mutate state under sniff_mux, touch
     * FreeRTOS timers outside it.
     *
     * The alternative considered was skipping the stop/start pair when the re-latch does
     * not change (address, FC), which would spare the queue on a burst of retries to one
     * slave. Rejected: it lowers the odds without bounding anything — a burst addressed to
     * different slaves posts every command exactly as before — and this check is what
     * guarantees the port cannot be left waiting forever, whatever the traffic. */
    if (d.start_timer && xTimerStart(ctx->resp_timer, 0) != pdPASS) {
        ESP_LOGW(TAG, "resp timer start failed, port %u returned to idle", port_index);
        taskENTER_CRITICAL(&sniff_mux);
        ctx->state     = SNIFF_IDLE;
        ctx->req_valid = false;
        taskEXIT_CRITICAL(&sniff_mux);
    }
}

/*
 * sniffer_process — public entry called from the serial RX callbacks.
 *
 * RS-485 is a trust boundary: the bytes on the wire are arbitrary. In gap-less
 * (repeater / transparent) mode a single idle-delimited buffer can carry many
 * valid back-to-back frames. stream_split() returns at most
 * STREAM_SPLITTER_MAX_FRAMES frames per call (up to 15 parsed frames plus one
 * trailing remainder holding everything it could not break up in that pass).
 *
 * A previous implementation recursed into sniffer_process() for every sub-frame,
 * including that trailing remainder, producing ~len/15 nested calls on a long
 * gap-less stream. Each level placed a stream_frame_t[16] array plus two
 * sniff_packet_t (~1 KB total) on the 4 KB UART task stack and overflowed it after
 * only a handful of levels, crashing the device.
 *
 * This version is iterative. It splits the current buffer, dispatches every
 * resulting sub-frame to the non-recursive sniffer_process_frame(), and loops on
 * the trailing remainder instead of recursing. Only the LAST frame returned by
 * stream_split() can still need a further split (the broken-frame remainder or the
 * defensive tail); all earlier frames are either CRC-valid frames or 0xFF
 * arbitration runs, neither of which re-splits. The remainder is strictly shorter
 * every iteration, so the loop terminates and stack usage stays constant.
 */
static void sniffer_process(unsigned port_index, const uint8_t *data, size_t len)
{
    /* Entry point of the whole RX path: the per-port callbacks published by
     * sniffer_attach() land here, and every branch below eventually reaches either
     * sniff_queue (try_enqueue, and the timeout callback) or ctx->resp_timer
     * (xTimerStart/xTimerStop in sniffer_process_frame) — so one check at the entry
     * covers all of them, instead of repeating it at each use.
     *
     * Silent, and once per received buffer rather than per frame: this is the only
     * guard on a path driven by bus traffic, and a sniffer that is down must not turn
     * every packet on the wire into a log line. See the readiness note above for why
     * sniff_queue alone is a sufficient test. */
    if (sniff_queue == NULL) {
        return;
    }

    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    /* Run only when at least one reason (display and/or cache) is active. */
    if (ctx->reasons == 0) {
        return;
    }
    if (len < 4) {
        return;
    }

    const uint8_t *cur     = data;
    size_t         cur_len = len;

    for (;;) {
        /* Strip leading 0xFF arbitration bytes before deciding whether to split.
         * For all-0xFF / non-FM buffers effective == cur and effective_len == cur_len. */
        const uint8_t *effective;
        size_t         effective_len;
        strip_arbitration(cur, cur_len, &effective, &effective_len);

        /* is_arb excludes pure 0xFF arbitration noise: there is no embedded frame to
         * split out of it. A genuine single valid frame is excluded by the crc_check()
         * guard. Only a multi-frame / corrupted blob is handed to stream_split(). */
        bool is_arb = (effective[0] == 0xFF);
        if (!(effective_len > 8 && !crc_check(effective, effective_len) && !is_arb)) {
            /* Not a splittable blob — run the state machine on the whole buffer. */
            sniffer_process_frame(port_index, cur, cur_len);
            return;
        }

        stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];
        int nframes = stream_split(effective, effective_len,
                                   ctx->req_valid ? ctx->req_slave : 0,
                                   ctx->req_valid ? ctx->req_fc    : 0,
                                   frames);
        if (nframes <= 1) {
            /* Splitter found nothing useful — process the whole buffer as one frame. */
            sniffer_process_frame(port_index, cur, cur_len);
            return;
        }

        /* Only the LAST frame can be a remainder that still needs splitting. Detect it
         * with the same predicate as the split guard above: long enough, not a 0xFF run,
         * CRC invalid as a whole. */
        int            last      = nframes - 1;
        const uint8_t *tail_data = frames[last].data;
        size_t         tail_len  = frames[last].len;
        bool tail_needs_split = !frames[last].crc_valid &&
                                tail_len > 8 &&
                                tail_data[0] != 0xFF &&
                                !crc_check(tail_data, tail_len);

        int dispatch_count = tail_needs_split ? last : nframes;
        for (int fi = 0; fi < dispatch_count; fi++) {
            sniffer_process_frame(port_index, frames[fi].data, frames[fi].len);
        }

        if (!tail_needs_split) {
            return;
        }

        /* Continue splitting the trailing remainder on the next loop iteration.
         * tail_len < cur_len always (earlier frames consumed >= 1 byte), so this loop
         * terminates. */
        cur     = tail_data;
        cur_len = tail_len;
    }
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

    if (fd == -1 || srv == NULL) {
        return;
    }

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
    /* Both branches below need the sniffer: the upgrade path takes ws_mutex, and the
     * frame path drives sniffer_enable()/sniffer_disable(), whose bookkeeping is
     * pointless while the pipeline behind it does not exist.
     *
     * Answer instead of pretending: 503, not 4xx. Nothing is wrong with the request —
     * the device is either still finishing its boot or running degraded after a failed
     * sniffer_init(), and the same request is expected to succeed once it is not. The
     * web UI reconnects on its own, so a transient status is what it needs to hear.
     *
     * Caveat, the same one the auth rejection above documents: on a genuine WebSocket
     * upgrade IDF has already put the 101 on the wire before this handler runs, so such
     * a client sees the connection break rather than a clean 503. A plain HTTP GET to
     * this URI (no Upgrade header) gets the real status line.
     *
     * Answered ahead of the auth check below on purpose: "this device is not ready to
     * serve this endpoint" is a state, not data, and it is what any server says before
     * it can authenticate anyone. Nothing about the sniffer or its contents leaks. */
    if (ws_mutex == NULL) {
        ESP_LOGW(TAG, "/sniffer/ws request before the sniffer was initialized, replying 503");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        const char *msg = "sniffer not initialized";
        httpd_resp_send(req, msg, (ssize_t)strlen(msg));
        return ESP_OK;
    }

    if (req->method == HTTP_GET) {
        /* Authenticate the upgrade request — cookie is available in the HTTP headers.
         * IDF v5.4 limitation: cannot reject WS upgrade from the handler (ESP_FAIL is ignored
         * by the WS layer, 101 is always sent). Instead, close the connection immediately
         * after the upgrade if auth fails — unauthenticated clients get 101 then FIN. */
        if (!auth_middleware_check(req)) {
            httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
            return ESP_OK;
        }
        /* The sniffer has a single client slot. Remember the client we are about to
         * evict so its httpd session can be closed below: overwriting ws_client_fd
         * alone drops our reference but leaves the old session open forever. With
         * MAX_OPEN_SOCKETS = 12, lru_purge_enable = false and no WS idle timeout,
         * such orphans accumulate (second tab, reconnect after a Wi-Fi drop that
         * never produced a clean TCP close) until httpd stops accepting ANY
         * connection and the whole web UI goes dark.
         * Policy — one client, the newest wins — matches the transparent TCP bridge. */
        xSemaphoreTake(ws_mutex, portMAX_DELAY);
        int            old_fd  = ws_client_fd;
        httpd_handle_t old_srv = ws_server;
        ws_server    = req->handle;
        ws_client_fd = httpd_req_to_sockfd(req);
        int new_fd   = ws_client_fd; // capture under mutex to avoid race in log
        xSemaphoreGive(ws_mutex);

        /* Outside the mutex: httpd_sess_trigger_close() reaches into the httpd task
         * and must not run with our lock held. Skip when the fd is unchanged — httpd
         * can hand out the same fd number for the new session once the old one is
         * already gone, and closing it would kill the client we just accepted.
         *
         * ws_client_fd is cleared lazily (only sniffer_ws_dispatch() notices a dead
         * fd, and only once a packet actually flows through the queue), so a client
         * that closed its tab while the bus was silent leaves a stale fd here for an
         * arbitrarily long time. That number can already have been recycled by the
         * httpd layer for an unrelated plain-HTTP connection — closing it blind would
         * kill a stranger's REST request or OTA upload. Same guard as the send path
         * below: evict only what is still a WebSocket session. */
        if (old_fd >= 0 && old_fd != new_fd && old_srv != NULL &&
            httpd_ws_get_fd_info(old_srv, old_fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
            ESP_LOGI(TAG, "WS client replaced, closing previous session fd=%d", old_fd);
            httpd_sess_trigger_close(old_srv, old_fd);
        }

        ESP_LOGI(TAG, "WS client connected, fd=%d", new_fd);
        return ESP_OK;
    }

    /* WebSocket frame message path — auth was verified during the HTTP GET upgrade above;
     * unauthenticated connections are closed immediately via httpd_sess_trigger_close(). */
    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len == 0 || ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;
    }

    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }

    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    buf[ws_pkt.len] = '\0';

    cJSON *root = cJSON_Parse((char *)buf);
    free(buf);

    if (!root) {
        return ESP_OK;
    }

    cJSON *cmd  = cJSON_GetObjectItem(root, "cmd");
    cJSON *port = cJSON_GetObjectItem(root, "port");

    if (cmd && cJSON_IsString(cmd) && cJSON_IsNumber(port)) {
        bool enable = (strcmp(cmd->valuestring, "start") == 0);
        unsigned idx = port_name_to_index((unsigned)port->valuedouble);
        if (idx < BRIDGES_COUNT) {
            if (enable) {
                sniffer_enable(idx, SNIFF_REASON_DISPLAY);
            } else {
                sniffer_disable(idx, SNIFF_REASON_DISPLAY);
            }
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

    /* No readiness check here on purpose: this handler touches no FreeRTOS handle, only
     * the statically allocated sniff_ctx[]. Before initialisation every reason bit is
     * zero and stays zero (sniffer_enable() refuses to set one), so the honest answer —
     * "not running on either port" — is exactly what this reports. */
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


/* Release everything sniffer_init() has created so far. Safe to call at any early
 * exit: every handle is NULL-checked and cleared, so a partially built state is
 * torn down exactly once and a later sniffer_init() starts from a clean slate. */
static void sniffer_init_cleanup(void)
{
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (sniff_ctx[i].resp_timer) {
            xTimerDelete(sniff_ctx[i].resp_timer, 0);
            sniff_ctx[i].resp_timer = NULL;
        }
    }
    if (sniff_queue) {
        vQueueDelete(sniff_queue);
        sniff_queue = NULL;
    }
    if (ws_mutex) {
        vSemaphoreDelete(ws_mutex);
        ws_mutex = NULL;
    }
}

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
        sniffer_init_cleanup();
        return ESP_ERR_NO_MEM;
    }

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        sniff_ctx[i].state           = SNIFF_IDLE;
        sniff_ctx[i].reasons         = 0;
        sniff_ctx[i].req_valid       = false;
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
            sniffer_init_cleanup();
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t res = xTaskCreate(sniffer_ws_task, "sniffer_ws",
        SNIFFER_WS_TASK_STACK, NULL, SNIFFER_WS_TASK_PRIO, NULL);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sniffer WS task");
        sniffer_init_cleanup();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Sniffer initialized");
    return ESP_OK;
}

void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc)
{
    if (port_index >= BRIDGES_COUNT || serial_desc == NULL) {
        return;
    }
    /* Do not put a sniffer that is not up into the UART hot path. Publishing the
     * callback below is what makes every RX buffer walk into sniff_queue and
     * ctx->resp_timer; refusing here keeps the whole path dark at zero runtime cost,
     * where sniffer_process()'s own check pays a comparison per buffer. Quiet: the
     * caller (port bring-up) has no recovery to attempt, and the reason the sniffer is
     * missing was already logged by sniffer_init() and by main.c. */
    if (sniff_queue == NULL) {
        return;
    }
    // Store the descriptor in the context before publishing the callback, so the
    // UART event task never sees sniff_handler set while serial_desc is still stale.
    sniff_ctx[port_index].serial_desc = serial_desc;
    // Publish the callback with a release store. Plain ordering of the two writes is not
    // enough: nothing stops the compiler from reordering them, and the reader is the UART
    // event task, potentially on the other core. The release store makes the serial_desc
    // write above visible to any core that observes this non-NULL sniff_handler.
    __atomic_store_n(&serial_desc->sniff_handler, s_port_callbacks[port_index], __ATOMIC_RELEASE);
}

void sniffer_detach(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return;
    }
    /* Fully disable: clear all reasons so framing state and RX timeout are reset. */
    sniffer_disable(port_index, SNIFF_REASON_DISPLAY);
    sniffer_disable(port_index, SNIFF_REASON_CACHE);
    // Do NOT clear serial_desc->sniff_handler here. port_manager now calls
    // sniffer_detach() AFTER the transport is torn down (bridge_port_deinit() /
    // serial_deinit() / repeater_deinit_port()). By this point no reader of sniff_handler
    // is left, and serial_desc itself has already been freed: there is nobody to retract
    // the callback for, and touching the freed descriptor would be a use-after-free.
    // Just drop our own reference to it.
    //
    // "No reader left" is not everywhere a join. serial_deinit() does join the port's own
    // UART event task, but sniff_handler is also called from serial_send() (serial.c:373-376),
    // and on a repeater port that caller is the PEER's UART task, which repeater_deinit_port()
    // never joins — there the last such call is ended by the s_inflight drain, which brackets
    // exactly the serial_send() window. transparent_tcp rules its own TCP -> serial senders
    // out under the port's serial lock, likewise without joining them.
    sniff_ctx[port_index].serial_desc = NULL;
}

void sniffer_enable(unsigned port_index, sniff_reason_t reason)
{
    if (port_index >= BRIDGES_COUNT) {
        return;
    }
    /* The reasons bitmask is the switch that un-gates the RX pipeline and the answer
     * /sniffer/status gives the UI. Setting it while the pipeline does not exist would
     * make both lie: the port would claim to be sniffing, and the cache overlay would
     * be told to expect data that no queue can carry. Quiet for the same reason as
     * sniffer_attach() above. */
    if (sniff_queue == NULL) {
        return;
    }
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
    if (port_index >= BRIDGES_COUNT) {
        return;
    }
    sniff_ctx_t *ctx = &sniff_ctx[port_index];

    /* Perform the read-modify-write and non-zero -> 0 edge detection under the
     * spinlock so concurrent writers cannot lose a bit. The framing-state reset is
     * safe here (sniffer_process mutates these fields under sniff_mux too).
     *
     * This runs unconditionally, ahead of any handle check: the bitmask is plain
     * memory, it is what /sniffer/status reports and what sniffer_detach() clears a
     * port through. Skipping it because a FreeRTOS handle went missing would strand
     * exactly the state this function exists to undo — a port claiming to be sniffing
     * with nothing behind it. The handle check belongs at the one place a handle is
     * used, which is xTimerStop() at the tail. */
    taskENTER_CRITICAL(&sniff_mux);
    uint8_t prev = ctx->reasons;
    ctx->reasons = (uint8_t)(prev & ~(uint8_t)reason);
    bool became_idle = (prev != 0) && (ctx->reasons == 0);
    if (became_idle) {
        ctx->req_valid = false;  /* Prevent stale timer CB from emitting a packet */
        ctx->state     = SNIFF_IDLE;
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
     * spinlock-safe, so it runs after releasing the lock.
     *
     * The timer is the only handle this function hands to FreeRTOS, so this is where it
     * is checked. Deliberately not phrased as "sniffer_enable() would have refused to
     * set the bit, so this line cannot be reached with a NULL timer" — that is true
     * today and is exactly the kind of cross-function reasoning this file is moving away
     * from. Nothing ties the lifetime of the bitmask to the lifetime of the timers, so
     * the function checks what it is about to use. With the handle gone there is nothing
     * left to quiesce anyway — the pipeline it belonged to went with it, and the bits
     * that described it have just been cleared above. */
    if (ctx->resp_timer != NULL) {
        xTimerStop(ctx->resp_timer, 0);
    }
    ESP_LOGI(TAG, "Sniffer fully disabled on port %u", port_index);
}

esp_err_t sniffer_register_handlers(httpd_handle_t server)
{
    esp_err_t ret = httpd_register_uri_handler(server, &sniffer_ws_uri);
    if (ret != ESP_OK) {
        return ret;
    }
    return httpd_register_uri_handler(server, &sniffer_status_uri);
}
