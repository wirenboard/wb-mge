#include "unity.h"
#include "console_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Forward declarations for the 4 helper functions exposed via SNIFFER_STATIC
 * (when __unittest_env__ is defined, SNIFFER_STATIC expands to nothing, making
 * these functions have external linkage and visible here). */
void     bytes_to_hex(const uint8_t *data, uint16_t len, char *out, size_t out_size);
bool     crc_check(const uint8_t *data, size_t len);
void     strip_arbitration(uint8_t *data, size_t len, uint8_t **effective, size_t *effective_len);
bool     fm_is_slave_subcmd(uint8_t subcmd);

void setUp(void)
{
}

void tearDown(void)
{
}

/* ============================================================
 * bytes_to_hex tests
 * ============================================================ */

void test_bytes_to_hex_empty(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - empty input (len=0)");
    LOG_MESSAGE();

    uint8_t empty[] = {0x00};  /* non-NULL pointer, len=0 so no bytes are read */
    char out[16] = {0xFF};
    bytes_to_hex(empty, 0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_bytes_to_hex_single_byte(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - single byte 0xAB");
    LOG_MESSAGE();

    uint8_t data[] = {0xAB};
    char out[16];
    bytes_to_hex(data, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AB", out);
}

void test_bytes_to_hex_multiple_bytes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - two bytes 0x8B 0xF7");
    LOG_MESSAGE();

    uint8_t data[] = {0x8B, 0xF7};
    char out[16];
    bytes_to_hex(data, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("8BF7", out);
}

void test_bytes_to_hex_zero_byte(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - single zero byte 0x00");
    LOG_MESSAGE();

    uint8_t data[] = {0x00};
    char out[16];
    bytes_to_hex(data, 1, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("00", out);
}

void test_bytes_to_hex_all_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - two 0xFF bytes");
    LOG_MESSAGE();

    uint8_t data[] = {0xFF, 0xFF};
    char out[16];
    bytes_to_hex(data, 2, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("FFFF", out);
}

void test_bytes_to_hex_buffer_exact_fit(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - buffer exact fit (out_size=5, 2 bytes)");
    LOG_MESSAGE();

    /* out_size=5: condition (pos+2) < 5 allows pos=0 and pos=2 → 2 bytes fit */
    uint8_t data[] = {0x01, 0x02};
    char out[5];
    bytes_to_hex(data, 2, out, 5);
    TEST_ASSERT_EQUAL_STRING("0102", out);
}

void test_bytes_to_hex_truncation(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - truncation: 3 bytes, out_size=5 → only 2 fit");
    LOG_MESSAGE();

    /* out_size=5: (pos+2) < 5 allows only pos 0 and 2, i.e., 2 bytes fit.
     * Third byte (0x03) is truncated because pos+2 would equal out_size. */
    uint8_t data[] = {0x01, 0x02, 0x03};
    char out[5];
    bytes_to_hex(data, 3, out, 5);
    TEST_ASSERT_EQUAL_STRING("0102", out);
}

/* ============================================================
 * crc_check tests
 * ============================================================ */

void test_crc_check_len_0(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - len=0 → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(crc_check(NULL, 0));
}

void test_crc_check_len_1(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - len=1 → false");
    LOG_MESSAGE();

    uint8_t data[] = {0x83};
    TEST_ASSERT_FALSE(crc_check(data, 1));
}

void test_crc_check_len_2(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - len=2 → false");
    LOG_MESSAGE();

    uint8_t data[] = {0x83, 0x03};
    TEST_ASSERT_FALSE(crc_check(data, 2));
}

void test_crc_check_len_3(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - len=3 → false");
    LOG_MESSAGE();

    uint8_t data[] = {0x83, 0x03, 0x00};
    TEST_ASSERT_FALSE(crc_check(data, 3));
}

void test_crc_check_valid_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - valid Modbus RTU request [83 03 00 61 00 02 8B F7]");
    LOG_MESSAGE();

    /* CRC of first 6 bytes = 0xF78B (lo=0x8B, hi=0xF7) */
    uint8_t data[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    TEST_ASSERT_TRUE(crc_check(data, sizeof(data)));
}

void test_crc_check_valid_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - valid Modbus RTU response [83 03 04 00 03 00 1E 28 33]");
    LOG_MESSAGE();

    uint8_t data[] = {0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33};
    TEST_ASSERT_TRUE(crc_check(data, sizeof(data)));
}

void test_crc_check_valid_fm(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - valid Fast Modbus packet [FD 46 12 52 5D]");
    LOG_MESSAGE();

    /* CRC of first 3 bytes [FD 46 12] = 0x5D52 (lo=0x52, hi=0x5D) */
    uint8_t data[] = {0xFD, 0x46, 0x12, 0x52, 0x5D};
    TEST_ASSERT_TRUE(crc_check(data, sizeof(data)));
}

void test_crc_check_invalid_crc(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - invalid CRC (last byte flipped)");
    LOG_MESSAGE();

    /* Valid packet with last byte corrupted */
    uint8_t data[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7 ^ 0xFF};
    TEST_ASSERT_FALSE(crc_check(data, sizeof(data)));
}

void test_crc_check_all_zeros_len4(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - all-zero 4-byte packet (invalid CRC)");
    LOG_MESSAGE();

    /* CRC of [0x00, 0x00] is 0x4040 (lo=0x40, hi=0x40), not [0x00, 0x00] */
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(crc_check(data, sizeof(data)));
}

/* ============================================================
 * strip_arbitration tests
 * ============================================================ */

void test_strip_arbitration_no_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - no leading 0xFF, data unchanged");
    LOG_MESSAGE();

    uint8_t data[] = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, sizeof(data), &effective, &effective_len);

    TEST_ASSERT_EQUAL_PTR(data, effective);
    TEST_ASSERT_EQUAL(sizeof(data), effective_len);
}

void test_strip_arbitration_len0(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - len=0, early return");
    LOG_MESSAGE();

    uint8_t data[] = {0xFF, 0xFF};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, 0, &effective, &effective_len);

    TEST_ASSERT_EQUAL_PTR(data, effective);
    TEST_ASSERT_EQUAL(0, effective_len);
}

void test_strip_arbitration_all_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - all-0xFF, NOT stripped (tlen=0 < 4)");
    LOG_MESSAGE();

    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, sizeof(data), &effective, &effective_len);

    /* After truncating all FF bytes, tlen=0 < 4 → not stripped */
    TEST_ASSERT_EQUAL_PTR(data, effective);
    TEST_ASSERT_EQUAL(sizeof(data), effective_len);
}

void test_strip_arbitration_ff_non_fm(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - 0xFF prefix but non-FM func code (0x03), NOT stripped");
    LOG_MESSAGE();

    /* After stripping 2 FF bytes: t[0]=0x01, t[1]=0x03 → not 0x46/0x60 */
    uint8_t data[] = {0xFF, 0xFF, 0x01, 0x03, 0x00, 0x00, 0xAA, 0xBB};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, sizeof(data), &effective, &effective_len);

    TEST_ASSERT_EQUAL_PTR(data, effective);
    TEST_ASSERT_EQUAL(sizeof(data), effective_len);
}

void test_strip_arbitration_ff_fm_fc46(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - 2x0xFF prefix, FM func 0x46, stripped");
    LOG_MESSAGE();

    /* [FF FF FD 46 12 52 5D]: after stripping 2 FFs, t=[FD 46 12 52 5D], tlen=5, t[1]=0x46 → stripped */
    uint8_t data[] = {0xFF, 0xFF, 0xFD, 0x46, 0x12, 0x52, 0x5D};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, sizeof(data), &effective, &effective_len);

    TEST_ASSERT_EQUAL_PTR(&data[2], effective);
    TEST_ASSERT_EQUAL(5, effective_len);
    TEST_ASSERT_EQUAL_HEX8(0xFD, effective[0]);
    TEST_ASSERT_EQUAL_HEX8(0x46, effective[1]);
}

void test_strip_arbitration_ff_fm_fc60(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - 1x0xFF prefix, FM func 0x60, stripped");
    LOG_MESSAGE();

    /* [FF FD 60 03 00 06 24 66 83 C4 61]: after stripping 1 FF, t=[FD 60 03...], tlen=10, t[1]=0x60 → stripped */
    uint8_t data[] = {0xFF, 0xFD, 0x60, 0x03, 0x00, 0x06, 0x24, 0x66, 0x83, 0xC4, 0x61};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, sizeof(data), &effective, &effective_len);

    TEST_ASSERT_EQUAL_PTR(&data[1], effective);
    TEST_ASSERT_EQUAL(10, effective_len);
    TEST_ASSERT_EQUAL_HEX8(0xFD, effective[0]);
    TEST_ASSERT_EQUAL_HEX8(0x60, effective[1]);
}

void test_strip_arbitration_ff_fm_too_short(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test strip_arbitration - 2x0xFF + 2 payload bytes, too short after strip (tlen=2 < 4)");
    LOG_MESSAGE();

    /* [FF FF FD 46]: after stripping 2 FFs, t=[FD 46], tlen=2 < 4 → NOT stripped */
    uint8_t data[] = {0xFF, 0xFF, 0xFD, 0x46};
    uint8_t *effective;
    size_t effective_len;
    strip_arbitration(data, sizeof(data), &effective, &effective_len);

    TEST_ASSERT_EQUAL_PTR(data, effective);
    TEST_ASSERT_EQUAL(sizeof(data), effective_len);
}

/* ============================================================
 * fm_is_slave_subcmd tests
 * ============================================================ */

void test_fm_is_slave_subcmd_03(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x03 → true");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE(fm_is_slave_subcmd(0x03));
}

void test_fm_is_slave_subcmd_04(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x04 → true");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE(fm_is_slave_subcmd(0x04));
}

void test_fm_is_slave_subcmd_09(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x09 → true");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE(fm_is_slave_subcmd(0x09));
}

void test_fm_is_slave_subcmd_11(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x11 → true");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE(fm_is_slave_subcmd(0x11));
}

void test_fm_is_slave_subcmd_12(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x12 → true");
    LOG_MESSAGE();

    TEST_ASSERT_TRUE(fm_is_slave_subcmd(0x12));
}

void test_fm_is_slave_subcmd_master_01(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x01 (master) → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0x01));
}

void test_fm_is_slave_subcmd_master_02(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x02 (master) → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0x02));
}

void test_fm_is_slave_subcmd_master_08(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x08 (master) → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0x08));
}

void test_fm_is_slave_subcmd_master_10(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x10 (master) → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0x10));
}

void test_fm_is_slave_subcmd_master_13(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x13 (master) → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0x13));
}

void test_fm_is_slave_subcmd_master_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0xFF (master) → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0xFF));
}

void test_fm_is_slave_subcmd_master_00(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fm_is_slave_subcmd - 0x00 → false");
    LOG_MESSAGE();

    TEST_ASSERT_FALSE(fm_is_slave_subcmd(0x00));
}

int main(void)
{
    UNITY_BEGIN();

    /* bytes_to_hex tests */
    RUN_TEST(test_bytes_to_hex_empty);
    RUN_TEST(test_bytes_to_hex_single_byte);
    RUN_TEST(test_bytes_to_hex_multiple_bytes);
    RUN_TEST(test_bytes_to_hex_zero_byte);
    RUN_TEST(test_bytes_to_hex_all_ff);
    RUN_TEST(test_bytes_to_hex_buffer_exact_fit);
    RUN_TEST(test_bytes_to_hex_truncation);

    /* crc_check tests */
    RUN_TEST(test_crc_check_len_0);
    RUN_TEST(test_crc_check_len_1);
    RUN_TEST(test_crc_check_len_2);
    RUN_TEST(test_crc_check_len_3);
    RUN_TEST(test_crc_check_valid_request);
    RUN_TEST(test_crc_check_valid_response);
    RUN_TEST(test_crc_check_valid_fm);
    RUN_TEST(test_crc_check_invalid_crc);
    RUN_TEST(test_crc_check_all_zeros_len4);

    /* strip_arbitration tests */
    RUN_TEST(test_strip_arbitration_no_ff);
    RUN_TEST(test_strip_arbitration_len0);
    RUN_TEST(test_strip_arbitration_all_ff);
    RUN_TEST(test_strip_arbitration_ff_non_fm);
    RUN_TEST(test_strip_arbitration_ff_fm_fc46);
    RUN_TEST(test_strip_arbitration_ff_fm_fc60);
    RUN_TEST(test_strip_arbitration_ff_fm_too_short);

    /* fm_is_slave_subcmd tests */
    RUN_TEST(test_fm_is_slave_subcmd_03);
    RUN_TEST(test_fm_is_slave_subcmd_04);
    RUN_TEST(test_fm_is_slave_subcmd_09);
    RUN_TEST(test_fm_is_slave_subcmd_11);
    RUN_TEST(test_fm_is_slave_subcmd_12);
    RUN_TEST(test_fm_is_slave_subcmd_master_01);
    RUN_TEST(test_fm_is_slave_subcmd_master_02);
    RUN_TEST(test_fm_is_slave_subcmd_master_08);
    RUN_TEST(test_fm_is_slave_subcmd_master_10);
    RUN_TEST(test_fm_is_slave_subcmd_master_13);
    RUN_TEST(test_fm_is_slave_subcmd_master_ff);
    RUN_TEST(test_fm_is_slave_subcmd_master_00);

    return UNITY_END();
}
