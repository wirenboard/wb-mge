#include "fp_lock.h"

#include "fp_acs.h"

#if FP_ACS_ENABLED

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mb_master.h"
#include "setting_items.h"

static const char *TAG = "fp_lock";

#define FP_LOCK_PORT                0           // RS-485 port 1 in mb_master's numbering
#define FP_LOCK_FIRST_CHANNEL       1           // lock_channel counts relays from 1, coils from 0
#define FP_LOCK_WRITE_RETRIES       3
// A port being restarted by a settings apply refuses at once; the pause lets it come back up.
#define FP_LOCK_RETRY_DELAY_MS      200
// A pulse waiting to be taken plus the manual commands and settings checks sent while a pulse
// runs, run after it.
#define FP_LOCK_QUEUE_LENGTH        4
// With no request for this long, the task reconciles the relay module's configuration with the settings
// (fp_lock_reconcile()), then reads the coil once to learn whether the module answers, or repeats a
// closing write that has not gone through.
#define FP_LOCK_PROBE_MS            5000
#define FP_LOCK_TASK_STACK_SIZE     (4 * 1024)  // as main/mb_master/user_app.c: the same call path
#define FP_LOCK_TASK_PRIORITY       5

// The relay module's own holding registers (FC06), as the Wiren Board wiki gives them for the WB-MR
// modules: "Relay Module Modbus Management", "I/O Mapping Matrix" and the timer registers (functions
// 6 and 7 from FW 1.28.0). Inputs and channels count from 1, the offsets below from 0.
#define FP_LOCK_FIRST_INPUT         1
#define FP_LOCK_INPUTS              6           // the inputs of a WB-MR6C, as lock_exit_input offers them
#define FP_LOCK_NO_INPUT            0           // lock_exit_input "off"
#define FP_LOCK_REG_INPUT_MODE      9           // + input - 1: what the input does
#define FP_LOCK_INPUT_MODE_EDGES    4           // switch the outputs by the edge mapping matrix
#define FP_LOCK_REG_LEADING_EDGE    864         // + (input - 1) * 8 + channel - 1: what closing the input does
#define FP_LOCK_EDGE_ROW            8           // channels per input in the mapping matrix
// The leading-edge rows of all the inputs, read in one FC03: 48 registers, within the 125 one
// response carries (MB_MASTER_MAX_READ_REGS in mb_master.c).
#define FP_LOCK_EDGE_REGS           (FP_LOCK_INPUTS * FP_LOCK_EDGE_ROW)
#define FP_LOCK_EDGE_NOTHING        0
#define FP_LOCK_EDGE_OFF            1
#define FP_LOCK_EDGE_ON             2
#define FP_LOCK_REG_TIMER_FUNCTION  1760        // + channel - 1
#define FP_LOCK_TIMER_NONE          0           // no timer: the channel only follows commands
#define FP_LOCK_TIMER_ON_FOR_TIME   6           // "on for a time (with command reset)": a strike
#define FP_LOCK_TIMER_OFF_FOR_TIME  7           // "off for a time (with command reset)": a magnet
#define FP_LOCK_REG_ON_TIME         1784        // + channel - 1: the time of FP_LOCK_TIMER_ON_FOR_TIME
#define FP_LOCK_REG_OFF_TIME        1792        // + channel - 1: the time of FP_LOCK_TIMER_OFF_FOR_TIME
#define FP_LOCK_TIME_UNIT_MS        100         // the timers count in 0.1 s
#define FP_LOCK_SINGLE_REG          1           // the register count of a one-register FC03
// esp-modbus reports a Modbus exception - how a module with a firmware older than 1.28.0 refuses the
// timer functions - and a damaged frame alike, as ESP_ERR_INVALID_RESPONSE: the refusal is told by
// its coming back at this many reconciliations in a row.
#define FP_LOCK_REFUSED_AFTER       3

typedef enum {
    FP_LOCK_REQUEST_PULSE,                      // fp_lock_open()
    FP_LOCK_REQUEST_ON,                         // fp_lock_set(true)
    FP_LOCK_REQUEST_OFF,                        // fp_lock_set(false)
    FP_LOCK_REQUEST_SYNC,                       // fp_lock_settings_changed()
} fp_lock_request_t;

// The relay the lock uses, the state that opens the door and what the relay module is to be configured
// with, as the settings give them.
typedef struct {
    uint8_t  unit;
    uint16_t coil;
    bool     open;                              // the coil level that opens the door: ON for a strike
    uint16_t time;                              // lock_pulse_ms in the 0.1 s of the channel's timer
    uint8_t  input;                             // the exit button's input, FP_LOCK_NO_INPUT for none
} fp_lock_target_t;

static QueueHandle_t fp_lock_queue;

// True from the moment an open request is taken until its pulse has ended (or was skipped); read and written through
// the atomic builtins, since fp_lock_open() is called from the sensor task, the bot and the MQTT task alike.
static bool fp_lock_busy;

// The relay as of the last coil write that succeeded or the last idle probe that read it: written by
// the lock task, read by the web server, hence the atomic builtins as well.
static bool fp_lock_relay_state;

// Whether the relay module answered the last exchange: a write (after its retries), an idle probe or a
// repeated closing write.
// Written by the lock task, read by the web server, hence the atomic builtins.
static bool fp_lock_online;

// How the last reconciliation of the relay module's configuration went (fp_lock_reconcile()): ESP_OK
// once the module holds all of it, ESP_ERR_NOT_FINISHED before the first one. Written by the lock task,
// read by the web server, hence the atomic builtins.
static esp_err_t fp_lock_config_err = ESP_ERR_NOT_FINISHED;

// Consecutive reconciliations whose timer registers drew ESP_ERR_INVALID_RESPONSE, up to
// FP_LOCK_REFUSED_AFTER. Only the lock task uses it.
static uint8_t fp_lock_timer_refusals;


// Logs only a change, not every exchange.
static void fp_lock_set_online(uint8_t unit, esp_err_t err)
{
    bool online = (err == ESP_OK);
    bool was_online = __atomic_exchange_n(&fp_lock_online, online, __ATOMIC_SEQ_CST);
    if (online == was_online) {
        return;
    }
    if (online) {
        ESP_LOGI(TAG, "Relay module unit %u is online", (unsigned)unit);
    } else {
        ESP_LOGW(TAG, "Relay module unit %u is offline: %s", (unsigned)unit, esp_err_to_name(err));
    }
}

// One coil write, retried: a relay module that missed a frame on a noisy bus answers the next.
static esp_err_t fp_lock_write(uint8_t unit, uint16_t coil, bool on)
{
    esp_err_t err = ESP_FAIL;
    for (unsigned attempt = 0; attempt <= FP_LOCK_WRITE_RETRIES; attempt++) {
        err = mb_master_write_coil(FP_LOCK_PORT, unit, coil, on);
        if (err == ESP_OK) {
            __atomic_store_n(&fp_lock_relay_state, on, __ATOMIC_SEQ_CST);
            fp_lock_set_online(unit, ESP_OK);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Unit %u coil %u %s: attempt %u failed: %s", (unsigned)unit, (unsigned)coil,
                 on ? "ON" : "OFF", attempt + 1, esp_err_to_name(err));
        if (attempt < FP_LOCK_WRITE_RETRIES) {
            vTaskDelay(pdMS_TO_TICKS(FP_LOCK_RETRY_DELAY_MS));
        }
    }
    ESP_LOGE(TAG, "Unit %u coil %u %s: giving up after %u attempts", (unsigned)unit, (unsigned)coil,
             on ? "ON" : "OFF", FP_LOCK_WRITE_RETRIES + 1);
    fp_lock_set_online(unit, err);
    return err;
}

// One coil read, not retried: the next probe comes FP_LOCK_PROBE_MS later anyway. The value read is
// what fp_lock_relay_on() reports from then on: the module's timer switches the coil by itself.
static void fp_lock_probe(const fp_lock_target_t *target)
{
    bool value = false;
    esp_err_t err = mb_master_read_coil(FP_LOCK_PORT, target->unit, target->coil, &value);
    if (err == ESP_OK) {
        __atomic_store_n(&fp_lock_relay_state, value, __ATOMIC_SEQ_CST);
    }
    fp_lock_set_online(target->unit, err);
}

// One write of the closed state, not retried, in place of a probe: the next try comes
// FP_LOCK_PROBE_MS later anyway. True once the relay took it.
static bool fp_lock_try_close(const fp_lock_target_t *target)
{
    bool closed = (!target->open);
    esp_err_t err = mb_master_write_coil(FP_LOCK_PORT, target->unit, target->coil, closed);
    if (err == ESP_OK) {
        __atomic_store_n(&fp_lock_relay_state, closed, __ATOMIC_SEQ_CST);
        ESP_LOGI(TAG, "Unit %u coil %u %s: the lock is closed", (unsigned)target->unit, (unsigned)target->coil,
                 closed ? "ON" : "OFF");
    }
    fp_lock_set_online(target->unit, err);
    return (err == ESP_OK);
}

// Read before every request (fp_lock_sync()), so a changed setting applies to the next one.
static void fp_lock_read_target(fp_lock_target_t *target)
{
    target->unit = (uint8_t)setting_items_read_int(KEY_LOCK_ADDR);
    target->coil = (uint16_t)(setting_items_read_int(KEY_LOCK_CHANNEL) - FP_LOCK_FIRST_CHANNEL);

    // Anything but "magnet" is the strike, the default: the value is validated on the way in.
    char mode[SETTING_ITEM_MAX_STR_LEN] = {0};
    (void)setting_items_read(KEY_LOCK_MODE, mode);
    target->open = (strcmp(mode, LOCK_MODE_MAGNET_STR) != 0);

    // Rounded up, so the module's timer never ends an opening before lock_pulse_ms. The validator holds
    // lock_pulse_ms to 100..60000, which makes this 1..600.
    target->time = (uint16_t)((setting_items_read_int(KEY_LOCK_PULSE_MS) + (FP_LOCK_TIME_UNIT_MS - 1))
                              / FP_LOCK_TIME_UNIT_MS);

    // "off" or a single digit 1..6: the value is validated on the way in.
    char input[SETTING_ITEM_MAX_STR_LEN] = {0};
    (void)setting_items_read(KEY_LOCK_EXIT_INPUT, input);
    if (strcmp(input, LOCK_EXIT_INPUT_OFF_STR) == 0) {
        target->input = FP_LOCK_NO_INPUT;
    } else {
        target->input = (uint8_t)strtol(input, NULL, 10);
    }
}

static const char *fp_lock_mode_str(bool open)
{
    return open ? LOCK_MODE_STRIKE_STR : LOCK_MODE_MAGNET_STR;
}

// The leading-edge action of an input on a channel (its coil): its index among the FP_LOCK_EDGE_REGS
// registers from FP_LOCK_REG_LEADING_EDGE.
static uint16_t fp_lock_edge_cell(uint16_t coil, uint8_t input)
{
    return (uint16_t)(((input - FP_LOCK_FIRST_INPUT) * FP_LOCK_EDGE_ROW) + coil);
}

// Writes want to a holding register just read to hold have, and nothing when it holds that already:
// the module keeps these registers in its EEPROM, which a write at every idle pass would wear out. Not
// retried: the next idle pass reconciles again. On a failure *failed says which register it was.
static esp_err_t fp_lock_update_reg(uint8_t unit, uint16_t reg, uint16_t have, uint16_t want, uint16_t *failed)
{
    if (have == want) {
        return ESP_OK;
    }
    esp_err_t err = mb_master_write_holding(FP_LOCK_PORT, unit, reg, want);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Unit %u register %u: %u -> %u", (unsigned)unit, (unsigned)reg, (unsigned)have,
                 (unsigned)want);
    } else {
        *failed = reg;
    }
    return err;
}

// One holding register read, then fp_lock_update_reg().
static esp_err_t fp_lock_reconcile_reg(uint8_t unit, uint16_t reg, uint16_t want, uint16_t *failed)
{
    uint16_t have = 0;
    esp_err_t err = mb_master_read_holding(FP_LOCK_PORT, unit, reg, FP_LOCK_SINGLE_REG, &have);
    if (err != ESP_OK) {
        *failed = reg;
        return err;
    }
    return fp_lock_update_reg(unit, reg, have, want, failed);
}

// Brings what the relay module does on its own in line with the target, so that the door works
// without this device: the channel's timer puts the relay back in the closed state target->time after
// every command to the channel - an opening of ours included, which re-locks the door should this
// device die during it - and, with an exit button, the closing of its input puts the channel in the
// open state, while no other input acts on the channel: the action of an input given up is cleared
// here, and the input keeps its mode. Reads the module's values and writes only the ones that differ,
// the time before the timer function, which would otherwise start with whatever time the channel had,
// and the button's action before its input mode, so the input starts switching by the matrix with its
// action in place. The other inputs' actions come last, so that one the module does not take never
// keeps the button's own from going through. Stops at the first exchange that fails, before the matrix
// when it is the timer's: the button would otherwise open a strike for good. *timer_ok says whether
// the time and the timer function now match. Returns ESP_OK when everything matches, else the error of
// the exchange that failed.
static esp_err_t fp_lock_reconcile(const fp_lock_target_t *target, bool *timer_ok)
{
    uint16_t failed = 0;
    uint16_t time_reg = (uint16_t)((target->open ? FP_LOCK_REG_ON_TIME : FP_LOCK_REG_OFF_TIME) + target->coil);
    esp_err_t err = fp_lock_reconcile_reg(target->unit, time_reg, target->time, &failed);
    if (err == ESP_OK) {
        uint16_t function = target->open ? FP_LOCK_TIMER_ON_FOR_TIME : FP_LOCK_TIMER_OFF_FOR_TIME;
        err = fp_lock_reconcile_reg(target->unit, (uint16_t)(FP_LOCK_REG_TIMER_FUNCTION + target->coil), function,
                                    &failed);
    }
    *timer_ok = (err == ESP_OK);
    if (err != ESP_ERR_INVALID_RESPONSE) {
        fp_lock_timer_refusals = 0;
    } else if (fp_lock_timer_refusals < FP_LOCK_REFUSED_AFTER) {
        fp_lock_timer_refusals++;
    }

    uint16_t edges[FP_LOCK_EDGE_REGS] = {0};
    if (err == ESP_OK) {
        err = mb_master_read_holding(FP_LOCK_PORT, target->unit, FP_LOCK_REG_LEADING_EDGE, FP_LOCK_EDGE_REGS, edges);
        if (err != ESP_OK) {
            failed = FP_LOCK_REG_LEADING_EDGE;
        }
    }
    if ((err == ESP_OK) && (target->input != FP_LOCK_NO_INPUT)) {
        uint16_t cell = fp_lock_edge_cell(target->coil, target->input);
        uint16_t action = target->open ? FP_LOCK_EDGE_ON : FP_LOCK_EDGE_OFF;
        err = fp_lock_update_reg(target->unit, (uint16_t)(FP_LOCK_REG_LEADING_EDGE + cell), edges[cell], action,
                                 &failed);
        if (err == ESP_OK) {
            uint16_t reg = (uint16_t)(FP_LOCK_REG_INPUT_MODE + (target->input - FP_LOCK_FIRST_INPUT));
            err = fp_lock_reconcile_reg(target->unit, reg, FP_LOCK_INPUT_MODE_EDGES, &failed);
        }
    }
    for (uint8_t input = FP_LOCK_FIRST_INPUT; (err == ESP_OK) && (input <= FP_LOCK_INPUTS); input++) {
        if (input == target->input) {
            continue;
        }
        uint16_t cell = fp_lock_edge_cell(target->coil, input);
        err = fp_lock_update_reg(target->unit, (uint16_t)(FP_LOCK_REG_LEADING_EDGE + cell), edges[cell],
                                 FP_LOCK_EDGE_NOTHING, &failed);
    }

    // Logged on a change only: this runs at every idle pass, and a relay module that refuses these
    // registers (an old firmware answers them with a Modbus exception) is asked again at each.
    esp_err_t was = __atomic_exchange_n(&fp_lock_config_err, err, __ATOMIC_SEQ_CST);
    if (err == was) {
        return err;
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Unit %u configured: channel %u back to %s %u x 0.1 s after each command, "
                 "exit button input %u (0: none)", (unsigned)target->unit,
                 (unsigned)(target->coil + FP_LOCK_FIRST_CHANNEL), target->open ? "OFF" : "ON",
                 (unsigned)target->time, (unsigned)target->input);
    } else {
        ESP_LOGW(TAG, "Unit %u register %u: %s: the relay module is not configured", (unsigned)target->unit,
                 (unsigned)failed, esp_err_to_name(err));
    }
    return err;
}

// Whether a closing write may yet be undone by the module: the last reconciliation did not find the
// channel's timer matching (timer_ok), and the module has not shown it refuses the timer functions -
// the timer may still be a function of the old mode, which would open the door after the time. A
// closing write made then stays pending, for the idle pass to repeat after reconciling.
static bool fp_lock_timer_unsettled(bool timer_ok)
{
    return ((!timer_ok) && (fp_lock_timer_refusals < FP_LOCK_REFUSED_AFTER));
}

// The target comes from the last sync, not from the settings, so the closing write goes to the same
// relay, in the same sense, as the opening one even if the settings change during the pulse.
// Returns how the closing write went.
static esp_err_t fp_lock_pulse(const fp_lock_target_t *target)
{
    int pulse_ms = setting_items_read_int(KEY_LOCK_PULSE_MS);

    ESP_LOGI(TAG, "Opening: unit %u, channel %u, %s, %d ms", (unsigned)target->unit,
             (unsigned)(target->coil + FP_LOCK_FIRST_CHANNEL), fp_lock_mode_str(target->open), pulse_ms);
    (void)fp_lock_write(target->unit, target->coil, target->open);
    vTaskDelay(pdMS_TO_TICKS(pulse_ms));
    // Whatever the opening write returned: one that timed out may still have switched the relay,
    // and a door left open is worse than a closing write the relay did not need.
    return fp_lock_write(target->unit, target->coil, !target->open);
}

// on is the coil level itself, as the web switch shows it, whatever the mode. Returns how the write went.
static esp_err_t fp_lock_switch(const fp_lock_target_t *target, bool on, bool *held)
{
    ESP_LOGI(TAG, "Manual %s: unit %u, channel %u, %s", on ? "ON" : "OFF", (unsigned)target->unit,
             (unsigned)(target->coil + FP_LOCK_FIRST_CHANNEL), fp_lock_mode_str(target->open));
    if (on == target->open) {
        // Held only once the relay did open: a hold over a relay left closed would skip every
        // opening, while the web UI, showing the relay unchanged, offers nothing but the same
        // command again.
        esp_err_t err = fp_lock_write(target->unit, target->coil, on);
        if (err == ESP_OK) {
            *held = true;
        }
        return err;
    }
    *held = false;
    return fp_lock_write(target->unit, target->coil, on);
}

// Releases the channel the lock no longer uses (old) after a change of the unit id or the channel:
// clears its timer function and the leading-edge actions of all the inputs on it, then switches it
// OFF. The timer goes first: under "off for a time" (a magnet) the module would switch the channel back
// ON after the time. Best effort, the old module may be gone: each register write is made once, the
// first that gets no answer ends the release, and what it did not clear is given up - nothing
// reconciles that channel any more. The inputs keep their mode.
static void fp_lock_release(const fp_lock_target_t *old)
{
    // A module that does not answer (one swapped out, say) is not waited for write after write.
    esp_err_t err = mb_master_write_holding(FP_LOCK_PORT, old->unit,
                                            (uint16_t)(FP_LOCK_REG_TIMER_FUNCTION + old->coil), FP_LOCK_TIMER_NONE);
    for (uint8_t input = FP_LOCK_FIRST_INPUT; (err != ESP_ERR_TIMEOUT) && (input <= FP_LOCK_INPUTS); input++) {
        err = mb_master_write_holding(FP_LOCK_PORT, old->unit,
                                      (uint16_t)(FP_LOCK_REG_LEADING_EDGE + fp_lock_edge_cell(old->coil, input)),
                                      FP_LOCK_EDGE_NOTHING);
    }
    if (err != ESP_ERR_TIMEOUT) {
        (void)fp_lock_write(old->unit, old->coil, false);
    }
}

// Brings the relay in line with the settings before a request is handled. On a change of the unit
// id or the channel, the relay the lock no longer uses is released first (fp_lock_release()). On any
// change, the pulse length and the exit button's input included, the relay module is then reconciled
// with the new target (fp_lock_reconcile()). On a change of the unit id, the channel or the mode, the
// relay the lock now uses is put in the closed state only after that, since under the channel's old
// timer function the write could have the module open the door by itself, and the hold is dropped: it
// was on the old relay, or in the old sense of the coil. That closing write is left pending
// (closing_pending), for the idle loop to repeat, when it did not go through and also while the
// timer is unsettled (fp_lock_timer_unsettled()): it may then have gone out under the old timer
// function, and the idle loop reconciles before it repeats it. A timer found matching (*timer_ok)
// drops the hold too: the module then closes the relay by itself. The new target counts as applied
// whatever the exchanges return; the idle loop reconciles it again at every pass.
static void fp_lock_sync(fp_lock_target_t *applied, bool *timer_ok, bool *held, bool *closing_pending)
{
    fp_lock_target_t current;
    fp_lock_read_target(&current);

    bool moved = ((current.unit != applied->unit) || (current.coil != applied->coil));
    bool switched = (moved || (current.open != applied->open));
    if ((!switched) && (current.input == applied->input) && (current.time == applied->time)) {
        return;
    }

    ESP_LOGI(TAG, "Lock settings changed: unit %u, channel %u, %s, %u x 0.1 s, exit button input %u (0: none)",
             (unsigned)current.unit, (unsigned)(current.coil + FP_LOCK_FIRST_CHANNEL),
             fp_lock_mode_str(current.open), (unsigned)current.time, (unsigned)current.input);
    if (moved) {
        fp_lock_release(applied);
        // The refusals counted were the old module's.
        fp_lock_timer_refusals = 0;
    }
    *applied = current;
    (void)fp_lock_reconcile(applied, timer_ok);
    if (switched) {
        bool closed = (fp_lock_write(current.unit, current.coil, !current.open) == ESP_OK);
        *closing_pending = ((!closed) || fp_lock_timer_unsettled(*timer_ok));
        *held = false;
    }
    if (*timer_ok) {
        *held = false;
    }
}

static void fp_lock_task(void *arg)
{
    (void)arg;

    // Only this task uses these. applied is the target - the relay, the mode, the pulse length and the
    // exit button's input - last applied, at boot or by a sync. timer_ok is whether the last
    // reconciliation found the channel's timer matching the settings, false until the first one. held
    // is set by a manual command to the open state while timer_ok is false, and cleared by one to the
    // closed state, by a sync that found the relay or the mode changed, and whenever a reconciliation
    // finds the timer matching: the module's timer then closes the relay by itself, and a hold would
    // only skip openings.
    fp_lock_target_t applied;
    bool timer_ok = false;
    bool held = false;

    // A reboot in the middle of a pulse or a hold leaves the relay module as it was: it has its own
    // power and keeps its coil, so the door would stay open until the next opening. A relay module
    // powered up with the device starts with its coil OFF, which leaves a magnet open. The bus is
    // not up yet this early at boot (measured: the relay did not answer for the first 10 s). So a
    // closing write that does not get through - this one, or the one that ends a pulse, a manual
    // close or a settings change - is repeated in place of the idle probe until it does, or until
    // a manual open goes through; the last three are also repeated after going through while the
    // timer is unsettled (fp_lock_timer_unsettled()). The module keeps its configuration across a
    // reboot of this device; the first idle pass that finds it answering reconciles it.
    fp_lock_read_target(&applied);
    bool closing_pending = (fp_lock_write(applied.unit, applied.coil, !applied.open) != ESP_OK);

    for (;;) {
        fp_lock_request_t request = FP_LOCK_REQUEST_PULSE;
        if (xQueueReceive(fp_lock_queue, &request, pdMS_TO_TICKS(FP_LOCK_PROBE_MS)) != pdTRUE) {
            // No request: the sync first, so everything below goes to the relay the settings now name.
            fp_lock_sync(&applied, &timer_ok, &held, &closing_pending);
            // Then the module's configuration, read back and repaired where it differs - on a module
            // that answered the last exchange, or whenever a closing write is pending: reads of a
            // module that does not answer would otherwise hold the task up to no purpose - and before
            // the repeated closing write, so that write goes out under the timer function the settings
            // call for.
            if (fp_lock_relay_online() || closing_pending) {
                (void)fp_lock_reconcile(&applied, &timer_ok);
                if (timer_ok) {
                    held = false;
                }
            }
            if (closing_pending) {
                bool closed = fp_lock_try_close(&applied);
                closing_pending = ((!closed) || fp_lock_timer_unsettled(timer_ok));
            } else {
                fp_lock_probe(&applied);
            }
            continue;
        }
        fp_lock_sync(&applied, &timer_ok, &held, &closing_pending);
        switch (request) {
        case FP_LOCK_REQUEST_PULSE:
            // The door is already open, and the pulse would end by closing it.
            if (held) {
                ESP_LOGI(TAG, "Opening skipped: the relay is held open by hand");
            } else {
                closing_pending = ((fp_lock_pulse(&applied) != ESP_OK) || fp_lock_timer_unsettled(timer_ok));
            }
            __atomic_store_n(&fp_lock_busy, false, __ATOMIC_SEQ_CST);
            break;
        case FP_LOCK_REQUEST_ON:
        case FP_LOCK_REQUEST_OFF: {
            bool on = (request == FP_LOCK_REQUEST_ON);
            esp_err_t err = fp_lock_switch(&applied, on, &held);
            // With the module's timer matching the relay closes by itself after lock_pulse_ms: a
            // manual open is an opening like any other, not a hold that would skip the next ones.
            if (timer_ok) {
                held = false;
            }
            // A manual close that did not go through, or went out under an unsettled timer, stays
            // pending. A manual open that went through ends a pending close, which would undo it; one
            // that did not leaves the pending close as it was: a write that timed out may still have
            // opened the relay, and the repeated close brings it back.
            if (on != applied.open) {
                closing_pending = ((err != ESP_OK) || fp_lock_timer_unsettled(timer_ok));
            } else if (err == ESP_OK) {
                closing_pending = false;
            }
            break;
        }
        case FP_LOCK_REQUEST_SYNC:
            // The sync above is the whole request.
            break;
        }
    }
}

void fp_lock_start(void)
{
    fp_lock_queue = xQueueCreate(FP_LOCK_QUEUE_LENGTH, sizeof(fp_lock_request_t));
    if (fp_lock_queue == NULL) {
        ESP_LOGE(TAG, "No memory for the request queue: the lock will not open");
        return;
    }
    if (xTaskCreate(fp_lock_task, "fp_lock", FP_LOCK_TASK_STACK_SIZE, NULL, FP_LOCK_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the lock task: the lock will not open");
        vQueueDelete(fp_lock_queue);
        fp_lock_queue = NULL;
    }
}

bool fp_lock_open(void)
{
    if (fp_lock_queue == NULL) {
        ESP_LOGE(TAG, "Open request ignored: the lock task is not running");
        return false;
    }

    bool idle = false;
    if (!__atomic_compare_exchange_n(&fp_lock_busy, &idle, true, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        ESP_LOGI(TAG, "Open request dropped: a pulse is already under way");
        return false;
    }

    fp_lock_request_t request = FP_LOCK_REQUEST_PULSE;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        __atomic_store_n(&fp_lock_busy, false, __ATOMIC_SEQ_CST);
        return false;
    }
    return true;
}

bool fp_lock_set(bool on)
{
    if (fp_lock_queue == NULL) {
        ESP_LOGE(TAG, "Manual %s ignored: the lock task is not running", on ? "ON" : "OFF");
        return false;
    }

    fp_lock_request_t request = on ? FP_LOCK_REQUEST_ON : FP_LOCK_REQUEST_OFF;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Manual %s dropped: the request queue is full", on ? "ON" : "OFF");
        return false;
    }
    return true;
}

void fp_lock_settings_changed(void)
{
    // The task was never started, and fp_lock_start() has said why.
    if (fp_lock_queue == NULL) {
        return;
    }

    // A full queue loses nothing: every request in it runs the same sync first.
    fp_lock_request_t request = FP_LOCK_REQUEST_SYNC;
    if (xQueueSend(fp_lock_queue, &request, 0) != pdTRUE) {
        ESP_LOGD(TAG, "Settings check not queued: the queue is full, and each request in it checks first");
    }
}

bool fp_lock_relay_on(void)
{
    return __atomic_load_n(&fp_lock_relay_state, __ATOMIC_SEQ_CST);
}

bool fp_lock_relay_online(void)
{
    return __atomic_load_n(&fp_lock_online, __ATOMIC_SEQ_CST);
}

esp_err_t fp_lock_config_error(void)
{
    return __atomic_load_n(&fp_lock_config_err, __ATOMIC_SEQ_CST);
}

#endif // FP_ACS_ENABLED
