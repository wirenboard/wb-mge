#pragma once

#include "config.h"
#include "esp_err.h"
#include <stdbool.h>

#define MAX_FUNCTION_CALLS                  20

typedef enum {
    RS485_1 = 1,
    RS485_2 = 2
} rs485_port_t;

extern int mock_rs485_pupd_on_off_called;
extern rs485_port_t mock_rs485_pupd_on_off_ports[MAX_FUNCTION_CALLS];
extern bool mock_rs485_pupd_on_off_on_values[MAX_FUNCTION_CALLS];

extern int mock_rs485_term_on_off_called;
extern rs485_port_t mock_rs485_term_on_off_ports[MAX_FUNCTION_CALLS];
extern bool mock_rs485_term_on_off_on_values[MAX_FUNCTION_CALLS];

extern int mock_rs485_bus_vout_on_off_called;
extern bool mock_rs485_bus_vout_on_off_on_values[MAX_FUNCTION_CALLS];

esp_err_t rs485_term_on_off(rs485_port_t port, bool on);
esp_err_t rs485_pupd_on_off(rs485_port_t port, bool on);
esp_err_t rs485_bus_vout_on_off(bool on);

void mock_rs485_control_reset(void);
