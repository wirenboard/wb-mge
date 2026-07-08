#pragma once
/*
 * Wirenboard Fast Modbus - event reading (simplified, single device).
 * Pure frame build/parse helpers; the caller performs the actual serial I/O.
 * Protocol: command 0x46, subcommands 0x10 (request events), 0x11 (events),
 * 0x12 (no events), 0x18 (enable events).
 */
#include <stdint.h>
#include <stdbool.h>

#define FM_ADDR_BROADCAST 0xFD
#define FM_CMD            0x46
#define FM_SUB_REQUEST    0x10
#define FM_SUB_EVENTS     0x11
#define FM_SUB_NO_EVENTS  0x12
#define FM_SUB_ENABLE     0x18

/* Fast Modbus register/event type codes (distinct from our reg_type_t). */
#define FM_TYPE_COIL      1
#define FM_TYPE_DISCRETE  2
#define FM_TYPE_HOLDING   3
#define FM_TYPE_INPUT     4
#define FM_TYPE_REBOOT    0x0F

#define FM_MAX_EVENT_DATA 8

typedef struct {
    uint8_t  type;                     /* FM_TYPE_* */
    uint16_t id;                       /* register address (big-endian on wire) */
    uint8_t  data_len;                 /* payload length in bytes */
    uint8_t  data[FM_MAX_EVENT_DATA];  /* payload (little-endian on wire) */
} fm_event_t;

typedef struct {
    uint8_t  reg_type;   /* FM_TYPE_COIL..FM_TYPE_INPUT */
    uint16_t addr;
    uint8_t  count;      /* number of consecutive registers */
    uint8_t  priority;   /* 0 off, 1 low, 2 high - applied to all `count` regs */
} fm_enable_range_t;

/* Build a "request events" frame. Returns frame length, or -1 if out too small. */
int fm_build_request(uint8_t *out, int out_sz,
                     uint8_t min_slave, uint8_t max_data_len,
                     uint8_t ack_slave, uint8_t ack_flag);

/* Build an "enable events" frame for one slave. Returns length, or -1 on error. */
int fm_build_enable(uint8_t *out, int out_sz, uint8_t slave,
                    const fm_enable_range_t *ranges, int n_ranges);

/* Parse a response frame (leading 0xFF padding tolerated).
 * Returns: >0 = number of events parsed into events[] (0x11 response);
 *           0 = no events (0x12 response);
 *          -1 = invalid frame / CRC error / truncated.
 * On a 0x11 response, *out_slave and *out_flag are set (for ACK). */
int fm_parse_response(const uint8_t *buf, int len,
                      fm_event_t *events, int max_events,
                      uint8_t *out_slave, uint8_t *out_flag);

/* Decode an event payload (little-endian, up to 4 bytes) as an unsigned int. */
uint32_t fm_event_value(const fm_event_t *ev);
