#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Control side of the wifi_apsta mock. The mock keeps the one piece of state the real
// driver exposes — whether the radio came up in this boot — and mirrors its refusal to
// touch an uninitialized radio, so a test that removes the guard under test sees exactly
// the ESP_ERR_NOT_ALLOWED the device reported.

extern int mock_wifi_init_apsta_called;
extern esp_err_t mock_wifi_init_apsta_return_value;

extern int mock_wifi_set_apsta_config_called;
extern esp_err_t mock_wifi_set_apsta_config_return_value;

void mock_wifi_apsta_reset(void);
