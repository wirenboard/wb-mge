#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Initialize indication module
// IO expander should be initialized before calling this function
esp_err_t indication_init(void);

// Start regular status LED blinking with specified period
void indication_status_led_blink(unsigned period_ms);

// Special mode: start status LED blinking with specified period and times count
// After the specified number of blinks has elapsed, it returns to regular blinking mode
void indication_status_led_blink_n_times(unsigned period_ms, unsigned count);

// Factory-test override: when enabled, indication_task stops driving the
// Wi-Fi/Eth/Status LEDs from their subsystem state and forces all of them on.
// Used by the /wb_test endpoint to light all indicator LEDs simultaneously.
void indication_set_test_all_leds(bool on);
