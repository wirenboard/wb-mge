#pragma once

/*
 * The door lock: one channel of a Wiren Board relay module on RS-485 port 1, switched ON for
 * lock_pulse_ms and OFF again through the Modbus master (FC05, coil = channel - 1). The unit
 * id, the channel and the pulse length are read from the settings at every pulse, so a change
 * applies to the next opening without a restart.
 */

#include <stdbool.h>

// Creates the pulse task. Call once, after the Modbus master has been started.
void fp_lock_start(void);

// Asks for one pulse and returns at once. True when the request was taken; false when it was
// dropped because a pulse is already under way (or the task is not running) - a second
// request during an opening adds nothing, so none is queued behind it.
bool fp_lock_open(void);
