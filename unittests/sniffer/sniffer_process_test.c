/*
 * sniffer_process_test.c — Unit tests for the sniffer_process() state machine
 *
 * TC-1  Basic Modbus RTU request/response pair
 * TC-2  Fast Modbus event poll (no events) — stays in IDLE throughout
 * TC-3  Normal Modbus is not inverted after a Fast Modbus block
 * TC-4  FD 46 arrives in RES_WAIT (phase slip)
 * TC-5  All-0xFF frame without preceding FD 46 → CRC error
 * TC-6  Broadcast (slave id 0x00) does not start RES_WAIT
 * TC-7  Timeout: no response within 200 ms
 * TC-8  Fast Modbus subcommand determines sender direction (FC 0x60)
 * TC-9  Scan Response with leading arbitration byte arrives in IDLE
 * TC-10 Orphan response FC04 at startup (first packet is a slave response)
 * TC-11 Orphan response FC01 with bytecount=1 (unambiguous: len=6, not 8)
 * TC-12 FC01 with len=8 treated as request even if data[2]=3 (not the ambiguous case)
 * TC-13 FC01 len=8 data[2]=3 — truly ambiguous case — DIRECTION_UNKNOWN → dropped
 * TC-14 FC05 in SNIFF_IDLE → DIRECTION_UNKNOWN → packet dropped, state stays IDLE
 * TC-15 CRC ERR in SNIFF_IDLE, no prior sync — packet dropped
 * TC-16 CRC ERR in SNIFF_IDLE after sync — alternates master/slave
 */

#include "unity.h"
#include "console_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* sniff_packet_t is declared in sniffer.h under __unittest_env__.
 * serial_desc_t is provided by the mock serial.h (unittests/sniffer/mocks/serial.h)
 * which is included transitively via sniffer.h — do NOT include serial.h directly
 * here to avoid conflict between the mock typedef and the production header.
 * Mock tracking data for serial_set_rx_timeout is exposed via serial_mock.h instead. */
#include "sniffer.h"
#include "serial_mock.h"

/* FreeRTOS queue and timer mocks */
#include "freertos/queue.h"
#include "freertos/timers.h"

/* ============================================================
 * Test fixtures — one serial descriptor per port
 * ============================================================ */

static serial_desc_t s_desc0;  /* descriptor for port 0 */
static serial_desc_t s_desc1;  /* descriptor for port 1 */

/* ============================================================
 * Helper: inject bytes into port 0 / port 1
 * ============================================================ */

/* Inject a raw byte sequence into port 0 */
#define SEND0(arr) s_desc0.sniff_handler(NULL, (arr), sizeof(arr))
/* Inject a raw byte sequence into port 1 */
#define SEND1(arr) s_desc1.sniff_handler(NULL, (arr), sizeof(arr))

/* ============================================================
 * Helper: dequeue one packet from the sniffer queue
 * ============================================================ */

static sniff_packet_t dequeue_packet(void)
{
    sniff_packet_t pkt = {0};
    QueueHandle_t q = mock_get_last_created_queue();
    TEST_ASSERT_NOT_NULL_MESSAGE(q, "sniffer queue not created");
    BaseType_t got = xQueueReceive(q, &pkt, 0);
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, got, "expected packet in queue but queue was empty");
    return pkt;
}

/* Assert that the sniffer queue currently holds no packets */
static void assert_queue_empty(void)
{
    sniff_packet_t pkt = {0};
    QueueHandle_t q = mock_get_last_created_queue();
    /* q may be NULL if the sniffer was never initialized — also counts as empty */
    if (q == NULL) return;
    BaseType_t got = xQueueReceive(q, &pkt, 0);
    TEST_ASSERT_EQUAL_MESSAGE(pdFAIL, got, "expected empty queue but packet was found");
}

/* ============================================================
 * setUp / tearDown
 * ============================================================ */

void setUp(void)
{
    /* Reset mock state first so that sniffer_init() uses fresh mocks */
    mock_freertos_queue_reset();
    mock_freertos_timers_reset();
    mock_serial_reset();

    memset(&s_desc0, 0, sizeof(s_desc0));
    memset(&s_desc1, 0, sizeof(s_desc1));

    sniffer_init();
    sniffer_attach(0, &s_desc0);
    sniffer_attach(1, &s_desc1);
    sniffer_enable(0);
    sniffer_enable(1);
}

void tearDown(void) {}

/* ============================================================
 * TC-1 — Basic Modbus RTU request/response
 * ============================================================ */

void test_tc1_basic_modbus_rtu_request_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-1: Basic Modbus RTU request/response");
    LOG_MESSAGE();

    /* Request: slave=0x83, func=0x03, valid CRC */
    uint8_t req[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};

    SEND0(req);
    /* In RES_WAIT: nothing should be enqueued yet */
    assert_queue_empty();

    /* Response: same slave/func, valid CRC */
    uint8_t res[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};

    SEND0(res);

    /* Both packets should now be in the queue */
    sniff_packet_t pkt0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt0.is_master, "pkt[0] must be MASTER (request)");
    TEST_ASSERT_TRUE_MESSAGE(pkt0.crc_valid, "pkt[0] must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt0.slave_id, "pkt[0] slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt0.function, "pkt[0] function must be 0x03");

    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt1.is_master, "pkt[1] must be SLAVE (response)");
    TEST_ASSERT_TRUE_MESSAGE(pkt1.crc_valid, "pkt[1] must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt1.slave_id, "pkt[1] slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt1.function, "pkt[1] function must be 0x03");

    assert_queue_empty();
}

/* ============================================================
 * TC-2 — Fast Modbus event poll, no events (stays in IDLE throughout)
 * ============================================================ */

void test_tc2_fast_modbus_event_poll_no_events(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-2: Fast Modbus event poll, no events — stays in IDLE throughout");
    LOG_MESSAGE();

    /* Packet 1: FD 46 subcmd=0x10 (master, Event Request), valid CRC */
    uint8_t p1[] = {0xFD, 0x46, 0x10, 0x00, 0x4F, 0x00, 0x00, 0xC9, 0x7D};
    SEND0(p1);
    /* FM in IDLE is emitted immediately */
    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt1.is_master, "pkt1 must be MASTER (subcmd 0x10 is not slave)");
    TEST_ASSERT_TRUE_MESSAGE(pkt1.crc_valid, "pkt1 must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFD, pkt1.slave_id, "pkt1 slave_id must be 0xFD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x46, pkt1.function, "pkt1 function must be 0x46");

    /*
     * Packet 2: FF FF FF FF FF
     * strip_arbitration: data[0]=0xFF, fast_modbus_truncate_ff → tlen=0 < 4 → NOT stripped.
     * effective == data, effective_len=5.
     * In IDLE: effective[0]=0xFF ≠ 0xFD, crc_check(effective,5)=false → !valid_crc branch.
     * synchronized=true (set by pkt1, subcmd=0x10, is_master=true), last_was_master=true.
     * CRC ERR with sync → is_master = !last_was_master = false.
     */
    uint8_t p2[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    SEND0(p2);
    sniff_packet_t pkt2 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt2.is_master, "pkt2 must be SLAVE (CRC ERR after master FM → alternates to slave)");
    TEST_ASSERT_FALSE_MESSAGE(pkt2.crc_valid, "pkt2 must have invalid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, pkt2.slave_id, "pkt2 slave_id must be 0xFF");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, pkt2.function, "pkt2 function must be 0xFF");

    /*
     * Packet 3: FD 46 12 52 5D (No events, subcmd=0x12)
     * fm_is_slave_subcmd(0x12) = true → is_master=false, valid CRC.
     */
    uint8_t p3[] = {0xFD, 0x46, 0x12, 0x52, 0x5D};
    SEND0(p3);
    sniff_packet_t pkt3 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt3.is_master,
        "pkt3 must be SLAVE (subcmd 0x12 is a slave subcmd)");
    TEST_ASSERT_TRUE_MESSAGE(pkt3.crc_valid, "pkt3 must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFD, pkt3.slave_id, "pkt3 slave_id must be 0xFD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x46, pkt3.function, "pkt3 function must be 0x46");

    assert_queue_empty();
}

/* ============================================================
 * TC-3 — Normal Modbus is not inverted after a Fast Modbus block
 * ============================================================ */

void test_tc3_normal_modbus_not_inverted_after_fm(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-3: Normal Modbus is not inverted after Fast Modbus block");
    LOG_MESSAGE();

    /* Replay TC-2 and discard those 3 packets */
    uint8_t p1[] = {0xFD, 0x46, 0x10, 0x00, 0x4F, 0x00, 0x00, 0xC9, 0x7D};
    uint8_t p2[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t p3[] = {0xFD, 0x46, 0x12, 0x52, 0x5D};
    SEND0(p1);
    dequeue_packet(); /* discard pkt1 */
    SEND0(p2);
    dequeue_packet(); /* discard pkt2 */
    SEND0(p3);
    dequeue_packet(); /* discard pkt3 */
    assert_queue_empty();

    /* Packet 4: normal Modbus request (valid CRC, non-broadcast, non-FM) */
    uint8_t p4[] = {0x83, 0x04, 0x00, 0x08, 0x00, 0x04, 0x6E, 0x29};
    SEND0(p4);
    /* Goes to RES_WAIT — nothing enqueued yet */
    assert_queue_empty();

    /* Packet 5: normal Modbus response */
    uint8_t p5[] = {0x83, 0x04, 0x08, 0x02, 0xE6, 0x00, 0x00, 0x00, 0x00, 0x01, 0x35, 0x41, 0xE7};
    SEND0(p5);

    sniff_packet_t pkt4 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt4.is_master,
        "pkt4 must be MASTER (first in RTU pair after FM block)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt4.slave_id, "pkt4 slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x04, pkt4.function, "pkt4 function must be 0x04");

    sniff_packet_t pkt5 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt5.is_master,
        "pkt5 must be SLAVE (second in RTU pair after FM block)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt5.slave_id, "pkt5 slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x04, pkt5.function, "pkt5 function must be 0x04");

    assert_queue_empty();
}

/* ============================================================
 * TC-4 — FD 46 arrives in RES_WAIT (phase slip)
 * ============================================================ */

void test_tc4_fd46_in_res_wait_phase_slip(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-4: FD 46 arrives in RES_WAIT (phase slip) — buffered req discarded");
    LOG_MESSAGE();

    /* Packet 1: normal RTU request — goes to RES_WAIT, buffered */
    uint8_t p1[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(p1);
    assert_queue_empty();

    /*
     * Packet 2: FM master arrives in RES_WAIT (subcmd=0x10, not slave).
     * The buffered pkt1 is discarded; pkt2 is emitted as standalone MASTER.
     */
    uint8_t p2[] = {0xFD, 0x46, 0x10, 0x00, 0x4F, 0x00, 0x00, 0xC9, 0x7D};
    SEND0(p2);
    sniff_packet_t pkt2 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt2.is_master,
        "pkt2 must be MASTER (FM in RES_WAIT, subcmd 0x10 is not slave)");
    TEST_ASSERT_TRUE_MESSAGE(pkt2.crc_valid, "pkt2 must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFD, pkt2.slave_id, "pkt2 slave_id must be 0xFD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x46, pkt2.function, "pkt2 function must be 0x46");
    assert_queue_empty();

    /*
     * Packet 3: FF FF FF FF FF
     * In IDLE: invalid CRC → synchronized=true (set by pkt2, is_master=true),
     * last_was_master=true → CRC ERR alternates → is_master=false, crc_valid=false.
     */
    uint8_t p3[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    SEND0(p3);
    sniff_packet_t pkt3 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt3.is_master,
        "pkt3 must be SLAVE (CRC ERR after master FM in RES_WAIT → alternates to slave)");
    TEST_ASSERT_FALSE_MESSAGE(pkt3.crc_valid, "pkt3 must have invalid CRC");
    assert_queue_empty();

    /*
     * Packet 4: FD 46 12 52 5D (subcmd=0x12, slave subcmd) → is_master=false.
     */
    uint8_t p4[] = {0xFD, 0x46, 0x12, 0x52, 0x5D};
    SEND0(p4);
    sniff_packet_t pkt4 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt4.is_master,
        "pkt4 must be SLAVE (subcmd 0x12 is a slave subcmd)");
    TEST_ASSERT_TRUE_MESSAGE(pkt4.crc_valid, "pkt4 must have valid CRC");
    assert_queue_empty();
}

/* ============================================================
 * TC-5 — All-0xFF frame without preceding FD 46 → CRC error
 * ============================================================ */

void test_tc5_all_ff_without_preceding_fd46(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-5: All-0xFF frame without preceding FD 46 is handled in RES_WAIT");
    LOG_MESSAGE();

    /*
     * Packet 1: 83 03 00 01 00 04 0B EB (valid CRC, FC03 request, len=8)
     * classify_direction: FC03, len==8 → DIRECTION_REQUEST → goes to RES_WAIT.
     * Nothing enqueued.
     */
    uint8_t p1[] = {0x83, 0x03, 0x00, 0x01, 0x00, 0x04, 0x0B, 0xEB};
    SEND0(p1);
    assert_queue_empty();

    /*
     * Packet 2: FF FF FF FF FF
     * In RES_WAIT: strip_arbitration → effective=data (all-FF not stripped), effective_len=5.
     * effective[0]=0xFF ≠ 0xFD → not FM → normal response path.
     * req_pkt = buffered pkt1, is_master=true.
     * res_pkt = pkt2, is_master=false, crc_valid=false (CRC of FF FF FF ≠ FF FF).
     */
    uint8_t p2[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    SEND0(p2);

    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt1.is_master,
        "pkt1 (buffered request) must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(pkt1.crc_valid,
        "pkt1 must have valid CRC (it was buffered because CRC was valid)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt1.slave_id, "pkt1 slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt1.function, "pkt1 function must be 0x03");

    sniff_packet_t pkt2 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt2.is_master,
        "pkt2 (all-FF in RES_WAIT) must be SLAVE (response slot)");
    TEST_ASSERT_FALSE_MESSAGE(pkt2.crc_valid,
        "pkt2 must have invalid CRC (all-FF has no valid Modbus CRC)");

    assert_queue_empty();
}

/* ============================================================
 * TC-6 — Broadcast (slave id 0x00) does not start RES_WAIT
 * ============================================================ */

void test_tc6_broadcast_does_not_start_res_wait(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-6: Broadcast (slave 0x00) does not start RES_WAIT");
    LOG_MESSAGE();

    /* Reset timer counter so we can check it after pkt2 */
    mock_freertos_timers_reset();

    /*
     * Packet 1: 00 10 00 01 00 02 04 00 01 12 34 6A 28 (broadcast write, valid CRC).
     * CRC of first 11 bytes = 0x286A (lo=0x6A, hi=0x28).
     * slave_id=0x00 → broadcast branch → emitted immediately, no RES_WAIT.
     */
    uint8_t p1[] = {0x00, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x01, 0x12, 0x34, 0x6A, 0x28};
    SEND0(p1);
    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt1.is_master, "broadcast pkt1 must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(pkt1.crc_valid, "broadcast pkt1 must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, pkt1.slave_id,
        "broadcast pkt1 slave_id must be 0x00");
    assert_queue_empty();

    /*
     * Packet 2: 83 03 00 61 00 02 8B F7 — must be treated as a fresh request (IDLE → RES_WAIT),
     * NOT as a response to the broadcast. Timer must start.
     */
    int timer_start_before = mock_xTimerStart_called;
    uint8_t p2[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(p2);
    /* Nothing enqueued — pkt2 is buffered in RES_WAIT */
    assert_queue_empty();
    /* Timer must have been started */
    TEST_ASSERT_EQUAL_MESSAGE(timer_start_before + 1, mock_xTimerStart_called,
        "xTimerStart must be called once after pkt2 (RES_WAIT entered)");
}

/* ============================================================
 * TC-7 — Timeout: no response within 200 ms
 * ============================================================ */

void test_tc7_timeout_no_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-7: Timeout — no response within 200 ms");
    LOG_MESSAGE();

    /*
     * Save the timer callback after sniffer_init() has registered it.
     * sniffer_init() creates BRIDGES_COUNT (2) timers; mock_xTimerCreate_pxCallbackFunction
     * holds the last registered callback (port 1's), but both ports use the same
     * resp_timer_cb function — so this is valid for port 0 tests too.
     * pvTimerGetTimerID is stubbed to always return 0, meaning the callback
     * always acts on port 0.
     */
    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* Packet 1: 83 04 00 03 00 09 DE 2E — valid CRC, unicast → RES_WAIT */
    uint8_t p1[] = {0x83, 0x04, 0x00, 0x03, 0x00, 0x09, 0xDE, 0x2E};
    SEND0(p1);
    assert_queue_empty();

    /* Simulate 200 ms elapsed by firing the timer callback manually */
    timer_cb(MOCK_TIMER_HANDLE);

    /* A single timeout packet must have been enqueued */
    sniff_packet_t pkt = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt.is_timeout, "timeout packet must have is_timeout=true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt.slave_id,
        "timeout packet slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x04, pkt.function,
        "timeout packet function must be 0x04");
    TEST_ASSERT_TRUE_MESSAGE(pkt.is_master, "timeout pkt must be MASTER (set in resp_timer_cb)");
    TEST_ASSERT_TRUE_MESSAGE(pkt.crc_valid, "timeout pkt must have crc_valid=true (set in resp_timer_cb)");

    assert_queue_empty();
}

/* ============================================================
 * TC-8 — Fast Modbus subcommand determines sender direction (FC 0x60)
 * ============================================================ */

void test_tc8_fm_subcmd_determines_direction(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-8: Fast Modbus subcmd determines direction (FC 0x60)");
    LOG_MESSAGE();

    /* --- packet a: FD 60 01 09 F0  (subcmd=0x01, master) --- */
    uint8_t pa[] = {0xFD, 0x60, 0x01, 0x09, 0xF0};
    SEND0(pa);
    sniff_packet_t pkta = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkta.is_master,
        "pa: subcmd 0x01 is not a slave subcmd → must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(pkta.crc_valid, "pa: must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFD, pkta.slave_id, "pa: slave_id must be 0xFD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x60, pkta.function, "pa: function must be 0x60");

    /* --- packet b: FD 60 08 ...  (subcmd=0x08, master) --- */
    uint8_t pb[] = {0xFD, 0x60, 0x08, 0x00, 0x06, 0x24, 0x66, 0x03, 0x00, 0xC8, 0x00, 0x14, 0x9D, 0x24};
    SEND0(pb);
    sniff_packet_t pktb = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pktb.is_master,
        "pb: subcmd 0x08 is not a slave subcmd → must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(pktb.crc_valid, "pb: must have valid CRC");

    /*
     * packet c: FD 60 09 ...  (subcmd=0x09, slave).
     * CRC of first 23 bytes = 0xDCEC (lo=0xEC, hi=0xDC).
     */
    uint8_t pc[] = {
        0xFD, 0x60, 0x09, 0x00, 0x06, 0x24, 0x66, 0x03,
        0x28, 0x00, 0x57, 0x00, 0x42, 0x00, 0x4D, 0x00,
        0x53, 0x00, 0x57, 0x00, 0x34, 0x00, 0x00, 0xEC, 0xDC
    };
    SEND0(pc);
    sniff_packet_t pktc = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pktc.is_master,
        "pc: subcmd 0x09 IS a slave subcmd → must be SLAVE");
    TEST_ASSERT_TRUE_MESSAGE(pktc.crc_valid, "pc: must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFD, pktc.slave_id, "pc: slave_id must be 0xFD");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x60, pktc.function, "pc: function must be 0x60");

    assert_queue_empty();
}

/* ============================================================
 * TC-9 — Scan Response with leading arbitration byte in IDLE
 * ============================================================ */

void test_tc9_scan_response_with_leading_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-9: Scan Response with leading 0xFF arbitration byte in IDLE");
    LOG_MESSAGE();

    /*
     * Packet: FF FD 60 03 00 06 24 66 83 C4 61
     * strip_arbitration: data[0]=0xFF, fast_modbus_truncate_ff strips 1 byte.
     * Result: effective=[FD 60 03 00 06 24 66 83 C4 61], tlen=10.
     * tlen >= 4 and effective[1]=0x60 → STRIPPED.
     * In IDLE: effective[0]=0xFD, effective[1]=0x60 → FM branch.
     * subcmd=0x03 → fm_is_slave_subcmd(0x03)=true → is_master=false.
     * crc_valid = crc_check(effective, 10).
     */
    uint8_t pkt_raw[] = {0xFF, 0xFD, 0x60, 0x03, 0x00, 0x06, 0x24, 0x66, 0x83, 0xC4, 0x61};
    SEND0(pkt_raw);

    sniff_packet_t pkt = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt.is_master,
        "TC-9: subcmd 0x03 is slave → must be SLAVE (is_master=false)");
    TEST_ASSERT_TRUE_MESSAGE(pkt.crc_valid,
        "TC-9: CRC must be valid after stripping the leading 0xFF");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFD, pkt.slave_id,
        "TC-9: slave_id must be 0xFD (first byte of stripped frame)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x60, pkt.function,
        "TC-9: function must be 0x60");

    assert_queue_empty();
}

/* ============================================================
 * TC-RX1 — sniffer_enable() switches RX timeout to SNIFFER value
 * ============================================================ */
void test_rx_timeout_enable_sets_sniffer_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-RX1: sniffer_enable() must set RX timeout to SERIAL_RX_TOUT_SNIFFER");
    LOG_MESSAGE();

    /* setUp already calls sniffer_enable(0), reset counters to test a fresh call */
    mock_serial_reset();

    sniffer_disable(0);
    mock_serial_reset();

    sniffer_enable(0);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_set_rx_timeout_data.called,
        "serial_set_rx_timeout must be called once on sniffer_enable");
    TEST_ASSERT_EQUAL_MESSAGE(SERIAL_RX_TOUT_SNIFFER, mock_serial_set_rx_timeout_data.tout_symbols,
        "sniffer_enable must set timeout to SERIAL_RX_TOUT_SNIFFER");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_desc0, mock_serial_set_rx_timeout_data.desc,
        "serial_set_rx_timeout must be called with the correct serial descriptor");
}

/* ============================================================
 * TC-RX2 — sniffer_disable() switches RX timeout to PROXY value
 * ============================================================ */
void test_rx_timeout_disable_sets_proxy_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-RX2: sniffer_disable() must set RX timeout to SERIAL_RX_TOUT_PROXY");
    LOG_MESSAGE();

    mock_serial_reset();
    sniffer_disable(0);

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_set_rx_timeout_data.called,
        "serial_set_rx_timeout must be called once on sniffer_disable");
    TEST_ASSERT_EQUAL_MESSAGE(SERIAL_RX_TOUT_PROXY, mock_serial_set_rx_timeout_data.tout_symbols,
        "sniffer_disable must set timeout to SERIAL_RX_TOUT_PROXY");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&s_desc0, mock_serial_set_rx_timeout_data.desc,
        "serial_set_rx_timeout must be called with the correct serial descriptor");
}

/* ============================================================
 * TC-RX3 — after sniffer_detach(), enable/disable do not call serial_set_rx_timeout
 * ============================================================ */
void test_rx_timeout_not_called_after_detach(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-RX3: after sniffer_detach(), serial_set_rx_timeout must not be called");
    LOG_MESSAGE();

    sniffer_detach(0);
    mock_serial_reset();

    sniffer_enable(0);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "serial_set_rx_timeout must NOT be called after sniffer_detach (enable)");

    sniffer_disable(0);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "serial_set_rx_timeout must NOT be called after sniffer_detach (disable)");
}

/* ============================================================
 * TC-10 — Orphan response FC04 at startup (first packet is a slave response)
 * ============================================================ */

void test_tc10_orphan_fc04_response_at_startup(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-10: Orphan FC04 response at startup — first packet classified as slave");
    LOG_MESSAGE();

    /*
     * Packet 1: FC04 response (13 bytes).
     * addr=0x83, fc=0x04, bytecount=0x08 (8 bytes of register data).
     * len=13, 5+data[2]=5+8=13 → DIRECTION_RESPONSE.
     * Sniffer starts in IDLE; classify_direction returns RESPONSE.
     * Expected: emitted immediately as is_master=false, crc_valid=true.
     * State stays SNIFF_IDLE.
     */
    uint8_t pkt1[] = {0x83, 0x04, 0x08, 0x02, 0xE6, 0x00, 0x00, 0x00, 0x00, 0x01, 0x35, 0x41, 0xE7};
    SEND0(pkt1);

    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p1.is_master,
        "TC-10 pkt1: FC04 response must be classified as SLAVE (is_master=false)");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,
        "TC-10 pkt1: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p1.slave_id,
        "TC-10 pkt1: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x04, p1.function,
        "TC-10 pkt1: function must be 0x04");
    assert_queue_empty();

    /*
     * Packet 2: normal FC03 request (8 bytes, CRC OK).
     * len=8 → DIRECTION_REQUEST → buffered in RES_WAIT.
     */
    uint8_t pkt2[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(pkt2);
    /* Still in RES_WAIT — nothing enqueued yet */
    assert_queue_empty();

    /*
     * Packet 3: FC03 response (9 bytes, CRC OK).
     * Both packets (request MASTER + response SLAVE) must be emitted.
     */
    uint8_t pkt3[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    SEND0(pkt3);

    sniff_packet_t p2 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p2.is_master,
        "TC-10 pkt2: FC03 request must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(p2.crc_valid,
        "TC-10 pkt2: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p2.slave_id, "TC-10 pkt2: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p2.function, "TC-10 pkt2: function must be 0x03");

    sniff_packet_t p3 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p3.is_master,
        "TC-10 pkt3: FC03 response must be SLAVE");
    TEST_ASSERT_TRUE_MESSAGE(p3.crc_valid,
        "TC-10 pkt3: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p3.slave_id, "TC-10 pkt3: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p3.function, "TC-10 pkt3: function must be 0x03");

    assert_queue_empty();
}

/* ============================================================
 * TC-11 — Orphan response FC01 with bytecount=1 (unambiguous: len=6, not 8)
 * ============================================================ */

void test_tc11_orphan_fc01_response_len6(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-11: Orphan FC01 response len=6 — unambiguous DIRECTION_RESPONSE");
    LOG_MESSAGE();

    /*
     * Packet: addr=0x83, fc=0x01, bytecount=0x01, data=0x00, CRC=79 F0.
     * len=6, 5+data[2]=5+1=6, len != 8 → DIRECTION_RESPONSE.
     * Expected: emitted as is_master=false, state stays IDLE.
     */
    uint8_t pkt[] = {0x83, 0x01, 0x01, 0x00, 0x79, 0xF0};
    SEND0(pkt);

    sniff_packet_t p = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p.is_master,
        "TC-11: FC01 response (len=6) must be classified as SLAVE (is_master=false)");
    TEST_ASSERT_TRUE_MESSAGE(p.crc_valid,
        "TC-11: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p.slave_id,
        "TC-11: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p.function,
        "TC-11: function must be 0x01");

    assert_queue_empty();
}

/* ============================================================
 * TC-12 — FC01 with len=8 treated as request even if data[2]=0x14 (not 3)
 * ============================================================ */

void test_tc12_fc01_len8_treated_as_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-12: FC01 len=8 always treated as request (DIRECTION_REQUEST)");
    LOG_MESSAGE();

    /*
     * Packet: addr=0x83, fc=0x01, start_addr=0x14B4, count=7, CRC=26 3C.
     * len=8, data[2]=0x14 (not 3) → DIRECTION_REQUEST (len==8 rule).
     * Expected: buffered in RES_WAIT; nothing enqueued yet.
     */
    uint8_t pkt_req[] = {0x83, 0x01, 0x14, 0xB4, 0x00, 0x07, 0x26, 0x3C};
    SEND0(pkt_req);
    assert_queue_empty();

    /*
     * Response: addr=0x83, fc=0x01, bytecount=0x01, data=0x7F, CRC=38 10.
     * len=6, 5+data[2]=5+1=6 → DIRECTION_RESPONSE (but this arrives in RES_WAIT,
     * so it is unconditionally treated as a response).
     * Both request (MASTER) and response (SLAVE) must be emitted.
     */
    uint8_t pkt_res[] = {0x83, 0x01, 0x01, 0x7F, 0x38, 0x10};
    SEND0(pkt_res);

    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_master,
        "TC-12: buffered FC01 request must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,
        "TC-12: request crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p1.slave_id,
        "TC-12: request slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.function,
        "TC-12: request function must be 0x01");

    sniff_packet_t p2 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p2.is_master,
        "TC-12: FC01 response must be SLAVE");
    TEST_ASSERT_TRUE_MESSAGE(p2.crc_valid,
        "TC-12: response crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p2.slave_id,
        "TC-12: response slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p2.function,
        "TC-12: response function must be 0x01");

    assert_queue_empty();
}

/* ============================================================
 * TC-13 — FC01 len=8 AND data[2]=3: truly ambiguous case → DIRECTION_UNKNOWN → dropped
 * ============================================================ */

void test_tc13_fc01_len8_data2_3_ambiguous_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-13: FC01 len=8 data[2]=3 — truly ambiguous case, DIRECTION_UNKNOWN → dropped");
    LOG_MESSAGE();

    /*
     * Packet: addr=0x83, fc=0x01, start_hi=0x03, start_lo=0x00, count_hi=0x00, count_lo=0x03,
     *         CRC=0x62, 0x6D.
     * len=8 AND data[2]=0x03 — this is the ONLY case where both formulas match:
     *   request formula: fixed 8 bytes ✓
     *   response formula: 5 + data[2] = 5 + 3 = 8 ✓
     * New logic: len==8 AND data[2]==3 → DIRECTION_UNKNOWN → dropped in SNIFF_IDLE.
     * Expected: queue is empty AND timer was NOT started (packet was never buffered).
     */
    int timer_start_before = mock_xTimerStart_called;
    uint8_t pkt_req[] = {0x83, 0x01, 0x03, 0x00, 0x00, 0x03, 0x62, 0x6D};
    SEND0(pkt_req);
    /* Must be dropped — queue must be empty and state stays IDLE */
    assert_queue_empty();
    /* Timer must NOT have been started (packet was dropped, not buffered) */
    TEST_ASSERT_EQUAL_MESSAGE(timer_start_before, mock_xTimerStart_called,
        "xTimerStart must NOT be called after dropped packet (DIRECTION_UNKNOWN in IDLE)");
}

/* ============================================================
 * TC-14 — FC05 (Write Single Coil) in SNIFF_IDLE → DIRECTION_UNKNOWN → drop
 * ============================================================ */

void test_tc14_fc05_direction_unknown_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-14: FC05 in SNIFF_IDLE → DIRECTION_UNKNOWN → packet dropped, state stays IDLE");
    LOG_MESSAGE();

    /*
     * FC05 Write Single Coil ON: addr=0x83, fc=0x05, addr_hi=0x00, addr_lo=0xAC,
     * value_hi=0xFF, value_lo=0x00, CRC lo=0x52, hi=0x39.
     * classify_direction: FC05 → DIRECTION_UNKNOWN (request and echo-response are
     * both 8 bytes, indistinguishable).
     * With the new logic, DIRECTION_UNKNOWN in SNIFF_IDLE → drop, stay in SNIFF_IDLE.
     * Expected: nothing enqueued, state stays IDLE.
     */
    uint8_t pkt_fc05[] = {0x83, 0x05, 0x00, 0xAC, 0xFF, 0x00, 0x52, 0x39};
    SEND0(pkt_fc05);
    /* Must be dropped — queue must be empty and state stays IDLE */
    assert_queue_empty();

    /*
     * Verify state is still SNIFF_IDLE: send a FC03 request (DIRECTION_REQUEST).
     * It must be buffered (queue still empty), then after response both packets
     * are emitted in the correct master/slave order.
     */
    uint8_t pkt_req[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(pkt_req);
    assert_queue_empty();

    uint8_t pkt_res[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    SEND0(pkt_res);

    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_master,
        "TC-14: FC03 request after dropped FC05 must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,
        "TC-14: FC03 request crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p1.slave_id,
        "TC-14: FC03 request slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function,
        "TC-14: FC03 request function must be 0x03");

    sniff_packet_t p2 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p2.is_master,
        "TC-14: FC03 response must be SLAVE");
    TEST_ASSERT_TRUE_MESSAGE(p2.crc_valid,
        "TC-14: FC03 response crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p2.slave_id,
        "TC-14: FC03 response slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p2.function,
        "TC-14: FC03 response function must be 0x03");

    assert_queue_empty();
}

/* ============================================================
 * TC-15 — CRC ERR in SNIFF_IDLE without prior sync → drop
 * ============================================================ */

void test_tc15_crc_err_no_sync_dropped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-15: CRC ERR in SNIFF_IDLE, no prior sync — packet dropped");
    LOG_MESSAGE();

    /*
     * Send a packet with an invalid CRC before any packet with known direction
     * has been seen (sniffer is not yet synchronized).
     * New logic: !ctx->synchronized → drop silently, do not enqueue.
     */
    uint8_t pkt[] = {0x83, 0x03, 0x00, 0x00, 0xDE, 0xAD};
    SEND0(pkt);
    assert_queue_empty();
}

/* ============================================================
 * TC-16 — CRC ERR in SNIFF_IDLE after sync → alternates master/slave
 * ============================================================ */

void test_tc16_crc_err_after_sync_alternates_direction(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-16: CRC ERR in SNIFF_IDLE after sync — alternates master/slave direction");
    LOG_MESSAGE();

    /*
     * Step 1: send a normal FC03 request + response to synchronize the sniffer.
     * After the response, last_was_master=false (last emitted packet was the SLAVE response).
     */
    uint8_t req[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    uint8_t res[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    SEND0(req);
    assert_queue_empty(); /* buffered in RES_WAIT */
    SEND0(res);
    /* Dequeue and discard both packets (synchronization complete) */
    sniff_packet_t p_req = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p_req.is_master, "TC-16: setup req must be MASTER");
    sniff_packet_t p_res = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p_res.is_master, "TC-16: setup res must be SLAVE");
    assert_queue_empty();

    /*
     * Step 2: send first CRC ERR packet.
     * synchronized=true, last_was_master=false (last was SLAVE response).
     * Expected: is_master = !false = true.
     */
    uint8_t crc_err1[] = {0x83, 0x03, 0x00, 0x00, 0xDE, 0xAD};
    SEND0(crc_err1);
    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt1.is_master,
        "TC-16: first CRC ERR after SLAVE → must be MASTER (is_master=true)");
    TEST_ASSERT_FALSE_MESSAGE(pkt1.crc_valid,
        "TC-16: first CRC ERR packet must have crc_valid=false");

    /*
     * Step 3: send second CRC ERR packet.
     * After step 2, last_was_master=true.
     * Expected: is_master = !true = false.
     */
    uint8_t crc_err2[] = {0x83, 0x03, 0x00, 0x00, 0xDE, 0xAD};
    SEND0(crc_err2);
    sniff_packet_t pkt2 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt2.is_master,
        "TC-16: second CRC ERR after MASTER → must be SLAVE (is_master=false)");
    TEST_ASSERT_FALSE_MESSAGE(pkt2.crc_valid,
        "TC-16: second CRC ERR packet must have crc_valid=false");

    assert_queue_empty();
}

/* ============================================================
 * TC-17 — Recursive stream split: two glued valid frames injected at once
 * ============================================================ */

void test_tc17_recursive_stream_split_two_frames(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-17: Recursive stream split — two glued FC03 frames injected via SEND0");
    LOG_MESSAGE();

    /*
     * Concatenation of two valid Modbus RTU frames (17 bytes total):
     *   Frame 0 (FC03 request,  8 bytes, valid CRC): 83 03 00 61 00 02 8B F7
     *   Frame 1 (FC03 response, 9 bytes, valid CRC): 83 03 04 00 03 00 1E 28 33
     *
     * The combined 17-byte buffer does not have a valid CRC as a single frame,
     * so the sniffer takes the recursive/stream-split path to recover both frames.
     * Expected: two packets in the queue.
     *   pkt[0]: is_master=true,  crc_valid=true, slave_id=0x83, function=0x03, data_len=8
     *   pkt[1]: is_master=false, crc_valid=true, slave_id=0x83, function=0x03
     */
    uint8_t buf[] = {
        /* FC03 request (8 bytes, valid CRC) */
        0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7,
        /* FC03 response (9 bytes, valid CRC) */
        0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33
    };

    SEND0(buf);

    sniff_packet_t pkt0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt0.is_master,
        "TC-17 pkt[0]: FC03 request must be MASTER (is_master=true)");
    TEST_ASSERT_TRUE_MESSAGE(pkt0.crc_valid,
        "TC-17 pkt[0]: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt0.slave_id,
        "TC-17 pkt[0]: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt0.function,
        "TC-17 pkt[0]: function must be 0x03");
    TEST_ASSERT_EQUAL_MESSAGE(8, pkt0.data_len,
        "TC-17 pkt[0]: data_len must be 8");

    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(pkt1.is_master,
        "TC-17 pkt[1]: FC03 response must be SLAVE (is_master=false)");
    TEST_ASSERT_TRUE_MESSAGE(pkt1.crc_valid,
        "TC-17 pkt[1]: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt1.slave_id,
        "TC-17 pkt[1]: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt1.function,
        "TC-17 pkt[1]: function must be 0x03");

    assert_queue_empty();
}

/* ============================================================
 * TC-CA1 — sniffer_set_cache_active(true): sets SNIFFER RX timeout
 * ============================================================ */

void test_sniffer_set_cache_active_sets_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-CA1: sniffer_set_cache_active(true) → serial_set_rx_timeout(SERIAL_RX_TOUT_SNIFFER)");
    LOG_MESSAGE();

    /*
     * setUp has enabled both ports via sniffer_enable().
     * sniffer_set_cache_active iterates only ports where enabled==false.
     * Disable both ports so the function can act on them, then reset the mock
     * counter before the call under test.
     */
    sniffer_disable(0);
    sniffer_disable(1);
    mock_serial_reset();

    sniffer_set_cache_active(true);

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_serial_set_rx_timeout_data.called,
        "TC-CA1: serial_set_rx_timeout must be called for both ports");
    TEST_ASSERT_EQUAL_MESSAGE(SERIAL_RX_TOUT_SNIFFER,
        mock_serial_set_rx_timeout_data.tout_symbols,
        "TC-CA1: timeout must be SERIAL_RX_TOUT_SNIFFER when active=true");
}

/* ============================================================
 * TC-CA2 — sniffer_set_cache_active(false): sets PROXY RX timeout
 * ============================================================ */

void test_sniffer_set_cache_active_inactive_sets_proxy_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-CA2: sniffer_set_cache_active(false) → serial_set_rx_timeout(SERIAL_RX_TOUT_PROXY)");
    LOG_MESSAGE();

    /*
     * Disable both ports so sniffer_set_cache_active can act on them.
     * cache_multimaster_is_enabled() returns false by default in the mock.
     */
    sniffer_disable(0);
    sniffer_disable(1);
    mock_serial_reset();

    sniffer_set_cache_active(false);

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_serial_set_rx_timeout_data.called,
        "TC-CA2: serial_set_rx_timeout must be called for both ports");
    TEST_ASSERT_EQUAL_MESSAGE(SERIAL_RX_TOUT_PROXY,
        mock_serial_set_rx_timeout_data.tout_symbols,
        "TC-CA2: timeout must be SERIAL_RX_TOUT_PROXY when active=false");
}

/* ============================================================
 * main
 * ============================================================ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_tc1_basic_modbus_rtu_request_response);
    RUN_TEST(test_tc2_fast_modbus_event_poll_no_events);
    RUN_TEST(test_tc3_normal_modbus_not_inverted_after_fm);
    RUN_TEST(test_tc4_fd46_in_res_wait_phase_slip);
    RUN_TEST(test_tc5_all_ff_without_preceding_fd46);
    RUN_TEST(test_tc6_broadcast_does_not_start_res_wait);
    RUN_TEST(test_tc7_timeout_no_response);
    RUN_TEST(test_tc8_fm_subcmd_determines_direction);
    RUN_TEST(test_tc9_scan_response_with_leading_ff);
    RUN_TEST(test_tc10_orphan_fc04_response_at_startup);
    RUN_TEST(test_tc11_orphan_fc01_response_len6);
    RUN_TEST(test_tc12_fc01_len8_treated_as_request);
    RUN_TEST(test_tc13_fc01_len8_data2_3_ambiguous_dropped);
    RUN_TEST(test_tc14_fc05_direction_unknown_dropped);
    RUN_TEST(test_tc15_crc_err_no_sync_dropped);
    RUN_TEST(test_tc16_crc_err_after_sync_alternates_direction);
    RUN_TEST(test_rx_timeout_enable_sets_sniffer_value);
    RUN_TEST(test_rx_timeout_disable_sets_proxy_value);
    RUN_TEST(test_rx_timeout_not_called_after_detach);
    RUN_TEST(test_tc17_recursive_stream_split_two_frames);
    RUN_TEST(test_sniffer_set_cache_active_sets_timeout);
    RUN_TEST(test_sniffer_set_cache_active_inactive_sets_proxy_timeout);

    return UNITY_END();
}
