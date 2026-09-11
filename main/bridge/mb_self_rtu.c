#include "mb_self_rtu.h"

#include "mb_device.h"
#include "modbus_helpers.h"
#include "stream_splitter.h"

#include <string.h>

/* Verify the RTU CRC of a complete frame. modbus_crc16() hands back a native word and
 * the wire carries its LOW byte first (the convention note at the top of
 * stream_splitter.c), which is what the two comparisons below spell out. */
static bool frame_crc_ok(const uint8_t *buf, size_t len)
{
    uint16_t crc = modbus_crc16(buf, (uint16_t)(len - 2u));
    return (buf[len - 2u] == (uint8_t)(crc & 0xFFu)) && (buf[len - 1u] == (uint8_t)(crc >> 8));
}

void mb_self_rtu_reset(mb_self_rtu_t *st)
{
    /* The whole buffer is cleared, not just the length: mb_self_rtu_feed() sizes the
     * frame in progress against the buffer's full extent, and the bytes past what is
     * actually held have to read as zeros for that to be sound. */
    memset(st->buf, 0, sizeof(st->buf));
    st->len = 0;
}

mb_self_rtu_result_t mb_self_rtu_feed(mb_self_rtu_t *st, const uint8_t *data, size_t len,
                                      int64_t now_us, uint8_t *resp_buf, size_t *resp_len)
{
    *resp_len = 0;

    if (len == 0) {
        return MB_SELF_RTU_HELD;
    }

    /* A frame in progress that the gap says cannot continue is abandoned here, so this
     * chunk gets the same treatment a chunk arriving on an empty accumulator would. */
    if ((st->len > 0) && ((now_us - st->last_chunk_us) > MB_SELF_RTU_GAP_US)) {
        mb_self_rtu_reset(st);
    }

    /* Nothing held: one look at the first byte decides it. Anything not addressed to
     * us is relayed as it arrived, and no state is kept for it — that is what keeps
     * the relayed traffic's timing identical to a plain repeater's. */
    if (st->len == 0) {
        if (data[0] != MB_DEVICE_UNIT_ID) {
            return MB_SELF_RTU_RELAY;
        }
        mb_self_rtu_reset(st);
    }

    if (len > (sizeof(st->buf) - st->len)) {
        /* Overflow: these bytes were addressed to us, so they are dropped rather than
         * relayed onto the peer's line. */
        mb_self_rtu_reset(st);
        return MB_SELF_RTU_HELD;
    }
    memcpy(st->buf + st->len, data, len);
    st->len += len;
    st->last_chunk_us = now_us;

    if (st->len < 2u) {
        return MB_SELF_RTU_HELD;   /* the function code has not arrived yet */
    }

    /*
     * Ask for the frame length against the buffer's full extent rather than the bytes
     * actually held, because stream_frame_expected_len() answers 0 for BOTH "unknown
     * function code" and "known, but longer than avail" — and those two need opposite
     * handling. Past st->len the buffer reads as zeros (mb_self_rtu_reset()), which is
     * enough for every sizeable function code to come back with some length: the
     * byte-count-driven ones (FC 0F/10) then report their minimum, which is still
     * larger than the bytes that produced it, so the frame simply stays incomplete
     * until the real byte count has arrived. A 0 here therefore means exactly one
     * thing: nothing can size this frame, and holding on to it would wedge the
     * accumulator, so it goes.
     */
    size_t expected = stream_frame_expected_len(st->buf, sizeof(st->buf), /*is_response=*/false);
    if (expected == 0) {
        mb_self_rtu_reset(st);
        return MB_SELF_RTU_HELD;
    }
    if (st->len < expected) {
        return MB_SELF_RTU_HELD;
    }

    /* Complete. Anything beyond `expected` cannot be the start of a second frame — the
     * peer is a Modbus master and sends its next request only after this transaction
     * has finished — so the accumulator is cleared whole below. */
    if (!frame_crc_ok(st->buf, expected)) {
        mb_self_rtu_reset(st);
        return MB_SELF_RTU_HELD;
    }

    size_t answer_len = mb_device_rtu_handle_request(st->buf, expected, resp_buf);
    mb_self_rtu_reset(st);

    if (answer_len == 0) {
        return MB_SELF_RTU_HELD;
    }
    *resp_len = answer_len;
    return MB_SELF_RTU_RESPOND;
}
