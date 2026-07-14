#include "unity.h"
#include "console_log.h"

#include "modbus_tcp_internal.h"
#include "mb_device.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ---- Mock state from mocks/tcp_server.c (R2) ----------------------------- */
extern uint8_t   mock_tcp_send_buf[];
extern size_t    mock_tcp_send_len;
extern int       mock_tcp_send_sock;
extern esp_err_t mock_tcp_send_result;
extern bool      mock_tcp_send_overflow;
void mock_tcp_server_reset(void);

/* ---- Mock state from mocks/packet_queue.c ------------------------------- */

#define MOCK_PQ_MAX_PACKETS 64
#define MOCK_PQ_MAX_LEN     300

typedef struct {
    uint8_t data[MOCK_PQ_MAX_LEN];
    size_t  len;
    int     sock;
} mock_pq_entry_t;

extern mock_pq_entry_t mock_pq_packets[];
extern int             mock_pq_push_count;
extern esp_err_t       mock_pq_push_result;
void mock_packet_queue_reset(void);

/* ---- Mock state from mocks/mb_device.c ---------------------------------- */
extern int     mock_mb_device_handle_self_count;
extern uint8_t mock_mb_device_handle_self_unit;
void mock_mb_device_reset(void);

/* ---- Mock state from mocks/serial.c ------------------------------------- */
extern int mock_serial_send_count;
void mock_serial_reset(void);

/* ---- Context index used by all tests ------------------------------------ */
#define TEST_CTX_IDX 0

/* A static tcp_desc used so on_tcp_conn_close can find the ctx */
static tcp_desc_t s_test_tcp_desc;

/* ---- Reassembly state, via the real mbtcp_reasm API ---------------------- */
/* Stream reassembly lives in bridge/mbtcp_reasm and is covered end-to-end by
 * unittests/mbtcp_reasm. The tests here are about what modbus_tcp does with the
 * frames that come OUT of it (queueing, fallback, close handling), so they only
 * need to see the port's reassembler — hence one accessor, not a shim per call. */

static bool discard_frame_cb(void *user_ctx, int sock, const uint8_t *frame, size_t len)
{
    (void)user_ctx; (void)sock; (void)frame; (void)len;
    return false;
}

static int reasm_has_slot(int sock)
{
    return mbtcp_reasm_has_slot(modbus_tcp_test_get_reasm(TEST_CTX_IDX), sock) ? 1 : 0;
}

static size_t reasm_pending_bytes(int sock)
{
    return mbtcp_reasm_pending(modbus_tcp_test_get_reasm(TEST_CTX_IDX), sock);
}

/* Claim a reassembly slot for sock. Feeding one byte allocates the slot and
 * yields no frame — enough to fill the table and drive the exhaustion fallback. */
static int reasm_occupy(int sock)
{
    const uint8_t stub = 0x00;
    int rc = mbtcp_reasm_feed(modbus_tcp_test_get_reasm(TEST_CTX_IDX), sock,
                              &stub, 1, discard_frame_cb, NULL);
    return (rc != MBTCP_REASM_NO_SLOT) ? 1 : 0;
}

/* ---- setUp / tearDown --------------------------------------------------- */
void setUp(void)
{
    mock_packet_queue_reset();
    mock_tcp_server_reset();
    mock_mb_device_reset();
    mock_serial_reset();
    modbus_tcp_test_init_ctx(TEST_CTX_IDX, (packet_queue_handle)1, &s_test_tcp_desc);
}

void tearDown(void) {}

/* ---- Helper: build a 12-byte Modbus TCP FC03 request ------------------- */
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

/* ---- MBTCP-U-007: single complete frame pushed to queue ----------------- */
void test_push_single_complete_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-007: single complete frame -> pushed to queue");
    LOG_MESSAGE();

    uint8_t req[12];
    build_fc03_request(req, 0x0001, 1, 100, 5);

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 10, req, sizeof(req));

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed, "one complete frame must be pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "packet_queue_push_with_client must be called once");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(12u, mock_pq_packets[0].len, "pushed frame length must be 12");
    TEST_ASSERT_EQUAL_INT_MESSAGE(10, mock_pq_packets[0].sock, "pushed frame must carry client sock 10");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(req, mock_pq_packets[0].data, 12u, "pushed frame data must match input");
}

/* ---- MBTCP-U-008: split frame — two calls ------------------------------- */
void test_push_split_frame_two_calls(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-008: split frame (two halves) -> accumulated, pushed on second call");
    LOG_MESSAGE();

    uint8_t req[12];
    build_fc03_request(req, 0x0002, 1, 0, 1);

    /* First 6 bytes: partial MBAP header only (full header is 8 bytes), frame not yet complete */
    unsigned pushed1 = modbus_tcp_test_push_data(TEST_CTX_IDX, 20, req, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed1, "first half: no frames pushed yet");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count, "no push call after first half");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, reasm_pending_bytes(20),
        "6 bytes must be pending after first half");

    /* Remaining 6 bytes: frame completes */
    unsigned pushed2 = modbus_tcp_test_push_data(TEST_CTX_IDX, 20, req + 6, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed2, "second half: frame must be pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "push called once after second half");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, reasm_pending_bytes(20),
        "no pending bytes after frame dispatched");
}

/* ---- MBTCP-U-009: two coalesced frames ---------------------------------- */
void test_push_two_coalesced_frames(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-009: two coalesced frames in one recv -> both pushed");
    LOG_MESSAGE();

    uint8_t buf[24];
    build_fc03_request(buf,      0x0010, 1, 0, 1);
    build_fc03_request(buf + 12, 0x0011, 2, 1, 1);

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 30, buf, 24);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(2u, pushed, "two frames must be pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_pq_push_count, "push must be called twice");

    /* Verify both frames have correct transaction IDs */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_pq_packets[0].data[0], "frame1 txid hi = 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x10u, mock_pq_packets[0].data[1], "frame1 txid lo = 0x10");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_pq_packets[1].data[0], "frame2 txid hi = 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x11u, mock_pq_packets[1].data[1], "frame2 txid lo = 0x11");
}

/* ---- MBTCP-U-010: 1.5 frames — first pushed, tail carried over ---------- */
void test_push_one_and_half_frames(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-010: 1.5 frames -> first pushed, tail accumulated");
    LOG_MESSAGE();

    uint8_t buf[24];
    build_fc03_request(buf,      0x0020, 1, 0, 1);  /* frame 1 */
    build_fc03_request(buf + 12, 0x0021, 1, 1, 1);  /* frame 2 */

    /* Send frame1(12) + first half of frame2(6) = 18 bytes */
    unsigned pushed1 = modbus_tcp_test_push_data(TEST_CTX_IDX, 40, buf, 18);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed1, "first 18 bytes: only frame1 pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "one push call after 18 bytes");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, reasm_pending_bytes(40),
        "6 bytes (half of frame2) must be pending");

    /* Send remaining 6 bytes of frame2 */
    mock_packet_queue_reset();
    unsigned pushed2 = modbus_tcp_test_push_data(TEST_CTX_IDX, 40, buf + 18, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed2, "frame2 must be pushed on second recv");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "one more push call for frame2");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, reasm_pending_bytes(40),
        "no pending bytes after frame2 dispatched");
}

/* ---- MBTCP-U-011: bogus length field -> byte-wise resync, nothing pushed -- */
/* The reassembler resyncs ONE BYTE at a time on a bad header rather than
 * discarding the buffer (see mbtcp_reasm). Nothing may be pushed; the bytes that
 * are still too few to hold a header stay buffered, because a real frame could
 * begin inside them and continue in the next recv(). MBTCP-U-011b proves that. */
void test_push_bogus_length_drops(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-011: bogus MBAP length -> resync, no push");
    LOG_MESSAGE();

    /* Build packet with length = 0xFFFF (total = 65535 + 6 = 65541 > 300) */
    uint8_t buf[12];
    build_fc03_request(buf, 0x0030, 1, 0, 1);
    buf[4] = 0xFF;
    buf[5] = 0xFF;

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 50, buf, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed, "bogus length: no frames pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count, "no push call for bogus frame");

    /* The scan advanced a byte at a time until fewer than a header's worth of
     * bytes remained: 12 - 5 = 7 unscannable bytes are held for the next recv. */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7u, reasm_pending_bytes(50),
        "resync must retain the sub-header tail, not discard the buffer");
}

/* ---- MBTCP-U-011b: a valid frame behind a bogus one must survive ---------- */
/* This is why the resync is byte-wise. The old gateway did `pos = c->len` on a
 * bad header, throwing away the ENTIRE buffer — including a perfectly good frame
 * that happened to be coalesced behind the bad one in the same recv(). */
void test_push_valid_frame_behind_bogus_header_survives(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-011b: garbage followed by a valid frame -> the valid frame is still pushed");
    LOG_MESSAGE();

    uint8_t buf[4 + 12];
    /* 4 bytes of garbage that cannot start a frame (protocol_id != 0) */
    buf[0] = 0xDE; buf[1] = 0xAD; buf[2] = 0xBE; buf[3] = 0xEF;
    /* ...immediately followed by a well-formed FC03 request */
    build_fc03_request(&buf[4], 0x0031, 1, 0, 1);

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 51, buf, sizeof(buf));

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed,
        "the valid frame behind the garbage must be recovered by the resync");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "exactly one frame pushed");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(12u, mock_pq_packets[0].len, "the pushed frame is the 12-byte request");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, reasm_pending_bytes(51),
        "nothing left buffered once the frame is consumed");
}

/* ---- MBTCP-U-012: queue full -> frame consumed but not counted ----------- */
void test_push_queue_full_frame_consumed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-012: queue full (push returns error) -> frame consumed, count=0");
    LOG_MESSAGE();

    mock_pq_push_result = 1; /* non-zero = ESP error (simulates queue-full rejection) */

    uint8_t req[12];
    build_fc03_request(req, 0x0040, 1, 0, 1);

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 60, req, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed, "queue full: pushed count must be 0");
    /* push_with_client WAS called (the queue rejected it) */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count,
        "push_with_client must be called even though queue is full");
    /* Buffer consumed — no pending bytes */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, reasm_pending_bytes(60),
        "frame bytes must be consumed even when queue full");
}

/* ---- MBTCP-U-013: table full -> fallback to separate_and_push_one_pass --- */
void test_push_table_full_falls_back_to_one_pass(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-013: reasm table full -> fallback to one-pass, frame pushed");
    LOG_MESSAGE();

    /* Fill all 8 slots */
    for (int i = 1; i <= 8; i++) {
        reasm_occupy(i);
    }

    /* Now push a complete frame as sock 9 (table full -> fallback) */
    uint8_t req[12];
    build_fc03_request(req, 0x0050, 1, 0, 1);

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 9, req, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed, "fallback one-pass must push one complete frame");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "push called once via one-pass fallback");
}

/* ---- MBTCP-U-014: on_tcp_conn_close -> slot freed ------------------------ */
void test_conn_close_frees_slot(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-014: on_tcp_conn_close frees the reassembly slot");
    LOG_MESSAGE();

    /* Push some data to allocate the slot for sock 70 */
    uint8_t req[12];
    build_fc03_request(req, 0x0060, 1, 0, 1);
    modbus_tcp_test_push_data(TEST_CTX_IDX, 70, req, 6);   /* half frame -> slot allocated, pending */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, reasm_has_slot(70),
        "slot must exist after partial push");

    /* Close connection */
    modbus_tcp_test_conn_close(TEST_CTX_IDX, 70);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, reasm_has_slot(70),
        "slot must be freed after on_tcp_conn_close");
}

/* ---- MBTCP-U-015: independent sockets keep separate buffers ------------- */
void test_push_independent_sockets(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-015: two sockets maintain independent reassembly buffers");
    LOG_MESSAGE();

    uint8_t buf_a[12], buf_b[12];
    build_fc03_request(buf_a, 0x0070, 1, 10, 1);
    build_fc03_request(buf_b, 0x0071, 2, 20, 1);

    /* Interleave first halves: neither dispatched */
    modbus_tcp_test_push_data(TEST_CTX_IDX, 80, buf_a, 6);  /* sock 80: first half */
    modbus_tcp_test_push_data(TEST_CTX_IDX, 81, buf_b, 6);  /* sock 81: first half */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count, "no frames yet after two first halves");

    /* Complete sock 80's frame */
    unsigned pushed_a = modbus_tcp_test_push_data(TEST_CTX_IDX, 80, buf_a + 6, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed_a, "sock 80: frame must dispatch on second half");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "sock 80: one push call");

    /* Sock 81 still has partial frame pending */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, reasm_pending_bytes(81),
        "sock 81 must still have 6 pending bytes (unaffected by sock 80)");

    mock_packet_queue_reset();

    /* Complete sock 81's frame */
    unsigned pushed_b = modbus_tcp_test_push_data(TEST_CTX_IDX, 81, buf_b + 6, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed_b, "sock 81: frame must dispatch on second half");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "sock 81: one push call");

    /* Verify sock 81's pushed frame has correct txid */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, mock_pq_packets[0].data[0], "sock 81 frame txid hi = 0x00");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x71u, mock_pq_packets[0].data[1], "sock 81 frame txid lo = 0x71");
}

/* ---- MBTCP-U-016: flen < sizeof(mb_tcp_header_t) -> bogus path ---------- */
/* MBAP length=0 gives flen=6 < 8 (sizeof header): the same bad-header branch as
 * flen > MBTCP_REASM_FRAME_MAX. Nothing is pushed and the stream resyncs a byte
 * at a time, leaving only the sub-header tail buffered. */
void test_push_bogus_length_too_small(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-016: MBAP length=0 (flen=6 < 8) -> bogus resync, no push");
    LOG_MESSAGE();

    /* Build packet with MBAP length = 0 -> flen = 0 + 6 = 6 < sizeof(mb_tcp_header_t)=8 */
    uint8_t buf[12];
    build_fc03_request(buf, 0x0080, 1, 0, 1);
    buf[4] = 0x00;
    buf[5] = 0x00;  /* MBAP length = 0, total flen = 6 */

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 90, buf, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed, "flen=6<8: no frames pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count, "no push call for too-small flen");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7u, reasm_pending_bytes(90),
        "resync must retain the sub-header tail after flen < sizeof(header)");
}

/* ---- MBTCP-U-021: one-pass fallback — declared length > actual bytes ------ */
/* In separate_and_push_one_pass(), when the declared req_len + pos > len the
 * loop breaks and the frame is skipped (not pushed, no crash). */
void test_one_pass_fallback_short_data(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-021: one-pass fallback: declared length > actual data -> 0 pushed, no crash");
    LOG_MESSAGE();

    /* Fill all 8 reassembly slots so that socket 9 falls back to one-pass. */
    for (int i = 1; i <= 8; i++) {
        reasm_occupy(i);
    }

    /* Build a frame that declares MBAP length = 6 (total = 12 bytes)
     * but supply only 8 bytes — declared length exceeds actual data. */
    uint8_t buf[8];
    buf[0] = 0x01; buf[1] = 0x00;   /* txid */
    buf[2] = 0x00; buf[3] = 0x00;   /* protocol_id = 0 */
    buf[4] = 0x00; buf[5] = 0x06;   /* MBAP length = 6 -> req_len = 12 */
    buf[6] = 0x01;                   /* unit_id */
    buf[7] = 0x03;                   /* FC03 */

    /* Push only 8 bytes for sock 9 (table is full -> one-pass fallback). */
    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 9, buf, 8);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed,
        "one-pass: declared length exceeds actual data -> 0 frames pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count,
        "no push call when declared length > actual data");
}

/* ---- MBTCP-U-022: one-pass fallback — invalid protocol ID -> dropped ------ */
/* modbus_tcp_check_request() rejects frames with protocol_id != 0x0000.
 * In the fallback path the frame must be skipped (break, not pushed). */
void test_one_pass_fallback_invalid_pid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-022: one-pass fallback: protocol_id=1 -> dropped by check_request, 0 pushed");
    LOG_MESSAGE();

    /* Fill all 8 reassembly slots so that socket 9 uses one-pass fallback. */
    for (int i = 1; i <= 8; i++) {
        reasm_occupy(i);
    }

    /* Build a complete 12-byte frame but with protocol_id = 0x0001 (invalid). */
    uint8_t buf[12];
    buf[0] = 0x00; buf[1] = 0x02;   /* txid */
    buf[2] = 0x00; buf[3] = 0x01;   /* protocol_id = 1 (invalid, must be 0) */
    buf[4] = 0x00; buf[5] = 0x06;   /* MBAP length = 6 */
    buf[6] = 0x01;                   /* unit_id */
    buf[7] = 0x03;                   /* FC03 */
    buf[8] = 0x00; buf[9] = 0x00;   /* start address */
    buf[10] = 0x00; buf[11] = 0x01; /* count */

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 9, buf, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed,
        "one-pass: protocol_id=1 must be dropped by check_request");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count,
        "no push call for frame with invalid protocol ID");
}

/* ---- MBTCP-U-023: reasm path — protocol_id != 0 -> never framed ----------- */
/* protocol_id is part of the reassembler's frame-boundary test: a header with a
 * non-zero protocol id is not a header at all, so the frame is never even
 * dispatched (previously it was framed, then rejected downstream by
 * modbus_tcp_check_request). Either way nothing reaches the queue. */
void test_reasm_invalid_pid_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-023: reasm path: protocol_id=0xFFFF -> dropped by check_request, 0 pushed");
    LOG_MESSAGE();

    /* Build a complete 12-byte frame with protocol_id = 0xFFFF (invalid). */
    uint8_t buf[12];
    buf[0] = 0x00; buf[1] = 0x03;   /* txid */
    buf[2] = 0xFF; buf[3] = 0xFF;   /* protocol_id = 0xFFFF (invalid) */
    buf[4] = 0x00; buf[5] = 0x06;   /* MBAP length = 6 */
    buf[6] = 0x01;                   /* unit_id */
    buf[7] = 0x03;                   /* FC03 */
    buf[8] = 0x00; buf[9] = 0x00;   /* start address */
    buf[10] = 0x00; buf[11] = 0x01; /* count */

    /* Use sock 100 — table is NOT full, so this goes through the normal reasm path. */
    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 100, buf, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed,
        "reasm path: protocol_id=0xFFFF must be dropped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count,
        "no push call for frame with invalid protocol ID in reasm path");
    /* Not a valid header -> byte-wise resync, sub-header tail retained. */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7u,
        reasm_pending_bytes(100),
        "invalid protocol id is a bad header: resync retains the sub-header tail");
}

/* ---- MBTCP-U-025: exact-header-size frame dispatched (MUT-10) ----------- */
/* separate_and_push_requests guards each frame with (c->len - pos) >= sizeof
 * (mb_tcp_header_t). A frame of exactly 8 bytes (== sizeof header, MBAP len=2)
 * must satisfy the >= guard and be pushed. The mutant (> instead of >=) would
 * skip the exact-size frame, leaving it unpushed and pending. */
void test_push_exact_header_size_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-025: exactly 8-byte (==sizeof header) complete frame -> pushed, 0 pending");
    LOG_MESSAGE();

    uint8_t buf[8];
    buf[0] = 0x00; buf[1] = 0x05;   /* transaction id */
    buf[2] = 0x00; buf[3] = 0x00;   /* protocol id = 0 */
    buf[4] = 0x00; buf[5] = 0x02;   /* MBAP length = 2 -> total flen = 8 */
    buf[6] = 0x01;                   /* unit id */
    buf[7] = 0x03;                   /* FC03 */

    TEST_ASSERT_EQUAL_size_t_MESSAGE(8u, mbtcp_reasm_frame_total_len(buf),
        "MBAP length=2 must give flen=8");

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 110, buf, 8);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed, "8-byte complete frame must be pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "push_with_client called once");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8u, mock_pq_packets[0].len, "pushed frame length must be 8");
    TEST_ASSERT_EQUAL_INT_MESSAGE(110, mock_pq_packets[0].sock, "pushed frame must carry client sock 110");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, reasm_pending_bytes(110),
        "no bytes must remain pending");
}

/* ---- MBTCP-U-027: one-pass fallback — short trailing fragment, no OOB read -- */
/* separate_and_push_one_pass() casts &data[pos] to mb_tcp_header_t* and reads
 * header->length, which lives in the first offsetof(unit_id)=6 bytes. A trailing
 * fragment shorter than 6 bytes cannot contain a full MBAP length field, so the
 * length guard must break before that cast/read (avoiding an out-of-bounds read
 * past the logical input). Only the complete leading frame must be processed. */
void test_one_pass_fallback_short_trailing_fragment(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-027: one-pass fallback: complete frame + <6-byte tail -> only frame pushed, no OOB read");
    LOG_MESSAGE();

    /* Fill all 8 reassembly slots so that socket 9 falls back to one-pass. */
    for (int i = 1; i <= 8; i++) {
        reasm_occupy(i);
    }

    /* One complete 8-byte frame (MBAP length = 2 -> total = 8) followed by a
     * 4-byte trailing fragment, for 12 bytes total. The fragment is shorter than
     * the 6-byte MBAP length field, so without the guard the loop would cast the
     * fragment to mb_tcp_header_t and read header->length from bytes 4..5 of the
     * fragment, i.e. input offsets 12..13 — past the buffer (OOB read).
     *
     * The buffer is heap-allocated and sized exactly to the input length so that
     * the stray read lands past the allocation: under an AddressSanitizer build
     * the unguarded code triggers a heap-buffer-overflow (the guard removes it),
     * giving a real Red/Green signal. Under a plain build the read is harmless
     * but the count assertions below still pin the correct behaviour. */
    const size_t in_len = 12;
    uint8_t     *buf    = (uint8_t *)malloc(in_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, "malloc for input buffer must succeed");

    /* Complete frame [0..7]. */
    buf[0] = 0x00; buf[1] = 0x05;   /* txid */
    buf[2] = 0x00; buf[3] = 0x00;   /* protocol_id = 0 */
    buf[4] = 0x00; buf[5] = 0x02;   /* MBAP length = 2 -> total = 8 */
    buf[6] = 0x01;                   /* unit_id */
    buf[7] = 0x03;                   /* FC03 */
    /* Trailing fragment [8..11] — only 4 bytes, no full MBAP length field. */
    buf[8] = 0x00; buf[9] = 0x06; buf[10] = 0x00; buf[11] = 0x00;

    /* Push 12 bytes: one complete frame + a 4-byte tail (table full -> one-pass). */
    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 9, buf, in_len);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed,
        "only the one complete leading frame must be pushed; short tail must not be processed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count,
        "push_with_client must be called exactly once (short trailing fragment ignored)");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8u, mock_pq_packets[0].len,
        "pushed frame length must be the 8-byte complete frame");
    TEST_ASSERT_EQUAL_INT_MESSAGE(9, mock_pq_packets[0].sock,
        "pushed frame must carry client sock 9");

    free(buf);
}

/* ---- MBTCP-U-028: Unit ID 0xFF dispatched to local self-device handler --- */
/* A request addressed to the gateway itself (Unit ID 0xFF) must be recognized
 * by mb_device_is_self() and answered locally via the self-device handler
 * (mb_device_handle_self_request + tcp_server_send back to the client), NOT
 * forwarded to RS485. Regression for modbus_tcp.c's self-device dispatch
 * branch in modbus_tcp_server_task(). */
void test_self_device_unit_ff_dispatched_locally(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-028: Unit ID 0xFF -> local self-device handler, replied to client, not RS485");
    LOG_MESSAGE();

    /* Build a 12-byte FC03 request addressed to the gateway itself (unit 0xFF). */
    uint8_t req[12];
    build_fc03_request(req, 0x00AB, 0xFF, 0, 1);

    /* Precondition: 0xFF is recognized as the gateway's own unit id. */
    TEST_ASSERT_TRUE_MESSAGE(mb_device_is_self(req[6]),
        "Unit ID 0xFF must be recognized as self-device");

    /* Dispatch via the production self-device handler for client sock 77. */
    modbus_tcp_test_handle_self_device_request(TEST_CTX_IDX, 77, req, sizeof(req));

    /* The local self-request handler must have been invoked exactly once... */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_mb_device_handle_self_count,
        "mb_device_handle_self_request must be called once for the self path");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, mock_mb_device_handle_self_unit,
        "self handler must receive the 0xFF unit id");

    /* ...and the locally built response must be sent back to the originating
     * client (sock 77) rather than forwarded to the serial bus. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(77, mock_tcp_send_sock,
        "self-device response must be sent back to the requesting client sock");
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0u, mock_tcp_send_len,
        "a non-empty self-device response must be sent");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, mock_tcp_send_buf[6],
        "self-device response ADU must carry unit id 0xFF");

    /* The self path must NOT forward anything to RS485. */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_serial_send_count,
        "self-device request must not be forwarded to RS485 (serial_send)");
}

/* ---- MBTCP-U-029: on_tcp_conn_close resets stale in-flight RTU state ----- */
/* When the client that issued the currently pending RTU request disconnects,
 * on_tcp_conn_close() must clear pending_tid / pending_slave_id and set
 * pending_client_sock back to -1. Otherwise the next client could receive a
 * response stamped with the disconnected client's transaction id, or the RTU
 * response could be sent to a reused fd (wrong client). Regression for the
 * stale-pending-reset block in on_tcp_conn_close(). */
void test_conn_close_resets_pending_for_in_flight_client(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-029: disconnect of the in-flight client clears pending tid/slave/sock");
    LOG_MESSAGE();

    /* Seed an in-flight RTU request from client sock 88. */
    modbus_tcp_test_set_pending(TEST_CTX_IDX, 0x1234, 0x07, 88);
    TEST_ASSERT_EQUAL_HEX16(0x1234, modbus_tcp_test_get_pending_tid(TEST_CTX_IDX));
    TEST_ASSERT_EQUAL_INT(88, modbus_tcp_test_get_pending_client_sock(TEST_CTX_IDX));

    /* The in-flight client disconnects. */
    modbus_tcp_test_conn_close(TEST_CTX_IDX, 88);

    /* All pending bookkeeping must be cleared so no stale TID leaks to the next
     * client and no RTU response targets a reused fd. */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0, modbus_tcp_test_get_pending_tid(TEST_CTX_IDX),
        "pending_tid must be cleared after the in-flight client disconnects");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, modbus_tcp_test_get_pending_slave_id(TEST_CTX_IDX),
        "pending_slave_id must be cleared after the in-flight client disconnects");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, modbus_tcp_test_get_pending_client_sock(TEST_CTX_IDX),
        "pending_client_sock must be reset to -1 after the in-flight client disconnects");
}

/* ---- MBTCP-U-030: on_tcp_conn_close keeps pending for a different client -- */
/* If a DIFFERENT client (not the in-flight one) disconnects, the pending state
 * for the still-active in-flight request must be left untouched. Guards against
 * over-eager clearing that would drop a legitimate in-flight request. */
void test_conn_close_keeps_pending_for_other_client(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-030: disconnect of an unrelated client must NOT clear pending state");
    LOG_MESSAGE();

    /* In-flight request belongs to sock 88. */
    modbus_tcp_test_set_pending(TEST_CTX_IDX, 0x1234, 0x07, 88);

    /* A different client (sock 99) disconnects. */
    modbus_tcp_test_conn_close(TEST_CTX_IDX, 99);

    /* Pending bookkeeping for sock 88 must be preserved. */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x1234, modbus_tcp_test_get_pending_tid(TEST_CTX_IDX),
        "pending_tid must be preserved when an unrelated client disconnects");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x07, modbus_tcp_test_get_pending_slave_id(TEST_CTX_IDX),
        "pending_slave_id must be preserved when an unrelated client disconnects");
    TEST_ASSERT_EQUAL_INT_MESSAGE(88, modbus_tcp_test_get_pending_client_sock(TEST_CTX_IDX),
        "pending_client_sock must be preserved when an unrelated client disconnects");
}

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();


    RUN_TEST(test_push_single_complete_frame);
    RUN_TEST(test_push_split_frame_two_calls);
    RUN_TEST(test_push_two_coalesced_frames);
    RUN_TEST(test_push_one_and_half_frames);
    RUN_TEST(test_push_bogus_length_drops);
    RUN_TEST(test_push_valid_frame_behind_bogus_header_survives);
    RUN_TEST(test_push_queue_full_frame_consumed);
    RUN_TEST(test_push_table_full_falls_back_to_one_pass);

    RUN_TEST(test_conn_close_frees_slot);
    RUN_TEST(test_push_independent_sockets);

    RUN_TEST(test_push_bogus_length_too_small);

    RUN_TEST(test_one_pass_fallback_short_data);
    RUN_TEST(test_one_pass_fallback_invalid_pid);

    RUN_TEST(test_reasm_invalid_pid_dropped);

    RUN_TEST(test_push_exact_header_size_frame);

    RUN_TEST(test_one_pass_fallback_short_trailing_fragment);

    RUN_TEST(test_self_device_unit_ff_dispatched_locally);
    RUN_TEST(test_conn_close_resets_pending_for_in_flight_client);
    RUN_TEST(test_conn_close_keeps_pending_for_other_client);

    return UNITY_END();
}
