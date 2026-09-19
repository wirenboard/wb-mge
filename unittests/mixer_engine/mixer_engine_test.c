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
#define PROG_PAUSE_100S     "AAAAZA=="

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
    /* The motor is wired the usual way round, so a step goes out as what it says it is. Named
     * here rather than left to the mock's default, because every expectation below is written
     * for a device that does NOT swap the two directions. */
    mock_setting_items_set(KEY_MIX_DIR_INV, "false");
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
 * edge - and quiet afterwards not because nothing looks, but because what looks finds nothing:
 * mixer_poll_vfd() does check register 3 every poll while the mixer is off, and a drive that is
 * standing still answers it with the stop code (measured), which is not a run code. */
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
                              "and then the bus is left quiet, because register 3 reads back a stop");
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

/* Found on the bench, and the reason this test exists: the drive answers a single FC06 several
 * times over, the extra frames land in the master's response window, and a write that DID reach
 * the drive comes back as an error. The engine used to believe that verdict and return before
 * writing the command at all - so the frequency visibly changed on the drive's display while the
 * motor never started, with the drive sitting in RS-485 control mode waiting for a command that
 * was never sent. A write that answers badly must not swallow the command. */
void test_a_failing_write_still_sends_the_command(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a badly answered write does not swallow the start command");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, true);
    set_drive_command(VFD_T13_CMD_FORWARD);

    // Every write comes back as "the answer made no sense", which is what this drive does to a
    // write it has in fact carried out.
    mock_mb_master_write_result = ESP_ERR_INVALID_RESPONSE;

    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "Both frames go out, the frequency and the command");
    assert_write(0, VFD_T13_REG_FREQ, 66, "The frequency is still written first");
    assert_write(1, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD,
                 "and the start command goes out after it, whatever the first write answered");

    // And the bus is not hammered: the step counts as pushed, so the next tick writes nothing.
    // What decides whether it really took is the feedback check, once a second.
    set_time_s(1);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "A pushed step is not rewritten on every tick");
}

/* The same distrust of a write answer, on the two paths that write a stop and nothing else. Both
 * used to take the verdict as the truth, and on this drive the verdict is always an error - so
 * neither would ever count as pushed, and both would put a stop frame on the bus every 200 ms
 * tick. For a pause that lasts as long as the pause; for a mixer that is switched off, which is
 * most of a device's life, forever. */
void test_a_failing_stop_write_is_not_repeated_every_tick(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a badly answered stop write is sent once, not every tick");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_PAUSE_100S, 1500, true);
    // What a standing drive answers in register 3 was measured on the bench: the stop code
    // itself, steadily. That is what makes the pause quiet rather than a stop write per poll.
    set_drive_command(VFD_T13_CMD_STOP);
    mock_mb_master_write_result = ESP_ERR_INVALID_RESPONSE;

    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called,
                              "A pause step writes its stop once");
    assert_write(0, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "and what it writes is a stop");

    for (unsigned s = 1; s <= 5; s++) {
        set_time_s(s);
        mixer_test_tick();
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called,
                              "and does not write it again for the rest of the pause");
}

/* A switched-off mixer writes one stop and then keeps quiet - but "quiet" must not mean "never
 * looks again". A stop frame can be lost like any other, and nothing else would ever notice: the
 * off branch has no step to re-push and the drive stays online, so the come-back-online path never
 * fires either. The motor would turn for as long as the mixer stayed off while /info reported it
 * stopped. So the poll watches register 3 here too, and only a RUN code there brings the stop
 * back - never a 0 or an unknown answer, which would be a stop frame a second, forever. */
void test_a_switched_off_mixer_stops_the_drive_once(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that a switched-off mixer stops the drive once, then only if it is running");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, false);
    mock_mb_master_write_result = ESP_ERR_INVALID_RESPONSE;

    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called,
                              "The drive is told to stop once");
    assert_write(0, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "and that is the stop command");

    // Register 3 answers 0 here - the value the mocks reset to, standing in for an answer this
    // build does not recognise. Nothing may be written on the strength of that.
    for (unsigned s = 1; s <= 5; s++) {
        set_time_s(s);
        mixer_test_tick();
    }
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_mb_master_write_called,
                              "and an answer that is not a run code leaves the bus alone");

    // Now the drive says it is running forward: the stop was lost, or something else started it.
    // The mock does not mirror a write back into the register it was written to, so the drive
    // obeying the stop that follows has to be said out loud - which is also what keeps the write
    // count here exact rather than "one per second until it obeys".
    set_drive_command(VFD_T13_CMD_FORWARD);
    set_time_s(6);
    mixer_test_tick();                      // this tick's poll is what notices
    set_drive_command(VFD_T13_CMD_STOP);    // and the drive obeys the stop this one writes
    set_time_s(7);
    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "A drive that reports running while the mixer is off is stopped again");
    assert_write(1, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "with a stop command");

    // The other half of the check, and the half that depends on configuration rather than on a
    // constant: reverse is whichever code mix_rev_code holds for this drive revision.
    set_drive_command(TEST_REV_CODE);
    set_time_s(8);
    mixer_test_tick();
    set_drive_command(VFD_T13_CMD_STOP);
    set_time_s(9);
    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(3, mock_mb_master_write_called,
                              "and the same for the configured reverse code");
    assert_write(2, VFD_T13_REG_COMMAND, VFD_T13_CMD_STOP, "again a stop command");
}

/* A motor can be wired so that the drive's "forward" turns the shaft backwards, and moving a
 * wire is not always an option. mix_dir_inv swaps the two directions on the way out, so a step
 * the program calls forward really turns the shaft forward - the program keeps its own names,
 * only what goes on the bus changes. The feedback check must follow the same swap, or every
 * second of every step would look like a drive running the wrong command. */
void test_inverted_direction_swaps_the_commands(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test a motor wired the other way round");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_TWO_STEPS, 1500, true);

    // start_engine brings the device up wired the usual way; this is the installer noticing the
    // shaft turns the wrong way and ticking the box. Nothing has been written yet - the first
    // tick below is what re-reads the settings and pushes the first step.
    mock_setting_items_set(KEY_MIX_DIR_INV, "true");
    mixer_settings_changed();

    // The drive obeys what it is written, and the mock does not mirror a write back into the
    // register it was written to, so it has to be said out loud here.
    set_drive_command(TEST_REV_CODE);

    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "The forward step is still two writes");
    assert_write(0, VFD_T13_REG_FREQ, 66, "at the speed the step asks for");
    assert_write(1, VFD_T13_REG_COMMAND, TEST_REV_CODE,
                 "but it goes out as the drive's REVERSE command");

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_FORWARD, status.step_type,
                              "while the status keeps the program's own name for the step");

    // The whole point of inverting inside mixer_step_dir(): register 3 answers with the command
    // that was written, and the check expects exactly that, so nothing is pushed again.
    set_time_s(1);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called,
                              "The feedback check accepts the swapped command");

    // The second step, reverse at 100 rpm, goes out the other way about.
    set_drive_command(VFD_T13_CMD_FORWARD);
    set_time_s(2);
    mixer_test_tick();

    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called, "The second step is two writes");
    assert_write(2, VFD_T13_REG_FREQ, 33, "at its own speed");
    assert_write(3, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD,
                 "and it goes out as the drive's FORWARD command");

    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_REVERSE, status.step_type,
                              "with the status again naming the step as the program does");

    set_time_s(3);
    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called,
                              "and this one is accepted by the feedback check too");
}

/* And the way it will actually be used: the batch is already running when someone sees the shaft
 * turning the wrong way and ticks the box. Swapping the directions is not a reason to start the
 * program again - the step keeps its place and its clock, and only the command it goes out as
 * changes. The same guarantee mix_rpm_50hz has, for the same reason. */
void test_inverting_mid_program_keeps_the_position(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test that swapping the directions mid-batch does not restart the program");
    LOG_MESSAGE();

    set_time_s(0);
    start_engine(PROG_FWD_200RPM, 1500, true);      // forward 200 rpm, 100 s
    set_drive_command(VFD_T13_CMD_FORWARD);

    mixer_test_tick();
    TEST_ASSERT_EQUAL_MESSAGE(2, mock_mb_master_write_called, "The step goes out forward");
    assert_write(1, VFD_T13_REG_COMMAND, VFD_T13_CMD_FORWARD, "as the drive's forward command");

    // Ninety seconds still to run when the box is ticked.
    mock_setting_items_set(KEY_MIX_DIR_INV, "true");
    mixer_settings_changed();
    set_time_s(10);
    mixer_test_tick();

    mixer_status_t status;
    mixer_get_status(&status);
    TEST_ASSERT_EQUAL_MESSAGE(0, status.step, "The same step is running");
    TEST_ASSERT_EQUAL_MESSAGE(90, status.step_left_s, "against the deadline it already had");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_RUNNING, status.state, "and the mixer never stopped");
    TEST_ASSERT_EQUAL_MESSAGE(MIXER_STEP_FORWARD, status.step_type,
                              "still called forward, because that is what the program says");

    TEST_ASSERT_EQUAL_MESSAGE(4, mock_mb_master_write_called, "The step is pushed again");
    assert_write(3, VFD_T13_REG_COMMAND, TEST_REV_CODE, "now as the drive's reverse command");
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
    RUN_TEST(test_a_failing_write_still_sends_the_command);
    RUN_TEST(test_a_failing_stop_write_is_not_repeated_every_tick);
    RUN_TEST(test_a_switched_off_mixer_stops_the_drive_once);
    RUN_TEST(test_inverted_direction_swaps_the_commands);
    RUN_TEST(test_inverting_mid_program_keeps_the_position);

    return UNITY_END();
}
