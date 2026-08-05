#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Can only be called after gpio_expander_init()
esp_err_t leds_control_init(void);

esp_err_t leds_control_set_eth_led(bool on);
esp_err_t leds_control_set_wifi_led(bool on);
esp_err_t leds_control_set_status_led(bool on);
