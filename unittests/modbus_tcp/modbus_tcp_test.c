#include "unity.h"
#include "console_log.h"

#include "modbus_tcp_internal.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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

/* ---- Context index used by all tests ------------------------------------ */
#define TEST_CTX_IDX 0

/* A static tcp_desc used so on_tcp_conn_close can find the ctx */
static tcp_desc_t s_test_tcp_desc;

/* ---- setUp / tearDown --------------------------------------------------- */
void setUp(void)
{
    mock_packet_queue_reset();
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

/* ---- MBTCP-U-001: mbtcp_reasm_get — allocate new slot ------------------- */
void test_reasm_get_allocates_new_slot(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-001: reasm_get allocates new slot for new sock");
    LOG_MESSAGE();

    /* Initially no slot for sock 42 */
    TEST_ASSERT_EQUAL_INT(0, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 42));

    /* After get, slot exists */
    int result = modbus_tcp_test_reasm_get(TEST_CTX_IDX, 42);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result, "reasm_get must return 1 for new sock");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 42),
        "slot must exist after reasm_get");
}

/* ---- MBTCP-U-002: mbtcp_reasm_get — find existing slot ------------------ */
void test_reasm_get_finds_existing_slot(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-002: reasm_get finds existing slot for same sock");
    LOG_MESSAGE();

    /* Allocate slot once */
    modbus_tcp_test_reasm_get(TEST_CTX_IDX, 55);
    TEST_ASSERT_EQUAL_INT(1, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 55));

    /* Second call must also succeed (same slot) */
    int result = modbus_tcp_test_reasm_get(TEST_CTX_IDX, 55);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result, "reasm_get must return 1 for existing sock");

    /* Still only one slot exists (no duplication) */
    int count = 0;
    for (int s = 50; s <= 60; s++) {
        count += modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, s);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, count, "exactly one slot must exist for sock 55");
}

/* ---- MBTCP-U-003: mbtcp_reasm_get — table full returns 0 ---------------- */
void test_reasm_get_table_full_returns_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-003: reasm_get returns 0 when table full (8 sockets)");
    LOG_MESSAGE();

    /* Fill all 8 slots */
    for (int i = 1; i <= 8; i++) {
        int res = modbus_tcp_test_reasm_get(TEST_CTX_IDX, i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, res, "first 8 allocations must succeed");
    }

    /* 9th socket must fail */
    int result = modbus_tcp_test_reasm_get(TEST_CTX_IDX, 9);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result, "reasm_get must return 0 when table is full");
}

/* ---- MBTCP-U-004: mbtcp_reasm_free — free existing slot ----------------- */
void test_reasm_free_removes_slot(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-004: reasm_free removes slot");
    LOG_MESSAGE();

    modbus_tcp_test_reasm_get(TEST_CTX_IDX, 77);
    TEST_ASSERT_EQUAL_INT(1, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 77));

    modbus_tcp_test_reasm_free(TEST_CTX_IDX, 77);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 77),
        "slot must be gone after reasm_free");
}

/* ---- MBTCP-U-005: mbtcp_reasm_free — idempotent (free non-existent) ----- */
void test_reasm_free_nonexistent_no_crash(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-005: reasm_free of non-existent sock does not crash");
    LOG_MESSAGE();

    /* sock 99 was never allocated */
    modbus_tcp_test_reasm_free(TEST_CTX_IDX, 99);   /* must not crash */

    /* All slots still clean */
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 99),
        "no slot for sock 99 after free of non-existent");
}

/* ---- MBTCP-U-006: mbtcp_frame_total_len — correct MBAP length parsing --- */
void test_frame_total_len_parsing(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-006: mbtcp_frame_total_len parses MBAP length correctly");
    LOG_MESSAGE();

    uint8_t buf[8] = {0};

    /* MBAP length = 6 -> total = 6 + 6 = 12 */
    buf[4] = 0x00; buf[5] = 0x06;
    TEST_ASSERT_EQUAL_size_t_MESSAGE(12u, modbus_tcp_test_frame_total_len(buf),
        "length=6 -> total must be 12");

    /* MBAP length = 254 -> total = 254 + 6 = 260 */
    buf[4] = 0x00; buf[5] = 0xFE;
    TEST_ASSERT_EQUAL_size_t_MESSAGE(260u, modbus_tcp_test_frame_total_len(buf),
        "length=254 -> total must be 260");

    /* MBAP length = 0 -> total = 0 + 6 = 6 */
    buf[4] = 0x00; buf[5] = 0x00;
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, modbus_tcp_test_frame_total_len(buf),
        "length=0 -> total must be 6");

    /* MBAP length = 0x0102 (258) -> total = 264; tests big-endian parsing */
    buf[4] = 0x01; buf[5] = 0x02;
    TEST_ASSERT_EQUAL_size_t_MESSAGE(264u, modbus_tcp_test_frame_total_len(buf),
        "length=0x0102 -> total must be 264 (big-endian field)");
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
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 20),
        "6 bytes must be pending after first half");

    /* Remaining 6 bytes: frame completes */
    unsigned pushed2 = modbus_tcp_test_push_data(TEST_CTX_IDX, 20, req + 6, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed2, "second half: frame must be pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "push called once after second half");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 20),
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
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 40),
        "6 bytes (half of frame2) must be pending");

    /* Send remaining 6 bytes of frame2 */
    mock_packet_queue_reset();
    unsigned pushed2 = modbus_tcp_test_push_data(TEST_CTX_IDX, 40, buf + 18, 6);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, pushed2, "frame2 must be pushed on second recv");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_pq_push_count, "one more push call for frame2");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 40),
        "no pending bytes after frame2 dispatched");
}

/* ---- MBTCP-U-011: bogus length field -> resync/drop ---------------------- */
void test_push_bogus_length_drops(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "MBTCP-U-011: bogus MBAP length -> resync (drop, no push)");
    LOG_MESSAGE();

    /* Build packet with length = 0xFFFF (total = 65535 + 6 = 65541 > 300) */
    uint8_t buf[12];
    build_fc03_request(buf, 0x0030, 1, 0, 1);
    buf[4] = 0xFF;
    buf[5] = 0xFF;

    unsigned pushed = modbus_tcp_test_push_data(TEST_CTX_IDX, 50, buf, 12);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0u, pushed, "bogus length: no frames pushed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_pq_push_count, "no push call for bogus frame");
    /* Buffer must be cleared (resync) */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 50),
        "buffer must be cleared after bogus length (resync)");
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
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 60),
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
        modbus_tcp_test_reasm_get(TEST_CTX_IDX, i);
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 70),
        "slot must exist after partial push");

    /* Close connection */
    modbus_tcp_test_conn_close(TEST_CTX_IDX, 70);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, modbus_tcp_test_reasm_has_slot(TEST_CTX_IDX, 70),
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
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 81),
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
/* MBAP length=0 gives flen=6 < 8 (sizeof header): triggers the bogus-length
 * branch (same branch as flen > MBTCP_REASM_FRAME_MAX).  Buffer cleared. */
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
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0u, modbus_tcp_test_reasm_pending_bytes(TEST_CTX_IDX, 90),
        "buffer must be cleared (resync) after flen < sizeof(header)");
}

/* ---- MBTCP-U-017: mbtcp_reasm_get — sock < 0 returns 0 (guard) ---------- */
/* The free-slot sentinel is sock=-1. Calling reasm_get with a negative sock
 * must not accidentally match a free slot and must return 0 (NULL internally).
 * After the call, all 8 table slots must remain unallocated (still available). */
void test_reasm_get_negative_sock_returns_zero(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "MBTCP-U-017: reasm_get with sock=-1 must return 0 (guard against free-slot collision)");
    LOG_MESSAGE();

    /* Table is empty: all slots have sock=-1 (the sentinel for free). */
    int result = modbus_tcp_test_reasm_get(TEST_CTX_IDX, -1);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result,
        "reasm_get must return 0 for sock=-1 (invalid socket fd)");

    /* After the failed get, all 8 table slots must still be allocatable for
     * legitimate sockets — meaning the guard did not consume a free slot. */
    for (int i = 1; i <= 8; i++) {
        int res = modbus_tcp_test_reasm_get(TEST_CTX_IDX, i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, res,
            "all 8 slots must still be free after rejected sock=-1 call");
    }
}

/* ---- main ---------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_reasm_get_allocates_new_slot);
    RUN_TEST(test_reasm_get_finds_existing_slot);
    RUN_TEST(test_reasm_get_table_full_returns_zero);

    RUN_TEST(test_reasm_free_removes_slot);
    RUN_TEST(test_reasm_free_nonexistent_no_crash);

    RUN_TEST(test_frame_total_len_parsing);

    RUN_TEST(test_push_single_complete_frame);
    RUN_TEST(test_push_split_frame_two_calls);
    RUN_TEST(test_push_two_coalesced_frames);
    RUN_TEST(test_push_one_and_half_frames);
    RUN_TEST(test_push_bogus_length_drops);
    RUN_TEST(test_push_queue_full_frame_consumed);
    RUN_TEST(test_push_table_full_falls_back_to_one_pass);

    RUN_TEST(test_conn_close_frees_slot);
    RUN_TEST(test_push_independent_sockets);

    RUN_TEST(test_push_bogus_length_too_small);
    RUN_TEST(test_reasm_get_negative_sock_returns_zero);

    return UNITY_END();
}
