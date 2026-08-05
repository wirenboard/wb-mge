// Unit tests for repeater.c
// Tests transparent serial<->serial forwarding, byte/drop counters, the active
// flag, deinit accounting and per-session counter reset.

#include "unity.h"
#include "console_log.h"

#include "repeater.h"
#include "bridge.h"
#include "mock_serial.h"
#include "call_sequence.h"   // call_sequence_get_call_id (drain-wait ordering)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"   // mock_freertos_task_reset (FreeRTOS task mock)
#include "freertos/semphr.h" // mock semaphore symbols (mutex-create / give call seq)
#include "esp_timer.h"       // mock esp_timer_get_time (controls uptime)

#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a minimal serial_config_t passed through to serial_init().
static serial_config_t make_serial_config(void)
{
    serial_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port_num = 1;
    cfg.tx_pin   = 10;
    cfg.rx_pin   = 9;
    cfg.dir_pin  = 4;
    cfg.baudrate = 9600;
    return cfg;
}

// Init both ports in repeater mode; store the returned descriptors.
static void init_both_ports(serial_desc_t **d0, serial_desc_t **d1)
{
    serial_config_t cfg0 = make_serial_config();
    serial_config_t cfg1 = make_serial_config();
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(0, &cfg0, d0));
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(1, &cfg1, d1));
    TEST_ASSERT_NOT_NULL(*d0);
    TEST_ASSERT_NOT_NULL(*d1);
}

// ---------------------------------------------------------------------------
// setUp / tearDown
// ---------------------------------------------------------------------------

// rs485_stats mock: per-port RX/TX activity notifications recorded by the mock.
extern int mock_rs485_busy_monitor_activity_called[BRIDGES_COUNT];
void mock_rs485_stats_reset(void);

void setUp(void)
{
    repeater_reset_for_test();
    mock_serial_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    mock_esp_timer_reset();
    mock_rs485_stats_reset();
}

void tearDown(void)
{
}

// ---------------------------------------------------------------------------
// Test: forwarding port 0 -> port 1 calls serial_send on peer + bytes_1to2++
// ---------------------------------------------------------------------------
void test_forward_port0_to_port1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: forward port0 -> port1");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    // serial_send went to the peer (port 1) descriptor.
    TEST_ASSERT_EQUAL(1, mock_serial_calls.send_called);
    TEST_ASSERT_EQUAL_PTR(d1, mock_serial_calls.send_last_desc);
    TEST_ASSERT_EQUAL(sizeof(payload), mock_serial_calls.send_last_len);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_2to1);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_1);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_2);
}

// ---------------------------------------------------------------------------
// Test: forwarding port 1 -> port 0 increments bytes_2to1
// ---------------------------------------------------------------------------
void test_forward_port1_to_port0(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    mock_serial_registered_handler(d1, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(1, mock_serial_calls.send_called);
    TEST_ASSERT_EQUAL_PTR(d0, mock_serial_calls.send_last_desc);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.bytes_2to1);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_1to2);
}

// ---------------------------------------------------------------------------
// Test: peer with TX disabled -> bytes counted as dropped, no send, no TX activity
// serial_send() returns ESP_OK without transmitting when desc->tx_disabled is set, so the
// repeater must not report those bytes as forwarded (nor blink the peer's TX indicator).
// ---------------------------------------------------------------------------
void test_drop_when_peer_tx_disabled(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: peer TX disabled -> dropped, not forwarded");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    // The user turned "Disable transmission (TX)" on for port 2, the peer of port 1.
    TEST_ASSERT_EQUAL(ESP_OK, serial_set_tx_disabled(d1, true));

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    // The send is skipped entirely — serial_send() would have swallowed the data and said ESP_OK.
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.send_called,
        "no serial_send() on a peer whose TX is disabled");

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, st.bytes_1to2,
        "bytes that never reach the wire must not be counted as forwarded");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(sizeof(payload), st.dropped_1,
        "bytes lost because the peer cannot transmit must be counted as dropped");

    // RX activity on the receiving port is real; TX activity on the peer is not.
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_rs485_busy_monitor_activity_called[0],
        "RX activity must still be reported on the receiving port");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_rs485_busy_monitor_activity_called[1],
        "no TX activity may be reported on a peer that transmitted nothing");
}

// ---------------------------------------------------------------------------
// C5: the residual logical window left open on purpose after the flag was made atomic
// ---------------------------------------------------------------------------
// Drives one of the three timings catalogued at the pre-check in repeater_rx_handler(): TX still enabled
// when the repeater decides to forward, parked once the send is already running. The mock returning ESP_OK
// is faithful — by then the real serial_send() is past its own check, and uart_write_bytes() returns ESP_OK
// for a write that fit. The wire is NOT silent for this timing: the bytes were already going out, so parking
// DE cuts the frame off rather than un-sending it, and the peer gets a truncated frame the repeater still
// books as forwarded. Asserting that is not blessing a bug — the truncation is inherent to cutting TX
// mid-frame, and one misattributed frame is the accepted, bounded price of leaving serial_send()'s return
// contract alone. If this starts failing because the frame lands in dropped_1, the decision has moved into
// serial_send(), which is the proper fix.
static void park_peer_tx_inside_send(serial_desc_t *dest, uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    // The repeater has already read the flag, dropped s_lock and committed to the forward:
    // this runs where the real serial_send() sits in uart_write_bytes().
    TEST_ASSERT_EQUAL(ESP_OK, serial_set_tx_disabled(dest, true));
}

void test_peer_tx_disabled_after_the_check_counts_forwarded(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test: peer TX parked mid-send -> still counted forwarded (C5 residual window)");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    mock_serial_calls.send_hook = park_peer_tx_inside_send;

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    mock_serial_calls.send_hook = NULL;

    // The hook ran, i.e. the repeater really did get past its pre-check and into the send.
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_serial_calls.send_called,
        "the forward must reach serial_send() — the pre-check saw TX still enabled");
    TEST_ASSERT_TRUE_MESSAGE(serial_tx_disabled(d1),
        "the hook must have parked the peer's TX inside the send window");

    // The documented outcome of the window: forwarded, not dropped.
    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(sizeof(payload), st.bytes_1to2,
        "a frame whose peer was parked after the check is still booked as forwarded");
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, st.dropped_1,
        "nothing is booked as dropped — serial_send() returns ESP_OK once it is past its own check");

    // Whatever the accounting, the in-flight guard is balanced: the peer descriptor is
    // releasable again, so a concurrent repeater_deinit_port(1) would not be blocked.
    TEST_ASSERT_EQUAL_MESSAGE(0, repeater_get_inflight_for_test(1),
        "the in-flight guard must be released even when the flag flipped mid-send");
}

// ---------------------------------------------------------------------------
// Test: peer not in repeater mode (NULL serial_desc) -> bytes counted as dropped
// ---------------------------------------------------------------------------
void test_drop_when_peer_not_inited(void)
{
    serial_config_t cfg0 = make_serial_config();
    serial_desc_t *d0 = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(0, &cfg0, &d0));
    TEST_ASSERT_NOT_NULL(d0);

    uint8_t payload[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    // No peer descriptor -> serial_send must NOT be called.
    TEST_ASSERT_EQUAL(0, mock_serial_calls.send_called);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.dropped_1);
}

// ---------------------------------------------------------------------------
// Test: when serial_send to the peer fails, bytes are counted as dropped
// ---------------------------------------------------------------------------
void test_drop_when_peer_send_fails(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    mock_serial_calls.send_ret = ESP_FAIL;

    uint8_t payload[] = {0x01, 0x02};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    TEST_ASSERT_EQUAL(1, mock_serial_calls.send_called);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.dropped_1);
}

// ---------------------------------------------------------------------------
// Test: active flag is true only when BOTH ports are inited
// ---------------------------------------------------------------------------
void test_active_requires_both_ports(void)
{
    repeater_stats_t st = {0};

    // No ports -> inactive.
    repeater_get_stats(&st);
    TEST_ASSERT_FALSE(st.active);

    serial_config_t cfg0 = make_serial_config();
    serial_desc_t *d0 = NULL;
    repeater_init_port(0, &cfg0, &d0);

    // One port -> still inactive.
    repeater_get_stats(&st);
    TEST_ASSERT_FALSE(st.active);

    serial_config_t cfg1 = make_serial_config();
    serial_desc_t *d1 = NULL;
    repeater_init_port(1, &cfg1, &d1);

    // Both ports -> active.
    repeater_get_stats(&st);
    TEST_ASSERT_TRUE(st.active);
}

// ---------------------------------------------------------------------------
// Test: deinit decrements active count (active flag clears, serial_deinit called)
// ---------------------------------------------------------------------------
void test_deinit_decrements_active(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_TRUE(st.active);

    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(1));
    TEST_ASSERT_EQUAL(1, mock_serial_calls.deinit_called);

    repeater_get_stats(&st);
    TEST_ASSERT_FALSE(st.active);

    // Deinit on an already-down port is a graceful no-op (no extra serial_deinit).
    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(1));
    TEST_ASSERT_EQUAL(1, mock_serial_calls.deinit_called);
}

// ---------------------------------------------------------------------------
// Test: counters reset on a fresh session (init when active_count was 0)
// ---------------------------------------------------------------------------
void test_counters_reset_on_fresh_session(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    uint8_t payload[] = {0x01, 0x02, 0x03};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.bytes_1to2);

    // Tear both ports down -> active_count back to 0.
    repeater_deinit_port(0);
    repeater_deinit_port(1);

    // Re-init: first init with active_count==0 starts a fresh session and
    // clears the accumulated counters.
    serial_config_t cfg0 = make_serial_config();
    serial_desc_t *nd0 = NULL;
    repeater_init_port(0, &cfg0, &nd0);

    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_1);
}

// ---------------------------------------------------------------------------
// Test: uptime tracks elapsed ticks while at least one port is active
// ---------------------------------------------------------------------------
void test_uptime_tracks_elapsed_time(void)
{
    // Start the session with esp_timer at 0 us; the session captures this snapshot.
    mock_esp_timer_get_time_value = 0;
    serial_config_t cfg0 = make_serial_config();
    serial_desc_t *d0 = NULL;
    repeater_init_port(0, &cfg0, &d0);

    // Advance esp_timer to 3,250,000 us: uptime_ms = 3250 ms.
    mock_esp_timer_get_time_value = 3250000;

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(3250, st.uptime_ms,
        "uptime_ms must report elapsed milliseconds since forwarding started");

    // After all ports are down, uptime reports 0.
    repeater_deinit_port(0);
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.uptime_ms);
}

// ---------------------------------------------------------------------------
// Test: invalid arguments are rejected
// ---------------------------------------------------------------------------
void test_init_invalid_args(void)
{
    serial_config_t cfg = make_serial_config();
    serial_desc_t *d = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, repeater_init_port(BRIDGES_COUNT, &cfg, &d));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, repeater_init_port(0, &cfg, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, repeater_deinit_port(BRIDGES_COUNT));
}

// ---------------------------------------------------------------------------
// Test: serial_init failure surfaces as ESP_FAIL and does not bump active
// ---------------------------------------------------------------------------
void test_init_serial_fail(void)
{
    mock_serial_calls.init_should_fail = true;
    serial_config_t cfg = make_serial_config();
    serial_desc_t *d = NULL;
    TEST_ASSERT_EQUAL(ESP_FAIL, repeater_init_port(0, &cfg, &d));

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_FALSE(st.active);
    TEST_ASSERT_EQUAL_UINT64(0, st.uptime_ms);
}

// ---------------------------------------------------------------------------
// U-R1: repeater_init() creates the global lock exactly once
// ---------------------------------------------------------------------------
void test_init_creates_mutex_once(void)
{
    // R1 lets s_lock start NULL each test. First init creates the lock; a second
    // init must NOT recreate it (guards the s_lock==NULL check — its removal would
    // leak the mutex and drop protection of live ports).
    repeater_init();
    TEST_ASSERT_EQUAL(1, mock_xSemaphoreCreateMutex_called);
    repeater_init();
    TEST_ASSERT_EQUAL(1, mock_xSemaphoreCreateMutex_called);
}

// ---------------------------------------------------------------------------
// U-R2: re-initing the same port is idempotent (same desc, no new serial, no
// double-count of active_count)
// ---------------------------------------------------------------------------
void test_double_init_same_port_is_idempotent(void)
{
    mock_esp_timer_get_time_value = 0;
    serial_config_t cfg0 = make_serial_config();
    serial_config_t cfg1 = make_serial_config();
    serial_desc_t *d0a = NULL, *d0b = NULL, *d1 = NULL;

    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(0, &cfg0, &d0a));
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(1, &cfg1, &d1));
    TEST_ASSERT_EQUAL(2, mock_serial_calls.init_called);   // one serial_init per distinct port

    // Re-init port 0: must hand back the SAME descriptor and NOT open a new serial.
    serial_config_t cfg0b = make_serial_config();
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(0, &cfg0b, &d0b));
    TEST_ASSERT_EQUAL_PTR(d0a, d0b);
    TEST_ASSERT_EQUAL(2, mock_serial_calls.init_called);   // no third serial_init

    // active_count must not have been double-counted: one deinit per port returns it
    // to 0, so active is false and uptime is forced to 0 even though time advanced.
    // (If port 0 had been double-counted, count would still be 1 here → uptime > 0.)
    mock_esp_timer_get_time_value = 1000000;
    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(0));
    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(1));

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_FALSE(st.active);
    TEST_ASSERT_EQUAL_UINT64(0, st.uptime_ms);
}

// ---------------------------------------------------------------------------
// U-R3: an unknown descriptor in the rx handler is dropped early, no side effects
// ---------------------------------------------------------------------------
void test_rx_handler_unknown_desc_ignored(void)
{
    serial_config_t cfg0 = make_serial_config();
    serial_desc_t *d0 = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(0, &cfg0, &d0));

    serial_desc_t bogus;   // a descriptor never registered with the repeater
    uint8_t payload[] = {0x11, 0x22, 0x33};
    mock_serial_registered_handler(&bogus, payload, sizeof(payload));

    // Unknown desc → early return: nothing forwarded, no counter touched.
    TEST_ASSERT_EQUAL(0, mock_serial_calls.send_called);
    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_2to1);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_1);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_2);
}

// ---------------------------------------------------------------------------
// U-R4: serial_deinit() runs AFTER xSemaphoreGive() (lock-order invariant)
// ---------------------------------------------------------------------------
void test_deinit_serial_deinit_runs_after_unlock(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(0));

    // serial_deinit() must be invoked only after the lock was released; if it were
    // moved back inside the critical section it would deadlock against the UART task
    // waiting on s_lock inside repeater_rx_handler().
    TEST_ASSERT_EQUAL(1, mock_serial_calls.deinit_called);
    TEST_ASSERT_TRUE(mock_serial_calls.deinit_call_seq > mock_xSemaphoreGive_call_seq);
}

// ---------------------------------------------------------------------------
// U-R5: the forward runs with the repeater lock RELEASED, guarded by the
// per-destination in-flight counter instead
// ---------------------------------------------------------------------------

// Set by the hook below so the test can prove the observation actually happened.
static bool s_send_window_observed;

// Runs INSIDE the mock serial_send(), i.e. where the real one sits in uart_write_bytes().
static void observe_send_window(serial_desc_t *dest, uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    s_send_window_observed = true;

    TEST_ASSERT_EQUAL_PTR(mock_serial_get_desc(1), dest);

    // The property under test. mock_xSemaphore_held_count is takes minus gives, so a
    // non-zero value here means the repeater is holding s_lock across a call that blocks
    // until the peer's TX ring drains — which is what stalled GET /info and POST
    // /ports/N/mode for up to two seconds.
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xSemaphore_held_count,
        "serial_send() must run with the repeater lock released");

    // ...and what replaces lock possession as the anti-use-after-free guard: the
    // DESTINATION port is registered as in-flight for the whole send.
    TEST_ASSERT_EQUAL_MESSAGE(1, repeater_get_inflight_for_test(1),
        "the destination port must be registered as in-flight across the send");
    TEST_ASSERT_EQUAL_MESSAGE(0, repeater_get_inflight_for_test(0),
        "the in-flight guard is indexed by destination port, not by source");
}

void test_send_runs_with_lock_released(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: forward runs outside the repeater lock");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    s_send_window_observed = false;
    mock_serial_calls.send_hook = observe_send_window;

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    mock_serial_calls.send_hook = NULL;
    TEST_ASSERT_TRUE_MESSAGE(s_send_window_observed, "the send hook never ran");

    // The guard is released once the send returns, and the lock is not left held.
    TEST_ASSERT_EQUAL(0, repeater_get_inflight_for_test(1));
    TEST_ASSERT_EQUAL(0, mock_xSemaphore_held_count);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_1);
}

// ---------------------------------------------------------------------------
// U-R5b: the peer-pointer read and the in-flight increment share ONE critical
// section
// ---------------------------------------------------------------------------
// The test above cannot see this: inside the send the held count is 0 and the in-flight count
// is 1 whether or not the lock was dropped between reading s_ctx[peer].serial_desc and
// incrementing s_inflight[peer]. Splitting them is the broken case repeater_rx_handler()
// documents — the pointer read before repeater_deinit_port(peer) NULLs it, the increment after
// its drain wait has already finished, i.e. a send into a descriptor that is being freed.
// Counting acquisitions is what pins it down: one forward takes the repeater lock exactly
// twice (read + register, then release + account), a split first section makes it three.
#define TEST_FORWARD_LOCK_TAKES     2

void test_forward_takes_one_critical_section_before_the_send(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: peer read and in-flight increment are one critical section");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    int takes_before = mock_xSemaphoreTake_called;

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    TEST_ASSERT_EQUAL_MESSAGE(TEST_FORWARD_LOCK_TAKES, mock_xSemaphoreTake_called - takes_before,
        "the peer-pointer read and the in-flight increment must stay in ONE critical section: "
        "a forward takes the repeater lock twice, once before the send and once after");
    TEST_ASSERT_EQUAL(0, mock_xSemaphore_held_count);
}

// ---------------------------------------------------------------------------
// U-R6: repeater_deinit_port() waits for in-flight forwards INTO that port
// before calling serial_deinit() (which frees the descriptor)
// ---------------------------------------------------------------------------

// Polls the staged sender stays "on the wire" for before it finishes.
#define TEST_DRAIN_POLLS    3

static bool     s_drain_hook_armed;
static unsigned s_inflight_release_seq;   // call id at which the staged sender finished

// Runs inside xSemaphoreTake(). Once armed, every acquisition is either the deinit's own
// critical section or one poll of its drain wait.
static void drain_wait_hook(void)
{
    if (!s_drain_hook_armed) {
        return;
    }

    // The core assertion: while a forward into port 1 is still in flight, port 1's
    // descriptor must not have been handed to serial_deinit().
    if (repeater_get_inflight_for_test(1) > 0) {
        TEST_ASSERT_EQUAL_MESSAGE(0, mock_serial_calls.deinit_called,
            "serial_deinit() ran while a forward into that port was still in flight");
    }

    // Stand in for the other port's UART task returning from uart_write_bytes() and
    // decrementing the guard, a few polls into the wait.
    if ((s_inflight_release_seq == 0) && (mock_vTaskDelay_data.called >= TEST_DRAIN_POLLS)) {
        repeater_set_inflight_for_test(1, 0);
        s_inflight_release_seq = call_sequence_get_call_id();
    }
}

void test_deinit_waits_for_inflight_forward(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: deinit drains in-flight forwards first");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    // Port 0's UART task is inside serial_send() writing into port 1 — the state its
    // handler leaves behind after it releases the lock. (Staged rather than driven through
    // the mock serial_send(): the test has one thread, and the sender would have to be
    // blocked in the send while repeater_deinit_port() runs.)
    repeater_set_inflight_for_test(1, 1);

    s_inflight_release_seq = 0;
    s_drain_hook_armed = true;
    mock_xSemaphoreTake_hook = drain_wait_hook;

    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(1));

    mock_xSemaphoreTake_hook = NULL;
    s_drain_hook_armed = false;

    // The deinit polled instead of freeing the descriptor straight away...
    TEST_ASSERT_EQUAL_MESSAGE(TEST_DRAIN_POLLS, mock_vTaskDelay_data.called,
        "deinit must poll the in-flight count until the forward drains");
    TEST_ASSERT_TRUE_MESSAGE(mock_vTaskDelay_data.xTicksToDelay > 0,
        "the drain wait must yield the CPU between polls, not spin");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, s_inflight_release_seq,
        "the staged forward was never released — the wait did not run");

    // ...and only then called it, after the sender was done with the descriptor.
    TEST_ASSERT_EQUAL(1, mock_serial_calls.deinit_called);
    TEST_ASSERT_TRUE_MESSAGE(mock_serial_calls.deinit_call_seq > s_inflight_release_seq,
        "serial_deinit() must run after the last in-flight forward finished");
}

// ---------------------------------------------------------------------------
// U-R7: the in-flight guard is released on every outcome, and the four
// forwarded/dropped outcomes keep counting exactly as before
// ---------------------------------------------------------------------------
void test_inflight_released_for_every_outcome(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};

    // (1) forwarded
    mock_serial_registered_handler(d0, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(0, repeater_get_inflight_for_test(1), "guard leaked after a forward");

    // (2) the send failed
    mock_serial_calls.send_ret = ESP_FAIL;
    mock_serial_registered_handler(d0, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(0, repeater_get_inflight_for_test(1), "guard leaked after a failed send");
    mock_serial_calls.send_ret = ESP_OK;

    // (3) peer TX disabled — no send, so nothing may be registered either
    TEST_ASSERT_EQUAL(ESP_OK, serial_set_tx_disabled(d1, true));
    mock_serial_registered_handler(d0, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(0, repeater_get_inflight_for_test(1), "guard raised for a peer that cannot transmit");
    TEST_ASSERT_EQUAL(ESP_OK, serial_set_tx_disabled(d1, false));

    // (4) peer not in repeater mode
    TEST_ASSERT_EQUAL(ESP_OK, repeater_deinit_port(1));
    mock_serial_registered_handler(d0, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_MESSAGE(0, repeater_get_inflight_for_test(1), "guard raised for a NULL peer");

    // One forward, three losses — the split between bytes and dropped is unchanged by the
    // in-flight guard, and every loss is attributed to the port that RECEIVED the bytes.
    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(sizeof(payload), st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(3 * sizeof(payload), st.dropped_1);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_2to1);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_2);
}

// ---------------------------------------------------------------------------
// Test: repeater registers a drop handler on each port's descriptor
// ---------------------------------------------------------------------------
void test_drop_handler_registered_on_ports(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test: drop handler registered on both ports");
    LOG_MESSAGE();

    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    TEST_ASSERT_NOT_NULL_MESSAGE(d0->drop_handler, "Port 0 should have a drop handler registered");
    TEST_ASSERT_NOT_NULL_MESSAGE(d1->drop_handler, "Port 1 should have a drop handler registered");
}

// ---------------------------------------------------------------------------
// Test: RX-stage drops accumulate into dropped_1 / dropped_2 per port
// ---------------------------------------------------------------------------
void test_drop_handler_accumulates_per_port(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    d0->drop_handler(d0, 50);
    d1->drop_handler(d1, 30);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(50, st.dropped_1);
    TEST_ASSERT_EQUAL_UINT64(30, st.dropped_2);
}

// ---------------------------------------------------------------------------
// Test: RX-stage drops add on top of forward-failure drops, leaving bytes intact
// ---------------------------------------------------------------------------
void test_drop_handler_adds_on_top_of_forward(void)
{
    serial_desc_t *d0 = NULL, *d1 = NULL;
    init_both_ports(&d0, &d1);

    // Forward a 3-byte payload port 0 -> port 1: bytes_1to2 == 3, dropped_1 == 0.
    uint8_t payload[] = {0x01, 0x02, 0x03};
    mock_serial_registered_handler(d0, payload, sizeof(payload));

    // Now an RX-stage drop of 7 bytes on port 0: dropped_1 == 7, bytes_1to2 unchanged.
    d0->drop_handler(d0, 7);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(3, st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(7, st.dropped_1);
}

// ---------------------------------------------------------------------------
// Test: an unknown descriptor in the drop handler is dropped early, no counter moves
// ---------------------------------------------------------------------------
void test_drop_handler_unknown_desc_ignored(void)
{
    serial_config_t cfg0 = make_serial_config();
    serial_desc_t *d0 = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, repeater_init_port(0, &cfg0, &d0));

    serial_desc_t bogus;   // a descriptor never registered with the repeater

    // d0->drop_handler is the repeater's static handler; passing an unregistered desc
    // must hit the index < 0 early-return and leave every counter untouched.
    d0->drop_handler(&bogus, 99);

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_1);
    TEST_ASSERT_EQUAL_UINT64(0, st.dropped_2);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_1to2);
    TEST_ASSERT_EQUAL_UINT64(0, st.bytes_2to1);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_forward_port0_to_port1);
    RUN_TEST(test_forward_port1_to_port0);
    RUN_TEST(test_drop_when_peer_not_inited);
    RUN_TEST(test_drop_when_peer_tx_disabled);
    RUN_TEST(test_peer_tx_disabled_after_the_check_counts_forwarded);
    RUN_TEST(test_drop_when_peer_send_fails);
    RUN_TEST(test_active_requires_both_ports);
    RUN_TEST(test_deinit_decrements_active);
    RUN_TEST(test_counters_reset_on_fresh_session);
    RUN_TEST(test_uptime_tracks_elapsed_time);
    RUN_TEST(test_init_invalid_args);
    RUN_TEST(test_init_serial_fail);
    RUN_TEST(test_init_creates_mutex_once);
    RUN_TEST(test_double_init_same_port_is_idempotent);
    RUN_TEST(test_rx_handler_unknown_desc_ignored);
    RUN_TEST(test_deinit_serial_deinit_runs_after_unlock);
    RUN_TEST(test_send_runs_with_lock_released);
    RUN_TEST(test_forward_takes_one_critical_section_before_the_send);
    RUN_TEST(test_deinit_waits_for_inflight_forward);
    RUN_TEST(test_inflight_released_for_every_outcome);
    RUN_TEST(test_drop_handler_registered_on_ports);
    RUN_TEST(test_drop_handler_accumulates_per_port);
    RUN_TEST(test_drop_handler_adds_on_top_of_forward);
    RUN_TEST(test_drop_handler_unknown_desc_ignored);

    return UNITY_END();
}
