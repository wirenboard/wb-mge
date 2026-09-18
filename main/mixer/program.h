#pragma once

/*
 * The mixing program: the list of steps the mixer walks while it runs.
 *
 * A program lives in ONE setting value, which is why it is base64 rather than JSON. A
 * setting value is capped at 63 bytes (setting_items.h), and base64 is the densest encoding
 * the settings API can carry unescaped: 11 steps of 4 bytes = 44 bytes = 60 base64
 * characters, and a twelfth step would not fit.
 *
 * The wire form of one step is fixed at four bytes, big-endian:
 *     [type u8][speed u8][duration u16]
 *   type     - 0 pause (the motor stands still), 1 forward, 2 reverse
 *   speed    - tens of rpm (20 = 200 rpm); meaningless, and ignored, for a pause, and zero on
 *              anything else is rejected: the engine would raise it to the drive's 1.0 Hz
 *              floor and turn the shaft where the program asked for no motion at all
 *   duration - seconds, 1..65535. Zero is not a step, it is a mistake, so it is rejected
 *              rather than silently skipped at run time.
 *
 * This file knows nothing about Modbus, the drive or the engine - it is the data model
 * alone, which is what lets it be unit-tested on the host and compiled into every Modbus
 * role even though the engine that runs the steps is master-only.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// One step on the wire: [type u8][speed u8][duration u16 big-endian].
#define MIXER_STEP_WIRE_SIZE        4

// The ceiling the 63-byte setting value imposes, see the comment above.
#define MIXER_PROGRAM_MAX_STEPS     11
#define MIXER_PROGRAM_MAX_BYTES     (MIXER_PROGRAM_MAX_STEPS * MIXER_STEP_WIRE_SIZE)

// Base64 of MIXER_PROGRAM_MAX_BYTES bytes, terminator included: 60 + 1.
#define MIXER_PROGRAM_MAX_B64_SIZE  61

typedef enum {
    MIXER_STEP_PAUSE   = 0,
    MIXER_STEP_FORWARD = 1,
    MIXER_STEP_REVERSE = 2,
} mixer_step_type_t;

typedef struct {
    uint8_t  type;          // mixer_step_type_t
    uint8_t  speed;         // tens of rpm; ignored when type is MIXER_STEP_PAUSE
    uint16_t duration_s;    // 1..65535
} mixer_step_t;

typedef struct {
    uint8_t      count;     // 0 = empty program
    mixer_step_t steps[MIXER_PROGRAM_MAX_STEPS];
} mixer_program_t;

// Decode a stored setting value into steps. The empty string is a valid, empty program.
// Returns ESP_ERR_INVALID_ARG for anything the validator below would reject, and leaves
// *out holding an empty program in that case rather than half a program.
esp_err_t mixer_program_decode(const char *b64, mixer_program_t *out);

// Encode steps back into the setting value. out_size must be at least
// MIXER_PROGRAM_MAX_B64_SIZE for a full program; ESP_ERR_INVALID_SIZE says it was not.
esp_err_t mixer_program_encode(const mixer_program_t *program, char *out, size_t out_size);

// The setting_validators-shaped answer to "may this string be stored as a program?".
bool mixer_program_is_valid(const char *b64);
