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
 * One task does every write. At boot it puts the relay in the closed state. Before each request
 * it re-reads the unit id, the channel and the mode and compares them with the ones it last
 * applied; on a change it switches the relay it no longer uses OFF, puts the one it now uses in
 * the closed state and drops a manual hold. A settings save also queues that comparison on its
 * own (fp_lock_settings_changed()), so a new target or mode takes effect at once rather than at
 * the next request: after a switch from "strike" to "magnet" the magnet would stay unpowered,
 * the door open. The pulse length is read at every opening.
 */

#include <stdbool.h>

// Creates the lock task. Call once, after the Modbus master has been started.
void fp_lock_start(void);

// Asks for one pulse and returns at once. True when the request was taken; false when it was
// dropped because a pulse is already under way (or the task is not running, or its queue is
// full) - a second request during an opening adds nothing, so none is queued behind it.
bool fp_lock_open(void);

// Asks the same task to switch the relay by hand and returns at once; a command sent during a
// pulse runs after it. on is the relay state itself, whatever the mode - the web switch shows
// and sets the relay. The open state is held: pulses are skipped while it is, the door being
// already open. The closed state releases the hold. A change of the target or the mode drops it
// too. Nothing is kept across a reboot: the relay starts closed and not held. False when the
// request was not taken: the task is not running, or its queue is full.
bool fp_lock_set(bool on);

// Asks the task to bring the relay in line with the lock settings (see above) and returns at
// once. Called by settings_update() after every settings save.
void fp_lock_settings_changed(void);

// The relay - not the door - as of the last coil write that succeeded, pulses included: true
// after an ON, false after an OFF, false after boot until a write goes through.
bool fp_lock_relay_on(void);
