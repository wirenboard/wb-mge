#include "rs485_control.h"
#include "slot_relay.h"

#include <stdbool.h>

/* ---- Mock state ---------------------------------------------------------- */

static bool mock_vout_enabled = false;
static bool mock_vout_allowed = true;

static int  mock_update_vout_called = 0;
static bool mock_update_vout_value = false;
static esp_err_t mock_update_vout_ret = ESP_OK;

static esp_err_t mock_slot_relay_ret = ESP_ERR_NOT_SUPPORTED;
static bool mock_slot_relay_state = false;
static int  mock_slot_relay_set_called = 0;

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

esp_err_t slot_relay_set(bool on)
{
    mock_slot_relay_set_called++;
    if (mock_slot_relay_ret == ESP_OK) {
        mock_slot_relay_state = on;
    }
    return mock_slot_relay_ret;
}

esp_err_t slot_relay_get(bool *on)
{
    /* The real get only knows "no slot on this board"; a busy pin still reads. */
    if (mock_slot_relay_ret == ESP_ERR_NOT_SUPPORTED) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *on = mock_slot_relay_state;
    return ESP_OK;
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

void mock_slot_relay_set_ret(esp_err_t ret)
{
    mock_slot_relay_ret = ret;
}

bool mock_slot_relay_get_state(void)
{
    return mock_slot_relay_state;
}

int mock_slot_relay_get_set_called(void)
{
    return mock_slot_relay_set_called;
}

void mock_vout_reset(void)
{
    mock_slot_relay_ret = ESP_ERR_NOT_SUPPORTED;
    mock_slot_relay_state = false;
    mock_slot_relay_set_called = 0;
    mock_vout_enabled = false;
    mock_vout_allowed = true;
    mock_update_vout_called = 0;
    mock_update_vout_value = false;
    mock_update_vout_ret = ESP_OK;
}
