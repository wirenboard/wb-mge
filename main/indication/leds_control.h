#pragma once

#include <stdbool.h>
#include "esp_io_expander_tca95xx_16bit.h"

void leds_control_init(esp_io_expander_handle_t io_expander_handle);
void leds_control_set_eth_led(bool on);
void leds_control_set_wifi_led(bool on);
void leds_control_set_status_led(bool on);
