#pragma once
#include "driver/gpio.h"          // gpio_mode_t
#include "virtual_io_qemu.h"      // vio_dir_state_t
// Map an ESP-IDF gpio mode to the virtual model's direction state.
vio_dir_state_t dir_from_mode(gpio_mode_t mode);
