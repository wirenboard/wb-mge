/* Test-side controls of the mixer mock: the snapshot the engine would report, and what the two
 * command calls recorded. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "mixer.h"

extern mixer_status_t mock_mixer_status;
extern bool mock_mixer_enabled;

/* What mixer_get_program() answers - the stored setting, not the engine's snapshot. The two are
 * separate here because the code under test must read the program from the same place as the
 * switch, and only this one moves the moment a command is accepted. */
extern uint8_t mock_mixer_program;

extern int mock_mixer_set_enabled_called;
extern bool mock_mixer_set_enabled_value;
extern esp_err_t mock_mixer_set_enabled_return;

extern int mock_mixer_set_program_called;
extern uint8_t mock_mixer_set_program_value;
extern esp_err_t mock_mixer_set_program_return;

void mock_mixer_reset(void);
