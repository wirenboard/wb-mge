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
 * The relay module is a fake (mocks/mb_master_fake.h) with coils and holding registers. Every
 * Modbus exchange and every delay lands in one trace, with "|" at the start of each turn:
 *     "W7.2=0 | W7.2=1 D1500 W7.2=0 | H7.1762=6 R7.2"
 * reads: at boot coil 2 of unit 7 was written OFF; turn 1 wrote it ON, waited 1500 ms and wrote it
 * OFF; turn 2 (idle) wrote 6 to holding register 1762 and read the coil. A write that fails is
 * retried three times with a 200 ms pause. Holding register reads are traced ("H7.1786?",
 * "H7.864*48?" for 48 registers) only in the tests that set mb_fake_trace_reads.
 *
 * setUp's lock is unit 7, channel 3 (coil 2), a strike, 1.5 s pulses, no exit button, on a module
 * that already holds that configuration: its idle reconciliations read and write nothing. The
 * registers of that module in the traces:
 *     1762            the timer function of channel 3 (1760 + coil): 6 strike, 7 magnet, 0 none
 *     1786, 1794      its "on for a time" (strike) and "off for a time" (magnet) time, in 0.1 s
 *     866 ... 906     the leading-edge action of inputs 1..6 on it (864 + (input - 1) * 8 + coil):
 *                     2 ON, 1 OFF, 0 nothing
 *     9 ... 14        the mode of inputs 1..6 (9 + input - 1): 4 switches by the mapping matrix
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

// The release of unit 7's channel 3: its timer function cleared, the leading-edge action of every
// input on it cleared, then the coil switched OFF.
#define FP_LOCK_TEST_RELEASE_7_2    "H7.1762=0 H7.866=0 H7.874=0 H7.882=0 H7.890=0 H7.898=0 H7.906=0 W7.2=0"

// setUp's lock in unit 7's register map (see the top of the file).
#define FP_LOCK_TEST_UNIT           7
#define FP_LOCK_TEST_REG_FUNCTION   1762
#define FP_LOCK_TEST_REG_ON_TIME    1786
#define FP_LOCK_TEST_REG_OFF_TIME   1794
#define FP_LOCK_TEST_PULSE_TIME     15      // 1500 ms in 0.1 s
#define FP_LOCK_TEST_FN_STRIKE      6
#define FP_LOCK_TEST_FN_MAGNET      7

typedef void (*fp_lock_test_hook_t)(void);

static jmp_buf             fp_lock_test_exit;
static unsigned            fp_lock_test_turns;
static unsigned            fp_lock_test_turn;
static fp_lock_test_hook_t fp_lock_test_hooks[FP_LOCK_TEST_TURNS_MAX];
static TickType_t          fp_lock_test_wait_ticks;
// The relay state, the module's online state and the configuration error as each turn began,
// i.e. after the turn before.
static bool                fp_lock_test_relay_at_turn[FP_LOCK_TEST_TURNS_MAX];
static bool                fp_lock_test_online_at_turn[FP_LOCK_TEST_TURNS_MAX];
static esp_err_t           fp_lock_test_config_at_turn[FP_LOCK_TEST_TURNS_MAX];

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
    fp_lock_test_config_at_turn[turn] = fp_lock_config_error();
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

// Unit 7's channel 3 timer as fp_lock sets it for a magnet with 1.5 s pulses.
static void fp_lock_test_module_holds_magnet(void)
{
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_OFF_TIME, FP_LOCK_TEST_PULSE_TIME);
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, FP_LOCK_TEST_FN_MAGNET);
}

// Input 2 switching channel 3 ON by the mapping matrix, as fp_lock sets it for a strike's exit button.
static void fp_lock_test_module_holds_button_2(void)
{
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, 874, 2);
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, 10, 4);
}

static void fp_lock_test_hook_open(void)
{
    fp_lock_test_hook_result = fp_lock_open();
}

static void fp_lock_test_hook_manual_on(void)
{
    TEST_ASSERT_TRUE(fp_lock_set(true));
}

static void fp_lock_test_hook_manual_on_and_open(void)
{
    TEST_ASSERT_TRUE(fp_lock_set(true));
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

static void fp_lock_test_hook_pulse_100_and_notify(void)
{
    mock_settings_set(KEY_LOCK_PULSE_MS, "100");
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_exit_input_3_and_notify(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "3");
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_exit_input_off_and_notify(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, LOCK_EXIT_INPUT_OFF_STR);
    fp_lock_settings_changed();
}

static void fp_lock_test_hook_on_time_times_out(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_TIMEOUT);
}

static void fp_lock_test_hook_on_time_refused(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_INVALID_RESPONSE);
}

static void fp_lock_test_hook_on_time_answers(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_OK);
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
    fp_lock_config_err = ESP_ERR_NOT_FINISHED;
    fp_lock_timer_refusals = 0;

    memset(fp_lock_test_hooks, 0, sizeof(fp_lock_test_hooks));
    memset(fp_lock_test_relay_at_turn, 0, sizeof(fp_lock_test_relay_at_turn));
    memset(fp_lock_test_online_at_turn, 0, sizeof(fp_lock_test_online_at_turn));
    memset(fp_lock_test_config_at_turn, 0, sizeof(fp_lock_test_config_at_turn));
    fp_lock_test_turns = 0;
    fp_lock_test_turn = 0;
    fp_lock_test_wait_ticks = 0;
    fp_lock_test_delay_hook = NULL;
    fp_lock_test_delay_hook_ms = 0;
    fp_lock_test_relay_in_delay = false;
    fp_lock_test_hook_result = false;

    // Unit 7, channel 3 (coil 2), a strike, 1.5 s pulses, no exit button.
    mock_settings_set(KEY_LOCK_ADDR, "7");
    mock_settings_set(KEY_LOCK_CHANNEL, "3");
    mock_settings_set(KEY_LOCK_PULSE_MS, "1500");
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_STRIKE_STR);
    mock_settings_set(KEY_LOCK_EXIT_INPUT, LOCK_EXIT_INPUT_OFF_STR);

    // A module that already holds that configuration.
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, FP_LOCK_TEST_PULSE_TIME);
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, FP_LOCK_TEST_FN_STRIKE);
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
    fp_lock_test_at_turn(2, fp_lock_test_hook_open);
    fp_lock_test_run(3);
    // After the idle turn has found the module's timer matching, the closing write is not repeated.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | W7.2=1 D1500 W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_TRUE(fp_lock_test_hook_result);
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
    // The new length also goes to the channel's timer, before the opening.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | H7.1786=3 W7.2=1 D300 W7.2=0", trace_get());
}

static void test_pulse_closes_the_relay_it_opened_when_settings_change_during_it(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_delay_hook = fp_lock_test_hook_channel_4;
    fp_lock_test_delay_hook_ms = 1500;
    fp_lock_test_run(2);
    // The close goes to coil 2 as the open did; the next turn releases it, configures channel 4 and
    // closes coil 3.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | "
                             FP_LOCK_TEST_RELEASE_7_2 " H7.1787=15 H7.1763=6 W7.3=0 R7.3", trace_get());
}


// ── Manual switching ─────────────────────────────────────────────────────────────────────

static void test_manual_open_holds_and_pulses_are_skipped(void)
{
    // Before the first reconciliation the module's timer is not known to close the relay.
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
    // Released, then channel 4 configured (off-time and timer function), then closed.
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | " FP_LOCK_TEST_RELEASE_7_2 " H7.1795=15 H7.1763=7 W7.3=1 | R7.3", trace_get());
}

static void test_unit_change_releases_the_old_relay_and_closes_the_new_one(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_unit_9_and_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | " FP_LOCK_TEST_RELEASE_7_2 " H9.1794=15 H9.1762=7 W9.2=1 | R9.2", trace_get());
}

static void test_mode_change_puts_the_same_relay_in_the_new_closed_state(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_magnet_and_notify);
    fp_lock_test_run(2);
    // No release: the relay is the same one. Its timer is set for a magnet - the time before the
    // function - and only then is the relay closed.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1794=15 H7.1762=7 W7.2=1 | R7.2", trace_get());
}

static void test_settings_check_with_nothing_changed_writes_nothing(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | | R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT(0, mb_fake_write_count);
}

static void test_settings_change_drops_the_hold(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_at_turn(2, fp_lock_test_hook_magnet_and_notify);
    fp_lock_test_at_turn(3, fp_lock_test_hook_open);
    fp_lock_test_run(4);
    // Held by the ON; the mode change closes the relay for a magnet and the next pulse runs.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 | H7.1794=15 H7.1762=7 W7.2=1 | W7.2=0 D1500 W7.2=1 | R7.2", trace_get());
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
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | " FP_LOCK_TEST_RELEASE_7_2 " H7.1795=15 H7.1763=7 "
                             FP_LOCK_TEST_RETRIED("W7.3=1") " | W7.3=1 | R7.3", trace_get());
}

static void test_idle_turn_applies_a_change_before_it_probes(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_channel_4);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | " FP_LOCK_TEST_RELEASE_7_2 " H7.1787=15 H7.1763=6 W7.3=0 R7.3", trace_get());
}

static void test_settings_check_not_queued_on_a_full_queue_still_runs_before_the_next_request(void)
{
    fp_lock_start();
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(fp_lock_set(true));
    }
    fp_lock_test_at_turn(1, fp_lock_test_hook_channel_4_and_notify);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | " FP_LOCK_TEST_RELEASE_7_2 " H7.1787=15 H7.1763=6 W7.3=0 W7.3=1", trace_get());
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

// ── The relay module's configuration ─────────────────────────────────────────────────────

static void test_config_error_is_not_finished_until_the_first_reconciliation(void)
{
    fp_lock_start();
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FINISHED, fp_lock_config_error());
    fp_lock_test_run(2);
    // The boot does not reconcile; the first idle turn does.
    TEST_ASSERT_EQUAL_INT(ESP_ERR_NOT_FINISHED, fp_lock_test_config_at_turn[0]);
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_test_config_at_turn[1]);
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_config_error());
}

static void test_fresh_module_gets_the_strike_timer_time_before_function(void)
{
    mb_fake_reset();
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(1);
    // Each register read before it is written; the leading-edge cells of all six inputs in one read.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? H7.1786=15 H7.1762? H7.1762=6 H7.864*48? R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT16(15, mb_fake_holding(7, 1786));
    TEST_ASSERT_EQUAL_UINT16(6, mb_fake_holding(7, 1762));
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_config_error());
    TEST_ASSERT_EQUAL_INT(0, mb_fake_wrong_port_count);
}

static void test_fresh_module_gets_the_magnet_timer(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    mb_fake_reset();
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | H7.1794=15 H7.1762=7 R7.2", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_config_error());
}

static void test_timer_time_is_the_pulse_length_rounded_up_to_tenths_of_a_second(void)
{
    mock_settings_set(KEY_LOCK_PULSE_MS, "1501");
    mb_fake_reset();
    fp_lock_start();
    fp_lock_test_at_turn(2, fp_lock_test_hook_pulse_100_and_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786=16 H7.1762=6 R7.2 | H7.1786=1", trace_get());
}

static void test_module_that_holds_the_configuration_is_only_read(void)
{
    fp_lock_start();
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | R7.2 | R7.2", trace_get());
    // Not a single EEPROM write; three reads a pass: the time, the timer function, the matrix.
    TEST_ASSERT_EQUAL_UINT(0, mb_fake_write_count);
    TEST_ASSERT_EQUAL_UINT(3 * 3, mb_fake_read_count);
}

static void test_only_the_registers_that_differ_are_written(void)
{
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, 0);
    fp_lock_start();
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1762=6 R7.2 | R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT(1, mb_fake_write_count);
}

static void test_exit_button_input_gets_the_open_action_before_its_mode(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? H7.1762? H7.864*48? H7.874=2 H7.10? H7.10=4 R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT16(2, mb_fake_holding(7, 874));
    TEST_ASSERT_EQUAL_UINT16(4, mb_fake_holding(7, 10));
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_config_error());
}

static void test_magnet_exit_button_input_gets_the_off_action(void)
{
    mock_settings_set(KEY_LOCK_MODE, LOCK_MODE_MAGNET_STR);
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "6");
    fp_lock_test_module_holds_magnet();
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=1 | H7.906=1 H7.14=4 R7.2", trace_get());
}

static void test_exit_button_already_in_place_is_not_written(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    fp_lock_test_module_holds_button_2();
    fp_lock_start();
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT(0, mb_fake_write_count);
}

static void test_other_inputs_lose_their_action_on_the_channel_but_keep_their_mode(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    mb_fake_set_holding(7, 866, 2);                             // input 1 switches channel 3 ON
    mb_fake_set_holding(7, 9, 4);
    mb_fake_set_holding(7, 898, 1);                             // input 5 switches it OFF
    mb_fake_set_holding(7, 13, 4);
    mb_fake_set_holding(7, 867, 2);                             // input 1 switches channel 4 ON
    fp_lock_start();
    fp_lock_test_run(1);
    // The button's action and mode first, the other inputs' actions on channel 3 cleared after.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.874=2 H7.10=4 H7.866=0 H7.898=0 R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT16(4, mb_fake_holding(7, 9));
    TEST_ASSERT_EQUAL_UINT16(4, mb_fake_holding(7, 13));
    TEST_ASSERT_EQUAL_UINT16(2, mb_fake_holding(7, 867));
}

static void test_exit_button_moved_to_another_input_is_reconciled_without_a_closing_write(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    fp_lock_test_module_holds_button_2();
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_exit_input_3_and_notify);
    fp_lock_test_run(2);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.882=2 H7.11=4 H7.874=0 | R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT16(4, mb_fake_holding(7, 10));
}

static void test_exit_button_given_up_has_its_action_cleared_and_its_input_keeps_its_mode(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    fp_lock_test_module_holds_button_2();
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_exit_input_off_and_notify);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.874=0", trace_get());
    TEST_ASSERT_EQUAL_UINT16(4, mb_fake_holding(7, 10));
}

static void test_refused_timer_time_stops_the_reconciliation_before_the_matrix(void)
{
    // With the timer not set, the exit button would open a strike for good.
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_INVALID_RESPONSE);
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? R7.2", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, fp_lock_config_error());
    TEST_ASSERT_EQUAL_UINT16(0, mb_fake_holding(7, 874));
}

static void test_failed_timer_function_write_stops_the_reconciliation_before_the_matrix(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    mb_fake_set_holding(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, 0);
    mb_fake_set_write_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, ESP_ERR_TIMEOUT);
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? H7.1762? H7.1762=6 R7.2", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, fp_lock_config_error());
}

static void test_failed_matrix_read_stops_the_reconciliation(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    mb_fake_set_read_error(7, 900, ESP_ERR_TIMEOUT);           // a cell of input 5, within the 48 read
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? H7.1762? H7.864*48? R7.2", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, fp_lock_config_error());
}

static void test_failed_button_action_write_leaves_its_mode_and_the_other_inputs_alone(void)
{
    mock_settings_set(KEY_LOCK_EXIT_INPUT, "2");
    mb_fake_set_holding(7, 866, 2);
    mb_fake_set_write_error(7, 874, ESP_ERR_INVALID_RESPONSE);
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? H7.1762? H7.864*48? H7.874=2 R7.2", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, fp_lock_config_error());
    TEST_ASSERT_EQUAL_UINT16(2, mb_fake_holding(7, 866));
}

static void test_failed_clearing_stops_at_the_input_it_failed_on(void)
{
    mb_fake_set_holding(7, 866, 2);
    mb_fake_set_holding(7, 882, 1);
    mb_fake_set_write_error(7, 866, ESP_ERR_TIMEOUT);
    fp_lock_start();
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.866=0 R7.2", trace_get());
    TEST_ASSERT_EQUAL_UINT16(1, mb_fake_holding(7, 882));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, fp_lock_config_error());
}

static void test_config_error_follows_each_reconciliation_and_is_logged_on_a_change_only(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    fp_lock_test_at_turn(4, fp_lock_test_hook_on_time_answers);
    fp_lock_test_run(4);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, fp_lock_test_config_at_turn[1]);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, fp_lock_test_config_at_turn[3]);
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_config_error());
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("the relay module is not configured"));
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("Unit 7 configured"));
}

static void test_module_that_does_not_answer_is_not_reconciled(void)
{
    mb_fake_push_results(ESP_OK, 1);
    mb_fake_push_results(ESP_ERR_TIMEOUT, 1);
    mb_fake_trace_reads = true;
    fp_lock_start();
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1786? H7.1762? H7.864*48? R7.2 | R7.2 | H7.1786? H7.1762? H7.864*48? R7.2",
                             trace_get());
}


// ── Closing writes while the timer is unsettled ──────────────────────────────────────────

static void test_closing_write_of_a_pulse_before_the_first_reconciliation_is_made_again_after_it(void)
{
    mb_fake_trace_reads = true;
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | H7.1786? H7.1762? H7.864*48? W7.2=0 | "
                             "H7.1786? H7.1762? H7.864*48? R7.2", trace_get());
}

static void test_manual_close_before_the_first_reconciliation_is_made_again_after_it(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(false));
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=0 | W7.2=0 | R7.2", trace_get());
}

static void test_closing_write_is_repeated_for_as_long_as_the_timer_does_not_answer(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_TIMEOUT);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_run(5);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | W7.2=0 | W7.2=0 | W7.2=0 | W7.2=0", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_ERR_TIMEOUT, fp_lock_config_error());
}

static void test_three_refusals_in_a_row_end_the_repeated_closing_writes(void)
{
    // A module with a firmware older than 1.28.0 refuses the timer registers: its relay stays
    // where the closing write put it.
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_at_turn(6, fp_lock_test_hook_open);
    fp_lock_test_run(7);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | W7.2=0 | W7.2=0 | W7.2=0 | R7.2 | "
                             "W7.2=1 D1500 W7.2=0 | R7.2", trace_get());
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, fp_lock_config_error());
}

static void test_another_answer_between_refusals_starts_their_count_again(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_open());
    fp_lock_test_at_turn(3, fp_lock_test_hook_on_time_times_out);
    fp_lock_test_at_turn(4, fp_lock_test_hook_on_time_refused);
    fp_lock_test_run(7);
    // Refused, timed out, then refused three times in a row: five repeats, not three.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 D1500 W7.2=0 | W7.2=0 | W7.2=0 | W7.2=0 | W7.2=0 | W7.2=0 | R7.2",
                             trace_get());
}

static void test_closing_write_of_a_mode_switch_stays_pending_while_the_new_timer_is_refused(void)
{
    mb_fake_set_write_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_magnet_and_notify);
    fp_lock_test_run(4);
    // Each idle turn asks for the timer function again before it repeats the closing write.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1794=15 H7.1762=7 W7.2=1 | H7.1762=7 W7.2=1 | H7.1762=7 W7.2=1 | "
                             "H7.1762=7 R7.2", trace_get());
}


// ── The hold and the module's timer ──────────────────────────────────────────────────────

static void test_hold_ends_once_a_reconciliation_finds_the_timer_matching(void)
{
    fp_lock_start();
    TEST_ASSERT_TRUE(fp_lock_set(true));
    fp_lock_test_at_turn(3, fp_lock_test_hook_open);
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | W7.2=1 | R7.2 | W7.2=1 D1500 W7.2=0", trace_get());
    TEST_ASSERT_EQUAL_size_t(0, mock_log_count_matching("Opening skipped"));
}

static void test_manual_open_with_the_timer_matching_is_no_hold(void)
{
    fp_lock_start();
    fp_lock_test_at_turn(2, fp_lock_test_hook_manual_on_and_open);
    fp_lock_test_run(3);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | W7.2=1 | W7.2=1 D1500 W7.2=0", trace_get());
    TEST_ASSERT_TRUE(fp_lock_test_hook_result);
}

static void test_manual_open_holds_while_the_module_refuses_the_timer(void)
{
    mb_fake_set_read_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_ON_TIME, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    fp_lock_test_at_turn(2, fp_lock_test_hook_manual_on);
    fp_lock_test_at_turn(4, fp_lock_test_hook_open);
    fp_lock_test_run(4);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | W7.2=1 | R7.2 |", trace_get());
    TEST_ASSERT_EQUAL_size_t(1, mock_log_count_matching("Opening skipped"));
}


// ── Timer refusals and a change of relay ─────────────────────────────────────────────────

static void test_refusals_are_counted_afresh_on_another_channel(void)
{
    mb_fake_set_read_error(7, 1786, ESP_ERR_INVALID_RESPONSE);
    mb_fake_set_read_error(7, 1787, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    fp_lock_test_at_turn(4, fp_lock_test_hook_channel_4_and_notify);
    fp_lock_test_run(5);
    // Three refusals on channel 3; channel 4's first one leaves its closing write pending again.
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | R7.2 | R7.2 | " FP_LOCK_TEST_RELEASE_7_2 " W7.3=0 | W7.3=0", trace_get());
}

static void test_mode_change_keeps_the_refusal_count(void)
{
    mb_fake_set_read_error(7, 1786, ESP_ERR_INVALID_RESPONSE);
    mb_fake_set_read_error(7, 1794, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    fp_lock_test_at_turn(4, fp_lock_test_hook_magnet_and_notify);
    fp_lock_test_run(5);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | R7.2 | R7.2 | R7.2 | W7.2=1 | R7.2", trace_get());
}


// ── The release of a relay given up ──────────────────────────────────────────────────────

static void test_release_stops_at_the_first_write_that_gets_no_answer(void)
{
    mb_fake_set_write_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, ESP_ERR_TIMEOUT);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_unit_9_and_notify);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1762=0 H9.1786=15 H9.1762=6 W9.2=0", trace_get());
    // The release does not count: the error is the new module's.
    TEST_ASSERT_EQUAL_INT(ESP_OK, fp_lock_config_error());
}

static void test_release_stops_partway_at_a_write_that_gets_no_answer(void)
{
    mb_fake_set_write_error(7, 882, ESP_ERR_TIMEOUT);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_unit_9_and_notify);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | H7.1762=0 H7.866=0 H7.874=0 H7.882=0 H9.1786=15 H9.1762=6 W9.2=0", trace_get());
}

static void test_release_goes_on_past_a_refusal(void)
{
    mb_fake_set_write_error(FP_LOCK_TEST_UNIT, FP_LOCK_TEST_REG_FUNCTION, ESP_ERR_INVALID_RESPONSE);
    fp_lock_start();
    fp_lock_test_at_turn(1, fp_lock_test_hook_unit_9_and_notify);
    fp_lock_test_run(1);
    TEST_ASSERT_EQUAL_STRING("W7.2=0 | " FP_LOCK_TEST_RELEASE_7_2 " H9.1786=15 H9.1762=6 W9.2=0", trace_get());
    TEST_ASSERT_EQUAL_UINT8(7, mb_fake_writes[0].unit);
    TEST_ASSERT_EQUAL_UINT16(1762, mb_fake_writes[0].reg);
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_RESPONSE, mb_fake_writes[0].result);
    TEST_ASSERT_EQUAL_UINT16(6, mb_fake_holding(7, 1762));
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

    RUN_TEST(test_config_error_is_not_finished_until_the_first_reconciliation);
    RUN_TEST(test_fresh_module_gets_the_strike_timer_time_before_function);
    RUN_TEST(test_fresh_module_gets_the_magnet_timer);
    RUN_TEST(test_timer_time_is_the_pulse_length_rounded_up_to_tenths_of_a_second);
    RUN_TEST(test_module_that_holds_the_configuration_is_only_read);
    RUN_TEST(test_only_the_registers_that_differ_are_written);
    RUN_TEST(test_exit_button_input_gets_the_open_action_before_its_mode);
    RUN_TEST(test_magnet_exit_button_input_gets_the_off_action);
    RUN_TEST(test_exit_button_already_in_place_is_not_written);
    RUN_TEST(test_other_inputs_lose_their_action_on_the_channel_but_keep_their_mode);
    RUN_TEST(test_exit_button_moved_to_another_input_is_reconciled_without_a_closing_write);
    RUN_TEST(test_exit_button_given_up_has_its_action_cleared_and_its_input_keeps_its_mode);
    RUN_TEST(test_refused_timer_time_stops_the_reconciliation_before_the_matrix);
    RUN_TEST(test_failed_timer_function_write_stops_the_reconciliation_before_the_matrix);
    RUN_TEST(test_failed_matrix_read_stops_the_reconciliation);
    RUN_TEST(test_failed_button_action_write_leaves_its_mode_and_the_other_inputs_alone);
    RUN_TEST(test_failed_clearing_stops_at_the_input_it_failed_on);
    RUN_TEST(test_config_error_follows_each_reconciliation_and_is_logged_on_a_change_only);
    RUN_TEST(test_module_that_does_not_answer_is_not_reconciled);

    RUN_TEST(test_closing_write_of_a_pulse_before_the_first_reconciliation_is_made_again_after_it);
    RUN_TEST(test_manual_close_before_the_first_reconciliation_is_made_again_after_it);
    RUN_TEST(test_closing_write_is_repeated_for_as_long_as_the_timer_does_not_answer);
    RUN_TEST(test_three_refusals_in_a_row_end_the_repeated_closing_writes);
    RUN_TEST(test_another_answer_between_refusals_starts_their_count_again);
    RUN_TEST(test_closing_write_of_a_mode_switch_stays_pending_while_the_new_timer_is_refused);

    RUN_TEST(test_hold_ends_once_a_reconciliation_finds_the_timer_matching);
    RUN_TEST(test_manual_open_with_the_timer_matching_is_no_hold);
    RUN_TEST(test_manual_open_holds_while_the_module_refuses_the_timer);

    RUN_TEST(test_refusals_are_counted_afresh_on_another_channel);
    RUN_TEST(test_mode_change_keeps_the_refusal_count);

    RUN_TEST(test_release_stops_at_the_first_write_that_gets_no_answer);
    RUN_TEST(test_release_stops_partway_at_a_write_that_gets_no_answer);
    RUN_TEST(test_release_goes_on_past_a_refusal);

    return UNITY_END();
}
