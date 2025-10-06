#pragma once

#include "esp_err.h"

esp_err_t system_voltage_init(void);

// Read system voltage from ADC with voltage divider
// Uses GPIO35 (ADC1_CH7) with 33k/3.3k voltage divider
// returns System voltage in volts, 0.0 on error
float system_voltage_read(void);
