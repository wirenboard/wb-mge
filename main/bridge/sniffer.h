#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "serial.h"

/* Maximum length of a single sniffed packet (bytes).
 *
 * The longest frame on the wire is a Fast Modbus packet with an encapsulated
 * standard command (FD 46 08 / FD 46 09):
 *
 *   RTU ADU          256 = 1 (addr) + 253 (PDU) + 2 (CRC16)
 *   FM wrapper         9 = FD + 46 + subcmd + serial(4) + CRC16(2)
 *                    ---
 *                    265 bytes
 *
 * Leading 0xFF arbitration bytes are not counted: strip_arbitration() removes
 * them before the frame reaches the packet buffer.
 *
 * Rounded up to 272 (a multiple of 8) so that sniff_packet_t, whose uint64_t
 * timestamp_us forces 8-byte alignment, has no tail padding — leaving 7 spare
 * bytes. At 256 a full FM-encapsulated frame was silently truncated in the WS
 * output (never an overflow: every memcpy is length-clamped, and crc_valid is
 * computed over the real length before the copy).
 */
#define SNIFFER_MAX_PACKET_LEN 272
/* Size of the JSON formatting buffer used by the sniffer WS task.
 * Worst case: 272 * 2 = 544 hex chars + NUL, plus ~160 bytes of JSON scaffolding
 * (keys, id, port, timestamp, slave_id, function, flags) ~= 705 bytes. */
#define SNIFFER_JSON_BUF_SIZE  1200

#ifndef __unittest_env__
#include "esp_http_server.h"
#else
typedef void* httpd_handle_t;
#endif

/* One sniffed frame as it travels from the state machine to the WS task.
 * Declared here (not in sniffer.c) so production code and unit tests share a
 * single definition: the previous per-build duplicate could drift silently.
 * Depends only on stdint/stdbool and SNIFFER_MAX_PACKET_LEN above. */
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

/* Direction classification result for a standard Modbus RTU PDU.
 * Shared with the tests for the same reason as sniff_packet_t above: one
 * definition, no value drift between the production and test builds. */
typedef enum {
    DIRECTION_REQUEST  = 0, /* Packet is unambiguously a master request */
    DIRECTION_RESPONSE = 1, /* Packet is unambiguously a slave response  */
    DIRECTION_UNKNOWN  = 2, /* Cannot determine direction from length/FC alone */
} pdu_direction_t;

/* Per-port framing state of the request/response state machine. */
typedef enum {
    SNIFF_IDLE = 0,
    SNIFF_RES_WAIT,
} sniff_state_t;

/*
 * The frame-decision model.
 *
 * Everything the state machine needs in order to decide what to do with one frame
 * is reduced to sniff_input_t, and everything it decides is a sniff_decision_t.
 * sniffer_decide() maps one to the other as a pure function: no locks, no clock, no
 * queue, no side effects. That keeps the CRC check, the direction classification and
 * the packet memcpy out of the spinlock (only the state transition needs it) and
 * makes the decision table directly testable.
 */
typedef struct {
    /* Port framing state — read under the spinlock by the caller. */
    sniff_state_t   state;
    bool            synchronized;    /* a packet with a known direction has been seen */
    bool            last_was_master; /* direction of the most recently emitted packet */

    /* Properties of the frame itself — derived by the caller before taking the lock. */
    bool            is_fm;           /* function code is Fast Modbus (0x46 / 0x60) */
    bool            fm_slave_subcmd; /* the FM subcommand marks a slave response */
    bool            crc_valid;       /* RTU CRC16 checks out over the whole frame */
    bool            short_frame;     /* below the 4-byte minimum (arbitration-only) */
    bool            broadcast;       /* slave address 0x00 — no response expected */
    pdu_direction_t dir;             /* classify_direction() of the frame */
} sniff_input_t;

typedef struct {
    bool          emit;          /* emit exactly one packet for this frame */
    bool          is_master;     /* sender of the emitted packet */
    bool          crc_valid;     /* CRC flag to stamp on the emitted packet */
    bool          from_raw;      /* build the packet from the raw, unstripped bytes */
    bool          latch_request; /* remember slave/fc/timestamp as the pending request */
    sniff_state_t new_state;     /* framing state to move to */
    bool          start_timer;   /* (re)arm the slave-response timeout */
    bool          stop_timer;    /* disarm the slave-response timeout */
} sniff_decision_t;

/**
 * @brief Reasons that keep the sniffer pipeline running for a port.
 *
 * The sniffer is an additive overlay: it runs whenever at least one reason bit
 * is set. Each reason is enabled/disabled independently by its owner.
 */
typedef enum {
    SNIFF_REASON_DISPLAY = 1u << 0,  // a WS client wants live display
    SNIFF_REASON_CACHE   = 1u << 1,  // the cache overlay wants data
} sniff_reason_t;

esp_err_t sniffer_init(void);
void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc);
void sniffer_detach(unsigned port_index);
void sniffer_enable(unsigned port_index, sniff_reason_t reason);
void sniffer_disable(unsigned port_index, sniff_reason_t reason);

esp_err_t sniffer_register_handlers(httpd_handle_t server);
