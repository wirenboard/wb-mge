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
 * TC-18 Multi-master in RES_WAIT — second master request flushes buffered req
 * TC-19 Post-disable timer callback does not emit a packet (race condition guard)
 * TC-20 Fast response: slave responds before timer fires → MASTER+SLAVE pair emitted
 * TC-21 Response after timeout: timer fires first → timeout pkt, then orphan SLAVE pkt
 * TC-22 No response at all: timer fires, slave never comes → exactly one timeout pkt
 * TC-23 Multiple consecutive timeouts: master polls dead slave 3×, 3 timeout pkts
 * TC-24 [DESIRED] Master request emitted immediately upon receipt (before slave responds)
 * TC-25 [DESIRED] Timeout is a separate event emitted AFTER the master packet
 * TC-26 [DESIRED] Slow response: 3 events — MASTER + TIMEOUT + orphan SLAVE
 * TC-27 [DESIRED] Fast response: MASTER emitted immediately, SLAVE emitted on response
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
    sniffer_enable(0, SNIFF_REASON_DISPLAY);
    sniffer_enable(1, SNIFF_REASON_DISPLAY);
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

    /* Master is emitted immediately upon receipt */
    SEND0(req);
    sniff_packet_t pkt0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt0.is_master, "pkt[0] must be MASTER (request)");
    TEST_ASSERT_TRUE_MESSAGE(pkt0.crc_valid, "pkt[0] must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt0.slave_id, "pkt[0] slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt0.function, "pkt[0] function must be 0x03");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /* Response: same slave/func, valid CRC */
    uint8_t res[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};

    /* Only the slave response is emitted now */
    SEND0(res);
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
    /* Master is emitted immediately upon receipt */
    SEND0(p4);
    sniff_packet_t pkt4 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt4.is_master,
        "pkt4 must be MASTER (first in RTU pair after FM block)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt4.slave_id, "pkt4 slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x04, pkt4.function, "pkt4 function must be 0x04");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /* Packet 5: normal Modbus response — only the slave response is emitted */
    uint8_t p5[] = {0x83, 0x04, 0x08, 0x02, 0xE6, 0x00, 0x00, 0x00, 0x00, 0x01, 0x35, 0x41, 0xE7};
    SEND0(p5);

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

    /* Packet 1: normal RTU request — master emitted immediately, enters RES_WAIT */
    uint8_t p1[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(p1);
    {
        sniff_packet_t p1_pkt = dequeue_packet();
        TEST_ASSERT_TRUE_MESSAGE(p1_pkt.is_master,
            "TC-4 p1: request must be emitted as MASTER immediately");
    }
    assert_queue_empty();

    /*
     * Packet 2: FM master arrives in RES_WAIT (subcmd=0x10, not slave).
     * The first master was already emitted; pkt2 is emitted as standalone MASTER.
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
     * classify_direction: FC03, len==8 → DIRECTION_REQUEST → master emitted immediately,
     * then enters RES_WAIT.
     */
    uint8_t p1[] = {0x83, 0x03, 0x00, 0x01, 0x00, 0x04, 0x0B, 0xEB};
    SEND0(p1);
    sniff_packet_t pkt1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt1.is_master,
        "pkt1 (request) must be MASTER (emitted immediately)");
    TEST_ASSERT_TRUE_MESSAGE(pkt1.crc_valid,
        "pkt1 must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, pkt1.slave_id, "pkt1 slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt1.function, "pkt1 function must be 0x03");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /*
     * Packet 2: FF FF FF FF FF
     * In RES_WAIT: strip_arbitration → effective=data (all-FF not stripped), effective_len=5.
     * effective[0]=0xFF ≠ 0xFD → not FM → normal response path.
     * Master was already emitted; only res_pkt (slave) is emitted now.
     * res_pkt = pkt2, is_master=false, crc_valid=false (CRC of FF FF FF ≠ FF FF).
     */
    uint8_t p2[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    SEND0(p2);

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
     * NOT as a response to the broadcast. Master emitted immediately; timer must start.
     */
    int timer_start_before = mock_xTimerStart_called;
    uint8_t p2[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(p2);
    /* Master is emitted immediately */
    sniff_packet_t pkt2 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(pkt2.is_master,
        "TC-6 pkt2: request must be MASTER (emitted immediately)");
    TEST_ASSERT_TRUE_MESSAGE(pkt2.crc_valid,
        "TC-6 pkt2: must have valid CRC");
    /* Queue empty — only master was emitted, no slave yet */
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

    /* Packet 1: 83 04 00 03 00 09 DE 2E — valid CRC, unicast.
     * Master is emitted immediately upon receipt. */
    uint8_t p1[] = {0x83, 0x04, 0x00, 0x03, 0x00, 0x09, 0xDE, 0x2E};
    SEND0(p1);

    /* Master packet emitted immediately */
    sniff_packet_t master_pkt = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(master_pkt.is_master,  "TC-7: MASTER packet must be emitted immediately");
    TEST_ASSERT_FALSE_MESSAGE(master_pkt.is_timeout, "TC-7: immediate packet must NOT be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(master_pkt.crc_valid,  "TC-7: immediate packet must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, master_pkt.slave_id, "TC-7: master slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x04, master_pkt.function, "TC-7: master function must be 0x04");

    /* Timer has not fired yet — queue must be empty */
    assert_queue_empty();

    /* Simulate 200 ms elapsed by firing the timer callback manually */
    timer_cb(MOCK_TIMER_HANDLE);

    /* A separate timeout packet must now be enqueued */
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
 * TC-RX1 — sniffer_enable() does NOT touch the RX timeout
 * (RX timeout is owned by the transport mode, not the overlay)
 * ============================================================ */
void test_rx_timeout_enable_does_not_change_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-RX1: sniffer_enable() must NOT call serial_set_rx_timeout");
    LOG_MESSAGE();

    /* setUp already calls sniffer_enable(0, SNIFF_REASON_DISPLAY), reset counters to test a fresh call */
    mock_serial_reset();

    sniffer_disable(0, SNIFF_REASON_DISPLAY);
    mock_serial_reset();

    sniffer_enable(0, SNIFF_REASON_DISPLAY);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "sniffer_enable must NOT call serial_set_rx_timeout (timeout owned by transport mode)");
}

/* ============================================================
 * TC-RX2 — sniffer_disable() does NOT touch the RX timeout
 * (RX timeout is owned by the transport mode, not the overlay)
 * ============================================================ */
void test_rx_timeout_disable_does_not_change_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-RX2: sniffer_disable() must NOT call serial_set_rx_timeout");
    LOG_MESSAGE();

    mock_serial_reset();
    sniffer_disable(0, SNIFF_REASON_DISPLAY);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "sniffer_disable must NOT call serial_set_rx_timeout (timeout owned by transport mode)");
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

    sniffer_enable(0, SNIFF_REASON_DISPLAY);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "serial_set_rx_timeout must NOT be called after sniffer_detach (enable)");

    sniffer_disable(0, SNIFF_REASON_DISPLAY);
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
     * len=8 → DIRECTION_REQUEST → master emitted immediately, enters RES_WAIT.
     */
    uint8_t pkt2[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(pkt2);
    sniff_packet_t p2 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p2.is_master,
        "TC-10 pkt2: FC03 request must be MASTER");
    TEST_ASSERT_TRUE_MESSAGE(p2.crc_valid,
        "TC-10 pkt2: crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p2.slave_id, "TC-10 pkt2: slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p2.function, "TC-10 pkt2: function must be 0x03");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /*
     * Packet 3: FC03 response (9 bytes, CRC OK).
     * Only the slave response is emitted now.
     */
    uint8_t pkt3[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    SEND0(pkt3);

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
     * Master is emitted immediately upon receipt.
     */
    uint8_t pkt_req[] = {0x83, 0x01, 0x14, 0xB4, 0x00, 0x07, 0x26, 0x3C};
    SEND0(pkt_req);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_master,
        "TC-12: FC01 request must be MASTER (emitted immediately)");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,
        "TC-12: request crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p1.slave_id,
        "TC-12: request slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.function,
        "TC-12: request function must be 0x01");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /*
     * Response: addr=0x83, fc=0x01, bytecount=0x01, data=0x7F, CRC=38 10.
     * Only the slave response is emitted now.
     */
    uint8_t pkt_res[] = {0x83, 0x01, 0x01, 0x7F, 0x38, 0x10};
    SEND0(pkt_res);

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
     * Master is emitted immediately; then after response only the slave is emitted.
     */
    uint8_t pkt_req[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(pkt_req);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_master,
        "TC-14: FC03 request after dropped FC05 must be MASTER (emitted immediately)");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,
        "TC-14: FC03 request crc_valid must be true");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p1.slave_id,
        "TC-14: FC03 request slave_id must be 0x83");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function,
        "TC-14: FC03 request function must be 0x03");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    uint8_t pkt_res[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    SEND0(pkt_res);

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
    /* Master is emitted immediately upon receipt */
    SEND0(req);
    sniff_packet_t p_req = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p_req.is_master, "TC-16: setup req must be MASTER");
    /* Slave has not arrived yet */
    assert_queue_empty();
    SEND0(res);
    /* Dequeue and discard the slave response (synchronization complete) */
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
 * TC-CA1 — enabling the CACHE reason does NOT touch the RX timeout
 * (RX timeout is owned by the transport mode, not the overlay)
 * ============================================================ */

void test_cache_reason_does_not_change_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-CA1: sniffer_enable(SNIFF_REASON_CACHE) must NOT call serial_set_rx_timeout");
    LOG_MESSAGE();

    /* setUp enabled DISPLAY on both ports; clear it so CACHE is a fresh 0->1 edge. */
    sniffer_disable(0, SNIFF_REASON_DISPLAY);
    sniffer_disable(1, SNIFF_REASON_DISPLAY);
    mock_serial_reset();

    sniffer_enable(0, SNIFF_REASON_CACHE);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "TC-CA1: enabling a reason must NOT call serial_set_rx_timeout");
}

/* ============================================================
 * TC-CA2 — clearing the last reason does NOT touch the RX timeout
 * (RX timeout is owned by the transport mode, not the overlay)
 * ============================================================ */

void test_clearing_last_reason_does_not_change_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-CA2: disabling the last reason must NOT call serial_set_rx_timeout");
    LOG_MESSAGE();

    /* Port 0 currently has DISPLAY (from setUp). Disabling it clears the last reason. */
    mock_serial_reset();
    sniffer_disable(0, SNIFF_REASON_DISPLAY);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "TC-CA2: clearing the last reason must NOT call serial_set_rx_timeout");
}

/* ============================================================
 * TC-CA3 — overlapping reasons bitmask transitions never touch the RX timeout
 * Exercises the reasons RMW across add/clear edges; the overlay must stay out
 * of the RX timeout (owned by the transport mode) on every transition.
 * ============================================================ */

void test_overlapping_reasons_keep_timeout_untouched(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-CA3: overlapping DISPLAY+CACHE reason transitions must NOT touch the RX timeout");
    LOG_MESSAGE();

    mock_serial_reset();

    /* Port 0 has DISPLAY from setUp. Add CACHE: bitmask now DISPLAY|CACHE. */
    sniffer_enable(0, SNIFF_REASON_CACHE);
    /* Clear DISPLAY: still CACHE active. */
    sniffer_disable(0, SNIFF_REASON_DISPLAY);
    /* Clear CACHE too: now the last reason clears. */
    sniffer_disable(0, SNIFF_REASON_CACHE);

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_set_rx_timeout_data.called,
        "TC-CA3: no reason transition (add/clear/last-clear) may call serial_set_rx_timeout");
}

/* ============================================================
 * TC-18 — Multi-master in RES_WAIT (Bug #17)
 * ============================================================ */

void test_tc18_multi_master_in_res_wait(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-18: Multi-master in RES_WAIT — second master flushes buffered req and restarts timer");
    LOG_MESSAGE();

    /*
     * Master A request: slave=0x83, FC03, addr=0x0061, count=2, valid CRC.
     * classify_direction: FC03, len==8 → DIRECTION_REQUEST → buffered in RES_WAIT.
     */
    uint8_t req_a[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};

    /*
     * Master B request: slave=0x01, FC03, addr=0x0000, count=2, valid CRC.
     * CRC of {01 03 00 00 00 02} = 0x0BC4 (lo=0xC4, hi=0x0B).
     * classify_direction: FC03, len==8 → DIRECTION_REQUEST.
     * Arrives in RES_WAIT → second master path: flushes buffered req_a, buffers req_b.
     */
    uint8_t req_b[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /*
     * Slave 0x01 response: 4 data bytes, valid CRC.
     * CRC of {01 03 04 00 00 00 00} = 0x33FA (lo=0xFA, hi=0x33).
     * Arrives in RES_WAIT → paired with buffered req_b → state → SNIFF_IDLE.
     */
    uint8_t resp_b[] = {0x01, 0x03, 0x04, 0x00, 0x00, 0x00, 0x00, 0xFA, 0x33};

    /* Step 1: Master A request → emitted immediately, enters RES_WAIT. */
    SEND0(req_a);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_master,
        "TC-18 p1: Master A request must be MASTER (emitted immediately)");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,
        "TC-18 p1: Master A request must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x83, p1.slave_id,
        "TC-18 p1: slave_id must be 0x83 (Master A target)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function,
        "TC-18 p1: function must be FC03");
    assert_queue_empty();

    /* Step 2: Master B request arrives in RES_WAIT.
     * Expected: Master B emitted immediately; timer restarted. */
    int timer_start_before = mock_xTimerStart_called;
    SEND0(req_b);

    /* Master B must be emitted immediately. */
    sniff_packet_t p2 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p2.is_master,
        "TC-18 p2: Master B request must be MASTER (emitted immediately)");
    TEST_ASSERT_TRUE_MESSAGE(p2.crc_valid,
        "TC-18 p2: Master B request must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p2.slave_id,
        "TC-18 p2: slave_id must be 0x01 (Master B target)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p2.function,
        "TC-18 p2: function must be FC03");

    /* Queue must be empty — no slave yet. */
    assert_queue_empty();

    /* Timer must have been restarted (xTimerStart called once more). */
    TEST_ASSERT_EQUAL_MESSAGE(timer_start_before + 1, mock_xTimerStart_called,
        "TC-18: xTimerStart must be called once to restart timer for Master B's request");

    /* Step 3: Slave 0x01 responds → only the slave response is emitted.
     *           State → SNIFF_IDLE. */
    SEND0(resp_b);

    sniff_packet_t p3 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p3.is_master,
        "TC-18 p3: Slave 0x01 response must be SLAVE (is_master=false)");
    TEST_ASSERT_TRUE_MESSAGE(p3.crc_valid,
        "TC-18 p3: Slave 0x01 response must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p3.slave_id,
        "TC-18 p3: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p3.function,
        "TC-18 p3: function must be FC03");

    assert_queue_empty();
}

/* ============================================================
 * TC-19 — Post-disable timer callback does not emit (Bug #3)
 * ============================================================ */

void test_tc19_post_disable_timer_no_spurious_packet(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-19: After sniffer_disable(), stale timer callback must not emit a packet");
    LOG_MESSAGE();

    /*
     * Get the timer callback reference registered by sniffer_init().
     * Both ports use the same resp_timer_cb function.
     * pvTimerGetTimerID is stubbed to always return 0, so the callback acts on port 0.
     */
    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* Send a request to port 0 → master emitted immediately, enters RES_WAIT. */
    uint8_t req[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    SEND0(req);
    {
        sniff_packet_t master_pkt = dequeue_packet();
        TEST_ASSERT_TRUE_MESSAGE(master_pkt.is_master,
            "TC-19: master must be emitted immediately on request");
    }
    assert_queue_empty();

    /* Disable the sniffer before the timer fires.
     * This clears req_len (Bug #3b fix) and sets enabled=false (Bug #3a guard). */
    sniffer_disable(0, SNIFF_REASON_DISPLAY);

    /* Simulate the timer firing anyway (race: callback was already queued
     * in the FreeRTOS timer service queue before xTimerStop was processed). */
    timer_cb(MOCK_TIMER_HANDLE);

    /* Queue must be empty — the guard must prevent the spurious timeout packet. */
    assert_queue_empty();
}

/* ============================================================
 * TC-20 — Fast response (response arrives before timer fires)
 * ============================================================ */

void test_tc20_fast_response_before_timer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-20: Fast response — slave responds before timer fires");
    LOG_MESSAGE();

    /* Request: slave=0x01, FC=03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Master is emitted immediately upon receipt, state enters RES_WAIT */
    SEND0(req);
    sniff_packet_t p0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p0.is_master, "TC-20 p0: must be MASTER (request)");
    TEST_ASSERT_FALSE_MESSAGE(p0.is_timeout, "TC-20 p0: must not be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(p0.crc_valid, "TC-20 p0: must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p0.slave_id, "TC-20 p0: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p0.function, "TC-20 p0: function must be 0x03");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /* Response: slave=0x01, FC=03, bytecount=4, data=0x0001 0x0002, valid CRC */
    uint8_t resp[] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02, 0x2A, 0x32};

    /* Only the slave response is emitted */
    SEND0(resp);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p1.is_master, "TC-20 p1: must be SLAVE (response)");
    TEST_ASSERT_FALSE_MESSAGE(p1.is_timeout, "TC-20 p1: must not be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid, "TC-20 p1: must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.slave_id, "TC-20 p1: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function, "TC-20 p1: function must be 0x03");

    assert_queue_empty();
}

/* ============================================================
 * TC-21 — Response exactly at timer boundary (timer fires, then response arrives)
 * ============================================================ */

void test_tc21_response_arrives_after_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-21: Response after timeout — timer fires first, slave arrives late");
    LOG_MESSAGE();

    /*
     * Get the timer callback registered by sniffer_init().
     * pvTimerGetTimerID is stubbed to always return 0, so callback acts on port 0.
     */
    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* Request: slave=0x01, FC=03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Master is emitted immediately upon receipt, enters RES_WAIT */
    SEND0(req);
    sniff_packet_t master_pkt = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(master_pkt.is_master,   "TC-21: master must be emitted immediately");
    TEST_ASSERT_FALSE_MESSAGE(master_pkt.is_timeout, "TC-21: immediate pkt must NOT be timeout");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, master_pkt.slave_id, "TC-21: master slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, master_pkt.function, "TC-21: master function must be 0x03");

    /* Queue empty before timer fires */
    assert_queue_empty();

    /* Timer fires before slave responds → separate timeout packet emitted */
    timer_cb(MOCK_TIMER_HANDLE);

    /* Dequeue the timeout packet (carries the buffered request data) */
    sniff_packet_t p0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p0.is_timeout, "TC-21 p0: must be a timeout packet");
    TEST_ASSERT_TRUE_MESSAGE(p0.is_master, "TC-21 p0: timeout pkt must be MASTER");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p0.slave_id, "TC-21 p0: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p0.function, "TC-21 p0: function must be 0x03");

    /* After the timeout, slave has not yet arrived — queue must be empty */
    assert_queue_empty();

    /* Response: slave=0x01, FC=03, bytecount=4, data=0x0001 0x0002, valid CRC.
     * State is now SNIFF_IDLE, so this is an orphan response (DIRECTION_RESPONSE branch)
     * and is emitted as a standalone SLAVE packet. */
    uint8_t resp[] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02, 0x2A, 0x32};
    SEND0(resp);

    /* Orphan slave response packet */
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p1.is_master, "TC-21 p1: orphan response must be SLAVE");
    TEST_ASSERT_FALSE_MESSAGE(p1.is_timeout, "TC-21 p1: orphan response must not be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid, "TC-21 p1: orphan response must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.slave_id, "TC-21 p1: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function, "TC-21 p1: function must be 0x03");

    assert_queue_empty();
}

/* ============================================================
 * TC-22 — No response at all (timer fires, slave never arrives)
 * ============================================================ */

void test_tc22_no_response_at_all(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-22: No response at all — timer fires, slave never comes");
    LOG_MESSAGE();

    /*
     * Get the timer callback registered by sniffer_init().
     * pvTimerGetTimerID is stubbed to always return 0, so callback acts on port 0.
     */
    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* Request: slave=0x01, FC=03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Master is emitted immediately upon receipt, enters RES_WAIT */
    SEND0(req);
    sniff_packet_t master_pkt = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(master_pkt.is_master,   "TC-22: master must be emitted immediately");
    TEST_ASSERT_FALSE_MESSAGE(master_pkt.is_timeout, "TC-22: immediate pkt must NOT be timeout");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, master_pkt.slave_id, "TC-22: master slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, master_pkt.function, "TC-22: master function must be 0x03");

    /* Queue empty before timer fires */
    assert_queue_empty();

    /* Timer fires → separate timeout packet emitted, state → SNIFF_IDLE */
    timer_cb(MOCK_TIMER_HANDLE);

    /* Exactly one timeout packet */
    sniff_packet_t p0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p0.is_timeout, "TC-22 p0: must be a timeout packet");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p0.slave_id, "TC-22 p0: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p0.function, "TC-22 p0: function must be 0x03");

    /* No slave response ever arrives — queue must remain empty */
    assert_queue_empty();
}

/* ============================================================
 * TC-23 — Multiple consecutive timeouts (master keeps polling a dead slave)
 * ============================================================ */

void test_tc23_multiple_consecutive_timeouts(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-23: Multiple consecutive timeouts — master polls dead slave 3 times");
    LOG_MESSAGE();

    /*
     * Get the timer callback registered by sniffer_init().
     * pvTimerGetTimerID is stubbed to always return 0, so callback acts on port 0.
     */
    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* Request: slave=0x01, FC=03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Repeat the request→timeout cycle 3 times.
     * Each iteration: req is sent in IDLE → master emitted immediately, enters RES_WAIT.
     * Timer fires → separate timeout packet emitted.
     * Dequeue both packets so the queue is empty before the next iteration. */
    for (int i = 0; i < 3; i++) {
        SEND0(req);

        /* Master is emitted immediately */
        sniff_packet_t master_pkt = dequeue_packet();
        TEST_ASSERT_TRUE_MESSAGE(master_pkt.is_master,
            "TC-23: each SEND0 must emit a MASTER packet immediately");
        TEST_ASSERT_FALSE_MESSAGE(master_pkt.is_timeout,
            "TC-23: immediate packet must NOT be a timeout");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, master_pkt.slave_id,
            "TC-23: master slave_id must be 0x01");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, master_pkt.function,
            "TC-23: master function must be 0x03");

        /* Queue empty before timer fires */
        assert_queue_empty();

        timer_cb(MOCK_TIMER_HANDLE);

        /* Separate timeout packet */
        sniff_packet_t pkt = dequeue_packet();
        TEST_ASSERT_TRUE_MESSAGE(pkt.is_timeout,
            "TC-23: each timeout must be a separate timeout packet");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, pkt.slave_id,
            "TC-23: slave_id must be 0x01");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt.function,
            "TC-23: function must be 0x03");
    }

    /* All packets have been consumed — queue must now be empty */
    assert_queue_empty();
}

/* ============================================================
 * TC-24 — Master request emitted immediately upon receipt
 *
 * Verifies that sniffer_process() enqueues the master packet immediately
 * when a DIRECTION_REQUEST packet arrives in SNIFF_IDLE state, without
 * waiting for a slave response or timer expiry.
 * ============================================================ */

void test_tc24_master_emitted_immediately(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-24: Master request emitted immediately upon receipt");
    LOG_MESSAGE();

    /* FC03 request: slave=0x01, func=0x03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Master packet must be enqueued immediately without waiting for
     * the slave response or timer expiry. */
    SEND0(req);
    sniff_packet_t pkt = dequeue_packet();

    TEST_ASSERT_TRUE_MESSAGE(pkt.is_master,    "TC-24: packet must be MASTER");
    TEST_ASSERT_FALSE_MESSAGE(pkt.is_timeout,  "TC-24: packet must NOT be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(pkt.crc_valid,    "TC-24: packet must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, pkt.slave_id, "TC-24: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, pkt.function, "TC-24: function must be 0x03");

    /* Slave has not arrived yet and timer has not fired — queue must be empty */
    assert_queue_empty();
}

/* ============================================================
 * TC-25 — Timeout is a separate event emitted AFTER the master packet
 *
 * Verifies that when the response timer fires without a slave reply, a separate
 * timeout packet is enqueued after the master packet that was already emitted
 * immediately upon receipt of the request.
 * ============================================================ */

void test_tc25_timeout_is_separate_after_master(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-25: Timeout is a separate event emitted AFTER the master packet");
    LOG_MESSAGE();

    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* FC03 request: slave=0x01, func=0x03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Step 1 — SEND0(req): master packet is emitted immediately upon receipt. */
    SEND0(req);
    sniff_packet_t p0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p0.is_master,   "TC-25 p0: must be MASTER");
    TEST_ASSERT_FALSE_MESSAGE(p0.is_timeout, "TC-25 p0: must NOT be a timeout");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p0.slave_id, "TC-25 p0: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p0.function, "TC-25 p0: function must be 0x03");

    /* Timer has not fired yet — only the master packet was emitted */
    assert_queue_empty();

    /* Step 2 — timer fires: desired behavior emits a SEPARATE timeout packet.
     * The master was already emitted; this packet carries only the timeout flag. */
    timer_cb(MOCK_TIMER_HANDLE);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_timeout,  "TC-25 p1: must be a timeout packet");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.slave_id, "TC-25 p1: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function, "TC-25 p1: function must be 0x03");

    /* Total: exactly 2 packets (master + timeout), no more */
    assert_queue_empty();
}

/* ============================================================
 * TC-26 — Slow response: 3 events — MASTER + TIMEOUT + orphan SLAVE
 *
 * Verifies that when the slave responds after the timer has already fired,
 * three separate packets are produced: the master (emitted immediately),
 * a timeout packet (emitted when the timer fires), and an orphan slave
 * packet (emitted when the late response arrives).
 * ============================================================ */

void test_tc26_slow_response_three_events(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-26: Slow response — MASTER + TIMEOUT + orphan SLAVE");
    LOG_MESSAGE();

    TimerCallbackFunction_t timer_cb = mock_xTimerCreate_pxCallbackFunction;
    TEST_ASSERT_NOT_NULL_MESSAGE(timer_cb,
        "resp_timer_cb must have been registered by sniffer_init()");

    /* FC03 request: slave=0x01, func=0x03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Event 1 — master emitted immediately on request. */
    SEND0(req);
    sniff_packet_t p0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p0.is_master,   "TC-26 p0: must be MASTER");
    TEST_ASSERT_FALSE_MESSAGE(p0.is_timeout, "TC-26 p0: must NOT be a timeout");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p0.slave_id, "TC-26 p0: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p0.function, "TC-26 p0: function must be 0x03");

    /* Event 2 — timer fires before slave responds: separate timeout packet */
    timer_cb(MOCK_TIMER_HANDLE);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p1.is_timeout,  "TC-26 p1: must be a timeout packet");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.slave_id, "TC-26 p1: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function, "TC-26 p1: function must be 0x03");

    /* Event 3 — slave arrives after timeout: emitted as orphan SLAVE packet.
     * FC03 response: slave=0x01, bytecount=4, data=0x0001 0x0002, valid CRC */
    uint8_t resp[] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02, 0x2A, 0x32};
    SEND0(resp);
    sniff_packet_t p2 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p2.is_master,  "TC-26 p2: orphan response must be SLAVE");
    TEST_ASSERT_FALSE_MESSAGE(p2.is_timeout, "TC-26 p2: orphan response must NOT be timeout");
    TEST_ASSERT_TRUE_MESSAGE(p2.crc_valid,   "TC-26 p2: orphan response must have valid CRC");

    /* Total: exactly 3 packets */
    assert_queue_empty();
}

/* ============================================================
 * TC-27 — Fast response: MASTER emitted immediately, SLAVE on response
 *
 * Verifies that when the slave responds before the timer fires, exactly two
 * packets are produced: the master (emitted immediately upon receipt of the
 * request) and the slave (emitted when the response arrives).
 * ============================================================ */

void test_tc27_fast_response_master_immediate_slave_on_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-27: Fast response — MASTER emitted immediately, SLAVE on response");
    LOG_MESSAGE();

    /* FC03 request: slave=0x01, func=0x03, addr=0x0000, count=0x0002, valid CRC */
    uint8_t req[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

    /* Step 1 — SEND0(req): master packet is emitted immediately upon receipt. */
    SEND0(req);
    sniff_packet_t p0 = dequeue_packet();
    TEST_ASSERT_TRUE_MESSAGE(p0.is_master,   "TC-27 p0: must be MASTER");
    TEST_ASSERT_FALSE_MESSAGE(p0.is_timeout, "TC-27 p0: must NOT be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(p0.crc_valid,   "TC-27 p0: must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p0.slave_id, "TC-27 p0: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p0.function, "TC-27 p0: function must be 0x03");

    /* Slave has not arrived yet — queue must be empty */
    assert_queue_empty();

    /* Step 2 — slave responds in time (before timer fires): emitted as SLAVE packet.
     * FC03 response: slave=0x01, bytecount=4, data=0x0001 0x0002, valid CRC */
    uint8_t resp[] = {0x01, 0x03, 0x04, 0x00, 0x01, 0x00, 0x02, 0x2A, 0x32};
    SEND0(resp);
    sniff_packet_t p1 = dequeue_packet();
    TEST_ASSERT_FALSE_MESSAGE(p1.is_master,  "TC-27 p1: must be SLAVE (response)");
    TEST_ASSERT_FALSE_MESSAGE(p1.is_timeout, "TC-27 p1: must NOT be a timeout");
    TEST_ASSERT_TRUE_MESSAGE(p1.crc_valid,   "TC-27 p1: must have valid CRC");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x01, p1.slave_id, "TC-27 p1: slave_id must be 0x01");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x03, p1.function, "TC-27 p1: function must be 0x03");

    /* Total: exactly 2 packets (master + slave), no more */
    assert_queue_empty();
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
    RUN_TEST(test_rx_timeout_enable_does_not_change_timeout);
    RUN_TEST(test_rx_timeout_disable_does_not_change_timeout);
    RUN_TEST(test_rx_timeout_not_called_after_detach);
    RUN_TEST(test_tc17_recursive_stream_split_two_frames);
    RUN_TEST(test_cache_reason_does_not_change_timeout);
    RUN_TEST(test_clearing_last_reason_does_not_change_timeout);
    RUN_TEST(test_overlapping_reasons_keep_timeout_untouched);
    RUN_TEST(test_tc18_multi_master_in_res_wait);
    RUN_TEST(test_tc19_post_disable_timer_no_spurious_packet);
    RUN_TEST(test_tc20_fast_response_before_timer);
    RUN_TEST(test_tc21_response_arrives_after_timeout);
    RUN_TEST(test_tc22_no_response_at_all);
    RUN_TEST(test_tc23_multiple_consecutive_timeouts);
    RUN_TEST(test_tc24_master_emitted_immediately);
    RUN_TEST(test_tc25_timeout_is_separate_after_master);
    RUN_TEST(test_tc26_slow_response_three_events);
    RUN_TEST(test_tc27_fast_response_master_immediate_slave_on_response);

    return UNITY_END();
}
