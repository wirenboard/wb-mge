#include "update_rs485_mio_gpio_states.h"

int mock_update_rs485_control_called = 0;
int mock_update_io_bus_control_called = 0;

void update_rs485_control(void)
{
    mock_update_rs485_control_called++;
}

void update_io_bus_control(void)
{
    mock_update_io_bus_control_called++;
}

void mock_update_rs485_mio_gpio_states_reset(void)
{
    mock_update_rs485_control_called = 0;
    mock_update_io_bus_control_called = 0;
}
