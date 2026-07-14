#include "unity.h"
#include "console_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

/* sniff_packet_t is declared in sniffer.h when __unittest_env__ is defined */
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

/* pdu_direction_t is now exported from sniffer.h under __unittest_env__,
 * so no local typedef is needed here. */

/* Forward declaration for classify_direction exposed via SNIFFER_STATIC */
pdu_direction_t classify_direction(const uint8_t *data, size_t len);

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

    return UNITY_END();
}
