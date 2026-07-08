/*
 * Unit tests for value_conv.c functions.
 * Tests cover: raw_to_double, value_to_string, double_to_regs, decode_string_regs,
 *              BCD formats, word_order, byte_order, error_value.
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "value_conv.h"

#include <string.h>
#include <math.h>
#include <stdint.h>

/* Shorthand defaults for existing tests */
#define RAW(r, fmt) raw_to_double(r, fmt, WORD_ORDER_BIG_ENDIAN, BYTE_ORDER_BIG_ENDIAN)
#define DTR(v, fmt, r) double_to_regs(v, fmt, WORD_ORDER_BIG_ENDIAN, BYTE_ORDER_BIG_ENDIAN, r)

void setUp(void)   {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* raw_to_double                                                        */
/* ------------------------------------------------------------------ */

void test_raw_u8(void)
{
    uint16_t r[1] = {0x00AB};
    TEST_ASSERT_EQUAL_INT(171, (int)RAW(r, FMT_U8));
}

void test_raw_s8_negative(void)
{
    uint16_t r[1] = {0x00FF}; /* -1 as int8 */
    TEST_ASSERT_EQUAL_INT(-1, (int)RAW(r, FMT_S8));
}

void test_raw_u16(void)
{
    uint16_t r[1] = {0xFFFF};
    TEST_ASSERT_EQUAL_INT(65535, (int)RAW(r, FMT_U16));
}

void test_raw_s16_negative(void)
{
    uint16_t r[1] = {0xFFFF}; /* -1 as int16 */
    TEST_ASSERT_EQUAL_INT(-1, (int)RAW(r, FMT_S16));
}

void test_raw_u32(void)
{
    /* Temperature = 2345 (23.45 C with scale 0.01) */
    uint16_t r[2] = {0x0000, 0x0929}; /* 0x00000929 = 2345 */
    TEST_ASSERT_EQUAL_INT(2345, (int)RAW(r, FMT_U32));
}

void test_raw_s32_negative(void)
{
    /* -1 as int32: 0xFFFFFFFF */
    uint16_t r[2] = {0xFFFF, 0xFFFF};
    TEST_ASSERT_EQUAL_INT(-1, (int)RAW(r, FMT_S32));
}

void test_raw_float(void)
{
    /* 3.14f as two 16-bit words big-endian */
    float f = 3.14f;
    uint32_t u;
    memcpy(&u, &f, 4);
    uint16_t r[2] = {(uint16_t)(u >> 16), (uint16_t)(u & 0xFFFF)};
    double result = RAW(r, FMT_FLOAT);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, (float)result);
}

/* ------------------------------------------------------------------ */
/* value_to_string                                                      */
/* ------------------------------------------------------------------ */

void test_vts_integer(void)
{
    char buf[32];
    value_to_string(100.0, 1.0, 0.0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("100", buf);
}

void test_vts_with_scale(void)
{
    char buf[32];
    /* raw=2345, scale=0.01 -> 23.45 */
    value_to_string(2345.0, 0.01, 0.0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("23.45", buf);
}

/* u16 max value 65535 -- tests large positive integer formatting */
void test_vts_large_integer(void)
{
    char buf[32];
    value_to_string(65535.0, 1.0, 0.0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("65535", buf);
}

void test_vts_signed_negative(void)
{
    char buf[32];
    /* s16: -1 raw, scale=0.1 */
    value_to_string(-1.0, 0.1, 0.0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("-0.1", buf);
}

/* ------------------------------------------------------------------ */
/* double_to_regs                                                       */
/* ------------------------------------------------------------------ */

void test_dtr_u16(void)
{
    uint16_t r[1] = {0};
    DTR(42.0, FMT_U16, r);
    TEST_ASSERT_EQUAL_HEX16(42, r[0]);
}

void test_dtr_s16_negative(void)
{
    uint16_t r[1] = {0};
    DTR(-1.0, FMT_S16, r);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r[0]);
}

void test_dtr_u32(void)
{
    uint16_t r[2] = {0, 0};
    DTR(131074.0, FMT_U32, r); /* 0x00020002 */
    TEST_ASSERT_EQUAL_HEX16(0x0002, r[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0002, r[1]);
}

void test_dtr_float_roundtrip(void)
{
    uint16_t r[2] = {0, 0};
    DTR(3.14, FMT_FLOAT, r);
    /* Read back via raw_to_double */
    double result = RAW(r, FMT_FLOAT);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, (float)result);
}

/* ------------------------------------------------------------------ */
/* decode_string_regs                                                   */
/* ------------------------------------------------------------------ */

void test_string_decode_ascii(void)
{
    /* "WB" stored big-endian: word0 = 'W'<<8 | 'B' = 0x5742 */
    uint16_t r[1] = {0x5742};
    char buf[8];
    decode_string_regs(r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("WB", buf);
}

void test_string_decode_multi_word(void)
{
    /* "WBMS" = 0x5742 0x4D53 */
    uint16_t r[2] = {0x5742, 0x4D53};
    char buf[8];
    decode_string_regs(r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("WBMS", buf);
}

void test_string_decode_null_terminated(void)
{
    /* "AB\0" stored as 0x4142 0x0000 */
    uint16_t r[2] = {0x4142, 0x0000};
    char buf[8];
    decode_string_regs(r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("AB", buf);
}

void test_string_decode_odd_null(void)
{
    /* "A\0" stored as 0x4100 */
    uint16_t r[1] = {0x4100};
    char buf[8];
    decode_string_regs(r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("A", buf);
}

void test_string_decode_buf_limit(void)
{
    /* 4-word string, buf only holds 3 chars + null */
    uint16_t r[4] = {0x4142, 0x4344, 0x4546, 0x4748};
    char buf[4]; /* max 3 chars + null */
    decode_string_regs(r, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("ABC", buf);
}

/* ==================================================================
 * BCD format tests
 * ================================================================== */

void test_bcd8_simple(void)
{
    /* BCD8: 1 byte, 0x45 = 45 */
    uint16_t r[1] = {0x0045};
    TEST_ASSERT_EQUAL_INT(45, (int)raw_to_double(r, FMT_BCD8,
                                                  WORD_ORDER_BIG_ENDIAN,
                                                  BYTE_ORDER_BIG_ENDIAN));
}

void test_bcd16_four_digits(void)
{
    /* BCD16: 2 bytes in 1 register, 0x1234 = 1234 */
    uint16_t r[1] = {0x1234};
    TEST_ASSERT_EQUAL_INT(1234, (int)raw_to_double(r, FMT_BCD16,
                                                    WORD_ORDER_BIG_ENDIAN,
                                                    BYTE_ORDER_BIG_ENDIAN));
}

void test_bcd32_eight_digits(void)
{
    /* BCD32: 4 bytes in 2 registers, 0x00001234 = 1234 */
    uint16_t r[2] = {0x0000, 0x1234};
    TEST_ASSERT_EQUAL_INT(1234, (int)raw_to_double(r, FMT_BCD32,
                                                    WORD_ORDER_BIG_ENDIAN,
                                                    BYTE_ORDER_BIG_ENDIAN));
}

void test_bcd32_large(void)
{
    /* 0x12345678 = 12345678 */
    uint16_t r[2] = {0x1234, 0x5678};
    TEST_ASSERT_EQUAL_INT(12345678, (int)raw_to_double(r, FMT_BCD32,
                                                        WORD_ORDER_BIG_ENDIAN,
                                                        BYTE_ORDER_BIG_ENDIAN));
}

void test_bcd24(void)
{
    /* BCD24: 3 bytes on 2 registers.
     * WB layout: norm[0] high byte = unused padding, norm[0] low byte = B0,
     *            norm[1] high byte = B1,              norm[1] low byte = B2.
     * Example: 12 34 56 -> B0=0x12, B1=0x34, B2=0x56 -> 123456 */
    uint16_t r[2] = {0x0012, 0x3456}; /* norm[0].lo=0x12, norm[1]=0x3456 */
    TEST_ASSERT_EQUAL_INT(123456, (int)raw_to_double(r, FMT_BCD24,
                                                      WORD_ORDER_BIG_ENDIAN,
                                                      BYTE_ORDER_BIG_ENDIAN));
}

void test_raw_u64(void)
{
    /* U64: 4 registers, big-endian. Value = 0x0000000100000001 = 4294967297 */
    uint16_t r[4] = {0x0000, 0x0001, 0x0000, 0x0001};
    /* 4294967297 fits in double exactly (< 2^53) */
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 4294967297.0f, (float)RAW(r, FMT_U64));
}

void test_raw_s64_negative(void)
{
    /* S64: -1 = 0xFFFFFFFFFFFFFFFF */
    uint16_t r[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    TEST_ASSERT_EQUAL_INT(-1, (long long)RAW(r, FMT_S64));
}

/* string_to_value edge cases */
void test_stv_normal(void)
{
    /* 1.0 / 0.1 = 10.0 raw */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, (float)string_to_value("1.0", 0.1, 0.0));
}

void test_stv_empty_string(void)
{
    /* Empty string -> strtod returns 0.0 (no chars consumed) -> raw = 0 */
    TEST_ASSERT_EQUAL_INT(0, (int)string_to_value("", 1.0, 0.0));
}

void test_stv_garbage(void)
{
    /* Non-numeric string -> raw = 0 */
    TEST_ASSERT_EQUAL_INT(0, (int)string_to_value("abc", 1.0, 0.0));
}

void test_stv_scale_zero(void)
{
    /* scale=0: division skipped, returns parsed value as-is */
    TEST_ASSERT_EQUAL_INT(5, (int)string_to_value("5", 0.0, 0.0));
}

/* ==================================================================
 * Word order tests
 * ================================================================== */

void test_word_order_little_endian_u32(void)
{
    /* Value 0x00010002 = 65538 stored little-endian word order:
     * regs[0] = LSW = 0x0002, regs[1] = MSW = 0x0001 */
    uint16_t r[2] = {0x0002, 0x0001};
    TEST_ASSERT_EQUAL_INT(65538, (int)raw_to_double(r, FMT_U32,
                                                     WORD_ORDER_LITTLE_ENDIAN,
                                                     BYTE_ORDER_BIG_ENDIAN));
}

void test_word_order_big_endian_u32(void)
{
    /* Same value big-endian: regs[0]=MSW=0x0001, regs[1]=LSW=0x0002 */
    uint16_t r[2] = {0x0001, 0x0002};
    TEST_ASSERT_EQUAL_INT(65538, (int)raw_to_double(r, FMT_U32,
                                                     WORD_ORDER_BIG_ENDIAN,
                                                     BYTE_ORDER_BIG_ENDIAN));
}

/* ==================================================================
 * Byte order tests
 * ================================================================== */

void test_byte_order_little_endian_u16(void)
{
    /* Value 0x0102 = 258, stored little-endian bytes: lo=0x02 hi=0x01 -> word = 0x0102 ?
     * Actually in little-endian byte order the word on the wire is 0x0201 (lo byte first),
     * which means the register contains 0x0201.  We swap bytes to get 0x0102 = 258. */
    uint16_t r[1] = {0x0201};
    TEST_ASSERT_EQUAL_INT(258, (int)raw_to_double(r, FMT_U16,
                                                   WORD_ORDER_BIG_ENDIAN,
                                                   BYTE_ORDER_LITTLE_ENDIAN));
}

void test_byte_order_big_endian_u16(void)
{
    /* Big-endian (default): register value is the value directly */
    uint16_t r[1] = {0x0102};
    TEST_ASSERT_EQUAL_INT(258, (int)raw_to_double(r, FMT_U16,
                                                   WORD_ORDER_BIG_ENDIAN,
                                                   BYTE_ORDER_BIG_ENDIAN));
}

/* ==================================================================
 * error_value tests
 * ================================================================== */

void test_error_value_match(void)
{
    /* 0x7FFF raw == configured error_value -> is_error_value returns true */
    TEST_ASSERT_TRUE(is_error_value(32767.0, 32767.0));
}

void test_error_value_no_match(void)
{
    TEST_ASSERT_FALSE(is_error_value(100.0, 32767.0));
}

void test_error_value_nan_disabled(void)
{
    /* NaN error_value means disabled -> never matches */
    TEST_ASSERT_FALSE(is_error_value(32767.0, (double)NAN));
}

void test_error_value_zero(void)
{
    /* Zero can be a valid error value */
    TEST_ASSERT_TRUE(is_error_value(0.0, 0.0));
}

/* ==================================================================
 * double_to_regs: U64 / S64
 * ================================================================== */

void test_dtr_u64_zero(void)
{
    uint16_t r[4] = {0};
    DTR(0.0, FMT_U64, r);
    TEST_ASSERT_EQUAL_INT(0, (long long)RAW(r, FMT_U64));
}

void test_dtr_u64_roundtrip(void)
{
    uint16_t r[4] = {0};
    DTR(4294967297.0, FMT_U64, r);
    TEST_ASSERT_EQUAL_INT(4294967297LL, (long long)RAW(r, FMT_U64));
}

void test_dtr_u64_2pow53(void)
{
    uint16_t r[4] = {0};
    DTR(9007199254740992.0, FMT_U64, r);
    TEST_ASSERT_EQUAL_INT(9007199254740992LL, (long long)RAW(r, FMT_U64));
}

void test_dtr_s64_negative(void)
{
    uint16_t r[4] = {0};
    DTR(-1.0, FMT_S64, r);
    TEST_ASSERT_EQUAL_INT(-1, (long long)RAW(r, FMT_S64));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r[0]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r[1]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r[2]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, r[3]);
}

void test_dtr_u64_word_order_little(void)
{
    uint16_t r[4] = {0};
    /* Value 4294967297 = 0x0000000100000001 -> canonical words:
     *   norm[0]=0x0000, norm[1]=0x0001, norm[2]=0x0000, norm[3]=0x0001.
     * Little-endian word order reverses: regs[0]=LSW=0x0001. */
    double_to_regs(4294967297.0, FMT_U64,
                   WORD_ORDER_LITTLE_ENDIAN, BYTE_ORDER_BIG_ENDIAN, r);
    TEST_ASSERT_EQUAL_HEX16(0x0001, r[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, r[1]);
    TEST_ASSERT_EQUAL_HEX16(0x0001, r[2]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, r[3]);
    /* Read back with matching little-endian word order */
    double back = raw_to_double(r, FMT_U64,
                                WORD_ORDER_LITTLE_ENDIAN, BYTE_ORDER_BIG_ENDIAN);
    TEST_ASSERT_EQUAL_INT(4294967297LL, (long long)back);
}

/* ==================================================================
 * double_to_regs: BCD formats
 * ================================================================== */

void test_dtr_bcd8(void)
{
    uint16_t r[1] = {0};
    DTR(99.0, FMT_BCD8, r);
    TEST_ASSERT_EQUAL_INT(99, (int)RAW(r, FMT_BCD8));

    DTR(45.0, FMT_BCD8, r);
    TEST_ASSERT_EQUAL_INT(45, (int)RAW(r, FMT_BCD8));
}

void test_dtr_bcd16(void)
{
    uint16_t r[1] = {0};
    DTR(9999.0, FMT_BCD16, r);
    TEST_ASSERT_EQUAL_INT(9999, (int)RAW(r, FMT_BCD16));

    DTR(1234.0, FMT_BCD16, r);
    TEST_ASSERT_EQUAL_INT(1234, (int)RAW(r, FMT_BCD16));
}

void test_dtr_bcd24(void)
{
    uint16_t r[2] = {0};
    DTR(999999.0, FMT_BCD24, r);
    TEST_ASSERT_EQUAL_INT(999999, (int)RAW(r, FMT_BCD24));

    DTR(123456.0, FMT_BCD24, r);
    TEST_ASSERT_EQUAL_INT(123456, (int)RAW(r, FMT_BCD24));
}

void test_dtr_bcd24_padding(void)
{
    uint16_t r[2] = {0};
    DTR(123456.0, FMT_BCD24, r);
    /* High byte of first reg must be padding (zero) */
    TEST_ASSERT_EQUAL_INT(0, r[0] & 0xFF00);
    TEST_ASSERT_EQUAL_INT(123456, (int)RAW(r, FMT_BCD24));
}

void test_dtr_bcd32(void)
{
    uint16_t r[2] = {0};
    DTR(99999999.0, FMT_BCD32, r);
    TEST_ASSERT_EQUAL_INT(99999999, (int)RAW(r, FMT_BCD32));

    DTR(12345678.0, FMT_BCD32, r);
    TEST_ASSERT_EQUAL_INT(12345678, (int)RAW(r, FMT_BCD32));
}

/* ==================================================================
 * encode_string_regs
 * ================================================================== */

void test_enc_str_even(void)
{
    uint16_t r[2] = {0};
    encode_string_regs("WBMS", 2, r);
    char buf[8];
    decode_string_regs(r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("WBMS", buf);
}

void test_enc_str_odd(void)
{
    uint16_t r[2] = {0};
    encode_string_regs("ABC", 2, r);
    char buf[8];
    decode_string_regs(r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("ABC", buf);
}

void test_enc_str_truncate(void)
{
    uint16_t r[2] = {0};
    encode_string_regs("ABCDE", 2, r);
    char buf[8];
    decode_string_regs(r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("ABCD", buf);
}

void test_enc_str_pad(void)
{
    uint16_t r[2] = {0};
    encode_string_regs("AB", 2, r);
    char buf[8];
    decode_string_regs(r, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("AB", buf);
}

void test_enc_str_empty(void)
{
    uint16_t r[1] = {0};
    encode_string_regs("", 1, r);
    char buf[8];
    decode_string_regs(r, 1, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_raw_u8);
    RUN_TEST(test_raw_s8_negative);
    RUN_TEST(test_raw_u16);
    RUN_TEST(test_raw_s16_negative);
    RUN_TEST(test_raw_u32);
    RUN_TEST(test_raw_s32_negative);
    RUN_TEST(test_raw_float);

    RUN_TEST(test_vts_integer);
    RUN_TEST(test_vts_with_scale);
    RUN_TEST(test_vts_large_integer);
    RUN_TEST(test_vts_signed_negative);

    RUN_TEST(test_dtr_u16);
    RUN_TEST(test_dtr_s16_negative);
    RUN_TEST(test_dtr_u32);
    RUN_TEST(test_dtr_float_roundtrip);

    /* double_to_regs: U64 / S64 */
    RUN_TEST(test_dtr_u64_zero);
    RUN_TEST(test_dtr_u64_roundtrip);
    RUN_TEST(test_dtr_u64_2pow53);
    RUN_TEST(test_dtr_s64_negative);
    RUN_TEST(test_dtr_u64_word_order_little);

    /* double_to_regs: BCD */
    RUN_TEST(test_dtr_bcd8);
    RUN_TEST(test_dtr_bcd16);
    RUN_TEST(test_dtr_bcd24);
    RUN_TEST(test_dtr_bcd24_padding);
    RUN_TEST(test_dtr_bcd32);

    RUN_TEST(test_string_decode_ascii);
    RUN_TEST(test_string_decode_multi_word);
    RUN_TEST(test_string_decode_null_terminated);
    RUN_TEST(test_string_decode_odd_null);
    RUN_TEST(test_string_decode_buf_limit);

    /* encode_string_regs */
    RUN_TEST(test_enc_str_even);
    RUN_TEST(test_enc_str_odd);
    RUN_TEST(test_enc_str_truncate);
    RUN_TEST(test_enc_str_pad);
    RUN_TEST(test_enc_str_empty);

    /* BCD */
    RUN_TEST(test_bcd8_simple);
    RUN_TEST(test_bcd16_four_digits);
    RUN_TEST(test_bcd32_eight_digits);
    RUN_TEST(test_bcd32_large);
    RUN_TEST(test_bcd24);
    RUN_TEST(test_raw_u64);
    RUN_TEST(test_raw_s64_negative);

    /* Word order */
    RUN_TEST(test_word_order_little_endian_u32);
    RUN_TEST(test_word_order_big_endian_u32);

    /* Byte order */
    RUN_TEST(test_byte_order_little_endian_u16);
    RUN_TEST(test_byte_order_big_endian_u16);

    /* error_value */
    RUN_TEST(test_error_value_match);
    RUN_TEST(test_error_value_no_match);
    RUN_TEST(test_error_value_nan_disabled);
    RUN_TEST(test_error_value_zero);

    /* string_to_value */
    RUN_TEST(test_stv_normal);
    RUN_TEST(test_stv_empty_string);
    RUN_TEST(test_stv_garbage);
    RUN_TEST(test_stv_scale_zero);

    return UNITY_END();
}
