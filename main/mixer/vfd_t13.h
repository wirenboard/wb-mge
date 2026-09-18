#pragma once

/*
 * Driver for the T13-750W-12-H frequency converter, on top of the Modbus RTU master.
 *
 * What the drive actually implements, and what the quirks below are for:
 *
 *   - only FC03 (read holding) and FC06 (write single holding). No FC16, no input
 *     registers, no coils. Register addresses are 0-based, exactly as they go on the wire;
 *   - register 2  - frequency setpoint, 0.1 Hz per count, 10..990 (1.0 .. 99.0 Hz);
 *   - register 3  - the command, and the feedback for it in the same place: 1 forward,
 *     2 stop, and reverse is 5 on new revisions and 3 on old ones. Which one this drive
 *     wants is not discoverable, so it is configuration (see vfd_t13_configure);
 *   - register 8  - DC bus voltage, 0.1 V;
 *   - register 9  - current, 0.01 A;
 *   - register 10 - heatsink temperature in whole degrees, signed. Part of the range has no
 *     sensor fitted and answers 0.
 *   Registers 4..7 and 11..17 hold nothing.
 *
 * The three quirks that shape every function here:
 *   - a read of a SINGLE register is not answered. Every read below asks for at least two,
 *     which is why the feedback read covers 0..3 rather than just 2..3;
 *   - the drive needs a quiet line before a frame. Every request is preceded by a short
 *     pause;
 *   - an FC06 write is answered more than once. The extra frames have to drain before the
 *     next request goes out, hence the longer pause after a write.
 *
 * The driver holds no settings of its own: the port, the unit id and the reverse code are
 * handed to it by the engine, which is the only thing here that reads NVS. Nothing in this
 * file touches the settings API.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Register map, 0-based holding registers.
#define VFD_T13_REG_FEEDBACK            0
#define VFD_T13_REG_FEEDBACK_COUNT      4
#define VFD_T13_REG_FREQ                2
#define VFD_T13_REG_COMMAND             3
#define VFD_T13_REG_TELEMETRY           8
#define VFD_T13_REG_TELEMETRY_COUNT     3

// Frequency setpoint limits, in 0.1 Hz.
#define VFD_T13_FREQ_MIN_DHZ            10
#define VFD_T13_FREQ_MAX_DHZ            990

// Register 3 command codes. Forward and stop are the same on every revision; reverse is not.
#define VFD_T13_CMD_FORWARD             1
#define VFD_T13_CMD_STOP                2
#define VFD_T13_CMD_REVERSE_NEW         5
#define VFD_T13_CMD_REVERSE_OLD         3

// Consecutive failed exchanges before the drive counts as gone. A single failure is normal
// traffic on an RS-485 bus and must NOT be reported as a fault.
#define VFD_T13_OFFLINE_THRESHOLD       8

typedef enum {
    VFD_T13_DIR_STOP = 0,
    VFD_T13_DIR_FORWARD,
    VFD_T13_DIR_REVERSE,
} vfd_t13_dir_t;

typedef struct {
    uint8_t  port;          // 0 = RS-485-1, 1 = RS-485-2, as mb_master.h numbers them
    uint8_t  slave_id;
    uint16_t rev_code;      // VFD_T13_CMD_REVERSE_NEW or ..._OLD
    uint8_t  fail_count;    // consecutive failed exchanges, capped at the threshold
} vfd_t13_t;

typedef struct {
    uint16_t bus_voltage_dv;    // register 8,  0.1 V
    uint16_t current_ca;        // register 9,  0.01 A
    int16_t  temp_c;            // register 10, whole degrees, signed, 0 with no sensor
} vfd_t13_telemetry_t;

typedef struct {
    uint16_t freq_dhz;          // register 2, the setpoint the drive is holding
    uint16_t command;           // register 3, the command it believes it is running
} vfd_t13_feedback_t;

// Point the driver at a drive. Re-configuring the SAME target (port and unit id unchanged)
// keeps the failure counter, so a settings write that changes nothing about the bus does not
// hide a drive that is already gone.
void vfd_t13_configure(vfd_t13_t *vfd, uint8_t port, uint8_t slave_id, uint16_t rev_code);

// Write the frequency setpoint, in 0.1 Hz. The value is clamped into 10..990 rather than
// refused: the caller derives it from a speed in rpm, and a converted value out of range is
// a program the user should still be able to run at the nearest speed the drive can do.
esp_err_t vfd_t13_set_frequency(vfd_t13_t *vfd, uint16_t freq_dhz);

// Write the run command. The reverse code comes from the configuration, not from here.
esp_err_t vfd_t13_send_command(vfd_t13_t *vfd, vfd_t13_dir_t dir);

// The value register 3 holds for a direction: what vfd_t13_send_command() writes into it, and
// what a read of the register gives back while the drive is running that direction. Exported so
// that the caller can compare the two without a second copy of the reverse-code rule.
uint16_t vfd_t13_command_code(const vfd_t13_t *vfd, vfd_t13_dir_t dir);

// One read of registers 8..10, and one of 0..3. Both are single requests, because a
// register-by-register poll is exactly what this drive refuses to answer.
esp_err_t vfd_t13_read_telemetry(vfd_t13_t *vfd, vfd_t13_telemetry_t *out);
esp_err_t vfd_t13_read_feedback(vfd_t13_t *vfd, vfd_t13_feedback_t *out);

// False once VFD_T13_OFFLINE_THRESHOLD exchanges in a row have failed, true again on the
// first one that succeeds.
bool vfd_t13_is_online(const vfd_t13_t *vfd);
