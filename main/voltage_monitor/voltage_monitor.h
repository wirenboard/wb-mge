#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef void (*voltage_monitor_callback_t)(float voltage, bool is_ok);

esp_err_t voltage_monitor_init(voltage_monitor_callback_t callback_fn);

float voltage_monitor_get_sys_voltage(void);
bool voltage_monitor_sys_voltage_is_ok(void);
