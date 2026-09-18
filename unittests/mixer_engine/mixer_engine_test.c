#include "unity.h"
#include "console_log.h"

#include "mixer.h"
#include "program.h"
#include "vfd_t13.h"
#include "setting_items.h"
#include "mock_setting_items.h"
#include "mb_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

/* The engine task's body, one tick at a time, and the way back to a known state.
 * Both exist only in the unit-test build (see the bottom of main/mixer/mixer.c): the task
 * itself is an infinite loop around mixer_test_tick(), which a host test cannot enter. */
void mixer_test_tick(void);
void mixer_test_reset(void);

/* Programs used below, with the steps they encode:
 *   TWO_STEPS   -> forward 200 rpm 2 s, then reverse 100 rpm 3 s
 *   FWD_200RPM  -> forward 200 rpm  (20 tens of rpm), 100 s
 *   FWD_10RPM   -> forward 10 rpm   (1 ten of rpm),   100 s
 *   FWD_2500RPM -> forward 2500 rpm (250 tens of rpm), 100 s */
#define PROG_TWO_STEPS      "ARQAAgIKAAM="
#define PROG_FWD_200RPM     "ARQAZA=="
#define PROG_FWD_10RPM      "AQEAZA=="
#define PROG_FWD_2500RPM    "AfoAZA=="

#define TEST_VFD_PORT       1       /* the case's numbering; mb_master counts from 0 */
#define TEST_VFD_ADDR       7
#define TEST_REV_CODE       VFD_T13_CMD_REVERSE_NEW

static void set_time_s(unsigned seconds)
{
    mock_esp_timer_get_time_value = (uint64_t)seconds * 1000000ULL;
}

/* What the drive answers when register 3 is read: the command it believes it is running. The
 * engine compares every poll against the command of the step it has pushed, so a test that
 * leaves this at its default of 0 is a test in which the drive has just been reset. */
static void set_drive_command(uint16_t command)
{
    mock_mb_master_regs[VFD_T13_REG_COMMAND] = command;
}

/* Bring the engine up as a device configured with this program and this gearbox ratio.
 * Resets everything first, so each test starts from a device that has just booted. */
static void start_engine(const char *program_b64, int rpm_50hz, bool enabled)
{
    char rpm_str[16];
    snprintf(rpm_str, sizeof(rpm_str), "%d", rpm_50hz);

    mock_setting_items_set(KEY_MIX_ENABLED, enabled ? "true" : "false");
    mock_setting_items_set(KEY_MIX_PROG, "0");
    mock_setting_items_set(KEY_MIX_RPM_50HZ, rpm_str);
    mock_setting_items_set(KEY_MIX_VFD_PORT, "1");
    mock_setting_items_set(KEY_MIX_VFD_ADDR, "7");
    mock_setting_items_set(KEY_MIX_REV_CODE, "5");
    mock_setting_items_set(KEY_MIX_P0, program_b64);

    mixer_start();
}

/* The value of the n-th write to a given register, counted over all writes made so far. */
static void assert_write(int index, uint16_t addr, uint16_t value, const char *message)
{
    TEST_ASSERT_TRUE_MESSAGE(index < mock_mb_master_write_called, message);
    TEST_ASSERT_EQUAL_MESSAGE(addr, mock_mb_master_writes[index].addr, message);
    TEST_ASSERT_EQUAL_MESSAGE(value, mock_mb_master_writes[index].value, message);
}

void setUp(void)
{
    mock_setting_items_reset();
    mock_mb_master_reset();
    mock_esp_timer_reset();
    mock_freertos_task_reset();
    mock_freertos_semaphore_reset();
    mixer_test_reset();
}

void tearDown(void)
{

}

/* A step change writes the frequency FIRST and the command after it — the drive must never
 * be told to run before it knows how fast — and the next step only starts when the current
 * one's seconds are up. */
void test_step_transitions(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test walking from one step to the next");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_TWO_STEPS, 1500, true);
    // The drive echoes the forward command, so the feedback poll below sees a drive that is
    // running the step it was given (test_drive_restart_repushes_the_step covers the case where
    // it is not).
    set_drive_command(VFD_T13_CMD_FORWARD);

    mixer_test_tick();

    // 200 rpm at 1500 rpm per 50 Hz is 6.6 Hz, which register 2 counts as 66.
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "A step is exactly two writes: frequency, then command");
    assert_write(0, VFD_T13_REG_FREQ, 66, "The frequency is written first");
    assert_write(1, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD,
                 "The run command follows the frequency");
    TEST_ASSERT_EQUAL_MESSAGE(TEST_VFD_PORT - 1, mock_mb_master_writes[0].port,
                              "mix_vfd_port 1 is mb_master port 0");
    TEST_ASSERT_EQUAL_MESSAGE(TEST_VFD_ADDR, mock_mb_master_writes[0].slave_id,
                              "The drive is addressed at mix_vfd_addr");

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_RUNNING, status.state, "The mixer is running");
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step, "It is on the first step");
    TEST_ASSERT_EQUAL_MESSAGE(2, status.step_left_s, "The first step lasts 2 seconds");
    TEST_ASSERT_EQUAL_MESSAGE(200, status.speed_rpm, "Speed 20 is 200 rpm");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_FORWARD, status.step_type, "Running forward");

    // Mid-step: the step is already on the drive, so nothing is written again.
    set_time_s(1);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "A tick inside a step writes nothing");
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(1, status.step_left_s, "One second left of the first step");

    // The second step: reverse at 100 rpm, which is 3.3 Hz -> 33.
    set_time_s(2);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called, "The second step is two writes");
    assert_write(2, VFD_T13_REG_FREQ, 33, "The second step's frequency comes first");
    assert_write(3, VFD_T13_REG_COMMAND, TEST_REV_CODE,
                 "Reverse uses the configured reverse code");

    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(1, status.step, "It is on the second step");
    TEST_ASSERT_EQUAL_MESSAGE(3, status.step_left_s, "The second step lasts 3 seconds");
    TEST_ASSERT_EQUAL_MESSAGE(100, status.speed_rpm, "Speed 10 is 100 rpm");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_REVERSE, status.step_type, "Running in reverse");
}

/* The end of the program is not the end of the run: the last step is followed by the first
 * one again, for as long as the mixer is switched on. */
void test_program_loops(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a finished program starts again");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_TWO_STEPS, 1500, true);

    mixer_test_tick();          // step 0, 2 s
    set_time_s(2);
    mixer_test_tick();          // step 1, 3 s

    set_time_s(5);              // the program is over
    mixer_test_tick();

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step, "The program wraps back to its first step");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_RUNNING, status.state, "And keeps running");
    TEST_ASSERT_EQUAL_MESSAGE(2, status.step_left_s, "With the first step's full duration");

    TEST_ASSERT_EQUAL_MESSAGE(6, mock_mb_master_write_called, "The first step is pushed again");
    assert_write(4, VFD_T13_REG_FREQ, 66, "Back to the first step's frequency");
    assert_write(5, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD, "And its forward command");
}

/* rpm -> the drive's 0.1 Hz setpoint: hz_dhz = rpm * 500 / mix_rpm_50hz, clamped into the
 * 10..990 the drive accepts. The clamps are what keeps a badly scaled program runnable
 * instead of refused. */
void test_rpm_to_frequency(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test converting shaft rpm into a frequency");
    LOG_MESSAGE();

    // 200 rpm on a 1500 rpm/50 Hz shaft -> 6.6 Hz
    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, true);
    mixer_test_tick();
    assert_write(0, VFD_T13_REG_FREQ, 66, "200 rpm of 1500 at 50 Hz is 6.6 Hz");

    // The same speed on a shaft that turns twice as fast per Hz -> half the frequency.
    setUp();
    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 3000, true);
    mixer_test_tick();
    assert_write(0, VFD_T13_REG_FREQ, 33, "200 rpm of 3000 at 50 Hz is 3.3 Hz");

    // Below the drive's floor: 10 rpm of 1500 would be 0.3 Hz, and the drive starts at 1.0.
    setUp();
    set_time_s(0);
    start_engine(PROG_FWD_10RPM, 1500, true);
    mixer_test_tick();
    assert_write(0, VFD_T13_REG_FREQ, VFD_T13_FREQ_MIN_DHZ,
                 "Anything below 1.0 Hz is clamped up to it");

    // Above the ceiling: 2500 rpm of 1000 would be 125 Hz, and the drive stops at 99.0.
    setUp();
    set_time_s(0);
    start_engine(PROG_FWD_2500RPM, 1000, true);
    mixer_test_tick();
    assert_write(0, VFD_T13_REG_FREQ, VFD_T13_FREQ_MAX_DHZ,
                 "Anything above 99.0 Hz is clamped down to it");
}

/* A program with no steps is not a fault and not a run: there is nothing to do, so the drive
 * is held stopped and the mixer reports itself stopped — even though it is switched on. */
void test_empty_program_holds_the_drive_stopped(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test an empty program");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine("", 1500, true);

    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called,
                              "An empty program writes the stop command and nothing else");
    assert_write(0, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "The drive is told to stop");

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STOPPED, status.state, "With nothing to run, it is stopped");
    TEST_ASSERT_EQUAL_MESSAGE(0, status.speed_rpm, "At no speed");
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step_left_s, "With no step running");
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step_type, "And no step type");

    // The stop is a state, not a stream of commands: once the drive has taken it, the engine
    // stops writing.
    set_time_s(1);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called,
                              "A drive already stopped is not told again every tick");
}

/* A drive that was reset in the middle of a step — an acknowledged fault, a breaker that
 * blinked — comes back standing still. It answers register 3 with the command it is actually
 * running, and when that is not the step's command the step is written into it again on the
 * next tick, WITHOUT the deadline being moved: the step still ends when it was going to. */
void test_drive_restart_repushes_the_step(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test a drive that restarted in the middle of a step");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, true);      // one forward step, 200 rpm, 100 s
    set_drive_command(VFD_T13_CMD_FORWARD);

    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called, "The step is pushed once");

    // The drive is running what it was told, so nothing is written again.
    set_time_s(1);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "A tick inside a step the drive is running writes nothing");

    // It restarts: register 3 now answers "stopped" while the step says forward.
    set_drive_command(VFD_T13_CMD_STOP);
    set_time_s(2);
    mixer_test_tick();                              // the poll notices the mismatch
    set_time_s(3);
    mixer_test_tick();                              // and the step goes out again

    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called,
                              "A drive that is not running the step is given it again");
    assert_write(2, VFD_T13_REG_FREQ, 66, "Frequency first, as on any push");
    assert_write(3, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD, "Then the step's own command");

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step, "Still the same step");
    TEST_ASSERT_EQUAL_MESSAGE(97, status.step_left_s,
                              "And its deadline was not touched: 3 of the 100 seconds are gone");
}

/* The other half of the same defect: a drive that vanished from the bus and came back. Only the
 * polls fail while it is gone — a step that has been pushed writes nothing — so the link coming
 * back is the only signal there is that the drive may have been reset under it. */
void test_drive_back_online_repushes_the_step(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test a drive that dropped off the bus and came back");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, true);
    set_drive_command(VFD_T13_CMD_FORWARD);

    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called, "The step is pushed once");

    // It stops answering. Two reads a poll, so the offline threshold is reached in four ticks.
    mock_mb_master_read_result = ESP_ERR_TIMEOUT;
    for (unsigned second = 1; second <= 5; second++) {
        set_time_s(second);
        mixer_test_tick();
    }

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_FALSE_MESSAGE(status.vfd_online, "A drive that answers nothing is gone");
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "A step already pushed writes nothing while the drive is away");

    // And it answers again.
    mock_mb_master_read_result = ESP_OK;
    set_time_s(6);
    mixer_test_tick();                              // the link is back; this tick notices it
    set_time_s(7);
    mixer_test_tick();                              // and this one writes the step again

    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called,
                              "A drive that has just come back is given the current step again");
    assert_write(3, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD, "The step's own command");

    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(93, status.step_left_s, "Again with the deadline left alone");
}

/* Re-reading the settings is not restarting the program. Only what the step hangs on — the
 * active program, its steps, the switch coming on, the drive parameters — puts the engine back
 * on step one; a hostname or a Wi-Fi password saved mid-batch does not. */
void test_unrelated_settings_change_keeps_the_position(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that only relevant settings restart the program");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_TWO_STEPS, 1500, true);       // forward 2 s, then reverse 3 s
    set_drive_command(VFD_T13_CMD_FORWARD);

    mixer_test_tick();                              // step 0
    set_time_s(2);
    mixer_test_tick();                              // step 1, 3 s long

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(1, status.step, "The engine is on the second step");

    // Something else entirely was saved, and the apply asks the engine to re-read.
    mixer_settings_changed();
    set_time_s(3);
    mixer_test_tick();

    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(1, status.step, "which is the step it is still on");
    TEST_ASSERT_EQUAL_MESSAGE(2, status.step_left_s, "with the deadline it was already running");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_RUNNING, status.state, "and still running");

    // The same value written again is not a change either: an MQTT command that repeats the
    // program already selected must not restart the batch.
    mock_setting_items_set(KEY_MIX_PROG, "0");
    mixer_settings_changed();
    set_time_s(4);
    mixer_test_tick();

    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(1, status.step, "The same program selected again changes nothing");

    // Selecting another program is a change the step does hang on.
    mock_setting_items_set(KEY_MIX_P1, PROG_FWD_200RPM);
    mock_setting_items_set(KEY_MIX_PROG, "1");
    mixer_settings_changed();
    set_time_s(5);
    mixer_test_tick();

    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step, "Another program starts from its first step");
    TEST_ASSERT_EQUAL_MESSAGE(100, status.step_left_s, "With that step's full duration");
}

/* A drive that drops off the bus and comes back may have been reset, and a reset drive was told
 * nothing at all - including the stop it was holding. A switched-off mixer therefore repeats its
 * stop once when the drive returns, the same way a running one repeats its step. Once, on the
 * edge: the stop is NOT re-verified against register 3 on every poll, see mixer_poll_vfd(). */
void test_stop_is_repeated_when_the_drive_comes_back(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a returning drive is told to stop again");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, false);     // a program is stored, the mixer is off
    set_drive_command(VFD_T13_CMD_STOP);

    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called, "A switched-off mixer writes one stop");
    assert_write(0, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "and that write is the stop command");

    // It goes away. Two reads a poll, so the offline threshold is reached in four ticks.
    mock_mb_master_read_result = ESP_ERR_TIMEOUT;
    for (unsigned second = 1; second <= 5; second++) {
        set_time_s(second);
        mixer_test_tick();
    }

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_FALSE_MESSAGE(status.vfd_online, "A drive that answers nothing is gone");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STOPPED, status.state,
                              "A switched-off mixer is stopped, not in error");

    // And it answers again.
    mock_mb_master_read_result = ESP_OK;
    set_time_s(6);
    mixer_test_tick();                              // the link is back; this tick notices it
    set_time_s(7);
    mixer_test_tick();                              // and this one writes the stop again

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called, "The stop is written once more");
    assert_write(1, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "as a stop and nothing else");

    set_time_s(8);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "and then the bus is left quiet, however register 3 reads back");
}

/* Measuring the shaft with a tachometer mid-batch changes the frequency every step converts to,
 * not which step is running: the new ratio is pushed into the drive, the position is kept. */
void test_new_gearbox_ratio_keeps_the_position(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a new gearbox ratio does not restart the program");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, true);      // 200 rpm of 1500 at 50 Hz -> 6.6 Hz
    set_drive_command(VFD_T13_CMD_FORWARD);

    mixer_test_tick();
    assert_write(0, VFD_T13_REG_FREQ, 66, "The frequency the old ratio gives");

    // The gearbox turned out to be half of what was entered.
    mock_setting_items_set(KEY_MIX_RPM_50HZ, "750");
    mixer_settings_changed();
    set_time_s(10);
    mixer_test_tick();

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step, "The same step is running");
    TEST_ASSERT_EQUAL_MESSAGE(90, status.step_left_s, "against the deadline it already had");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_RUNNING, status.state, "and the mixer never stopped");

    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called, "The step is pushed again");
    assert_write(2, VFD_T13_REG_FREQ, 133, "at the frequency the new ratio gives");
    assert_write(3, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD, "with the step's own command");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_step_transitions);
    RUN_TEST(test_program_loops);
    RUN_TEST(test_rpm_to_frequency);
    RUN_TEST(test_empty_program_holds_the_drive_stopped);
    RUN_TEST(test_drive_restart_repushes_the_step);
    RUN_TEST(test_drive_back_online_repushes_the_step);
    RUN_TEST(test_unrelated_settings_change_keeps_the_position);
    RUN_TEST(test_stop_is_repeated_when_the_drive_comes_back);
    RUN_TEST(test_new_gearbox_ratio_keeps_the_position);

    return UNITY_END();
}
