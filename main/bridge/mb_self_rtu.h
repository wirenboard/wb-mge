#pragma once

#include "modbus_helpers.h"   /* MODBUS_RTU_MAX_FRAME_LEN */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Modbus RTU responder for requests addressed to this gateway's own unit id, sitting
 * on a repeater port.
 *
 * The port it hangs on relays a foreign master's traffic byte for byte, and that
 * timing is a requirement, not an accident: the master runs its poll to a measured bus
 * budget. So this module buffers NOTHING that is not addressed to us — a chunk whose
 * first byte is another unit id is handed straight back for relaying, unchanged and
 * with no added latency. Only a frame that starts with MB_DEVICE_UNIT_ID is
 * accumulated, and it is never relayed: those bytes are ours, and putting them on the
 * peer's wire would be worse than losing them.
 *
 * The caller feeds it raw UART chunks, which have NO relation to frame boundaries
 * (the port runs with the idle-timeout split disabled). A frame may therefore arrive
 * in any number of pieces, and a piece may also carry more bytes than the frame needs.
 * What cannot happen is two frames in one chunk: the peer is a Modbus master and only
 * sends its next request once the current transaction has finished.
 */

/* A chunk that arrives more than this long after the previous one cannot belong to the
 * frame in progress. The port splits chunks on its own idle timeout, SERIAL_RX_TOUT_PROXY
 * = 10 symbol periods (set for repeater ports in port_manager.c): 0.95 ms at the 115200
 * this link runs at, and 91.7 ms even at the slowest rate the firmware allows. This
 * threshold therefore sits above the hardware's own split at every rate, so a pause that
 * reaches it cannot be a pause inside one frame.
 * Without it, one truncated request would wedge the responder for good — every later
 * byte would be appended to a frame that can never complete. */
#define MB_SELF_RTU_GAP_US   100000

/* What the caller must do with the chunk it just fed in. */
typedef enum {
    MB_SELF_RTU_RELAY,     /* not ours: relay the chunk to the peer port, unchanged */
    MB_SELF_RTU_HELD,      /* ours, nothing to send: frame incomplete, or dropped    */
    MB_SELF_RTU_RESPOND,   /* ours: *resp_len bytes in resp_buf go back on this port */
} mb_self_rtu_result_t;

/* Accumulator state. Zero-initialise it once; the module owns every field. */
typedef struct {
    uint8_t buf[MODBUS_RTU_MAX_FRAME_LEN];
    size_t  len;             /* bytes held; 0 means "no frame in progress"      */
    int64_t last_chunk_us;   /* arrival time of the last chunk, from the caller */
} mb_self_rtu_t;

/* Forget any frame in progress. */
void mb_self_rtu_reset(mb_self_rtu_t *st);

/*
 * Feed one received chunk.
 *
 *   now_us   : a monotonic microsecond timestamp (esp_timer_get_time() in production).
 *   resp_buf : at least MODBUS_RTU_MAX_FRAME_LEN bytes, written only on RESPOND.
 *   resp_len : set to the response length on RESPOND, 0 otherwise.
 *
 * On RELAY the chunk was not touched and no state is kept.
 */
mb_self_rtu_result_t mb_self_rtu_feed(mb_self_rtu_t *st, const uint8_t *data, size_t len,
                                      int64_t now_us, uint8_t *resp_buf, size_t *resp_len);
