#pragma once

#include <stdbool.h>
#include "esp_io_expander.h"

void mio_control_init(esp_io_expander_handle_t io_expander);
void mio_control_io_bus_onoff(bool enabled);
