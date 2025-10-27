#include "mio_control.h"

#include <string.h>

int mock_mio_control_io_bus_onoff_called = 0;
bool mock_mio_control_io_bus_onoff_on_values[MAX_FUNCTION_CALLS];

esp_err_t mio_control_io_bus_onoff(bool on)
{
    if (mock_mio_control_io_bus_onoff_called < MAX_FUNCTION_CALLS) {
        mock_mio_control_io_bus_onoff_on_values[mock_mio_control_io_bus_onoff_called] = on;
    }
    mock_mio_control_io_bus_onoff_called++;
    return ESP_OK;
}

void mock_mio_control_reset(void)
{
    mock_mio_control_io_bus_onoff_called = 0;
    memset(mock_mio_control_io_bus_onoff_on_values, 0, sizeof(mock_mio_control_io_bus_onoff_on_values));
}
