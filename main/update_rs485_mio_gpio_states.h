#pragma once

void update_rs485_control(void);
void update_io_bus_control(void);
void update_serial_tx_disabled(void);

// Leave both RS-485 transceivers in receive at boot. Only the "none" Modbus role does any
// work here - in the other two the UART owns those pins - but it is declared and called
// unconditionally, so app_main does not have to know which role it was built into.
void park_serial_direction_pins(void);
