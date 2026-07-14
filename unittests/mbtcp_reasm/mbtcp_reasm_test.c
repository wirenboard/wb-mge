#include "unity.h"
#include "console_log.h"

#include "bridge/mbtcp_reasm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The single home of the Modbus TCP stream-reassembly tests.
 *
 * The reassembler used to exist as two near-identical private copies — one in
 * modbus_tcp.c, one in cache_modbus_server.c — which had already drifted apart
 * (only one rejected sock < 0, only one checked protocol_id, they resynced
 * differently on a bad header, only one counted slot exhaustion). Both now use
 * bridge/mbtcp_reasm, and this suite is where its behaviour is pinned; the two
 * consumers' suites only test what they do with the frames that come out. */

/* ---- Reassembler under test + captured frames ---------------------------- */

static mbtcp_reasm_t s_reasm;

#define MAX_CAPTURED       16
#define MAX_CAPTURED_LEN  300

typedef struct {
    uint8_t data[MAX_CAPTURED_LEN];
    size_t  len;
    int     sock;
} captured_frame_t;

static captured_frame_t s_captured[MAX_CAPTURED];
static int              s_captured_count;
static bool             s_cb_accepts;      /* what the callback returns */

static bool capture_cb(void *user_ctx, int sock, const uint8_t *frame, size_t len)
{
    (void)user_ctx;
    if (s_captured_count < MAX_CAPTURED && len <= MAX_CAPTURED_LEN) {
        memcpy(s_captured[s_captured_count].data, frame, len);
        s_captured[s_captured_count].len  = len;
        s_captured[s_captured_count].sock = sock;
        s_captured_count++;
    }
    return s_cb_accepts;
}

/* Feed helper: always uses capture_cb. */
static int feed(int sock, const uint8_t *data, size_t len)
{
    return mbtcp_reasm_feed(&s_reasm, sock, data, len, capture_cb, NULL);
}

void setUp(void)
{
    /* No mutex: the host harness is single-threaded and mbtcp_reasm skips
     * locking when mutex == NULL. */
    memset(&s_reasm, 0, sizeof(s_reasm));
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        s_reasm.slots[i].sock = -1;
        s_reasm.slots[i].len  = 0;
    }
    s_reasm.mutex = NULL;
    s_reasm.tag   = "test";

    memset(s_captured, 0, sizeof(s_captured));
    s_captured_count = 0;
    s_cb_accepts     = true;
}

void tearDown(void) {}

/* ---- Helper: a 12-byte Modbus TCP FC03 request --------------------------- */
static void build_fc03_request(uint8_t *buf, uint16_t txid, uint8_t unit_id,
                               uint16_t start, uint16_t count)
{
    buf[0]  = (uint8_t)(txid >> 8);
    buf[1]  = (uint8_t)(txid & 0xFF);
    buf[2]  = 0x00;  /* protocol_id hi */
    buf[3]  = 0x00;  /* protocol_id lo */
    buf[4]  = 0x00;  /* length hi */
    buf[5]  = 0x06;  /* length lo: unit_id(1)+FC(1)+start(2)+count(2) = 6 */
    buf[6]  = unit_id;
    buf[7]  = 0x03;  /* FC03 */
    buf[8]  = (uint8_t)(start >> 8);
    buf[9]  = (uint8_t)(start & 0xFF);
    buf[10] = (uint8_t)(count >> 8);
    buf[11] = (uint8_t)(count & 0xFF);
}

/* Claim a slot for sock without producing a frame. */
static int occupy(int sock)
{
    const uint8_t stub = 0x00;
    return feed(sock, &stub, 1);
}

/* ======================================================================= *
 *  Slot table
 * ======================================================================= */

/* RSM-U-001: feeding an unknown socket allocates a slot for it. */
void test_slot_allocated_on_first_feed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-001: first feed allocates a slot");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 42),
        "no slot may exist before the first feed");

    TEST_ASSERT_NOT_EQUAL_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(42),
        "the first feed on a free table must get a slot");
    TEST_ASSERT_TRUE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 42),
        "a slot must exist for the socket after feeding it");
}

/* RSM-U-002: a second feed on the same socket reuses its slot, it does not
 * allocate a new one (which would leak slots and lose the partial frame). */
void test_slot_reused_for_same_socket(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-002: repeated feeds reuse one slot");
    LOG_MESSAGE();

    occupy(55);
    occupy(55);
    occupy(55);

    /* Exactly one slot may be taken, and the bytes must have accumulated in it. */
    int taken = 0;
    for (int s = 0; s < 128; s++) {
        if (mbtcp_reasm_has_slot(&s_reasm, s)) taken++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, taken, "exactly one slot may be in use");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(3u, mbtcp_reasm_pending(&s_reasm, 55),
        "the three fed bytes must have accumulated in the one slot");
}

/* RSM-U-003: with all MBTCP_REASM_MAX_CONNS slots taken, one more socket gets
 * MBTCP_REASM_NO_SLOT and the exhaustion counter records it. */
void test_slot_table_full(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-003: full table -> NO_SLOT + exhaustion counted");
    LOG_MESSAGE();

    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(i),
            "the first MBTCP_REASM_MAX_CONNS sockets must each get a slot");
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, mbtcp_reasm_slot_exhausted(&s_reasm),
        "no exhaustion while slots were still available");

    TEST_ASSERT_EQUAL_INT_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(MBTCP_REASM_MAX_CONNS),
        "one socket too many must get NO_SLOT");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, mbtcp_reasm_slot_exhausted(&s_reasm),
        "slot exhaustion must be counted, not silent");

    /* Counted per feed, not per connection. */
    occupy(MBTCP_REASM_MAX_CONNS);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, mbtcp_reasm_slot_exhausted(&s_reasm),
        "each rejected feed bumps the counter");
}

/* RSM-U-004: a full-table probe for an absent socket must not scan past the last
 * slot. The mutant (i <= MAX_CONNS) reads slots[8] — past the array — and could
 * spuriously match. */
void test_full_table_probe_no_overrun(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-004: full-table probe must not scan past slot 7");
    LOG_MESSAGE();

    /* Fill every slot with a non-zero socket, so none is 0 and none is free. */
    for (int i = 1; i <= MBTCP_REASM_MAX_CONNS; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(i),
            "the first MBTCP_REASM_MAX_CONNS allocations must succeed");
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(0),
        "probing socket 0 on a full table must fail (no scan past the last slot)");
    TEST_ASSERT_FALSE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 0),
        "no slot for socket 0 may exist after the rejected probe");
}

/* RSM-U-005: close() releases the slot and discards its partial frame — a reused
 * file descriptor must never inherit the previous connection's bytes. */
void test_close_releases_slot_and_discards_partial(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-005: close frees the slot and drops its partial frame");
    LOG_MESSAGE();

    uint8_t frame[12];
    build_fc03_request(frame, 0x0001, 1, 0, 1);

    /* Half a frame arrives, then the client disconnects. */
    feed(77, frame, 6);
    TEST_ASSERT_TRUE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 77), "slot allocated");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_pending(&s_reasm, 77), "6 bytes buffered");

    mbtcp_reasm_close(&s_reasm, 77);
    TEST_ASSERT_FALSE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 77), "close must release the slot");

    /* The fd is reused by a new client, whose stream starts with the SECOND half
     * of the old frame. If the partial had survived, those bytes would splice
     * into a bogus frame. Nothing may be emitted. */
    feed(77, frame + 6, 6);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_captured_count,
        "a reused fd must not inherit the closed connection's partial frame");
}

/* RSM-U-006: closing a socket that holds no slot is a no-op, not a crash. */
void test_close_unknown_socket_is_noop(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-006: closing an unknown socket is a no-op");
    LOG_MESSAGE();

    mbtcp_reasm_close(&s_reasm, 99);   /* must not crash */
    TEST_ASSERT_FALSE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 99), "still no slot");

    /* And it must not have clobbered a real one. */
    occupy(5);
    mbtcp_reasm_close(&s_reasm, 99);
    TEST_ASSERT_TRUE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, 5),
        "closing an unknown socket must not disturb other slots");
}

/* RSM-U-007: -1 is the free-slot sentinel. A negative descriptor must never be
 * allowed to match or claim a slot — in the cache server's old private copy it
 * could, aliasing every free slot. */
void test_negative_socket_rejected(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-007: a negative socket must never get a slot");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL_INT_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(-1),
        "feed(-1) must be refused even with the table empty");
    TEST_ASSERT_FALSE_MESSAGE(mbtcp_reasm_has_slot(&s_reasm, -1),
        "has_slot(-1) must be false — it must not match the free-slot sentinel");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, -1),
        "pending(-1) must be 0");

    /* The refusal is not slot exhaustion. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, mbtcp_reasm_slot_exhausted(&s_reasm),
        "an invalid socket is not a slot-exhaustion event");

    /* All slots must still be free. */
    for (int i = 0; i < MBTCP_REASM_MAX_CONNS; i++) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(MBTCP_REASM_NO_SLOT, occupy(i),
            "every slot must still be free after the rejected negative socket");
    }
}

/* ======================================================================= *
 *  MBAP length parsing
 * ======================================================================= */

/* RSM-U-008: the MBAP length field counts from unit_id, so the ADU length is
 * length + 6. */
void test_frame_total_len_parsing(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-008: frame_total_len = MBAP length + 6");
    LOG_MESSAGE();

    uint8_t buf[6] = {0};

    buf[4] = 0x00; buf[5] = 0x06;   /* a normal request: 6 + 6 = 12 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(12u, mbtcp_reasm_frame_total_len(buf),
        "MBAP length 6 -> 12-byte ADU");

    buf[4] = 0x00; buf[5] = 0xFE;   /* the largest legal ADU: 254 + 6 = 260 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(260u, mbtcp_reasm_frame_total_len(buf),
        "MBAP length 254 -> 260-byte ADU (the Modbus TCP maximum)");

    buf[4] = 0x00; buf[5] = 0x00;   /* degenerate: 0 + 6 = 6 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_frame_total_len(buf),
        "MBAP length 0 -> 6 (below the header size; rejected as a bad header)");

    buf[4] = 0x01; buf[5] = 0x02;   /* big-endian: 0x0102 = 258, + 6 = 264 */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(264u, mbtcp_reasm_frame_total_len(buf),
        "the length field must be read big-endian");
}

/* RSM-U-009: the accept/reject boundary at MBTCP_REASM_FRAME_MAX. A frame of
 * exactly FRAME_MAX is accepted; one byte more is a bad header. */
void test_frame_len_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-009: FRAME_MAX is inclusive, FRAME_MAX+1 is a bad header");
    LOG_MESSAGE();

    /* MBAP length that yields exactly MBTCP_REASM_FRAME_MAX (300): 300 - 6 = 294 */
    uint8_t exact[MBTCP_REASM_FRAME_MAX];
    memset(exact, 0, sizeof(exact));
    exact[2] = 0x00; exact[3] = 0x00;                 /* protocol id 0 */
    exact[4] = (uint8_t)((294u >> 8) & 0xFF);
    exact[5] = (uint8_t)(294u & 0xFF);
    TEST_ASSERT_EQUAL_size_t_MESSAGE((size_t)MBTCP_REASM_FRAME_MAX,
        mbtcp_reasm_frame_total_len(exact), "the vector must declare exactly FRAME_MAX");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(10, exact, sizeof(exact)),
        "a frame of exactly FRAME_MAX bytes must be dispatched");
    TEST_ASSERT_EQUAL_size_t_MESSAGE((size_t)MBTCP_REASM_FRAME_MAX, s_captured[0].len,
        "the dispatched frame must be the whole FRAME_MAX bytes");

    /* One byte over: 301 - 6 = 295 -> a bad header, nothing dispatched. */
    setUp();
    uint8_t over[MBTCP_REASM_FRAME_MAX];
    memset(over, 0, sizeof(over));
    over[4] = (uint8_t)((295u >> 8) & 0xFF);
    over[5] = (uint8_t)(295u & 0xFF);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(11, over, sizeof(over)),
        "a frame declaring FRAME_MAX+1 must not be dispatched");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_captured_count, "nothing may be dispatched");
}

/* ======================================================================= *
 *  Framing
 * ======================================================================= */

/* RSM-U-010: one complete frame in one recv. */
void test_single_complete_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-010: a whole frame in one feed is dispatched");
    LOG_MESSAGE();

    uint8_t buf[12];
    build_fc03_request(buf, 0x1234, 7, 100, 2);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(20, buf, sizeof(buf)), "one frame must be dispatched");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_captured_count, "callback called once");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(12u, s_captured[0].len, "frame length 12");
    TEST_ASSERT_EQUAL_INT_MESSAGE(20, s_captured[0].sock, "frame must carry its socket");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buf, s_captured[0].data, 12, "frame bytes must be intact");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 20), "nothing left buffered");
}

/* RSM-U-011: a frame split across two recvs is reassembled. */
void test_split_frame_reassembled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-011: a frame split across two feeds is reassembled");
    LOG_MESSAGE();

    uint8_t buf[12];
    build_fc03_request(buf, 0x2222, 3, 0, 1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(21, buf, 6), "half a frame yields nothing yet");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_pending(&s_reasm, 21),
        "the first half must be buffered");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(21, buf + 6, 6), "the second half completes the frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(12u, s_captured[0].len, "the reassembled frame is 12 bytes");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buf, s_captured[0].data, 12,
        "the reassembled frame must equal the original");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 21), "nothing left buffered");
}

/* RSM-U-012: two frames coalesced into one recv are both dispatched. */
void test_coalesced_frames(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-012: two coalesced frames are both dispatched");
    LOG_MESSAGE();

    uint8_t buf[24];
    build_fc03_request(buf, 0x0001, 1, 0, 1);
    build_fc03_request(buf + 12, 0x0002, 2, 10, 5);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, feed(22, buf, sizeof(buf)), "both frames must be dispatched");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_captured_count, "callback called twice");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buf, s_captured[0].data, 12, "first frame");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(buf + 12, s_captured[1].data, 12, "second frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 22), "nothing left buffered");
}

/* RSM-U-013: one and a half frames — the whole one is dispatched, the tail waits. */
void test_one_and_a_half_frames(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-013: 1.5 frames -> 1 dispatched, tail carried over");
    LOG_MESSAGE();

    uint8_t second[12];
    build_fc03_request(second, 0x0002, 2, 0, 1);

    uint8_t buf[18];
    build_fc03_request(buf, 0x0001, 1, 0, 1);
    memcpy(buf + 12, second, 6);   /* only the first half of the second frame is sent */

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(23, buf, sizeof(buf)),
        "only the complete frame is dispatched");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_pending(&s_reasm, 23),
        "the half frame must be carried over");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(23, second + 6, 6), "the tail completes the second frame");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_captured_count, "both frames dispatched in the end");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 23), "nothing left buffered");
}

/* RSM-U-014: sockets are independent — one connection's partial frame must never
 * splice into another's stream. */
void test_sockets_are_independent(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-014: each socket buffers independently");
    LOG_MESSAGE();

    uint8_t a[12], b[12];
    build_fc03_request(a, 0x00AA, 1, 0, 1);
    build_fc03_request(b, 0x00BB, 2, 0, 1);

    feed(80, a, 6);          /* half of A */
    feed(81, b, 6);          /* half of B */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_captured_count, "no frame from two halves");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_pending(&s_reasm, 80), "A half buffered");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_pending(&s_reasm, 81), "B half buffered");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(80, a + 6, 6), "A completes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(80, s_captured[0].sock, "A's frame carries A's socket");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a, s_captured[0].data, 12, "A's frame is A's bytes");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, mbtcp_reasm_pending(&s_reasm, 81),
        "B's buffer must be untouched by A completing");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(81, b + 6, 6), "B completes");
    TEST_ASSERT_EQUAL_INT_MESSAGE(81, s_captured[1].sock, "B's frame carries B's socket");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(b, s_captured[1].data, 12, "B's frame is B's bytes");
}

/* RSM-U-015: a frame of exactly the header size (MBAP length = 2) is dispatched.
 * The loop guard is `>=` sizeof(header), not `>`. */
void test_exact_header_size_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-015: an 8-byte frame (== header size) is dispatched");
    LOG_MESSAGE();

    uint8_t buf[8];
    buf[0] = 0x00; buf[1] = 0x05;   /* transaction id */
    buf[2] = 0x00; buf[3] = 0x00;   /* protocol id 0 */
    buf[4] = 0x00; buf[5] = 0x02;   /* MBAP length 2 -> ADU = 8 */
    buf[6] = 0x01;                   /* unit id */
    buf[7] = 0x03;                   /* FC03 */

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(24, buf, sizeof(buf)),
        "a frame of exactly the header size must be dispatched");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8u, s_captured[0].len, "the frame is 8 bytes");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 24), "nothing left buffered");
}

/* RSM-U-016: a frame the callback REJECTS is still consumed — the stream must
 * advance past it, or the reassembler would re-offer it forever. */
void test_rejected_frame_is_still_consumed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-016: a frame the callback rejects still advances the stream");
    LOG_MESSAGE();

    s_cb_accepts = false;   /* e.g. the consumer's queue is full */

    uint8_t buf[12];
    build_fc03_request(buf, 0x0001, 1, 0, 1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(25, buf, sizeof(buf)),
        "a rejected frame must not be counted as accepted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_captured_count, "the callback still saw the frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 25),
        "the rejected frame must be consumed, not left in the buffer");
}

/* ======================================================================= *
 *  Bad headers and resync
 * ======================================================================= */

/* RSM-U-017: a non-zero protocol id is not a frame header. Only one of the two
 * old private copies checked this. */
void test_nonzero_protocol_id_is_not_a_header(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-017: protocol_id != 0 is not a frame header");
    LOG_MESSAGE();

    uint8_t buf[12];
    build_fc03_request(buf, 0x0003, 1, 0, 1);
    buf[2] = 0xFF; buf[3] = 0xFF;   /* protocol id 0xFFFF */

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(30, buf, sizeof(buf)),
        "a frame with a non-zero protocol id must never be dispatched");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_captured_count, "the callback must not see it");
}

/* RSM-U-018: THE reason the resync is byte-wise. A bad header must not take a
 * valid frame coalesced behind it down with it — which is exactly what the
 * gateway's old `pos = c->len` did. */
void test_valid_frame_behind_garbage_survives(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "RSM-U-018: garbage + a valid frame -> the valid frame is still recovered");
    LOG_MESSAGE();

    uint8_t buf[4 + 12];
    buf[0] = 0xDE; buf[1] = 0xAD; buf[2] = 0xBE; buf[3] = 0xEF;   /* protocol id != 0 */
    build_fc03_request(&buf[4], 0x0031, 1, 0, 1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(31, buf, sizeof(buf)),
        "the valid frame behind the garbage must be recovered");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&buf[4], s_captured[0].data, 12,
        "the recovered frame must be the valid one, byte for byte");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, mbtcp_reasm_pending(&s_reasm, 31),
        "nothing left buffered after the frame is consumed");
}

/* RSM-U-019: an absurd length field is a bad header; the scan resyncs a byte at
 * a time and keeps only the tail too short to hold a header. */
void test_bogus_length_resyncs(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-019: a bogus length field resyncs byte-wise");
    LOG_MESSAGE();

    uint8_t buf[12];
    build_fc03_request(buf, 0x0030, 1, 0, 1);
    buf[4] = 0xFF; buf[5] = 0xFF;   /* ADU would be 65541 — far over FRAME_MAX */

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(32, buf, sizeof(buf)), "nothing may be dispatched");

    /* The scan stepped forward until fewer than sizeof(mb_tcp_header_t) bytes
     * were left: 12 - 5 = 7 bytes are held for the next recv. */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7u, mbtcp_reasm_pending(&s_reasm, 32),
        "only the sub-header tail may remain buffered");
}

/* RSM-U-020: a length field below the header size (MBAP length 0 -> ADU 6 < 8)
 * is the same bad-header branch. */
void test_length_below_header_size_resyncs(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-020: an ADU shorter than the header is a bad header");
    LOG_MESSAGE();

    uint8_t buf[12];
    build_fc03_request(buf, 0x0080, 1, 0, 1);
    buf[4] = 0x00; buf[5] = 0x00;   /* ADU = 6 < sizeof(mb_tcp_header_t) = 8 */

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(33, buf, sizeof(buf)), "nothing may be dispatched");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7u, mbtcp_reasm_pending(&s_reasm, 33),
        "only the sub-header tail may remain buffered");
}

/* RSM-U-021: a stream of pure garbage must never grow the buffer without bound —
 * once it fills, it is dropped and the connection resyncs. */
void test_buffer_full_of_garbage_is_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "RSM-U-021: a buffer full of garbage is dropped, not grown");
    LOG_MESSAGE();

    /* Bytes that can never form a header: protocol id != 0 at every offset. */
    uint8_t junk[MBTCP_REASM_FRAME_MAX];
    memset(junk, 0xFF, sizeof(junk));

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, feed(34, junk, sizeof(junk)),
            "garbage must never produce a frame");
        TEST_ASSERT_TRUE_MESSAGE(mbtcp_reasm_pending(&s_reasm, 34) < MBTCP_REASM_FRAME_MAX,
            "the buffer must never stay full — it is dropped and resynced");
    }

    /* And the connection must still work afterwards. */
    uint8_t good[12];
    build_fc03_request(good, 0x0044, 1, 0, 1);
    mbtcp_reasm_close(&s_reasm, 34);          /* the client reconnects */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, feed(34, good, sizeof(good)),
        "a good frame after a garbage flood must still be framed");
}

int main(void)
{
    UNITY_BEGIN();

    /* Slot table */
    RUN_TEST(test_slot_allocated_on_first_feed);
    RUN_TEST(test_slot_reused_for_same_socket);
    RUN_TEST(test_slot_table_full);
    RUN_TEST(test_full_table_probe_no_overrun);
    RUN_TEST(test_close_releases_slot_and_discards_partial);
    RUN_TEST(test_close_unknown_socket_is_noop);
    RUN_TEST(test_negative_socket_rejected);

    /* MBAP length parsing */
    RUN_TEST(test_frame_total_len_parsing);
    RUN_TEST(test_frame_len_boundary);

    /* Framing */
    RUN_TEST(test_single_complete_frame);
    RUN_TEST(test_split_frame_reassembled);
    RUN_TEST(test_coalesced_frames);
    RUN_TEST(test_one_and_a_half_frames);
    RUN_TEST(test_sockets_are_independent);
    RUN_TEST(test_exact_header_size_frame);
    RUN_TEST(test_rejected_frame_is_still_consumed);

    /* Bad headers and resync */
    RUN_TEST(test_nonzero_protocol_id_is_not_a_header);
    RUN_TEST(test_valid_frame_behind_garbage_survives);
    RUN_TEST(test_bogus_length_resyncs);
    RUN_TEST(test_length_below_header_size_resyncs);
    RUN_TEST(test_buffer_full_of_garbage_is_dropped);

    return UNITY_END();
}
