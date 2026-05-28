#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Can only be called after gpio_expander_init()
esp_err_t mio_control_init(void);

esp_err_t mio_control_io_bus_onoff(bool enabled);
