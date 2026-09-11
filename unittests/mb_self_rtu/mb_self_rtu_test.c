#include "unity.h"
#include "console_log.h"

#include "mb_self_rtu.h"
#include "mb_device.h"
#include "mock_mb_device.h"
#include "modbus_helpers.h"

#include <stdint.h>
#include <string.h>

/* ---- Helpers ------------------------------------------------------------- */

/* Append the RTU CRC (low byte first) to len bytes in buf; returns the ADU length. */
static size_t add_crc(uint8_t *buf, size_t len)
{
    uint16_t crc = modbus_crc16(buf, (uint16_t)len);
    buf[len]     = (uint8_t)(crc & 0xFFu);
    buf[len + 1] = (uint8_t)(crc >> 8);
    return len + 2u;
}

/* Build an 8-byte RTU read request for the given unit. */
static size_t make_read_req(uint8_t *buf, uint8_t unit, uint16_t start, uint16_t count)
{
    buf[0] = unit;
    buf[1] = 0x03u;
    buf[2] = (uint8_t)(start >> 8);
    buf[3] = (uint8_t)(start & 0xFFu);
    buf[4] = (uint8_t)(count >> 8);
    buf[5] = (uint8_t)(count & 0xFFu);
    return add_crc(buf, 6u);
}

/* Build an RTU FC16 write request for the given unit. */
static size_t make_write_req(uint8_t *buf, uint8_t unit, uint16_t start, uint16_t count)
{
    buf[0] = unit;
    buf[1] = 0x10u;
    buf[2] = (uint8_t)(start >> 8);
    buf[3] = (uint8_t)(start & 0xFFu);
    buf[4] = (uint8_t)(count >> 8);
    buf[5] = (uint8_t)(count & 0xFFu);
    buf[6] = (uint8_t)(count * 2u);
    for (uint16_t i = 0; i < count; i++) {
        buf[7 + i * 2]     = (uint8_t)(i + 1u);
        buf[7 + i * 2 + 1] = (uint8_t)(i + 2u);
    }
    return add_crc(buf, (size_t)(7u + count * 2u));
}

static mb_self_rtu_t   st;
static uint8_t         resp[MODBUS_RTU_MAX_FRAME_LEN];
static size_t          resp_len;
static int64_t         now_us;

/* Feed one chunk at the current mock clock. */
static mb_self_rtu_result_t feed(const uint8_t *data, size_t len)
{
    return mb_self_rtu_feed(&st, data, len, now_us, resp, &resp_len);
}

void setUp(void)
{
    memset(&st, 0, sizeof(st));
    memset(resp, 0, sizeof(resp));
    resp_len = 0;
    now_us   = 1000000;   /* an arbitrary non-zero start, so a gap can be measured back */
    mock_mb_device_reset();
}

void tearDown(void)
{
}

/* ---- Tests --------------------------------------------------------------- */

/* A frame addressed to another unit is relayed untouched and leaves no state behind:
 * the relayed traffic's timing is the point of the whole design. */
void test_foreign_frame_relayed_untouched(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A frame for another unit is relayed, not held");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, 0x01u, 100u, 4u);

    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RELAY, feed(req, len), "a foreign frame must be relayed");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, resp_len, "nothing is answered on a relayed frame");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mb_device_calls.called, "the responder is not consulted");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "no state is kept for relayed traffic");

    /* A chunk that is only part of a foreign frame is relayed the same way, chunk by
     * chunk, with nothing buffered in between. */
    TEST_ASSERT_EQUAL(MB_SELF_RTU_RELAY, feed(req, 3u));
    TEST_ASSERT_EQUAL_UINT(0u, st.len);
    TEST_ASSERT_EQUAL(MB_SELF_RTU_RELAY, feed(req + 3, len - 3u));
    TEST_ASSERT_EQUAL_UINT(0u, st.len);
}

/* A complete frame for us in a single chunk is answered on the spot. */
void test_own_frame_single_chunk(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A whole frame for unit 0xFF is answered");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);

    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RESPOND, feed(req, len), "our frame must be answered");
    TEST_ASSERT_EQUAL_INT(1, mock_mb_device_calls.called);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(len, mock_mb_device_calls.last_req_len,
        "the responder gets the frame without its trailing bytes");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, mock_mb_device_calls.last_req, len);
    TEST_ASSERT_EQUAL_UINT(mock_mb_device_calls.resp_len, resp_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(mock_mb_device_calls.resp, resp, resp_len);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "the accumulator is cleared after the answer");
}

/* The same frame split across chunks: nothing is relayed while it is incomplete, and
 * the responder sees the reassembled frame exactly once. */
void test_own_frame_split_across_chunks(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A frame split across chunks is reassembled");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);

    /* One byte at a time — the address alone, then the function code, then the rest. */
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_HELD, feed(req, 1u), "the address alone is not a frame");
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_HELD, feed(req + 1, 1u), "the FC alone is not a frame");
    for (size_t i = 2; i < len - 1u; i++) {
        now_us += 1000;   /* a normal inter-chunk gap */
        TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_HELD, feed(req + i, 1u), "an incomplete frame is held");
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mb_device_calls.called,
            "the responder must not see a partial frame");
    }

    now_us += 1000;
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RESPOND, feed(req + len - 1u, 1u),
        "the last byte completes the frame");
    TEST_ASSERT_EQUAL_INT(1, mock_mb_device_calls.called);
    TEST_ASSERT_EQUAL_UINT(len, mock_mb_device_calls.last_req_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, mock_mb_device_calls.last_req, len);
}

/* An FC16 write, whose length can only be known once the byte count has arrived. */
void test_own_write_frame_variable_length(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "An FC16 frame is sized from its byte count");
    LOG_MESSAGE();

    uint8_t req[64];
    size_t len = make_write_req(req, MB_DEVICE_UNIT_ID, 541u, 11u);

    /* Stop one byte short of the byte-count field: the length is not knowable yet. */
    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(req, 6u));
    TEST_ASSERT_EQUAL_INT(0, mock_mb_device_calls.called);
    /* Now the byte count, and everything but the final byte. */
    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(req + 6, len - 7u));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mb_device_calls.called,
        "a write frame one byte short must still be held");
    TEST_ASSERT_EQUAL(MB_SELF_RTU_RESPOND, feed(req + len - 1u, 1u));
    TEST_ASSERT_EQUAL_UINT(len, mock_mb_device_calls.last_req_len);
}

/* A frame for us whose CRC is wrong is dropped, not relayed: those bytes were
 * addressed to us, and putting them on the peer's line would be worse than losing them. */
void test_bad_crc_dropped_not_relayed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A CRC failure drops the frame silently");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);
    req[len - 1u] ^= 0xFFu;   /* corrupt the CRC */

    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_HELD, feed(req, len),
        "a bad CRC must be dropped, never relayed");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, resp_len, "nothing is answered");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mb_device_calls.called, "the responder is not consulted");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "the accumulator is cleared");

    /* The responder is usable again straight away. */
    len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);
    TEST_ASSERT_EQUAL(MB_SELF_RTU_RESPOND, feed(req, len));
}

/* A function code nothing can size is dropped rather than held forever. */
void test_unknown_function_code_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "An unsizeable function code is dropped");
    LOG_MESSAGE();

    const uint8_t frame[] = {MB_DEVICE_UNIT_ID, 0x2Bu, 0x0Eu, 0x01u};   /* FC43, no length rule */

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_INT(0, mock_mb_device_calls.called);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "an unsizeable frame must not wedge the accumulator");
}

/* A truncated frame followed, much later, by a fresh one: the stale bytes must not
 * swallow the new frame. Without the gap reset, one truncated request would wedge the
 * responder for good. */
void test_stale_accumulator_reset_by_gap(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A gap drops the frame in progress");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(req, 4u));   /* half a frame, then silence */
    TEST_ASSERT_EQUAL_UINT(4u, st.len);

    now_us += MB_SELF_RTU_GAP_US + 1;
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RESPOND, feed(req, len),
        "after the gap the new frame must start from scratch");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(len, mock_mb_device_calls.last_req_len,
        "the stale bytes must not be prepended to the new frame");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(req, mock_mb_device_calls.last_req, len);
}

/* After the same gap, a frame for ANOTHER unit goes back to being relayed — the
 * accumulator must not hold it just because a stale frame was in progress. */
void test_stale_accumulator_gap_then_foreign_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A gap restores relaying for foreign traffic");
    LOG_MESSAGE();

    uint8_t own[16];
    uint8_t foreign[16];
    make_read_req(own, MB_DEVICE_UNIT_ID, 535u, 6u);
    size_t foreign_len = make_read_req(foreign, 0x01u, 100u, 4u);

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(own, 4u));

    now_us += MB_SELF_RTU_GAP_US + 1;
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RELAY, feed(foreign, foreign_len),
        "a foreign frame after the gap must be relayed");
    TEST_ASSERT_EQUAL_UINT(0u, st.len);
}

/* What repeater.c's drop handler does when the port reports lost bytes: the frame in progress
 * cannot complete any more, and the stream that continues after the lost bytes is offset against
 * frame boundaries, so the accumulator has to be abandoned on the spot. Left standing, it would
 * keep swallowing whatever arrives next — including a relayed Airzone request — until the CRC
 * check and the gap recovered on their own. */
void test_reset_clears_a_partially_accumulated_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A reset abandons the frame in progress");
    LOG_MESSAGE();

    uint8_t own[16];
    uint8_t foreign[16];
    size_t own_len = make_read_req(own, MB_DEVICE_UNIT_ID, 535u, 6u);
    size_t foreign_len = make_read_req(foreign, 0x01u, 100u, 4u);

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(own, 4u));   /* half a frame, then bytes are lost */
    TEST_ASSERT_EQUAL_UINT(4u, st.len);

    mb_self_rtu_reset(&st);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "the reset must leave no frame in progress");

    /* Same microsecond as the lost bytes: the gap cannot be what saves this one. */
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RELAY, feed(foreign, foreign_len),
        "a foreign frame arriving after the reset must be relayed, not swallowed");
    TEST_ASSERT_EQUAL_UINT(0u, st.len);

    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RESPOND, feed(own, own_len),
        "an own frame after the reset must be judged from scratch");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(own_len, mock_mb_device_calls.last_req_len,
        "the abandoned bytes must not be prepended to the new frame");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(own, mock_mb_device_calls.last_req, own_len);
}

/* A gap SHORTER than the threshold leaves the frame in progress alone. */
void test_short_gap_keeps_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A short pause does not drop the frame");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(req, 4u));
    now_us += MB_SELF_RTU_GAP_US - 1;
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_RESPOND, feed(req + 4, len - 4u),
        "a pause below the threshold must not break a frame in two");
    TEST_ASSERT_EQUAL_UINT(len, mock_mb_device_calls.last_req_len);
}

/* More bytes than the accumulator can hold are dropped, never relayed: they were
 * addressed to us, so the peer's line must not see them. */
void test_overflow_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "An overflowing frame is dropped, not relayed");
    LOG_MESSAGE();

    uint8_t chunk[MODBUS_RTU_MAX_FRAME_LEN];
    memset(chunk, 0x00, sizeof(chunk));
    chunk[0] = MB_DEVICE_UNIT_ID;
    chunk[1] = 0x10u;   /* FC16: the length comes from the byte count */
    chunk[6] = 246u;    /* 246 data bytes => 255 total, just inside the buffer */

    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_HELD, feed(chunk, 200u), "255 bytes are expected");
    TEST_ASSERT_EQUAL_UINT(200u, st.len);

    /* 200 + 100 is past the end of the accumulator. */
    TEST_ASSERT_EQUAL_MESSAGE(MB_SELF_RTU_HELD, feed(chunk, 100u),
        "overflowing bytes addressed to us must never be relayed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_mb_device_calls.called, "nothing is answered");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "the accumulator is cleared");
}

/* A frame that claims to be longer than a Modbus RTU frame can be cannot be sized, so
 * it is dropped rather than held until the gap timer notices. */
void test_frame_longer_than_buffer_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A frame claiming more than 256 bytes is dropped");
    LOG_MESSAGE();

    uint8_t chunk[16];
    memset(chunk, 0x00, sizeof(chunk));
    chunk[0] = MB_DEVICE_UNIT_ID;
    chunk[1] = 0x10u;
    chunk[6] = 0xFAu;   /* 250 data bytes => 259 total, more than any RTU frame */

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(chunk, sizeof(chunk)));
    TEST_ASSERT_EQUAL_INT(0, mock_mb_device_calls.called);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, st.len, "an unsizeable frame must not wedge the accumulator");
}

/* A chunk carrying the frame plus trailing bytes: the responder gets the frame, and the
 * remainder is discarded with the accumulator. The peer is a Modbus master and never
 * pipelines, so those bytes cannot be a second request. */
void test_trailing_bytes_discarded(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Bytes past the end of the frame are discarded");
    LOG_MESSAGE();

    uint8_t req[24];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);
    req[len]     = 0xAAu;
    req[len + 1] = 0xBBu;

    TEST_ASSERT_EQUAL(MB_SELF_RTU_RESPOND, feed(req, len + 2u));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(len, mock_mb_device_calls.last_req_len,
        "the responder is handed the frame, not the trailing bytes");
    TEST_ASSERT_EQUAL_UINT(0u, st.len);
}

/* When the responder declines the frame there is nothing to send and nothing to relay. */
void test_responder_declines(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "A declined frame produces no response");
    LOG_MESSAGE();

    uint8_t req[16];
    size_t len = make_read_req(req, MB_DEVICE_UNIT_ID, 535u, 6u);
    mock_mb_device_calls.resp_len = 0;

    TEST_ASSERT_EQUAL(MB_SELF_RTU_HELD, feed(req, len));
    TEST_ASSERT_EQUAL_INT(1, mock_mb_device_calls.called);
    TEST_ASSERT_EQUAL_UINT(0u, resp_len);
    TEST_ASSERT_EQUAL_UINT(0u, st.len);
}

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_foreign_frame_relayed_untouched);
    RUN_TEST(test_own_frame_single_chunk);
    RUN_TEST(test_own_frame_split_across_chunks);
    RUN_TEST(test_own_write_frame_variable_length);
    RUN_TEST(test_bad_crc_dropped_not_relayed);
    RUN_TEST(test_unknown_function_code_dropped);
    RUN_TEST(test_stale_accumulator_reset_by_gap);
    RUN_TEST(test_stale_accumulator_gap_then_foreign_frame);
    RUN_TEST(test_reset_clears_a_partially_accumulated_frame);
    RUN_TEST(test_short_gap_keeps_frame);
    RUN_TEST(test_overflow_dropped);
    RUN_TEST(test_frame_longer_than_buffer_dropped);
    RUN_TEST(test_trailing_bytes_discarded);
    RUN_TEST(test_responder_declines);

    return UNITY_END();
}
