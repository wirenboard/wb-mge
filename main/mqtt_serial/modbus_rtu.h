#pragma once
/*
 * Minimal Modbus RTU implementation.
 *
 * Supported function codes (read/write, WB devices use standard Modbus):
 *   FC01 - Read Coils
 *   FC02 - Read Discrete Inputs
 *   FC03 - Read Holding Registers
 *   FC04 - Read Input Registers
 *   FC05 - Write Single Coil
 *   FC06 - Write Single Holding Register
 *   FC16 - Write Multiple Holding Registers
 *
 * The transport is abstracted behind a simple fd (Linux POSIX serial).
 * On an MCU you replace modbus_rtu_open/close with UART init and
 * modbus_rtu_send/recv with uart_write/uart_read calls.
 */

#include <stdint.h>
#include <stdbool.h>

/* Opaque handle */
typedef struct mb_rtu_port mb_rtu_port_t;

/* Open a serial port for Modbus RTU.
 * baud: e.g. 9600, 115200
 * parity: 'N', 'E', 'O'
 * stop_bits: 1 or 2
 * response_timeout_ms: max time to wait for a response (typical: 300 ms)
 * Returns NULL on error. */
mb_rtu_port_t *mb_rtu_open(const char *device, int baud, char parity,
                            int stop_bits, int response_timeout_ms);

void mb_rtu_close(mb_rtu_port_t *port);

/* ------------------------------------------------------------------
 * High-level register read/write
 * All functions return 0 on success, negative on error.
 * ------------------------------------------------------------------ */

/* Read n_regs 16-bit registers starting at addr into regs[] */
int mb_read_holding(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_regs, uint16_t *regs);
int mb_read_input  (mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_regs, uint16_t *regs);

/* Read n_bits coils/discrete inputs starting at addr into bits[].
 * bits[i] will be 0 or 1. */
int mb_read_coils   (mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_bits, uint8_t *bits);
int mb_read_discrete(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_bits, uint8_t *bits);

/* Write a single coil (value: 0 = off, non-zero = on) */
int mb_write_coil(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, int value);

/* Write a single holding register */
int mb_write_holding(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t value);

/* Write multiple holding registers */
int mb_write_holding_multi(mb_rtu_port_t *p, uint8_t slave, uint16_t addr, uint16_t n_regs, const uint16_t *regs);

/* Send a raw, already-framed request and receive a raw response.
 * No Modbus framing/validation is applied (used for Fast Modbus).
 * Returns bytes received (>=0), or negative on send error. */
int mb_rtu_raw_txn(mb_rtu_port_t *p, const uint8_t *tx, int tx_len,
                   uint8_t *rx, int rx_max, int timeout_ms);
