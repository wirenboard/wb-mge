// Unit tests for repeater.c
// Tests transparent serial<->serial forwarding, byte/drop counters, the active
// flag, deinit accounting and per-session counter reset.

#include "unity.h"
#include "console_log.h"

#include "repeater.h"
#include "bridge.h"
#include "mock_serial.h"

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

void setUp(void)
{
    repeater_reset_for_test();
    mock_serial_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    mock_esp_timer_reset();
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

    // Advance esp_timer to 3,000,000 us. uptime_s = (now - start) / 1,000,000 = 3 s.
    mock_esp_timer_get_time_value = 3000000;

    repeater_stats_t st = {0};
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(3, st.uptime_s);

    // After all ports are down, uptime reports 0.
    repeater_deinit_port(0);
    repeater_get_stats(&st);
    TEST_ASSERT_EQUAL_UINT64(0, st.uptime_s);
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
    TEST_ASSERT_EQUAL_UINT64(0, st.uptime_s);
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
    TEST_ASSERT_EQUAL_UINT64(0, st.uptime_s);
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
// main
// ---------------------------------------------------------------------------
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_forward_port0_to_port1);
    RUN_TEST(test_forward_port1_to_port0);
    RUN_TEST(test_drop_when_peer_not_inited);
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

    return UNITY_END();
}
