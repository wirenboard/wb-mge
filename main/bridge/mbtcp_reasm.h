#pragma once

/* Modbus TCP stream reassembly.
 *
 * TCP is a byte stream: one recv() may deliver half a frame, or several frames
 * coalesced, or a frame boundary in the middle of a header. This module keeps a
 * per-connection accumulation buffer, finds ADU boundaries via the MBAP length
 * field, hands every complete frame to a callback, and carries the remainder
 * over to the next recv().
 *
 * The context is explicit — each server (each gateway port, the cache server,
 * ...) owns its own mbtcp_reasm_t. No module-level state.
 */

#include "modbus_helpers.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum concurrent TCP clients tracked per reassembler.
 *
 * This is NOT backed by a connection limit in tcp_server.c: tcp_desc_t.max_connections
 * defaults to 0 (unlimited) and only the transparent bridge ever sets it (to 1). The
 * Modbus TCP gateway ports and the cache server therefore accept as many clients as
 * lwIP allows, and the 9th one onwards gets no slot here.
 *
 * That is a graceful degradation, not a failure: feed() returns MBTCP_REASM_NO_SLOT and
 * the caller falls back to parsing the recv() buffer unbuffered — a frame that arrives
 * whole still works, one split across recvs on that connection does not. The condition
 * is counted (slot_exhausted / mbtcp_reasm_slot_exhausted()). */
#define MBTCP_REASM_MAX_CONNS  8

/* Per-connection accumulation buffer size. A Modbus TCP ADU is at most
 * MODBUS_TCP_MAX_ADU_LEN (260) bytes; the extra headroom means an over-long
 * length field is rejected as a bad header rather than mistaken for a frame we
 * are still waiting on. */
#define MBTCP_REASM_FRAME_MAX  300

/* Returned by mbtcp_reasm_feed() when no reassembly slot was available (more
 * than MBTCP_REASM_MAX_CONNS simultaneous connections). Nothing was buffered and
 * nothing was dispatched: the caller decides how to handle the buffer — typically
 * a best-effort unbuffered parse. */
#define MBTCP_REASM_NO_SLOT  (-1)

/**
 * @brief Called once per complete Modbus TCP ADU found in the stream.
 *
 * @param user_ctx  Opaque pointer passed through from mbtcp_reasm_feed().
 * @param sock      Client socket the frame arrived on.
 * @param frame     Start of the ADU (MBAP header first). Valid only for the
 *                  duration of the call — copy it if you need to keep it.
 * @param len       Full ADU length, as declared by the MBAP length field.
 * @return true if the frame was accepted; feed() counts these. A frame the
 *         callback rejects (bad framing, full queue, ...) still advances the
 *         stream — it is consumed either way.
 */
typedef bool (*mbtcp_reasm_frame_cb_t)(void *user_ctx, int sock,
                                       const uint8_t *frame, size_t len);

typedef struct {
    int     sock;                        /* -1 = free slot */
    size_t  len;                         /* bytes currently buffered */
    uint8_t buf[MBTCP_REASM_FRAME_MAX];
} mbtcp_reasm_slot_t;

typedef struct {
    mbtcp_reasm_slot_t slots[MBTCP_REASM_MAX_CONNS];
    /* Guards slot allocation/release only. Per-connection buf/len needs no lock:
     * after allocation a slot is touched by exactly one receiver task. May be
     * NULL (single-threaded unit tests), in which case locking is skipped. */
    SemaphoreHandle_t  mutex;
    /* Count of feed() calls that found no free slot. Best-effort (plain ++,
     * unlocked): a diagnostic signal that the connection limit was exceeded,
     * not an exact metric. */
    volatile uint32_t  slot_exhausted;
    const char        *tag;              /* log tag of the owning module */
} mbtcp_reasm_t;

/* Clear all slots and create the mutex. Returns false if the mutex could not be
 * created (the reassembler is then unusable). @p tag is used as the ESP log tag
 * and must outlive the reassembler (a string literal). */
bool mbtcp_reasm_init(mbtcp_reasm_t *r, const char *tag);

/* Release the mutex. Call only once every task that could feed() has stopped. */
void mbtcp_reasm_deinit(mbtcp_reasm_t *r);

/* Release the slot held by @p sock, discarding any partially buffered frame.
 * Call from the connection-close hook — otherwise the slot leaks and, worse, a
 * reused file descriptor would inherit the previous connection's partial frame. */
void mbtcp_reasm_close(mbtcp_reasm_t *r, int sock);

/* Bytes currently buffered for @p sock; 0 if it holds no slot. Lets a caller
 * distinguish "this recv produced no frame because the data was rejected" from
 * "…because a partial frame is still accumulating". */
size_t mbtcp_reasm_pending(mbtcp_reasm_t *r, int sock);

/* True if @p sock currently holds a slot. Distinguishes "no slot" from "a slot
 * holding zero buffered bytes", which mbtcp_reasm_pending() cannot. */
bool mbtcp_reasm_has_slot(mbtcp_reasm_t *r, int sock);

/* Number of feed() calls that found no free slot since init. */
uint32_t mbtcp_reasm_slot_exhausted(const mbtcp_reasm_t *r);

/**
 * @brief Feed one recv() buffer into the reassembler.
 *
 * Appends the bytes to @p sock's buffer and dispatches every complete ADU to
 * @p on_frame. A trailing partial frame stays buffered for the next call.
 *
 * A frame boundary is accepted only when the MBAP header is plausible: protocol
 * id 0 and a length field that yields an ADU between the header size and
 * MBTCP_REASM_FRAME_MAX. On a bad header the stream resyncs ONE BYTE at a time —
 * Modbus TCP has no sync marker, so discarding the whole buffer would also throw
 * away any valid frame coalesced behind the bad one.
 *
 * @return the number of frames @p on_frame accepted (>= 0), or
 *         MBTCP_REASM_NO_SLOT if no slot was available for this socket.
 */
int mbtcp_reasm_feed(mbtcp_reasm_t *r, int sock, const uint8_t *data, size_t len,
                     mbtcp_reasm_frame_cb_t on_frame, void *user_ctx);

/* Total ADU length declared by an MBAP header (needs >= 6 readable bytes).
 * Exposed for callers that parse a header themselves. */
size_t mbtcp_reasm_frame_total_len(const uint8_t *buf);
