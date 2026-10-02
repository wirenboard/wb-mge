#include "unity.h"

/*
 * fp_lock: the one task that writes the lock relay (main/fp_acs/fp_lock.h).
 *
 * fp_lock.c is #included so that the test can run its task loop and reset its statics. The loop
 * never returns, so the test owns the queue receive it blocks in: every receive is one turn of the
 * loop, a turn may first run a hook (change a setting, queue a request through the public API),
 * and the receive after the last turn longjmps back to the test. A turn with nothing queued is the
 * idle turn the real task gets every FP_LOCK_PROBE_MS.
 *
 * Every Modbus exchange and every delay lands in one trace, with "|" at the start of each turn:
 *     "W7.2=0 | W7.2=1 D1500 W7.2=0 | R7.2"
 * reads: at boot coil 2 of unit 7 was written OFF; turn 1 wrote it ON, waited 1500 ms and wrote it
 * OFF; turn 2 (idle) read it. A write that fails is retried three times with a 200 ms pause.
 */

#include <setjmp.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "mb_master_fake.h"
#include "setting_items_fake.h"
#include "trace.h"

static BaseType_t fp_lock_test_receive(QueueHandle_t queue, void *const buffer, TickType_t ticks);
static void fp_lock_test_delay(const TickType_t ticks);

#define xQueueReceive   fp_lock_test_receive
#define vTaskDelay      fp_lock_test_delay
#include "fp_lock.c"
#undef xQueueReceive
#undef vTaskDelay

#if !FP_ACS_ENABLED
    #error "fp_lock_test needs FP_ACS_ENABLED: check DEFS in the Makefile"
#endif

#define FP_LOCK_TEST_TURNS_MAX      16

// A write that never goes through: four attempts, 200 ms apart.
#define FP_LOCK_TEST_RETRIED(w)     w " D200 " w " D200 " w " D200 " w

typedef void (*fp_lock_test_hook_t)(void);

static jmp_buf             fp_lock_test_exit;
static unsigned            fp_lock_test_turns;
static unsigned            fp_lock_test_turn;
static fp_lock_test_hook_t fp_lock_test_hooks[FP_LOCK_TEST_TURNS_MAX];
static TickType_t          fp_lock_test_wait_ticks;
// The relay state and the module's online state as each turn began, i.e. after the turn before.
static bool                fp_lock_test_relay_at_turn[FP_LOCK_TEST_TURNS_MAX];
static bool                fp_lock_test_online_at_turn[FP_LOCK_TEST_TURNS_MAX];

// Run once, inside the first delay of fp_lock_test_delay_hook_ms.
static fp_lock_test_hook_t fp_lock_test_delay_hook;
static unsigned            fp_lock_test_delay_hook_ms;
static bool                fp_lock_test_relay_in_delay;

static bool                fp_lock_test_hook_result;


// ── Harness ──────────────────────────────────────────────────────────────────────────────

static BaseType_t fp_lock_test_receive(QueueHandle_t queue, void *const buffer, TickType_t ticks)
{
    if (fp_lock_test_turn >= fp_lock_test_turns) {
        longjmp(fp_lock_test_exit, 1);
    }
    unsigned turn = fp_lock_test_turn++;
    fp_lock_test_relay_at_turn[turn] = fp_lock_relay_on();
    fp_lock_test_online_at_turn[turn] = fp_lock_relay_online();
    trace_add("|");
    if (fp_lock_test_hooks[turn] != NULL) {
        fp_lock_test_hooks[turn]();
    }
    fp_lock_test_wait_ticks = ticks;
    return xQueueReceive(queue, buffer, ticks);
}

static void fp_lock_test_delay(const TickType_t ticks)
{
    trace_add("D%u", (unsigned)pdTICKS_TO_MS(ticks));
    fp_lock_test_relay_in_delay = fp_lock_relay_on();
    if ((fp_lock_test_delay_hook != NULL) && (ticks == pdMS_TO_TICKS(fp_lock_test_delay_hook_ms))) {
        fp_lock_test_hook_t hook = fp_lock_test_delay_hook;
        fp_lock_test_delay_hook = NULL;
        hook();
    }
    vTaskDelay(ticks);
}

// Boots the task (its closing write) and runs it for the given number of turns.
static void fp_lock_test_run(unsigned turns)
{
    TEST_ASSERT_LESS_OR_EQUAL(FP_LOCK_TEST_TURNS_MAX, turns);
    fp_lock_test_turns = turns;
    fp_lock_test_turn = 0;
    if (setjmp(fp_lock_test_exit) == 0) {
        fp_lock_task(NULL);
    }
    TEST_ASSERT_EQUAL_UINT(turns, fp_lock_test_turn);
}

// Turns are counted from 1, as the trace reads.
static void fp_lock_test_at_turn(unsigned turn, fp_lock_test_hook_t hook)
{
    fp_lock_test_hooks[turn - 1] = hook;
}

static void fp_lock_test_hook_open(void)
{
    fp_lock_test_hook_result = fp_lock_open();
}

static void fp_lock_test_hook_channel_4_and_notify(void)
{
    mock_settings_set(KEY_LOCK_CHANNEL, "4");
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_unit_9_and_notify(void)
{
    mock_settings_set(KEY_LOCK_ADDR, "9");
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_magnet_and_notify(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_channel_4(void)
{
    mock_settings_set(KEY_LOCK_CHANNEL, "4");
}

static void fp_lock_test_hook_notify(void)
{
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_pulse_300_and_open(void)
{
    mock_settings_set(KEY_LOCK_PULSE_MS, "300");
    fp_lock_test_hook_result = fp_lock_open();
}

void setUp(void)
{
    mock_freertos_queue_reset();
    mock_freertos_task_reset();
    mock_log_reset();
    mock_settings_reset();
    mb_fake_reset();
    trace_reset();

    fp_lock_queue = NULL;
    fp_lock_busy = false;
    fp_lock_relay_state = false;
    fp_lock_online = false;

    memset(fp_lock_test_hooks, 0, sizeof(fp_lock_test_hooks));
    memset(fp_lock_test_relay_at_turn, 0, sizeof(fp_lock_test_relay_at_turn));
    memset(fp_lock_test_online_at_turn, 0, sizeof(fp_lock_test_online_at_turn));
    fp_lock_test_turns = 0;
    fp_lock_test_turn = 0;
    fp_lock_test_wait_ticks = 0;
    fp_lock_test_delay_hook = NULL;
    fp_lock_test_delay_hook_ms = 0;
    fp_lock_test_relay_in_delay = false;
    fp_lock_test_hook_result = false;

    // Unit 7, channel 3 (coil 2), a strike, 1.5 s pulses.
    mock_settings_set(KEY_LOCK_ADDR, "7");
    mock_settings_set(KEY_LOCK_CHANNEL, "3");
    mock_settings_set(KEY_LOCK_PULSE_MS, "1500");
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_STRIKE_STR);
}

void tearDown(void)
{
    mock_freertos_queue_reset();
    fp_lock_queue = NULL;
}


// ── Start and the request API ────────────────────────────────────────────────────────────

static void test_requests_before_start_are_refused(void)
{
    TEST_ASSERT_FALSE(fp_lock_open());
    TEST_ASSERT_FALSE(fp_lock_set(true));
    TEST_ASSERT_FALSE(fp_lock_set(false));
    fp_lock_settings_changed();
    TEST_ASSERT_EQUAL_INT(0, mock_xQueueSend_data.called);
    TEST_ASSERT_FALSE(fp_lock_busy);
}

static void test_start_without_queue_creates_no_task(void)
{
    mock_xQueueCreate_data.should_fail = true;
    fp_lock_start();
    TEST_ASSERT_EQUAL_INT(0, mock_xTaskCreate_data.called);
    TEST_ASSERT_FALSE(fp_lock_open());
}

static void test_start_without_task_deletes_queue(void)
{
    mock_xTaskCreate_data.should_fail = true;
    fp_lock_start();
    TEST_ASSERT_EQUAL_INT(1, mock_vQueueDelete_data.called);
    TEST_ASSERT_NULL(fp_lock_queue);
    TEST_ASSERT_FALSE(fp_lock_open());
    TEST_ASSERT_FALSE(fp_lock_set(true));
}

static void test_start_creates_queue_of_four_and_one_task(void)
{
    fp_lock_start();
    TEST_ASSERT_EQUAL_INT(1, mock_xQueueCreate_data.called);
    TEST_ASSERT_EQUAL_UINT(4, mock_xQueueCreate_data.max_len);
    TEST_ASSERT_EQUAL_UINT(sizeof(fp_lock_request_t), mock_xQueueCreate_data.item_size);
    TEST_ASSERT_EQUAL_INT(1, mock_xTaskCreate_data.called);
    TEST_ASSERT_TRUE(mock_xTaskCreate_data.pvTaskCode == fp_lock_task);
}

static void test_second_open_during_a_pulse_is_dropped_not_queued(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    TEST_ASSERT_FALSE(fp_lock_open());
    TEST_ASSERT_EQUAL_UINT(1, uxQueueMessagesWaiting(fp_lock_queue));
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("a pulse is already under way"));

    fp_lock_test_run(1);
    // The pulse has ended: the next open is taken again.
    TEST_ASSERT_TRUE(fp_lock_open());
}

static void test_open_refused_by_a_full_queue_does_not_stay_busy(void)
{
    fp_lock_start();
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(fp_lock_set(true));
    }
    TEST_ASSERT_FALSE(fp_lock_set(true));
    TEST_ASSERT_FALSE(fp_lock_open());
    TEST_ASSERT_FALSE(fp_lock_busy);

    fp_lock_test_run(1);
    TEST_ASSERT_TRUE(fp_lock_open());
}

static void test_task_waits_one_probe_period_for_a_request(void)
{
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_UINT32(pdMS_TO_TICKS(5000), fp_lock_test_wait_ticks);
}


// ── Boot ─────────────────────────────────────────────────────────────────────────────────

static void test_boot_puts_strike_in_closed_state_then_probes(void)
{
    fp_lock_start();
    TEST_ASSERT_FALSE(fp_lock_relay_online());

    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_FALSE(fp_lock_relay_on());
    TEST_ASSERT_TRUE(fp_lock_relay_online());
    TEST_ASSERT_EQUAL_INT(0, mb_fake_wrong_port_count);
}

static void test_boot_puts_magnet_in_closed_state(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_start();
    fp_lock_test_run(0);
    TEST_ASSERT_EQUAL_STRING("W7.2=1", trace_get());
    TEST_ASSERT_TRUE(fp_lock_relay_on());
}

static void test_channel_counts_from_one_coil_from_zero(void)
{
    mock_settings_set(KEY_LOCK_CHANNEL, "1");
    fp_lock_start();
    fp_lock_test_run(0);
    TEST_ASSERT_EQUAL_STRING("W7.0=0", trace_get());
}

static void test_write_is_tried_four_times_200ms_apart(void)
{
    mb_fake_default_result = ESP_ERR_TIMEOUT;
    fp_lock_start();
    fp_lock_test_run(0);
    TEST_ASSERT_EQUAL_STRING(FP_LOCK_TEST_RETRIED("W7.2=0"), trace_get());
    TEST_ASSERT_FALSE(fp_lock_relay_online());
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("giving up after 4 attempts"));
}

static void test_write_that_goes_through_on_a_retry_leaves_nothing_pending(void)
{
    mb_fake_push_results(ESP_ERR_TIMEOUT, 2);
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 D200 W7.2=0 D200 W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_TRUE(fp_lock_relay_online());
}

static void test_failed_boot_close_is_repeated_in_place_of_the_probe_until_it_goes_through(void)
{
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4 + 2);
    fp_lock_start();
    fp_lock_test_run(4);
    // One write a turn, not retried, until the third one goes through; then probes again.
    TEST_ASSERT_EQUAL_STRING(FP_LOCK_TEST_RETRIED("W7.2=0") " | W7.2=0 | W7.2=0 | W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_FALSE(fp_lock_test_online_at_turn[2]);
    TEST_ASSERT_TRUE(fp_lock_test_online_at_turn[3]);
    TEST_ASSERT_FALSE(fp_lock_relay_on());
}


// ── Pulses ───────────────────────────────────────────────────────────────────────────────

static void test_pulse_opens_for_the_pulse_length_then_closes(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_TRUE(fp_lock_test_relay_in_delay);
    TEST_ASSERT_FALSE(fp_lock_relay_on());
    TEST_ASSERT_FALSE(fp_lock_busy);
}

static void test_magnet_pulse_switches_the_relay_off_to_open(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | W7.2=0 D1500 W7.2=1", trace_get());
    TEST_ASSERT_FALSE(fp_lock_test_relay_in_delay);
    TEST_ASSERT_TRUE(fp_lock_relay_on());
}

static void test_pulse_closes_even_when_its_opening_write_failed(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | " FP_LOCK_TEST_RETRIED("W7.2=1") " D1500 W7.2=0", trace_get());
}

static void test_failed_closing_write_of_a_pulse_is_repeated(void)
{
    mb_fake_push_results(ESP_OK, 2);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 " FP_LOCK_TEST_RETRIED("W7.2=0") " | W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_FALSE(fp_lock_relay_on());
}

static void test_pulse_length_is_read_at_every_opening(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_at_turn(2, fp_lock_test_hook_pulse_300_and_open);
    fp_lock_test_run(2);
    TEST_ASSERT_TRUE(fp_lock_test_hook_result);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | W7.2=1 D300 W7.2=0", trace_get());
}

static void test_pulse_closes_the_relay_it_opened_when_settings_change_during_it(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_delay_hook = fp_lock_test_hook_channel_4;
    fp_lock_test_delay_hook_ms = 1500;
    fp_lock_test_run(2);
    // The close goes to coil 2 as the open did; the next turn releases it and closes coil 3.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | W7.2=0 W7.3=0 R7.3", trace_get());
}


// ── Manual switching ─────────────────────────────────────────────────────────────────────

static void test_manual_open_holds_and_pulses_are_skipped(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 | | R7.2", trace_get());
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("Opening skipped"));
    TEST_ASSERT_TRUE(fp_lock_relay_on());
    // A skipped pulse ends the opening all the same.
    TEST_ASSERT_TRUE(fp_lock_open());
}

static void test_manual_close_releases_the_hold(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    TEST_ASSERT_TRUE(fp_lock_set(false));
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 | W7.2=0 | W7.2=1 D1500 W7.2=0", trace_get());
}

static void test_failed_manual_open_does_not_hold(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | " FP_LOCK_TEST_RETRIED("W7.2=1") " | W7.2=1 D1500 W7.2=0", trace_get());
}

static void test_failed_manual_close_is_repeated(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4 + 1);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(false));
    fp_lock_test_run(4);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | " FP_LOCK_TEST_RETRIED("W7.2=0") " | W7.2=0 | W7.2=0 | R7.2", trace_get());
}

static void test_manual_open_that_goes_through_ends_a_pending_close(void)
{
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_run(2);
    // The idle turn probes: a repeated close would undo the opening just made.
    TEST_ASSERT_EQUAL_STRING(FP_LOCK_TEST_RETRIED("W7.2=0") " | W7.2=1 | R7.2", trace_get());
    TEST_ASSERT_TRUE(fp_lock_relay_on());
}

static void test_failed_manual_open_leaves_the_pending_close(void)
{
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4 + 4);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING(FP_LOCK_TEST_RETRIED("W7.2=0") " | " FP_LOCK_TEST_RETRIED("W7.2=1") " | W7.2=0 | R7.2",
                             trace_get());
}

static void test_magnet_manual_off_is_the_open_state_and_holds(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(false));
    TEST_ASSERT_TRUE(fp_lock_open());
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_at_turn(4, fp_lock_test_hook_open);
    fp_lock_test_run(4);
    // Held open by the OFF, skipped; closed by the ON, the pulse after it runs.
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | W7.2=0 | | W7.2=1 | W7.2=0 D1500 W7.2=1", trace_get());
    TEST_ASSERT_TRUE(fp_lock_test_hook_result);
}

static void test_relay_state_follows_only_writes_that_went_through(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_run(3);
    TEST_ASSERT_FALSE(fp_lock_test_relay_at_turn[0]);
    TEST_ASSERT_FALSE(fp_lock_test_relay_at_turn[1]);
    TEST_ASSERT_TRUE(fp_lock_test_relay_at_turn[2]);
}


// ── Settings changes ─────────────────────────────────────────────────────────────────────

static void test_channel_change_releases_the_old_relay_and_closes_the_new_one(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_channel_4_and_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | W7.2=0 W7.3=1 | R7.3", trace_get());
}

static void test_unit_change_releases_the_old_relay_and_closes_the_new_one(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_unit_9_and_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | W7.2=0 W9.2=1 | R9.2", trace_get());
}

static void test_mode_change_puts_the_same_relay_in_the_new_closed_state(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_magnet_and_notify);
    fp_lock_test_run(2);
    // No release write: the relay is the same one.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 | R7.2", trace_get());
}

static void test_settings_check_with_nothing_changed_writes_nothing(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | | R7.2", trace_get());
}

static void test_settings_change_drops_the_hold(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_at_turn(2, fp_lock_test_hook_magnet_and_notify);
    fp_lock_test_at_turn(3, fp_lock_test_hook_open);
    fp_lock_test_run(4);
    // Held by the ON; the mode change closes the relay for a magnet and the next pulse runs.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 | W7.2=1 | W7.2=0 D1500 W7.2=1 | R7.2", trace_get());
    TEST_ASSERT_TRUE(fp_lock_test_hook_result);
}

static void test_failed_close_after_a_settings_change_is_repeated_on_the_new_relay(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    mb_fake_push_results(ESP_OK, 2);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 4);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_channel_4_and_notify);
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | W7.2=0 " FP_LOCK_TEST_RETRIED("W7.3=1") " | W7.3=1 | R7.3", trace_get());
}

static void test_idle_turn_applies_a_change_before_it_probes(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_channel_4);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=0 W7.3=0 R7.3", trace_get());
}

static void test_settings_check_not_queued_on_a_full_queue_still_runs_before_the_next_request(void)
{
    fp_lock_start();
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(fp_lock_set(true));
    }
    fp_lock_test_at_turn(1, fp_lock_test_hook_channel_4_and_notify);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=0 W7.3=0 W7.3=1", trace_get());
}


// ── Online state ─────────────────────────────────────────────────────────────────────────

static void test_online_state_follows_the_last_exchange_and_logs_only_changes(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 2);
    fp_lock_start();
    fp_lock_test_run(4);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | R7.2 | R7.2 | R7.2", trace_get());
    TEST_ASSERT_TRUE(fp_lock_test_online_at_turn[0]);
    TEST_ASSERT_FALSE(fp_lock_test_online_at_turn[1]);
    TEST_ASSERT_FALSE(fp_lock_test_online_at_turn[2]);
    TEST_ASSERT_TRUE(fp_lock_test_online_at_turn[3]);
    TEST_ASSERT_TRUE(fp_lock_relay_online());
    TEST_ASSERT_EQUAL_size_t(2, mock_log_count_matching("is online"));
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("is offline"));
}

static void test_relay_read_failure_takes_the_module_offline(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_INVALID_RESPONSE, 1);
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_FALSE(fp_lock_relay_online());
    TEST_ASSERT_NOT_NULL(mock_log_find("ESP_ERR_INVALID_RESPONSE"));
}


int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_requests_before_start_are_refused);
    RUN_TEST(test_start_without_queue_creates_no_task);
    RUN_TEST(test_start_without_task_deletes_queue);
    RUN_TEST(test_start_creates_queue_of_four_and_one_task);
    RUN_TEST(test_second_open_during_a_pulse_is_dropped_not_queued);
    RUN_TEST(test_open_refused_by_a_full_queue_does_not_stay_busy);
    RUN_TEST(test_task_waits_one_probe_period_for_a_request);

    RUN_TEST(test_boot_puts_strike_in_closed_state_then_probes);
    RUN_TEST(test_boot_puts_magnet_in_closed_state);
    RUN_TEST(test_channel_counts_from_one_coil_from_zero);
    RUN_TEST(test_write_is_tried_four_times_200ms_apart);
    RUN_TEST(test_write_that_goes_through_on_a_retry_leaves_nothing_pending);
    RUN_TEST(test_failed_boot_close_is_repeated_in_place_of_the_probe_until_it_goes_through);

    RUN_TEST(test_pulse_opens_for_the_pulse_length_then_closes);
    RUN_TEST(test_magnet_pulse_switches_the_relay_off_to_open);
    RUN_TEST(test_pulse_closes_even_when_its_opening_write_failed);
    RUN_TEST(test_failed_closing_write_of_a_pulse_is_repeated);
    RUN_TEST(test_pulse_length_is_read_at_every_opening);
    RUN_TEST(test_pulse_closes_the_relay_it_opened_when_settings_change_during_it);

    RUN_TEST(test_manual_open_holds_and_pulses_are_skipped);
    RUN_TEST(test_manual_close_releases_the_hold);
    RUN_TEST(test_failed_manual_open_does_not_hold);
    RUN_TEST(test_failed_manual_close_is_repeated);
    RUN_TEST(test_manual_open_that_goes_through_ends_a_pending_close);
    RUN_TEST(test_failed_manual_open_leaves_the_pending_close);
    RUN_TEST(test_magnet_manual_off_is_the_open_state_and_holds);
    RUN_TEST(test_relay_state_follows_only_writes_that_went_through);

    RUN_TEST(test_channel_change_releases_the_old_relay_and_closes_the_new_one);
    RUN_TEST(test_unit_change_releases_the_old_relay_and_closes_the_new_one);
    RUN_TEST(test_mode_change_puts_the_same_relay_in_the_new_closed_state);
    RUN_TEST(test_settings_check_with_nothing_changed_writes_nothing);
    RUN_TEST(test_settings_change_drops_the_hold);
    RUN_TEST(test_failed_close_after_a_settings_change_is_repeated_on_the_new_relay);
    RUN_TEST(test_idle_turn_applies_a_change_before_it_probes);
    RUN_TEST(test_settings_check_not_queued_on_a_full_queue_still_runs_before_the_next_request);

    RUN_TEST(test_online_state_follows_the_last_exchange_and_logs_only_changes);
    RUN_TEST(test_relay_read_failure_takes_the_module_offline);

    return UNITY_END();
}
