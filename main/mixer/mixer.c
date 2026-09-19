#include "mixer.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "program.h"
#include "setting_items.h"
#include "vfd_t13.h"

static const char *TAG = "mixer";

#define MIXER_TASK_STACK_SIZE       (4 * 1024)
#define MIXER_TASK_PRIORITY         4

// The tick is what bounds the reaction to "this step is over" and to a settings change. It is
// not what measures time: the step deadlines below are microsecond stamps from
// esp_timer_get_time(). A deadline is set from the time of the tick that noticed the previous
// step was over, so a step is NEVER shorter than it says and may be longer by however late that
// tick was - a drive that is not answering can cost the master's 500 ms response timeout per
// request, and a step then runs that much over. For a mixer that is the safe direction to err
// in: a few tenths of a second of extra stirring changes nothing, and a step cut short would.
#define MIXER_TICK_MS               200
#define MIXER_POLL_PERIOD_US        1000000     // telemetry + feedback, once a second

#define MIXER_US_PER_S              1000000

// Settings, as the engine holds them between reloads.
static vfd_t13_t       vfd;
static mixer_program_t program;
static uint8_t         program_index;
static uint16_t        rpm_50hz;
static bool            enabled;

// Where in the program we are. step_running says the deadline is meaningful; step_pushed says
// this step's frequency and command have been SENT to the drive - not that it accepted them,
// which is not knowable from a write answer on this drive and is decided by the comparison
// against register 3 in mixer_poll_vfd(). They are separate because clearing step_pushed - on a
// mismatch, or when the drive returns to the bus - must re-send the step WITHOUT restarting its
// clock.
static uint8_t  step_index;
static int64_t  step_deadline_us;
static bool     step_running;
static bool     step_pushed;
static bool     stop_pushed;

static int64_t  next_poll_us;

// Last values read back from the drive, kept between polls so the status does not blink
// between two one-second reads.
static uint16_t last_cmd_raw;
static uint16_t last_freq_dhz;
static vfd_t13_telemetry_t last_telemetry;

// Whether the drive was answering at the end of the previous tick. What makes "it has just come
// back" a moment the engine can act on. True to begin with, so a device that boots with the
// drive present does not read as a drive that has just returned.
static bool vfd_was_online = true;

// The stored value of the active program, as the engine last read it. Compared as the setting
// string rather than as decoded steps: that string is what a settings write puts in NVS, and
// two equal strings are the same program.
static char applied_program[SETTING_ITEM_MAX_STR_LEN];

// The published snapshot. Written by the engine task only, read from the httpd task through
// mixer_get_status(), so the two are separated by a mutex rather than left to tear.
static mixer_status_t    status;
static SemaphoreHandle_t status_mutex;
static StaticSemaphore_t status_mutex_buffer;

// Raised by every caller that changes a setting, cleared by the task when it has re-read
// them. Written from other tasks, hence the atomics.
static bool settings_dirty;

static const char *const program_keys[MIXER_PROGRAM_COUNT] = {
    KEY_MIX_P0, KEY_MIX_P1, KEY_MIX_P2, KEY_MIX_P3,
};


// rpm -> the drive's 0.1 Hz setpoint. The shaft does mix_rpm_50hz turns a minute at 50 Hz,
// so 500 is 50.0 Hz in the units register 2 counts in.
static uint16_t mixer_rpm_to_dhz(uint16_t rpm)
{
    uint32_t dhz = ((uint32_t)rpm * 500u) / rpm_50hz;

    if (dhz < VFD_T13_FREQ_MIN_DHZ) {
        dhz = VFD_T13_FREQ_MIN_DHZ;
    } else if (dhz > VFD_T13_FREQ_MAX_DHZ) {
        dhz = VFD_T13_FREQ_MAX_DHZ;
    }
    return (uint16_t)dhz;
}

static void mixer_reload_settings(void)
{
    // What the position in the program actually depends on, as it was before this read. A
    // POST /settings that changes none of it - a new hostname, a new Wi-Fi password, the same
    // MQTT command sent twice - must re-read the settings WITHOUT moving the program: a batch
    // an hour into its cycle does not start again because someone renamed the device.
    bool      was_enabled  = enabled;
    uint8_t   was_index    = program_index;
    uint16_t  was_rpm_50hz = rpm_50hz;
    vfd_t13_t was_vfd      = vfd;
    char      was_program[SETTING_ITEM_MAX_STR_LEN];
    memcpy(was_program, applied_program, sizeof(was_program));

    enabled = setting_items_read_bool(KEY_MIX_ENABLED);

    int index = setting_items_read_int(KEY_MIX_PROG);
    program_index = (uint8_t)((index >= 0) && (index < MIXER_PROGRAM_COUNT) ? index : 0);

    int rpm = setting_items_read_int(KEY_MIX_RPM_50HZ);
    // Never zero: it is the divisor in mixer_rpm_to_dhz().
    rpm_50hz = (uint16_t)((rpm >= 1) ? rpm : 1);

    int port = setting_items_read_int(KEY_MIX_VFD_PORT);
    int addr = setting_items_read_int(KEY_MIX_VFD_ADDR);
    int rev = setting_items_read_int(KEY_MIX_REV_CODE);
    // The setting numbers the ports 1..2 the way the case does; mb_master counts from 0.
    vfd_t13_configure(&vfd, (uint8_t)(port - 1), (uint8_t)addr, (uint16_t)rev);

    char stored[SETTING_ITEM_MAX_STR_LEN] = {0};
    if (setting_items_read(program_keys[program_index], stored) != ESP_OK) {
        stored[0] = '\0';
    }
    strncpy(applied_program, stored, sizeof(applied_program) - 1);
    applied_program[sizeof(applied_program) - 1] = '\0';

    if (mixer_program_decode(stored, &program) != ESP_OK) {
        // The validator refuses to store anything this cannot decode, so reaching here means
        // NVS holds something no settings write put there. An empty program is the safe
        // reading of it: the mixer stands still instead of running an invented step.
        ESP_LOGE(TAG, "program %u is unreadable, treating it as empty", (unsigned)program_index);
        memset(&program, 0, sizeof(program));
    }

    // Only a change the step actually hangs on restarts the program from its first step: which
    // program is active, what that program says, the switch coming on, and the drive parameters
    // every step is written through. Selecting a program and switching the mixer on are what a
    // user expects to start a cycle from the beginning; nothing else here is.
    bool restart = ((program_index != was_index) ||
                    (strncmp(applied_program, was_program, sizeof(applied_program)) != 0) ||
                    (enabled && !was_enabled) ||
                    (vfd.port != was_vfd.port) ||
                    (vfd.slave_id != was_vfd.slave_id) ||
                    (vfd.rev_code != was_vfd.rev_code));

    if (restart) {
        step_index = 0;
        step_running = false;
        step_pushed = false;
        stop_pushed = false;
    }

    // A new gearbox ratio changes the frequency the current step converts to, not which step is
    // running: someone who measures the shaft with a tachometer halfway through a three-hour
    // batch must not send the program back to its first step. Re-pushing the step is the whole
    // of what the new ratio needs, and it leaves the deadline alone.
    if (rpm_50hz != was_rpm_50hz) {
        step_pushed = false;
    }

    ESP_LOGI(TAG, "settings: enabled=%d program=%u steps=%u rpm@50Hz=%u port=%u addr=%u rev=%u",
             (int)enabled, (unsigned)program_index, (unsigned)program.count,
             (unsigned)rpm_50hz, (unsigned)(vfd.port + 1), (unsigned)vfd.slave_id,
             (unsigned)vfd.rev_code);
}

// Which way a step turns the shaft. A pause is the motor standing still, so it is a stop.
static vfd_t13_dir_t mixer_step_dir(const mixer_step_t *step)
{
    if (step->type == MIXER_STEP_PAUSE) {
        return VFD_T13_DIR_STOP;
    }
    return (step->type == MIXER_STEP_REVERSE) ? VFD_T13_DIR_REVERSE : VFD_T13_DIR_FORWARD;
}

// Push the current step to the drive: frequency first, then the command, because the drive
// must not be told to run before it knows how fast. A pause step has no speed of its own -
// it is the motor standing still - so it writes the stop command and nothing else.
//
// Nothing is returned, and that is the point: what a write answered says nothing about whether
// this drive took it. The caller marks the step pushed because the frames went out, and the
// once-a-second comparison against register 3 in mixer_poll_vfd() is what decides whether it
// really took and sends it again when it did not.
static void mixer_push_step(void)
{
    const mixer_step_t *step = &program.steps[step_index];
    vfd_t13_dir_t dir = mixer_step_dir(step);

    if (dir == VFD_T13_DIR_STOP) {
        // Same contract as the two writes below: sent is sent. Believing the verdict here would
        // leave step_pushed false for the whole pause and write a stop frame five times a second
        // until it ended.
        esp_err_t stop_err = vfd_t13_send_command(&vfd, VFD_T13_DIR_STOP);
        if (stop_err != ESP_OK) {
            // Debug, not a warning: on this drive a badly answered write is the ordinary case,
            // not an event. The last one is published as write_err in /info for when it matters.
            ESP_LOGD(TAG, "the pause's stop write answered %s, the feedback check will confirm it",
                     esp_err_to_name(stop_err));
        }
        return;
    }

    uint16_t dhz = mixer_rpm_to_dhz((uint16_t)(step->speed * 10u));

    // The frequency write's verdict must NOT abort the sequence. This drive answers a single
    // FC06 several times over, and the extra frames land inside the master's response window,
    // so a write that reached the drive comes back as an error. Believing that verdict is
    // exactly what left the motor standing on the bench: the sequence returned here, the start
    // command was never written at all, and the drive sat in RS-485 control mode waiting for
    // it - visibly taking the frequency all the while.
    esp_err_t freq_err = vfd_t13_set_frequency(&vfd, dhz);
    if (freq_err != ESP_OK) {
        ESP_LOGD(TAG, "the frequency write answered %s, sending the command anyway",
                 esp_err_to_name(freq_err));
    }

    esp_err_t cmd_err = vfd_t13_send_command(&vfd, dir);
    if (cmd_err != ESP_OK) {
        ESP_LOGD(TAG, "the command write answered %s, the feedback check will confirm it",
                 esp_err_to_name(cmd_err));
    }
}

static void mixer_begin_step(int64_t now)
{
    step_deadline_us = now + ((int64_t)program.steps[step_index].duration_s * MIXER_US_PER_S);
    step_running = true;
    step_pushed = false;
}

static void mixer_poll_vfd(bool running)
{
    vfd_t13_feedback_t feedback;
    if (vfd_t13_read_feedback(&vfd, &feedback) == ESP_OK) {
        last_freq_dhz = feedback.freq_dhz;
        last_cmd_raw = feedback.command;

        // Register 3 answers with the command the drive believes it is running, and that is the
        // only way to notice one that restarted in the middle of a step: an acknowledged fault
        // or a breaker that blinked leaves it standing still, while the engine - which has
        // already pushed this step - would write nothing again until the step ended. A step of
        // an hour is then an hour of a motionless motor that /info and MQTT report as running.
        //
        // What a mismatch does is only mark the step as not pushed, so the next tick writes it
        // again. The deadline it is running against is deliberately left alone: the step still
        // ends when it was going to, and a drive that keeps dropping the command cannot stretch
        // the program by being re-written.
        // Only the command is compared, never the frequency, although register 2 is read right
        // here. Measured on the drive: register 2 answers with the frequency it is CURRENTLY
        // putting out, ramping through 6.6, 11.1, 25.2, 35.0 Hz on its way to a setpoint - not
        // with the setpoint itself. An equality check against it would therefore re-push the step
        // every second of every ramp. The gap this leaves is known and accepted: if the frequency
        // frame is lost while the command frame lands, the step runs at the previous step's speed
        // for its whole duration and nothing notices.
        uint16_t expected = vfd_t13_command_code(&vfd, mixer_step_dir(&program.steps[step_index]));
        if (running && step_running && step_pushed && (feedback.command != expected)) {
            ESP_LOGW(TAG, "the drive is running command %u and not this step's %u, pushing it again",
                     (unsigned)feedback.command, (unsigned)expected);
            step_pushed = false;
        }

        // The same check for a mixer that is switched off, and the reason it is written as "is the
        // drive reporting a RUN code" rather than "does it differ from stop": what register 3 holds
        // on an idle drive is not in the manufacturer's table. Measured on the drive instead - it
        // answers with the stop code itself, steadily - so in the normal case this costs no traffic
        // at all. Anything the register might hold that is neither of the two run codes, 0 included,
        // is deliberately read as "not running": a stop frame every second for as long as the mixer
        // is off is worse than the case this guards against. Without it a single lost stop frame -
        // ordinary traffic on an RS-485 bus, as VFD_T13_OFFLINE_THRESHOLD itself says - would leave
        // the motor turning for as long as the mixer stayed off, with /info reporting it stopped.
        uint16_t run_fwd = vfd_t13_command_code(&vfd, VFD_T13_DIR_FORWARD);
        uint16_t run_rev = vfd_t13_command_code(&vfd, VFD_T13_DIR_REVERSE);
        if (!running && stop_pushed &&
            ((feedback.command == run_fwd) || (feedback.command == run_rev))) {
            ESP_LOGW(TAG, "the mixer is off but the drive reports command %u, stopping it again",
                     (unsigned)feedback.command);
            stop_pushed = false;
        }
    }

    vfd_t13_telemetry_t telemetry;
    if (vfd_t13_read_telemetry(&vfd, &telemetry) == ESP_OK) {
        last_telemetry = telemetry;
    }
}

static void mixer_publish_status(int64_t now, bool running)
{
    mixer_status_t snapshot = {0};

    bool online = vfd_t13_is_online(&vfd);

    if (running) {
        const mixer_step_t *step = &program.steps[step_index];

        snapshot.state = online ? MIXER_RUNNING : MIXER_ERROR;
        snapshot.step = step_index;

        int64_t left_us = step_deadline_us - now;
        if (left_us < 0) {
            left_us = 0;
        }
        // Rounded up, so a step with anything left on it never reads as 0 seconds.
        int64_t left_s = (left_us + (MIXER_US_PER_S - 1)) / MIXER_US_PER_S;
        snapshot.step_left_s = (uint16_t)((left_s > UINT16_MAX) ? UINT16_MAX : left_s);

        snapshot.step_type = step->type;
        snapshot.speed_rpm = (step->type == MIXER_STEP_PAUSE) ? 0
                                                              : (uint16_t)(step->speed * 10u);
    } else {
        snapshot.state = MIXER_STOPPED;
    }

    snapshot.program = program_index;
    snapshot.vfd_online = online;
    snapshot.freq_dhz = last_freq_dhz;
    snapshot.bus_voltage_dv = last_telemetry.bus_voltage_dv;
    snapshot.current_ca = last_telemetry.current_ca;
    snapshot.temp_c = last_telemetry.temp_c;
    snapshot.cmd_raw = last_cmd_raw;
    snapshot.last_write_err = vfd.last_write_err;

    if ((status_mutex != NULL) && (xSemaphoreTake(status_mutex, portMAX_DELAY) == pdTRUE)) {
        status = snapshot;
        xSemaphoreGive(status_mutex);
    }
}

static void mixer_tick(void)
{
    if (__atomic_exchange_n(&settings_dirty, false, __ATOMIC_SEQ_CST)) {
        mixer_reload_settings();
    }

    int64_t now = esp_timer_get_time();

    // An empty program is not a fault and not a run: there is nothing to do, so the drive is
    // held stopped exactly as if the mixer were switched off.
    bool running = (enabled && (program.count > 0));

    if (running) {
        if (!step_running) {
            step_index = 0;
            mixer_begin_step(now);
        } else if (now >= step_deadline_us) {
            step_index++;
            if (step_index >= program.count) {
                // Programs loop: the last step is followed by the first one again.
                step_index = 0;
            }
            mixer_begin_step(now);
        }

        // Retried on the next tick when the drive did not take it, without touching the
        // deadline the step is already running against.
        if (!step_pushed) {
            mixer_push_step();
            step_pushed = true;     // sent; whether it was taken is the feedback check's answer
        }
    } else {
        if (step_running) {
            step_running = false;
            stop_pushed = false;
        }
        if (!stop_pushed) {
            esp_err_t stop_err = vfd_t13_send_command(&vfd, VFD_T13_DIR_STOP);
            if (stop_err != ESP_OK) {
                ESP_LOGD(TAG, "the stop write answered %s, not repeating it every tick",
                         esp_err_to_name(stop_err));
            }
            // Sent is sent here too, and this is the case where it matters most: a switched-off
            // mixer is where a device spends most of its life, and trusting this drive's write
            // answers would put a stop frame on the bus five times a second for all of it. What
            // makes that safe is the poll below, which clears this flag again if the drive turns
            // out to be running after all.
            stop_pushed = true;
        }
    }

    if (now >= next_poll_us) {
        next_poll_us = now + MIXER_POLL_PERIOD_US;
        mixer_poll_vfd(running);
    }

    // A drive that has just started answering again is a drive that may have been reset, and a
    // reset one is standing still whatever it was doing before. Nothing it was told survived
    // that - neither the step nor the stop - so both are written into it again on the next tick,
    // and again without touching the deadline.
    bool online = vfd_t13_is_online(&vfd);
    if (online && !vfd_was_online) {
        ESP_LOGI(TAG, "the drive is answering again, telling it what it should be doing");
        step_pushed = false;
        stop_pushed = false;
    }
    vfd_was_online = online;

    mixer_publish_status(now, running);
}

static void mixer_task(void *arg)
{
    (void)arg;

    while (1) {
        mixer_tick();
        vTaskDelay(pdMS_TO_TICKS(MIXER_TICK_MS));
    }
}

void mixer_start(void)
{
    if (status_mutex == NULL) {
        // Statically allocated, so this cannot fail for a non-NULL buffer.
        status_mutex = xSemaphoreCreateMutexStatic(&status_mutex_buffer);
    }

    mixer_reload_settings();

    BaseType_t ret = xTaskCreate(mixer_task, "mixer_task", MIXER_TASK_STACK_SIZE, NULL,
                                 MIXER_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the mixer task");
    }
}

void mixer_get_status(mixer_status_t *out)
{
    if (out == NULL) {
        return;
    }

    if ((status_mutex != NULL) && (xSemaphoreTake(status_mutex, portMAX_DELAY) == pdTRUE)) {
        *out = status;
        xSemaphoreGive(status_mutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

esp_err_t mixer_set_enabled(bool on)
{
    esp_err_t ret = setting_items_save_bool(KEY_MIX_ENABLED, on);
    if (ret == ESP_OK) {
        mixer_settings_changed();
    }
    return ret;
}

esp_err_t mixer_set_program(uint8_t index)
{
    if (index >= MIXER_PROGRAM_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = setting_items_save_int(KEY_MIX_PROG, index);
    if (ret == ESP_OK) {
        mixer_settings_changed();
    }
    return ret;
}

bool mixer_is_enabled(void)
{
    // Straight from the setting rather than from the engine's copy: a caller that has just
    // switched the mixer on must not be told "off" for the rest of the tick.
    return setting_items_read_bool(KEY_MIX_ENABLED);
}

uint8_t mixer_get_program(void)
{
    // From the setting for the same reason as the switch above, and from the SAME place as it:
    // a caller that publishes the two together would otherwise show a fresh switch next to the
    // program number the engine held before the command - the snapshot only moves on its tick.
    int index = setting_items_read_int(KEY_MIX_PROG);
    return (uint8_t)(((index >= 0) && (index < MIXER_PROGRAM_COUNT)) ? index : 0);
}

void mixer_settings_changed(void)
{
    __atomic_store_n(&settings_dirty, true, __ATOMIC_SEQ_CST);
}

#ifdef __unittest_env__
    // The engine task's body, one tick at a time, and a way back to a known state. The task
    // itself is an infinite loop around this call, so a host test drives the engine by
    // stepping the esp_timer mock and calling this.
    void mixer_test_tick(void)
    {
        mixer_tick();
    }

    void mixer_test_reset(void)
    {
        memset(&vfd, 0, sizeof(vfd));
        memset(&program, 0, sizeof(program));
        memset(&status, 0, sizeof(status));
        memset(&last_telemetry, 0, sizeof(last_telemetry));
        memset(applied_program, 0, sizeof(applied_program));
        vfd_was_online = true;
        program_index = 0;
        rpm_50hz = 1;
        enabled = false;
        step_index = 0;
        step_deadline_us = 0;
        step_running = false;
        step_pushed = false;
        stop_pushed = false;
        next_poll_us = 0;
        last_freq_dhz = 0;
        last_cmd_raw = 0;
        status_mutex = NULL;
        settings_dirty = false;
    }
#endif
