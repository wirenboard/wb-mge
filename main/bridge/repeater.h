#pragma once
#include "esp_err.h"
#include "serial.h"
#include <stdbool.h>
#include <stdint.h>

// Aggregated live statistics for the serial<->serial repeater.
typedef struct {
    uint64_t bytes_1to2;   // bytes forwarded Port 1 RX -> Port 2 TX (64-bit: no practical overflow)
    uint64_t bytes_2to1;   // bytes forwarded Port 2 RX -> Port 1 TX (64-bit: no practical overflow)
    uint64_t dropped_1;    // bytes received on Port 1 that were lost: forward failures plus RX-stage drops (receive-buffer / ring overflow) (64-bit)
    uint64_t dropped_2;    // bytes received on Port 2 that were lost: forward failures plus RX-stage drops (receive-buffer / ring overflow) (64-bit)
    uint64_t uptime_ms;    // milliseconds since the repeater started forwarding (0 if inactive, 64-bit: no practical overflow)
    bool     active;       // true when BOTH ports are running in repeater mode
} repeater_stats_t;

// Create the repeater-global mutex once. Idempotent. Call before any port concurrency
// starts (port_manager_init_subsystems() does this, from main.c before the HTTP server
// starts). repeater_lock() also lazily creates the mutex, so unit tests that skip this
// still work.
void repeater_init(void);

// Open a port's serial in repeater mode (transparent serial<->serial forwarding).
// Mirrors bridge_port_init_serial_only(): serial_init() with the repeater RX handler.
esp_err_t repeater_init_port(unsigned index, serial_config_t *config, serial_desc_t **serial_desc_out);

// Tear down a repeater port's serial.
esp_err_t repeater_deinit_port(unsigned index);

// Fill *out with a consistent snapshot of repeater statistics.
void repeater_get_stats(repeater_stats_t *out);

#ifdef __unittest_env__
void repeater_reset_for_test(void);
#endif
