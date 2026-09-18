#pragma once

/*
 * The mixer engine: one task that walks the active program and drives the frequency
 * converter through vfd_t13.c.
 *
 * This header is the whole contract the rest of the firmware has with the mixer - the web
 * UI reads mixer_get_status() through GET /info, and the MQTT side drives it through the
 * three calls below. Everything else (which port the drive is on, which program is active,
 * what the steps are) is settings, and the engine is the only thing that reads them.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Programs the device stores. Indexes the mix_pN / mix_pN_name setting pairs.
#define MIXER_PROGRAM_COUNT     4

typedef enum {
    MIXER_STOPPED,      // switched off, or the active program has no steps
    MIXER_RUNNING,      // walking the steps
    MIXER_ERROR,        // meant to be running, but the drive has stopped answering
} mixer_state_t;

typedef struct {
    mixer_state_t state;
    uint8_t  program;           // active program index, 0..MIXER_PROGRAM_COUNT-1
    uint8_t  step;              // current step, 0-based; 0 while stopped
    uint16_t step_left_s;       // whole seconds left in the current step, rounded up
    uint16_t speed_rpm;         // speed of the current step; 0 while stopped or pausing
    uint8_t  step_type;         // mixer_step_type_t of the current step; 0 while stopped
    bool     vfd_online;
    uint16_t freq_dhz;          // last frequency read back from the drive, 0.1 Hz
    uint16_t bus_voltage_dv;    // 0.1 V
    uint16_t current_ca;        // 0.01 A
    int16_t  temp_c;
} mixer_status_t;

// Read the settings and start the engine task. Called once, from user_app_start().
void mixer_start(void);

// A consistent snapshot of what the engine is doing. Safe from any task.
void mixer_get_status(mixer_status_t *out);

// Switch the mixer on or off. The flag is STORED (mix_enabled), so a device that was
// running when the power went keeps running when it comes back - from the first step.
esp_err_t mixer_set_enabled(bool on);

// Select the active program, 0..MIXER_PROGRAM_COUNT-1. Stored in mix_prog, and the engine
// restarts from the first step.
esp_err_t mixer_set_program(uint8_t index);

// The master switch and the active program, both read straight from the settings rather than
// from the engine's own copy: a caller that has just written one of them must not be told the
// old value for the rest of the engine's tick.
bool mixer_is_enabled(void);
uint8_t mixer_get_program(void);

// Re-read every mixer setting on the next tick. Called from the settings apply.
void mixer_settings_changed(void);
