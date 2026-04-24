#pragma once
/*
 * Value conversion between Modbus raw register words and numeric strings.
 * Pure functions, no I/O, easy to unit-test.
 */

#include "template.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Word order: which 16-bit word comes first on the wire */
typedef enum {
    WORD_ORDER_BIG_ENDIAN    = 0,  /* regs[0] = MSW (default) */
    WORD_ORDER_LITTLE_ENDIAN = 1,  /* regs[0] = LSW           */
} word_order_t;

/* Byte order: byte layout within each 16-bit word */
typedef enum {
    BYTE_ORDER_BIG_ENDIAN    = 0,  /* hi byte first (default) */
    BYTE_ORDER_LITTLE_ENDIAN = 1,  /* lo byte first           */
} byte_order_t;

/*
 * raw_to_double()
 * Convert an array of 16-bit Modbus words to a double.
 * word_order: which word is MSW; byte_order: byte swap within each word.
 */
double raw_to_double(const uint16_t *regs, reg_format_t fmt,
                     word_order_t word_order, byte_order_t byte_order);

/*
 * value_to_string()
 * Multiply raw by scale, add offset, format as shortest string.
 * Writes into buf[0..buf_size-1], always null-terminates.
 */
void value_to_string(double raw, double scale, double offset,
                     char *buf, int buf_size);

/*
 * double_to_regs()
 * Convert a double (already un-scaled/offset-removed) to raw Modbus words.
 * regs[] must have at least format_num_regs() entries.
 */
void double_to_regs(double v, reg_format_t fmt,
                    word_order_t word_order, byte_order_t byte_order,
                    uint16_t *regs);

/*
 * is_error_value()
 * Returns true if raw matches the channel's error_value (nan = disabled).
 * error_value is stored as double; NaN means "no error value configured".
 */
bool is_error_value(double raw, double error_value);

/*
 * decode_string_regs()
 * Decode n_regs 16-bit Modbus words as big-endian ASCII.
 * Writes at most buf_size-1 printable bytes, null-terminates.
 */
void decode_string_regs(const uint16_t *regs, uint32_t n_regs,
                        char *buf, int buf_size);

/*
 * string_to_value()
 * Parse s as a double, reverse the scale/offset transform.
 * Returns the raw value suitable for passing to double_to_regs.
 */
double string_to_value(const char *s, double scale, double offset);
