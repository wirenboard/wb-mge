#define _POSIX_C_SOURCE 200809L

#include "value_conv.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ------------------------------------------------------------------
 * Internal helpers: byte-swap a single 16-bit word
 * ------------------------------------------------------------------ */
static uint16_t swap16(uint16_t w)
{
    return (uint16_t)((w >> 8) | (w << 8));
}

/*
 * Normalise register array: apply word_order and byte_order so the
 * result is always in canonical big-endian form (regs_out[0] = MSW,
 * MSB of each word is the high byte).
 * n is number of 16-bit words.
 */
static void normalise_regs(const uint16_t *regs_in, uint16_t *regs_out,
                            uint32_t n,
                            word_order_t word_order, byte_order_t byte_order)
{
    for (uint32_t i = 0; i < n; i++) {
        uint32_t src = (word_order == WORD_ORDER_LITTLE_ENDIAN) ? (n - 1 - i) : i;
        uint16_t w = regs_in[src];
        if (byte_order == BYTE_ORDER_LITTLE_ENDIAN) w = swap16(w);
        regs_out[i] = w;
    }
}

/* ------------------------------------------------------------------
 * BCD decoding
 * bcd_byte(b): decode one BCD byte (e.g. 0x45 -> 45)
 * ------------------------------------------------------------------ */
/* Decode one BCD byte: upper nibble = tens, lower nibble = units.
 * Invalid nibbles (> 9) are accepted silently -- no extra validation
 * to keep the code small for MCU targets. Callers must ensure data is valid BCD. */
static uint32_t decode_bcd_byte(uint8_t b)
{
    return (uint32_t)((b >> 4) & 0xF) * 10 + (uint32_t)(b & 0xF);
}

/* Decode n_bytes of BCD from canonical (big-endian) byte stream into uint32_t */
static uint32_t decode_bcd_bytes(const uint8_t *bytes, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        v = v * 100 + decode_bcd_byte(bytes[i]);
    }
    return v;
}

/* ------------------------------------------------------------------
 * raw_to_double
 * ------------------------------------------------------------------ */
double raw_to_double(const uint16_t *regs, reg_format_t fmt,
                     word_order_t word_order, byte_order_t byte_order)
{
    /* How many words do we need to normalise? */
    uint32_t n = 1;
    switch (fmt) {
        case FMT_U32: case FMT_S32: case FMT_FLOAT:
        case FMT_BCD32: case FMT_BCD24:
            n = 2; break;
        case FMT_U64: case FMT_S64:
            n = 4; break;
        default: n = 1; break;
    }

    uint16_t norm[4];
    normalise_regs(regs, norm, n, word_order, byte_order);

    switch (fmt) {
        case FMT_U8:  return (double)(uint8_t)(norm[0] & 0xFF);
        case FMT_S8:  return (double)(int8_t) (norm[0] & 0xFF);
        case FMT_U16: return (double)(uint16_t)norm[0];
        case FMT_S16: return (double)(int16_t) norm[0];
        case FMT_U32: {
            uint32_t v = ((uint32_t)norm[0] << 16) | norm[1];
            return (double)v;
        }
        case FMT_S32: {
            int32_t v = (int32_t)(((uint32_t)norm[0] << 16) | norm[1]);
            return (double)v;
        }
        case FMT_U64: {
            uint64_t v = ((uint64_t)norm[0] << 48)
                       | ((uint64_t)norm[1] << 32)
                       | ((uint64_t)norm[2] << 16)
                       |  (uint64_t)norm[3];
            return (double)v;
        }
        case FMT_S64: {
            int64_t v = (int64_t)(((uint64_t)norm[0] << 48)
                                | ((uint64_t)norm[1] << 32)
                                | ((uint64_t)norm[2] << 16)
                                |  (uint64_t)norm[3]);
            return (double)v;
        }
        case FMT_FLOAT: {
            uint32_t u = ((uint32_t)norm[0] << 16) | norm[1];
            float f;
            memcpy(&f, &u, 4);
            return (double)f;
        }
        case FMT_BCD8: {
            uint8_t b = (uint8_t)(norm[0] & 0xFF);
            return (double)decode_bcd_byte(b);
        }
        case FMT_BCD16: {
            uint8_t bytes[2] = {(uint8_t)(norm[0] >> 8), (uint8_t)(norm[0] & 0xFF)};
            return (double)decode_bcd_bytes(bytes, 2);
        }
        case FMT_BCD24: {
            /* BCD24 occupies 3 bytes in 2 registers.
             * WB convention: the high byte of the first register is unused (padding).
             * Layout: [norm[0]:unused | B0] [norm[1]:B1 | B2]  -> decode B0,B1,B2.
             * This matches the wb-mqtt-serial bcd_utils behavior. */
            uint8_t bytes[3] = {
                (uint8_t)(norm[0] & 0xFF),   /* B0 = low byte of first reg  */
                (uint8_t)(norm[1] >> 8),     /* B1 = high byte of second reg */
                (uint8_t)(norm[1] & 0xFF)    /* B2 = low byte of second reg  */
            };
            return (double)decode_bcd_bytes(bytes, 3);
        }
        case FMT_BCD32: {
            uint8_t bytes[4] = {
                (uint8_t)(norm[0] >> 8),   (uint8_t)(norm[0] & 0xFF),
                (uint8_t)(norm[1] >> 8),   (uint8_t)(norm[1] & 0xFF)
            };
            return (double)decode_bcd_bytes(bytes, 4);
        }
        default:
            return (double)(uint16_t)norm[0];
    }
}

/* ------------------------------------------------------------------
 * value_to_string
 * ------------------------------------------------------------------ */
void value_to_string(double raw, double scale, double offset,
                     char *buf, int buf_size)
{
    double v = raw * scale + offset;
    /* Print without decimal point if result is exactly integral.
     * Guard against UB: only cast to long long if within safe range.
     * LLONG_MIN literal causes signed overflow warning; use -9.22e18 approx. */
#define LLONG_MAX_D  9.2233720368547758e18
    if (v >= -LLONG_MAX_D && v <= LLONG_MAX_D && v == (double)(long long)v) {
        snprintf(buf, (size_t)buf_size, "%lld", (long long)v);
    } else {
        snprintf(buf, (size_t)buf_size, "%.6g", v);
    }
#undef LLONG_MAX_D
}

/* ------------------------------------------------------------------
 * double_to_regs  (write path: only big-endian output for now)
 * word_order / byte_order applied in reverse (encode -> wire)
 * ------------------------------------------------------------------ */
void double_to_regs(double v, reg_format_t fmt,
                    word_order_t word_order, byte_order_t byte_order,
                    uint16_t *regs)
{
    /* Build canonical (big-endian) words first */
    uint16_t norm[4] = {0};
    uint32_t n = 1;

    switch (fmt) {
        case FMT_U8:  case FMT_U16:
            norm[0] = (uint16_t)(unsigned long long)v;
            break;
        case FMT_S8:  case FMT_S16:
            norm[0] = (uint16_t)(long long)v;
            break;
        case FMT_U32: {
            uint32_t u = (uint32_t)(unsigned long long)v;
            norm[0] = (uint16_t)(u >> 16);
            norm[1] = (uint16_t)(u & 0xFFFF);
            n = 2; break;
        }
        case FMT_S32: {
            int32_t u = (int32_t)(long long)v;
            norm[0] = (uint16_t)((uint32_t)u >> 16);
            norm[1] = (uint16_t)((uint32_t)u & 0xFFFF);
            n = 2; break;
        }
        case FMT_FLOAT: {
            float f = (float)v;
            uint32_t u;
            memcpy(&u, &f, 4);
            norm[0] = (uint16_t)(u >> 16);
            norm[1] = (uint16_t)(u & 0xFFFF);
            n = 2; break;
        }
        default:
            /* BCD, string, U64, S64 write is not supported.
             * Write path is only used for coils and holding registers,
             * and BCD/string channels are always readonly in WB templates.
             * Fallback: write lower 16 bits, caller should not reach here. */
            norm[0] = (uint16_t)(unsigned long long)v;
            break;
    }

    /* Apply word_order and byte_order to output */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t dst = (word_order == WORD_ORDER_LITTLE_ENDIAN) ? (n - 1 - i) : i;
        uint16_t w = norm[i];
        if (byte_order == BYTE_ORDER_LITTLE_ENDIAN) w = swap16(w);
        regs[dst] = w;
    }
}

/* ------------------------------------------------------------------
 * decode_string_regs
 * ------------------------------------------------------------------ */
void decode_string_regs(const uint16_t *regs, uint32_t n_regs,
                        char *buf, int buf_size)
{
    int out = 0;
    for (uint32_t i = 0; i < n_regs && out < buf_size - 1; i++) {
        char hi = (char)(regs[i] >> 8);
        char lo = (char)(regs[i] & 0xFF);
        if (hi == '\0') break;
        buf[out++] = hi;
        if (out >= buf_size - 1 || lo == '\0') break;
        buf[out++] = lo;
    }
    buf[out] = '\0';
}

/* ------------------------------------------------------------------
 * string_to_value
 * ------------------------------------------------------------------ */
double string_to_value(const char *s, double scale, double offset)
{
    char *end;
    double v = strtod(s, &end);
    /* If nothing was consumed, s is not a valid number; treat as 0.
     * This prevents writing garbage to a device register when MQTT
     * receives a malformed payload (e.g. empty string or plain text). */
    if (end == s) v = 0.0;
    if (scale != 0.0) v = (v - offset) / scale;
    return v;
}

/* ------------------------------------------------------------------
 * is_error_value
 * ------------------------------------------------------------------ */
bool is_error_value(double raw, double error_value)
{
    if (isnan(error_value)) return false;
    return raw == error_value;
}
