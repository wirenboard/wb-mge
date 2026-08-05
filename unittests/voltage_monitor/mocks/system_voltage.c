// Stub implementation of system_voltage for voltage_monitor unit tests.

#include "system_voltage.h"

float mock_system_voltage_value = 12.0f;

esp_err_t system_voltage_init(void)
{
    return ESP_OK;
}

float system_voltage_read(void)
{
    return mock_system_voltage_value;
}
