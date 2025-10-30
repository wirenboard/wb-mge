#pragma once

extern int mock_update_rs485_control_called;
extern int mock_update_io_bus_control_called;

void mock_update_rs485_mio_gpio_states_reset(void);

void update_rs485_control(void);
void update_io_bus_control(void);
