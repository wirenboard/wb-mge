#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Virtual IO state bus for the QEMU build.
//
// This module replaces the real I2C GPIO expander and native GPIO so that the
// real hardware-logic modules (indication/leds_control/rs485_control/mio_control/
// config_button) can run unmodified against a RAM-backed shadow. Pin-state
// changes are mirrored to a host over a UDP side-channel (port 5570), and the
// host can inject the config-button input.
//
// The native GPIO direction/level model is driven ONLY by the wrap shim
// (main/qemu/gpio_shim_qemu.c), which transparently intercepts the firmware's
// real ESP-IDF GPIO calls via the linker, and by inbound host RX records. The
// firmware itself contains NO QEMU-specific GPIO code: the SAME gpio_config /
// gpio_set_direction / gpio_set_level / gpio_get_level / uart_set_pin calls
// configure both real and emulated pins, so the two stay in sync automatically.
//
// It also provides the gpio_expander.h symbols (declared there); see
// virtual_io_qemu.c for those implementations.

// Native GPIO direction state. UNCONFIGURED is the default: a pin only becomes
// INPUT or OUTPUT once the firmware actually configures it (no hardcoded
// per-pin defaults).
typedef enum {
    VIO_DIR_UNCONFIGURED = 0,
    VIO_DIR_INPUT,
    VIO_DIR_OUTPUT,
} vio_dir_state_t;

// Initialize the UDP IO state bus: create the socket, bind 0.0.0.0:5570 and
// spawn the RX task. Non-fatal on failure (logs and returns). Safe to call
// once after network_init().
esp_err_t virtual_io_init(void);

// --- Internal model API used by the wrap shim (gpio_shim_qemu.c) ---------------
// These are NOT called by firmware logic; they are the bridge between the
// linker-wrapped ESP-IDF GPIO calls and the virtual native model.

// Set a native GPIO's model direction (derived from the firmware's gpio mode).
// On a change to INPUT/OUTPUT a "D<NN>/<d>" record is emitted; switching to
// UNCONFIGURED emits nothing.
void vio_native_set_direction(int gpio_num, vio_dir_state_t dir);

// FIRMWARE write path (mirrors gpio_set_level). OUTPUT: update + emit G on
// change. INPUT: violation V<NN>/1 (firmware drove input), rejected.
// UNCONFIGURED: violation V<NN>/2 (operate uninitialized), rejected.
void vio_native_fw_set_level(int gpio_num, int level);

// Return the model level for a native GPIO (for gpio_get_level on a tracked pin).
int vio_native_get_level(int gpio_num);

// True if the pin is tracked in the model (configured INPUT or OUTPUT), i.e. the
// shim should serve gpio_get_level from the model rather than the QEMU stub.
bool vio_native_is_tracked(int gpio_num);

#ifdef __cplusplus
}
#endif
