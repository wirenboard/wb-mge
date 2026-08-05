#include "voltage_monitor.h"

/* ---- Mock state ---------------------------------------------------------- */

static float mock_sys_voltage = 12.0f;  /* default supply voltage in volts */

/* ---- Mock implementations ------------------------------------------------ */

float voltage_monitor_get_sys_voltage(void)
{
    return mock_sys_voltage;
}

bool voltage_monitor_sys_voltage_is_ok(void)
{
    return true;
}

esp_err_t voltage_monitor_init(voltage_monitor_callback_t callback_fn)
{
    (void)callback_fn;
    return 0; /* ESP_OK */
}

/* ---- Test helpers -------------------------------------------------------- */

void mock_voltage_set(float volts)
{
    mock_sys_voltage = volts;
}

void mock_voltage_reset(void)
{
    mock_sys_voltage = 12.0f;
}
