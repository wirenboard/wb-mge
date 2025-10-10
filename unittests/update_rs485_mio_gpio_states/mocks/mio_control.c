#include "mio_control.h"
#include "esp_err.h"

int mock_mio_control_io_bus_onoff_called = 0;
bool mock_mio_control_io_bus_onoff_on_values[MAX_CALLS];

esp_err_t mio_control_io_bus_onoff(bool on)
{
    if (mock_mio_control_io_bus_onoff_called < MAX_CALLS) {
        mock_mio_control_io_bus_onoff_on_values[mock_mio_control_io_bus_onoff_called] = on;
    }
    mock_mio_control_io_bus_onoff_called++;
    return ESP_OK;
}
