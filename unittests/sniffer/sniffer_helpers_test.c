#include "unity.h"
#include "console_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* sniff_packet_t is declared in sniffer.h (shared by production and test builds). */
#include "sniffer.h"

/* Forward declarations for the 4 helper functions exposed via SNIFFER_STATIC
 * (when __unittest_env__ is defined, SNIFFER_STATIC expands to nothing, making
 * these functions have external linkage and visible here). */
void     bytes_to_hex(const uint8_t *data, uint16_t len, char *out, size_t out_size);
bool     crc_check(const uint8_t *data, size_t len);
void     strip_arbitration(uint8_t *data, size_t len, uint8_t **effective, size_t *effective_len);
bool     fm_is_slave_subcmd(uint8_t subcmd);

/* Forward declarations for JSON formatter functions */
int      format_packet_json(char *buf, size_t buf_size, uint32_t id, const sniff_packet_t *pkt);

/* pdu_direction_t comes from sniffer.h, so no local typedef is needed here. */

/* Forward declaration for classify_direction exposed via SNIFFER_STATIC */
pdu_direction_t classify_direction(const uint8_t *data, size_t len);

/* The frame-decision state machine, exposed via SNIFFER_STATIC. sniff_input_t and
 * sniff_decision_t come from sniffer.h. */
sniff_decision_t sniffer_decide(const sniff_input_t *in);

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

void test_bytes_to_hex_no_overflow_at_tight_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - tight boundary must not write past out_size (off-by-one)");
    LOG_MESSAGE();

    struct { char out[4]; char canary; } buf;
    buf.canary = (char)0x7E;
    memset(buf.out, (int)0xAA, sizeof(buf.out));

    uint8_t data[] = {0x01, 0x02};
    bytes_to_hex(data, 2, buf.out, sizeof(buf.out));

    TEST_ASSERT_EQUAL_STRING("01", buf.out);
    TEST_ASSERT_EQUAL_HEX8(0x7E, buf.canary);
}

/* Exhaustively check every byte value against the reference snprintf("%02X")
 * formatting. Pins the nibble lookup table down: a swapped nibble order, a
 * lowercase table or an off-by-one in the digit/letter transition would fail here. */
void test_bytes_to_hex_all_byte_values_match_printf(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test bytes_to_hex - all 256 byte values match %%02X");
    LOG_MESSAGE();

    uint8_t all[256];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;

    char expected[256 * 2 + 1];
    for (int i = 0; i < 256; i++) {
        snprintf(expected + i * 2, 3, "%02X", all[i]);
    }

    char out[256 * 2 + 1];
    bytes_to_hex(all, 256, out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING(expected, out);
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

void test_crc_check_valid_4byte_fc07(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test crc_check - minimal valid 4-byte RTU frame FC07 [01 07 41 E2]");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x07, 0x41, 0xE2};
    TEST_ASSERT_TRUE(crc_check(data, sizeof(data)));
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

/* ============================================================
 * format_packet_json tests
 * ============================================================ */

void test_json_packet_type_field(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - type field is 'packet'");
    LOG_MESSAGE();

    /* Non-timeout packet must produce "type":"packet" */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = true, .is_timeout = false,
        .slave_id = 1, .function = 3,
        .data = {0xAB, 0xCD}, .data_len = 2,
    };
    char buf[512];
    int n = format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"type\":\"packet\""));
}

void test_json_packet_sender_master(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - is_master=true → sender:'master'");
    LOG_MESSAGE();

    /* is_master=true → "sender":"master" */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = true, .slave_id = 1, .function = 3,
        .data = {0x01}, .data_len = 1,
    };
    char buf[512];
    format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sender\":\"master\""));
}

void test_json_packet_sender_slave(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - is_master=false → sender:'slave'");
    LOG_MESSAGE();

    /* is_master=false → "sender":"slave" */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = false,
        .crc_valid = true, .slave_id = 1, .function = 3,
        .data = {0x01}, .data_len = 1,
    };
    char buf[512];
    format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"sender\":\"slave\""));
}

void test_json_packet_crc_valid_true(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - crc_valid=true → JSON boolean true, not string");
    LOG_MESSAGE();

    /* crc_valid must be JSON boolean true, NOT the quoted string "true" */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = true, .slave_id = 1, .function = 3,
        .data = {0x01}, .data_len = 1,
    };
    char buf[512];
    format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"crc_valid\":true"));
    TEST_ASSERT_NULL(strstr(buf, "\"crc_valid\":\"true\""));
}

void test_json_packet_crc_valid_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - crc_valid=false → JSON boolean false, not string");
    LOG_MESSAGE();

    /* crc_valid=false must produce JSON boolean false, NOT quoted "false" */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = false, .slave_id = 1, .function = 3,
        .data = {0x01}, .data_len = 1,
    };
    char buf[512];
    format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"crc_valid\":false"));
    TEST_ASSERT_NULL(strstr(buf, "\"crc_valid\":\"false\""));
}

void test_json_packet_raw_uppercase_hex(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - raw field is uppercase hex without spaces");
    LOG_MESSAGE();

    /* raw field must be uppercase hex: 0xab → "AB", no spaces */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = true, .slave_id = 0x83, .function = 3,
        .data = {0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7},
        .data_len = 8,
    };
    char buf[512];
    format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw\":\"8303006100028BF7\""));
}

void test_json_packet_raw_length_matches_size(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - raw string length equals data_len * 2");
    LOG_MESSAGE();

    /* raw string length must equal data_len * 2, and size field must match data_len */
    uint8_t data[4] = {0x11, 0x22, 0x33, 0x44};
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = true, .slave_id = 1, .function = 3,
        .data_len = 4,
    };
    memcpy(pkt.data, data, 4);
    char buf[512];
    format_packet_json(buf, sizeof(buf), 1, &pkt);
    /* raw must be "11223344" (8 chars for 4 bytes) */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"raw\":\"11223344\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"size\":4"));
}

void test_json_packet_id_increments(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - id field matches passed counter value");
    LOG_MESSAGE();

    /* id field must match the counter value passed into the function */
    sniff_packet_t pkt = {
        .port = 0, .timestamp_us = 0, .is_master = true,
        .crc_valid = true, .slave_id = 1, .function = 3,
        .data = {0x01}, .data_len = 1,
    };
    char buf[512];

    format_packet_json(buf, sizeof(buf), 1, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":1"));

    format_packet_json(buf, sizeof(buf), 2, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":2"));

    format_packet_json(buf, sizeof(buf), 100, &pkt);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":100"));
}

void test_json_packet_max_length_fits_in_buffer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test format_packet_json - worst-case packet fits in SNIFFER_JSON_BUF_SIZE bytes");
    LOG_MESSAGE();

    /* Worst-case packet: id=UINT32_MAX, port index 0 (name=1),
     * timestamp_us=UINT64_MAX, slave_id=255, function=255,
     * 256 bytes of 0xFF, crc_valid=false.
     * The formatted JSON must fit in SNIFFER_JSON_BUF_SIZE bytes. */
    sniff_packet_t pkt = {
        .port = 0,
        .timestamp_us = UINT64_MAX,
        .is_master = true,
        .crc_valid = false,
        .is_timeout = false,
        .slave_id = 255,
        .function = 255,
        .data_len = 256,
    };
    memset(pkt.data, 0xFF, 256);

    /* Use a buffer matching SNIFFER_JSON_BUF_SIZE */
    char buf[SNIFFER_JSON_BUF_SIZE];
    int n = format_packet_json(buf, sizeof(buf), UINT32_MAX, &pkt);
    /* snprintf returns chars that WOULD have been written; if n < buf_size, no truncation */
    TEST_ASSERT_LESS_THAN((int)sizeof(buf), n);
    /* Only check null terminator if the message was not truncated */
    if (n < (int)sizeof(buf)) {
        TEST_ASSERT_EQUAL('\0', buf[n]);
    }
}

/* ============================================================
 * classify_direction tests
 * ============================================================ */

/* FC02 — Read Discrete Inputs (same length rules as FC01) */

void test_classify_direction_fc02_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC02 - len=8, data[2]=0 → DIRECTION_REQUEST");
    LOG_MESSAGE();

    /* len=8, data[2]=0 (≠3) → DIRECTION_REQUEST */
    uint8_t data[] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_REQUEST, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc02_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC02 - len=6, data[2]=1 (5+1=6, ≠8) → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    /* len=6, 5+data[2]=5+1=6, len≠8 → DIRECTION_RESPONSE */
    uint8_t data[] = {0x01, 0x02, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc02_ambiguous(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC02 - len=8, data[2]=3 → DIRECTION_UNKNOWN (ambiguous)");
    LOG_MESSAGE();

    /* len=8 AND data[2]=3: both request (fixed 8) and response (5+3=8) match → UNKNOWN */
    uint8_t data[] = {0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc04_odd_bytecount_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC04 - len=6, data[2]=1 (odd bytecount) -> DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x04, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* FC07 — Read Exception Status */

void test_classify_direction_fc07_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC07 - len=4 → DIRECTION_REQUEST");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x07, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_REQUEST, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc07_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC07 - len=5 → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x07, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc07_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC07 - len=6 → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x07, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* FC08 — Diagnostics (request and response are both 8 bytes → indistinguishable) */

void test_classify_direction_fc08_always_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC08 - len=8 → DIRECTION_UNKNOWN (indistinguishable)");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* FC0B — Get Comm Event Counter */

void test_classify_direction_fc0b_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC0B - len=4 → DIRECTION_REQUEST");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x0B, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_REQUEST, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc0b_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC0B - len=8 → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc0b_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC0B - len=6 → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x0B, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* FC0F — Write Multiple Coils */

void test_classify_direction_fc0f_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC0F - len=8 → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc0f_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC0F - data[6]=4, len=13 (9+4) → DIRECTION_REQUEST");
    LOG_MESSAGE();

    /* byte_count = data[6] = 4, total = 9 + 4 = 13 */
    uint8_t data[] = {0x01, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_REQUEST, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc0f_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC0F - len=7 → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* FC10 — Write Multiple Registers */

void test_classify_direction_fc10_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC10 - len=8 → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x10, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc10_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC10 - data[6]=4, len=13 (9+4) → DIRECTION_REQUEST");
    LOG_MESSAGE();

    /* byte_count = data[6] = 4, total = 9 + 4 = 13 */
    uint8_t data[] = {0x01, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_REQUEST, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc10_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC10 - len=7 → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x10, 0x00, 0x01, 0x00, 0x02, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* FC11 — Report Server ID */

void test_classify_direction_fc11_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC11 - len=4 → DIRECTION_REQUEST");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x11, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_REQUEST, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc11_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC11 - len=6, data[2]=1 (5+1=6) → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x11, 0x01, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(data, sizeof(data)));
}

void test_classify_direction_fc11_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction FC11 - len=5, data[2]=5 (5+5=10≠5) → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    /* len=5, data[2]=5: 5+data[2]=10 ≠ len=5 → not RESPONSE; len≠4 → not REQUEST → UNKNOWN */
    uint8_t data[] = {0x01, 0x11, 0x05, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* default — unknown function code */

void test_classify_direction_default_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction default - FC=0x20 → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    uint8_t data[] = {0x01, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(data, sizeof(data)));
}

/* exception replies — the high bit of the function code */

/* A device that is on the bus but rejects the request answers with fc|0x80 and an
 * exception code. The shape is unambiguous — nothing but a server ever sets that bit —
 * and it is common: writing an unsupported register is exactly this. Without an explicit
 * case it fell to `default:` and came out UNKNOWN, i.e. guessed at by the state machine:
 * in SNIFF_IDLE that means the reply is announced as a master request and latched as a
 * transaction with fc = 0x86 that nobody ever sent. */
void test_classify_direction_exception_reply(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction exception - FC|0x80, len=5 → DIRECTION_RESPONSE");
    LOG_MESSAGE();

    /* Exception to an FC06 write: addr(1) + 0x86 + exception code 0x02 + CRC(2) = 5. */
    uint8_t fc86[] = {0x23, 0x86, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(fc86, sizeof(fc86)));

    /* The rule is the high bit, not the particular code: FC03 and FC10 alike. */
    uint8_t fc83[] = {0x01, 0x83, 0x02, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(fc83, sizeof(fc83)));
    uint8_t fc90[] = {0x01, 0x90, 0x03, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_RESPONSE, classify_direction(fc90, sizeof(fc90)));
}

void test_classify_direction_exception_wrong_length_unknown(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test classify_direction exception - FC|0x80 of any other length → DIRECTION_UNKNOWN");
    LOG_MESSAGE();

    /* An exception reply is 5 bytes and nothing else. Anything longer or shorter that
     * still carries the high bit is not a frame this classifier can vouch for. */
    uint8_t short_exc[] = {0x23, 0x86, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(short_exc, sizeof(short_exc)));
    uint8_t long_exc[] = {0x23, 0x86, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(DIRECTION_UNKNOWN, classify_direction(long_exc, sizeof(long_exc)));
}

/* ============================================================
 * sniffer_decide tests — the frame-decision table, exercised directly.
 *
 * sniffer_decide() is the whole request/response state machine as a pure function
 * (state + frame properties -> decision). Testing it here covers every branch without
 * having to drive real frames, timers and queues through sniffer_process().
 * ============================================================ */

/* Build the input for a frame arriving in the given state. */
static sniff_input_t decide_in(sniff_state_t state)
{
    sniff_input_t in = {0};
    in.state = state;
    in.dir   = DIRECTION_UNKNOWN;
    return in;
}

/* --- SNIFF_IDLE --- */

void test_decide_idle_fm_master_subcmd(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + FM master subcmd → emit master, stay IDLE");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.is_fm           = true;
    in.fm_slave_subcmd = false;
    in.crc_valid       = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE(d.is_master);
    TEST_ASSERT_TRUE(d.crc_valid);
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
    TEST_ASSERT_FALSE(d.start_timer);
    TEST_ASSERT_FALSE(d.stop_timer);
    TEST_ASSERT_FALSE(d.latch_request);
}

void test_decide_idle_fm_slave_subcmd(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + FM slave subcmd → emit slave");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.is_fm           = true;
    in.fm_slave_subcmd = true;
    in.crc_valid       = false;   /* an FM frame is emitted regardless of CRC */

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE(d.is_master);
    TEST_ASSERT_FALSE_MESSAGE(d.crc_valid, "the frame's real CRC verdict must be carried through");
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
}

void test_decide_idle_crc_error_unsynchronized_drops(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + CRC error, never synced → drop");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid    = false;
    in.synchronized = false;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_FALSE_MESSAGE(d.emit, "direction is unguessable before the first known packet");
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
    TEST_ASSERT_FALSE(d.start_timer);
}

void test_decide_idle_crc_error_alternates_direction(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + CRC error, synced → alternate direction");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid       = false;
    in.synchronized    = true;
    in.last_was_master = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE_MESSAGE(d.is_master, "after a master, a corrupt frame is assumed to be the slave");
    TEST_ASSERT_FALSE(d.crc_valid);

    in.last_was_master = false;
    d = sniffer_decide(&in);
    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE_MESSAGE(d.is_master, "and vice versa");
}

void test_decide_idle_broadcast_no_response_wait(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + broadcast → emit master, no RES_WAIT");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid = true;
    in.broadcast = true;
    in.dir       = DIRECTION_REQUEST;   /* must not reach the request branch */

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE(d.is_master);
    TEST_ASSERT_EQUAL_MESSAGE(SNIFF_IDLE, d.new_state, "a broadcast expects no response");
    TEST_ASSERT_FALSE_MESSAGE(d.start_timer, "no response timer for a broadcast");
    TEST_ASSERT_FALSE(d.latch_request);
}

void test_decide_idle_orphan_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + response → emit slave, stay IDLE");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid = true;
    in.dir       = DIRECTION_RESPONSE;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE(d.is_master);
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
    TEST_ASSERT_FALSE(d.latch_request);
}

void test_decide_idle_request_starts_wait(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + request → emit master, latch, arm timer");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid = true;
    in.dir       = DIRECTION_REQUEST;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE(d.is_master);
    TEST_ASSERT_TRUE(d.crc_valid);
    TEST_ASSERT_TRUE(d.latch_request);
    TEST_ASSERT_EQUAL(SNIFF_RES_WAIT, d.new_state);
    TEST_ASSERT_TRUE(d.start_timer);
    TEST_ASSERT_FALSE(d.stop_timer);
}

/* An ambiguous frame (FC05/FC06/FC08 — request and echo-response have the same shape)
 * arriving on a port that has never seen a packet with a known direction. Dropping it
 * used to hide a whole class of traffic: a master that only writes with FC06 to devices
 * that never answer never leaves SNIFF_IDLE, so nothing at all reached the UI. */
void test_decide_idle_unknown_direction_unsynchronized_emits_master(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + ambiguous, never synced → emit master, latch, arm timer");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid    = true;
    in.dir          = DIRECTION_UNKNOWN;
    in.synchronized = false;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE_MESSAGE(d.emit, "an ambiguous frame must be shown, not silently dropped");
    TEST_ASSERT_TRUE_MESSAGE(d.is_master, "a lone frame on an idle bus is almost always a request");
    TEST_ASSERT_TRUE_MESSAGE(d.crc_valid, "the frame's CRC is valid and must be reported as such");
    TEST_ASSERT_TRUE_MESSAGE(d.latch_request, "latched so the echo-response pairs with it");
    TEST_ASSERT_EQUAL(SNIFF_RES_WAIT, d.new_state);
    TEST_ASSERT_TRUE(d.start_timer);
    TEST_ASSERT_FALSE(d.stop_timer);
}

/* SNIFF_IDLE says the SNIFFER is not tracking an open exchange, which is weaker than
 * "the bus has none": resp_timer_cb() gives up after 200 ms of its own accord, the
 * queue-full path abandons the request outright, a Fast Modbus frame in RES_WAIT
 * resynchronises to IDLE with the request unanswered, and sniffer_enable() can switch
 * the sniffer on mid-transaction. The frame is still called a master, because the
 * alternative is worse rather than righter: alternating off last_was_master would call
 * the frame a slave exactly when it is a retried, still unanswered request, since
 * resp_timer_cb() returns to SNIFF_IDLE with last_was_master = true. */
void test_decide_idle_unknown_direction_master_even_after_master(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - IDLE + ambiguous, synced after a master → still master");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid       = true;
    in.dir             = DIRECTION_UNKNOWN;
    in.synchronized    = true;
    in.last_was_master = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE_MESSAGE(d.is_master, "in SNIFF_IDLE no response is pending, so the frame is a request");
    TEST_ASSERT_TRUE(d.crc_valid);
    TEST_ASSERT_TRUE_MESSAGE(d.latch_request, "an assumed request is latched like a real one");
    TEST_ASSERT_EQUAL(SNIFF_RES_WAIT, d.new_state);
    TEST_ASSERT_TRUE(d.start_timer);
    TEST_ASSERT_FALSE(d.stop_timer);

    /* Same after a slave, where alternation would have agreed anyway. */
    in.last_was_master = false;
    d = sniffer_decide(&in);
    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE_MESSAGE(d.is_master, "and after a slave the answer is the same");
    TEST_ASSERT_TRUE(d.latch_request);
    TEST_ASSERT_EQUAL(SNIFF_RES_WAIT, d.new_state);
    TEST_ASSERT_TRUE(d.start_timer);
}

/* The whole point of latching the guess: an FC06 write and its byte-identical echo are
 * reported as a master/slave pair rather than as two frames of the same direction. */
void test_decide_ambiguous_exchange_yields_master_then_slave(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - ambiguous request + echo → master then slave");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_IDLE);
    in.crc_valid = true;
    in.dir       = DIRECTION_UNKNOWN;

    sniff_decision_t req = sniffer_decide(&in);
    TEST_ASSERT_TRUE(req.emit);
    TEST_ASSERT_TRUE(req.is_master);
    TEST_ASSERT_EQUAL(SNIFF_RES_WAIT, req.new_state);

    /* The echo arrives in the state the request put the port into, and carries the same
     * address and function code as the frame the request latched — which is what tells
     * RES_WAIT it may be the reply at all. */
    in.state           = req.new_state;
    in.synchronized    = true;
    in.last_was_master = req.is_master;
    in.matches_pending = true;

    sniff_decision_t echo = sniffer_decide(&in);
    TEST_ASSERT_TRUE(echo.emit);
    TEST_ASSERT_FALSE_MESSAGE(echo.is_master, "the echo is the awaited response");
    TEST_ASSERT_TRUE(echo.crc_valid);
    TEST_ASSERT_EQUAL_MESSAGE(SNIFF_IDLE, echo.new_state, "the transaction is complete");
    TEST_ASSERT_TRUE(echo.stop_timer);
    TEST_ASSERT_FALSE(echo.latch_request);
}

/* --- SNIFF_RES_WAIT --- */

void test_decide_res_wait_stops_timer_except_for_a_broadcast(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "Test sniffer_decide - every RES_WAIT path stops the response timer, except a broadcast");
    LOG_MESSAGE();

    /* Short frame, FM frame, new request, and plain response — all four conclude the
     * wait for the pending request, so all four must disarm the timer. */
    sniff_input_t in = decide_in(SNIFF_RES_WAIT);

    in.short_frame = true;
    TEST_ASSERT_TRUE(sniffer_decide(&in).stop_timer);

    in.short_frame = false;
    in.is_fm = true;
    TEST_ASSERT_TRUE(sniffer_decide(&in).stop_timer);

    in.is_fm = false;
    in.dir = DIRECTION_REQUEST;
    TEST_ASSERT_TRUE(sniffer_decide(&in).stop_timer);

    in.dir = DIRECTION_RESPONSE;
    TEST_ASSERT_TRUE(sniffer_decide(&in).stop_timer);

    /* A broadcast is the one path that concludes nothing: nobody answers address 0x00, so
     * there is nothing to latch, and the request latched BEFORE it is still pending and
     * still owed a reply. Its timer has to be left running to its own deadline — the port
     * comes out of the broadcast waiting for exactly the reply it was waiting for. */
    in.dir       = DIRECTION_UNKNOWN;
    in.crc_valid = true;
    in.broadcast = true;
    sniff_decision_t bc = sniffer_decide(&in);
    TEST_ASSERT_FALSE_MESSAGE(bc.stop_timer,
        "a broadcast must not disarm the pending request's timer");
    TEST_ASSERT_FALSE_MESSAGE(bc.start_timer, "and must not arm a timer of its own");
    TEST_ASSERT_FALSE_MESSAGE(bc.latch_request, "nor become the pending request");
    TEST_ASSERT_TRUE_MESSAGE(bc.emit, "the broadcast is still shown");
    TEST_ASSERT_TRUE_MESSAGE(bc.is_master, "as the master frame it is");
    TEST_ASSERT_TRUE(bc.crc_valid);
    TEST_ASSERT_EQUAL_MESSAGE(SNIFF_RES_WAIT, bc.new_state,
        "the port keeps waiting for the reply it was already waiting for");
}

void test_decide_res_wait_short_frame_uses_raw_bytes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - RES_WAIT + arbitration-only → slave packet from raw bytes");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_RES_WAIT);
    in.short_frame = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE(d.is_master);
    TEST_ASSERT_FALSE(d.crc_valid);
    TEST_ASSERT_TRUE_MESSAGE(d.from_raw, "the arbitration bytes themselves are the payload here");
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
}

void test_decide_res_wait_fm_resyncs_to_idle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - RES_WAIT + FM frame → emit standalone, resync to IDLE");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_RES_WAIT);
    in.is_fm           = true;
    in.fm_slave_subcmd = true;
    in.crc_valid       = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE(d.is_master);
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
    TEST_ASSERT_FALSE(d.latch_request);
    TEST_ASSERT_FALSE(d.start_timer);
}

void test_decide_res_wait_second_master_relatches(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - RES_WAIT + second request → re-latch, restart timer, keep waiting");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_RES_WAIT);
    in.crc_valid = true;
    in.dir       = DIRECTION_REQUEST;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE(d.is_master);
    TEST_ASSERT_TRUE(d.latch_request);
    TEST_ASSERT_EQUAL_MESSAGE(SNIFF_RES_WAIT, d.new_state, "still waiting — now for the second request's response");
    TEST_ASSERT_TRUE_MESSAGE(d.stop_timer,  "the old timeout must be disarmed");
    TEST_ASSERT_TRUE_MESSAGE(d.start_timer, "and rearmed for the new request");
}

void test_decide_res_wait_response_completes_transaction(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - RES_WAIT + response → emit slave, back to IDLE");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_RES_WAIT);
    in.crc_valid = true;
    in.dir       = DIRECTION_RESPONSE;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE(d.is_master);
    TEST_ASSERT_TRUE(d.crc_valid);
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
    TEST_ASSERT_TRUE(d.stop_timer);
    TEST_ASSERT_FALSE(d.start_timer);
    TEST_ASSERT_FALSE(d.latch_request);
}

void test_decide_res_wait_unknown_treated_as_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - RES_WAIT + ambiguous → treated as the awaited response");
    LOG_MESSAGE();

    /* An ambiguous frame in RES_WAIT is the awaited response when the context supports
     * it: we asked at this address with this function code, and something of an
     * unclassifiable shape came back carrying both. The CRC verdict is carried through. */
    sniff_input_t in = decide_in(SNIFF_RES_WAIT);
    in.crc_valid       = true;
    in.dir             = DIRECTION_UNKNOWN;
    in.matches_pending = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE(d.is_master);
    TEST_ASSERT_TRUE(d.crc_valid);
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
    TEST_ASSERT_FALSE(d.latch_request);

    /* A corrupt frame lands here too, matching or not: its address and function code
     * are not evidence of anything, so they are not consulted. In this state the
     * likeliest thing a corrupt frame was is the reply we were waiting for. */
    in.crc_valid       = false;
    in.matches_pending = false;

    d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_FALSE_MESSAGE(d.is_master, "a corrupt frame in RES_WAIT is still the presumed reply");
    TEST_ASSERT_FALSE_MESSAGE(d.crc_valid, "the frame's real CRC verdict must be carried through");
    TEST_ASSERT_EQUAL(SNIFF_IDLE, d.new_state);
}

/* A Modbus RTU reply always carries the address the request was sent to and echoes its
 * function code. An ambiguous frame in RES_WAIT that carries neither therefore cannot be
 * the reply, whatever its shape — it is somebody's next request, and calling it a slave
 * response is how a run of unanswered writes used to come out alternating master/slave.
 * This is the only thing that makes the rule hold inside the 200 ms response window and
 * inside a gap-less buffer, where resp_timer_cb() cannot run between frames at all. */
void test_decide_res_wait_unknown_not_matching_pending_is_master(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - RES_WAIT + ambiguous addressed elsewhere → new master request");
    LOG_MESSAGE();

    sniff_input_t in = decide_in(SNIFF_RES_WAIT);
    in.crc_valid       = true;
    in.dir             = DIRECTION_UNKNOWN;
    in.matches_pending = false;
    in.synchronized    = true;
    in.last_was_master = true;

    sniff_decision_t d = sniffer_decide(&in);

    TEST_ASSERT_TRUE(d.emit);
    TEST_ASSERT_TRUE_MESSAGE(d.is_master, "a reply carries the address it was asked at — this one does not");
    TEST_ASSERT_TRUE(d.crc_valid);
    TEST_ASSERT_TRUE_MESSAGE(d.latch_request, "it becomes the new pending request");
    TEST_ASSERT_EQUAL_MESSAGE(SNIFF_RES_WAIT, d.new_state, "still waiting — now for this request's response");
    TEST_ASSERT_TRUE_MESSAGE(d.stop_timer,  "the old timeout must be disarmed");
    TEST_ASSERT_TRUE_MESSAGE(d.start_timer, "and rearmed for the new request");
}

/* Emitting is what resynchronises the port: a decision that emits nothing must never
 * be able to move the direction tracking. Guards the caller's
 * "if (d.emit) { synchronized = true; last_was_master = d.is_master; }" contract. */
void test_decide_non_emitting_decisions_are_inert(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test sniffer_decide - dropped frames change nothing but the state field");
    LOG_MESSAGE();

    /* A corrupt frame on a port that has never seen a packet with a known direction is
     * the only frame the state machine still drops: there is nothing to infer from. */
    sniff_input_t drop_unsynced = decide_in(SNIFF_IDLE);
    drop_unsynced.crc_valid    = false;
    drop_unsynced.synchronized = false;

    sniff_decision_t d = sniffer_decide(&drop_unsynced);

    TEST_ASSERT_FALSE(d.emit);
    TEST_ASSERT_FALSE(d.latch_request);
    TEST_ASSERT_FALSE(d.start_timer);
    TEST_ASSERT_FALSE(d.stop_timer);
    TEST_ASSERT_EQUAL_MESSAGE(SNIFF_IDLE, d.new_state, "a dropped frame must not change the framing state");
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
    RUN_TEST(test_bytes_to_hex_no_overflow_at_tight_boundary);
    RUN_TEST(test_bytes_to_hex_all_byte_values_match_printf);

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
    RUN_TEST(test_crc_check_valid_4byte_fc07);

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

    /* format_packet_json tests */
    RUN_TEST(test_json_packet_type_field);
    RUN_TEST(test_json_packet_sender_master);
    RUN_TEST(test_json_packet_sender_slave);
    RUN_TEST(test_json_packet_crc_valid_true);
    RUN_TEST(test_json_packet_crc_valid_false);
    RUN_TEST(test_json_packet_raw_uppercase_hex);
    RUN_TEST(test_json_packet_raw_length_matches_size);
    RUN_TEST(test_json_packet_id_increments);
    RUN_TEST(test_json_packet_max_length_fits_in_buffer);

    /* classify_direction tests — FC02 */
    RUN_TEST(test_classify_direction_fc02_request);
    RUN_TEST(test_classify_direction_fc02_response);
    RUN_TEST(test_classify_direction_fc02_ambiguous);
    RUN_TEST(test_classify_direction_fc04_odd_bytecount_unknown);

    /* classify_direction tests — FC07 */
    RUN_TEST(test_classify_direction_fc07_request);
    RUN_TEST(test_classify_direction_fc07_response);
    RUN_TEST(test_classify_direction_fc07_unknown);

    /* classify_direction tests — FC08 */
    RUN_TEST(test_classify_direction_fc08_always_unknown);

    /* classify_direction tests — FC0B */
    RUN_TEST(test_classify_direction_fc0b_request);
    RUN_TEST(test_classify_direction_fc0b_response);
    RUN_TEST(test_classify_direction_fc0b_unknown);

    /* classify_direction tests — FC0F */
    RUN_TEST(test_classify_direction_fc0f_response);
    RUN_TEST(test_classify_direction_fc0f_request);
    RUN_TEST(test_classify_direction_fc0f_unknown);

    /* classify_direction tests — FC10 */
    RUN_TEST(test_classify_direction_fc10_response);
    RUN_TEST(test_classify_direction_fc10_request);
    RUN_TEST(test_classify_direction_fc10_unknown);

    /* classify_direction tests — FC11 */
    RUN_TEST(test_classify_direction_fc11_request);
    RUN_TEST(test_classify_direction_fc11_response);
    RUN_TEST(test_classify_direction_fc11_unknown);

    /* classify_direction tests — default FC */
    RUN_TEST(test_classify_direction_default_unknown);

    /* classify_direction — exception replies */
    RUN_TEST(test_classify_direction_exception_reply);
    RUN_TEST(test_classify_direction_exception_wrong_length_unknown);

    /* sniffer_decide tests — the frame-decision table */
    RUN_TEST(test_decide_idle_fm_master_subcmd);
    RUN_TEST(test_decide_idle_fm_slave_subcmd);
    RUN_TEST(test_decide_idle_crc_error_unsynchronized_drops);
    RUN_TEST(test_decide_idle_crc_error_alternates_direction);
    RUN_TEST(test_decide_idle_broadcast_no_response_wait);
    RUN_TEST(test_decide_idle_orphan_response);
    RUN_TEST(test_decide_idle_request_starts_wait);
    RUN_TEST(test_decide_idle_unknown_direction_unsynchronized_emits_master);
    RUN_TEST(test_decide_idle_unknown_direction_master_even_after_master);
    RUN_TEST(test_decide_ambiguous_exchange_yields_master_then_slave);
    RUN_TEST(test_decide_res_wait_stops_timer_except_for_a_broadcast);
    RUN_TEST(test_decide_res_wait_short_frame_uses_raw_bytes);
    RUN_TEST(test_decide_res_wait_fm_resyncs_to_idle);
    RUN_TEST(test_decide_res_wait_second_master_relatches);
    RUN_TEST(test_decide_res_wait_response_completes_transaction);
    RUN_TEST(test_decide_res_wait_unknown_treated_as_response);
    RUN_TEST(test_decide_res_wait_unknown_not_matching_pending_is_master);
    RUN_TEST(test_decide_non_emitting_decisions_are_inert);

    return UNITY_END();
}
