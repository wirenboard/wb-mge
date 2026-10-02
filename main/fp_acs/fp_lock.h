#pragma once

/*
 * The door lock: one channel of a Wiren Board relay module on RS-485 port 1, switched through
 * the Modbus master (FC05, coil = channel - 1). lock_mode says which relay state opens the door:
 *
 *   "strike"  an electric strike, powered to open: relay ON = open, OFF = locked (the default)
 *   "magnet"  an electromagnet, powered to hold the door: relay OFF = open, ON = locked
 *
 * An opening puts the relay in the open state for lock_pulse_ms and back in the closed state.
 *
 * The relay module is configured to do its part on its own (FC03 and FC06, WB-MR firmware 1.28.0
 * or later): the channel's timer puts the relay back in the closed state lock_pulse_ms (rounded up
 * to 0.1 s) after every command to the channel, ours included - function 6 "on for a time" for a
 * strike, 7 "off for a time" for a magnet - and, with lock_exit_input set, the closing of that
 * module input puts the channel in the open state (input mode 4, the leading-edge mapping matrix),
 * so the exit button works without this device; no other input acts on the channel. That
 * configuration is reconciled rather than tracked: the task reads it back from the module and
 * writes only the registers that differ, so the module's EEPROM is not written while it matches.
 * An input given up has its action on the channel cleared at the next reconciliation and keeps its
 * mode 4.
 *
 * One task does every write. At boot it puts the relay in the closed state. A closing write that
 * does not get through (at boot, at the end of an opening, a manual close or a settings change) is
 * repeated every 5 s until the relay takes it or a manual open goes through; one at the end of an
 * opening, a manual close or a settings change is also repeated while the last reconciliation has not
 * found the channel's timer matching, unless the module has refused the timer registers at three in a
 * row, since a timer function of the old mode would open the door again. Every 5 s without a
 * request it also reconciles the module's configuration, while the module answers, before that
 * repeated closing write. Before each request it re-reads the unit id, the channel, the mode, the
 * pulse length and the exit button's input and compares them with the ones it last applied. On a
 * change of the unit id or the channel it first releases the channel it no longer uses: clears its
 * timer function and the leading-edge actions of all the inputs on it, then switches it OFF - each
 * write once, the first that gets no answer ending the release (that module may be gone). On any
 * change it then reconciles the module with the new settings, and on a change of the unit id, the
 * channel or the mode it puts the relay it now uses in the closed state - after the reconciliation,
 * so that state is not written under the channel's old timer function; when it did not go through
 * or the channel's timer is not yet found matching, that closing write is repeated as above, the
 * idle pass reconciling first - and drops a manual hold. A settings save also queues that
 * comparison on its own (fp_lock_settings_changed()), so a new target or mode takes effect at once
 * rather than at the next request: after a switch from "strike" to "magnet" the magnet would stay
 * unpowered, the door open. The pulse length is read at every opening.
 */

#include <stdbool.h>

#include "esp_err.h"

// Creates the lock task. Call once, after the Modbus master has been started.
void fp_lock_start(void);

// Asks for one pulse and returns at once. True when the request was taken; false when it was
// dropped because a pulse is already under way (or the task is not running, or its queue is
// full) - a second request during an opening adds nothing, so none is queued behind it.
bool fp_lock_open(void);

// Asks the same task to switch the relay by hand and returns at once; a command sent during a
// pulse runs after it. on is the relay state itself, whatever the mode - the web switch shows
// and sets the relay. Unless the last reconciliation found the channel's timer matching the
// settings (it did whenever fp_lock_config_error() is ESP_OK) the open state is held: pulses are
// skipped while it is, the door being already open; the closed state releases the hold, and so does
// a change of the target or the mode, or a reconciliation that finds the timer matching. With the
// timer matching the module closes the relay by itself after lock_pulse_ms, so the open state is
// not held.
// Nothing is kept across a reboot: the relay starts closed and not held. False when the
// request was not taken: the task is not running, or its queue is full.
bool fp_lock_set(bool on);

// Asks the task to bring the relay in line with the lock settings (see above) and returns at
// once. Called by settings_update() after every settings save.
void fp_lock_settings_changed(void);

// The relay - not the door - as of the last successful coil write (pulses included) or idle-probe
// coil read, whichever came last: the read also sees the module's timer switching the relay by
// itself. False after boot until a write or a read goes through.
bool fp_lock_relay_on(void);

// Whether the relay module answered the last exchange with it: a coil write (after its retries)
// or the coil read the task makes whenever it has had no request for FP_LOCK_PROBE_MS (fp_lock.c),
// or, while a closing write has not gone through, a single closing write in place of that read.
// False after boot until the module answers.
bool fp_lock_relay_online(void);

// How the last reconciliation of the relay module's configuration went - made every 5 s without a
// request while the module answers, and on every change of the lock settings: ESP_OK once the module
// holds all of it, no other input acting on the channel included, else the error of the first read or
// write that failed: ESP_ERR_INVALID_RESPONSE for a Modbus exception, which is how a module with a
// firmware older than 1.28.0 refuses the timer functions, or for a damaged frame (esp-modbus does not
// tell the two apart), ESP_ERR_TIMEOUT when the module did not answer;
// ESP_ERR_NOT_FINISHED until the first try after boot. The best-effort release of a channel given up
// does not count.
esp_err_t fp_lock_config_error(void);
