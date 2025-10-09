#pragma once

#include <stdbool.h>
#include "esp_io_expander.h"

esp_err_t mio_control_init(esp_io_expander_handle_t io_expander);
esp_err_t mio_control_io_bus_onoff(bool enabled);
