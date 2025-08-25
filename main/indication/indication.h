#pragma once

#include "esp_io_expander_tca95xx_16bit.h"

// Initialize indication module
// IO expander should be initialized before call this function
esp_err_t indication_init(esp_io_expander_handle_t io_expander_handle);

// Start regular status LED blinking with specified period
void indication_status_led_blink(unsigned period_ms);

// Especial status LED blinking with specified period and times count
void indication_status_led_count_blink(unsigned period_ms, unsigned count);
