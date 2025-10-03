#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hardware mock functions for QEMU builds

// System voltage
float voltage_monitor_get_sys_voltage(void);

// Config button
uint32_t config_button_get_press_count(void);
void config_button_init(void);

// RS485 control
void update_rs485_control(void);
void rs485_control_init(void);

// IO bus control
void update_io_bus_control(void);

// MIO control
void mio_control_init(void);

// GPIO states
void update_rs485_mio_gpio_states(void);

#ifdef __cplusplus
}
#endif
