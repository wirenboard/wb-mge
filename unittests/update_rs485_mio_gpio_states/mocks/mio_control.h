#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define MAX_FUNCTION_CALLS                  20

extern int mock_mio_control_io_bus_onoff_called;
extern bool mock_mio_control_io_bus_onoff_on_values[MAX_FUNCTION_CALLS];

esp_err_t mio_control_io_bus_onoff(bool enabled);

void mock_mio_control_reset(void);
