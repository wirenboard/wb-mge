#include "rs485_control.h"

#include <stdbool.h>

/* ---- Mock state ---------------------------------------------------------- */

static bool mock_vout_enabled = false;
static bool mock_vout_allowed = true;

static int  mock_update_vout_called = 0;
static bool mock_update_vout_value = false;
static esp_err_t mock_update_vout_ret = ESP_OK;

/* ---- Mock implementations ------------------------------------------------ */

void rs485_bus_vout_get_state(bool *enabled, bool *allowed)
{
    *enabled = mock_vout_enabled;
    *allowed = mock_vout_allowed;
}

esp_err_t update_rs485_vout(bool enabled)
{
    mock_update_vout_called++;
    mock_update_vout_value = enabled;
    return mock_update_vout_ret;
}

/* ---- Test helpers -------------------------------------------------------- */

void mock_vout_set_state(bool enabled, bool allowed)
{
    mock_vout_enabled = enabled;
    mock_vout_allowed = allowed;
}

void mock_update_vout_set_ret(esp_err_t ret)
{
    mock_update_vout_ret = ret;
}

int mock_update_vout_get_called(void)
{
    return mock_update_vout_called;
}

bool mock_update_vout_get_value(void)
{
    return mock_update_vout_value;
}

void mock_vout_reset(void)
{
    mock_vout_enabled = false;
    mock_vout_allowed = true;
    mock_update_vout_called = 0;
    mock_update_vout_value = false;
    mock_update_vout_ret = ESP_OK;
}
