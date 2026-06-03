#include "unity.h"
#include "console_log.h"

#include "bridge/stream_splitter.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

void setUp(void)
{
}

void tearDown(void)
{
}

/* TC-1: single valid FC06 Write Single Register frame — no split needed.
 * Buffer: 83 06 00 72 00 01 F6 33 (8 bytes, CRC OK) */
void test_single_valid_frame_no_split(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-1: single valid FC06 frame, no split needed");
    LOG_MESSAGE();

    static const uint8_t buf[] = { 0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33 };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_size_t(8, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_PTR(buf, frames[0].data);
}

/* TC-2: two FC06 frames glued together (request + echo response).
 * Buffer: 83 06 00 72 00 01 F6 33 | 83 03 00 FA 00 10 7A 15 (16 bytes, errors.csv row 62) */
void test_two_fc06_frames_glued(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-2: two FC06 frames glued (req + echo resp)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33,  /* frame 0: FC06, 8 bytes */
        0x83, 0x03, 0x00, 0xFA, 0x00, 0x10, 0x7A, 0x15   /* frame 1: FC03 req, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_size_t(8, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_size_t(8, frames[1].len);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-3: FC03 response followed by FC04 request (Length table Level 2).
 * Buffer: 83 03 04 00 03 00 1E 28 33 | 83 04 00 03 00 09 DE 2E (17 bytes, errors.csv row 86) */
void test_fc03_response_plus_fc04_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-3: FC03 response + FC04 request (Level 2)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33,  /* frame 0: FC03 resp, 9 bytes */
        0x83, 0x04, 0x00, 0x03, 0x00, 0x09, 0xDE, 0x2E           /* frame 1: FC04 req, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x83, frames[0].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, frames[0].data[1]);
    TEST_ASSERT_EQUAL_size_t(9, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_HEX8(0x83, frames[1].data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frames[1].data[1]);
    TEST_ASSERT_EQUAL_size_t(8, frames[1].len);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-4: FC03 response + FC04 request with context hint (Level 1 assist).
 * Same bytes as TC-3 but with context_slave=0x83 context_fc=0x03.
 * The first frame should be found via Level 1 (is_response=true path). */
void test_fc03_response_plus_fc04_request_with_context(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-4: FC03 resp + FC04 req with context hint (Level 1)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33,  /* frame 0: FC03 resp, 9 bytes */
        0x83, 0x04, 0x00, 0x03, 0x00, 0x09, 0xDE, 0x2E           /* frame 1: FC04 req, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0x83, 0x03, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_size_t(9, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_size_t(8, frames[1].len);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-5: two FC01 frames glued (response 7 bytes + request 8 bytes).
 * Buffer: 83 01 02 08 00 C6 22 | 83 01 13 89 00 01 36 86 (15 bytes, errors.csv row 92) */
void test_two_fc01_frames_glued(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-5: FC01 resp (7b) + FC01 req (8b) glued");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x01, 0x02, 0x08, 0x00, 0xC6, 0x22,              /* frame 0: FC01 resp, 7 bytes */
        0x83, 0x01, 0x13, 0x89, 0x00, 0x01, 0x36, 0x86         /* frame 1: FC01 req, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_size_t(7, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_size_t(8, frames[1].len);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-6: FC04 large response (23 bytes) + FC04 request (8 bytes).
 * Buffer from errors.csv row 77.
 * FC04 resp: byte_count=0x12=18, total=3+18+2=23 bytes.
 * FC04 req:  fixed 8 bytes. */
void test_fc04_large_response_plus_fc04_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-6: FC04 large resp (23b) + FC04 req (8b)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC04 response, byte_count=0x12=18, total 23 bytes */
        0x83, 0x04, 0x12,
        0x10, 0x34, 0x0C, 0x07, 0x07, 0x1B, 0xFF, 0xFE,
        0xFF, 0xFE, 0x01, 0xD2, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x92, 0x66, 0xCA,
        /* frame 1: FC04 request, 8 bytes */
        0x83, 0x04, 0x01, 0x0E, 0x00, 0x0E, 0x0F, 0xD3
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_size_t(23, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_size_t(8, frames[1].len);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-7: context hint fc=0x03 but actual first frame is FC04 response.
 * The splitter should still find the correct split because frame_expected_len
 * uses the actual fc byte from the buffer, not from context.
 * Verifies that a context_fc mismatch does not prevent correct parsing. */
void test_context_hint_fc_mismatch_still_splits_correctly(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-7: context fc mismatch — Level 1 uses buf fc, 2 frames CRC OK");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC04 response, 23 bytes (context says FC03 but buf has FC04) */
        0x83, 0x04, 0x12,
        0x10, 0x34, 0x0C, 0x07, 0x07, 0x1B, 0xFF, 0xFE,
        0xFF, 0xFE, 0x01, 0xD2, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x92, 0x66, 0xCA,
        /* frame 1: FC04 request, 8 bytes */
        0x83, 0x04, 0x01, 0x0E, 0x00, 0x0E, 0x0F, 0xD3
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    /* context_fc=0x03 does not match buf[1]=0x04; splitter must handle this gracefully */
    int n = stream_split(buf, sizeof(buf), 0x83, 0x03, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-8: CRC scan fallback (Level 3).
 * Buffer: 83 01 01 00 79 F0 | 83 01 14 B4 00 07 26 3C (14 bytes, errors.csv row 85)
 * First frame is FC01 response with byte_count=1 → len=3+1+2=6.
 * Second frame is FC01 request → len=8.
 * The first frame cannot be identified by the length table without is_response;
 * the CRC scan (Level 3) must find the boundary. */
void test_crc_scan_fallback_level3(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-8: CRC scan fallback (Level 3) — FC01 resp 6b + FC01 req 8b");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x01, 0x01, 0x00, 0x79, 0xF0,              /* frame 0: FC01 resp, 6 bytes */
        0x83, 0x01, 0x14, 0xB4, 0x00, 0x07, 0x26, 0x3C   /* frame 1: FC01 req, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_size_t(6, frames[0].len);
    TEST_ASSERT_TRUE(frames[0].crc_valid);
    TEST_ASSERT_EQUAL_size_t(8, frames[1].len);
    TEST_ASSERT_TRUE(frames[1].crc_valid);
}

/* TC-9a: too-short buffer (len=3) — must return 1 frame with crc_valid=false
 * (frame_crc_ok requires len >= 4). */
void test_too_short_buffer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-9a: too-short buffer (len=3) — 1 frame, crc_valid=false");
    LOG_MESSAGE();

    static const uint8_t buf[] = { 0x83, 0x06, 0x00 };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_FALSE(frames[0].crc_valid);
}

/* TC-9b: empty buffer (len=0) — must not crash and return >= 0 frames. */
void test_empty_buffer_no_crash(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "TC-9b: empty buffer (len=0) — no crash");
    LOG_MESSAGE();

    static const uint8_t buf[] = { 0x00 }; /* dummy; only 0 bytes will be passed */
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];
    memset(frames, 0, sizeof(frames));

    /* Must not segfault; result of 0 or 1 frame is both acceptable */
    int n = stream_split(buf, 0, 0, 0, frames);

    TEST_ASSERT_TRUE(n >= 0);
}

/* TC-10: FC03 large response (byte_count=32, total 37 bytes) + FC06 request (8 bytes).
 * Buffer from errors.csv row 63 (45 bytes total).
 * FC03 resp: byte_count=0x20=32 → len = 3 + 32 + 2 = 37.
 * FC06 req:  fixed 8 bytes. */
void test_tc10_fc03_large_response_plus_fc06_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-10: FC03 large response (37b, byte_count=32) + FC06 request");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC03 response, byte_count=0x20=32, total 37 bytes */
        /* header(3) + 32 data bytes + CRC(2) = 37 */
        0x83, 0x03, 0x20,
        0x00, 0x34, 0x00, 0x2E, 0x00, 0x33, 0x00, 0x35,
        0x00, 0x2E, 0x00, 0x35, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x41, 0x40,
        /* frame 1: FC06 request, 8 bytes */
        0x83, 0x06, 0x00, 0x5B, 0x00, 0x14, 0xE6, 0x34
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(37, frames[0].len, "frame[0] len must be 37");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
}

/* TC-11: FC03 short response (byte_count=2, total 7 bytes) + FC04 request (8 bytes).
 * Buffer from errors.csv row 76 (15 bytes total).
 * FC03 resp: byte_count=2 → len = 3 + 2 + 2 = 7.
 * FC04 req:  fixed 8 bytes. */
void test_tc11_fc03_short_response_plus_fc04_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-11: FC03 short response (7b, byte_count=2) + FC04 request");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC03 response, byte_count=2, total 7 bytes */
        0x83, 0x03, 0x02, 0x00, 0x1E, 0x40, 0x52,
        /* frame 1: FC04 request, 8 bytes */
        0x83, 0x04, 0x00, 0x03, 0x00, 0x09, 0xDE, 0x2E
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(7, frames[0].len, "frame[0] len must be 7");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
}

/* TC-12: FC04 large response (byte_count=28, total 33 bytes) + FC01 request (8 bytes).
 * Buffer from errors.csv row 91 (41 bytes total).
 * FC04 resp: byte_count=0x1C=28 → len = 3 + 28 + 2 = 33.
 * FC01 req:  fixed 8 bytes.
 * Extra check: frames[0].len + frames[1].len == sizeof(buf) (full byte coverage). */
void test_tc12_fc04_large_response_plus_fc01_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-12: FC04 large response (33b, byte_count=28) + FC01 request, full coverage");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC04 response, byte_count=0x1C=28, total 33 bytes */
        /* header(3) + 28 data bytes + CRC(2) = 33 */
        0x83, 0x04, 0x1C,
        0x00, 0x06, 0x24, 0x66, 0xFF, 0xFE, 0xFF, 0xFE,
        0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE,
        0xFF, 0xFE, 0x03, 0xF5, 0x00, 0x12,
        0x07, 0xFF, 0x00, 0x0A, 0x00, 0x0E, 0x8A, 0xE9,
        /* frame 1: FC01 request, 8 bytes */
        0x83, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x22, 0x2D
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(33, frames[0].len, "frame[0] len must be 33");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
    /* full byte coverage: no bytes left unaccounted */
    TEST_ASSERT_EQUAL_size_t_MESSAGE(sizeof(buf), frames[0].len + frames[1].len,
        "sum of frame lengths must equal buffer size");
}

/* TC-13: three consecutive FC06 frames (8 bytes each, 24 bytes total).
 * Pattern from errors.csv rows 64+65.
 * All three frames are FC06 write-single-register echoes. */
void test_tc13_three_fc06_frames_consecutive(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-13: three consecutive FC06 frames (8b each, 24b total)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC06, 8 bytes */
        0x83, 0x06, 0x00, 0x5B, 0x00, 0x14, 0xE6, 0x34,
        /* frame 1: FC06, 8 bytes */
        0x83, 0x06, 0x00, 0x71, 0x00, 0x00, 0xC7, 0xF3,
        /* frame 2: FC06, 8 bytes */
        0x83, 0x06, 0x00, 0xF5, 0x00, 0x00, 0x87, 0xDA
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, n, "expected 3 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[0].len, "frame[0] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[2].len, "frame[2] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[2].crc_valid, "frame[2] CRC must be valid");
}

/* TC-14: FC01 response (byte_count=1, total 6 bytes) + FC03 request (8 bytes).
 * Buffer from errors.csv row 85 (14 bytes total).
 * FC01 resp: byte_count=1 → len = 3 + 1 + 2 = 6. Boundary found via Level 3 CRC scan.
 * FC03 req:  fixed 8 bytes. */
void test_tc14_fc01_short_response_plus_fc03_request(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-14: FC01 resp (6b, byte_count=1) via Level 3 CRC scan + FC03 req");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC01 response, byte_count=1, total 6 bytes */
        0x83, 0x01, 0x01, 0x00, 0x79, 0xF0,
        /* frame 1: FC03 request, 8 bytes */
        0x83, 0x03, 0x00, 0x61, 0x00, 0x02, 0x8B, 0xF7
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(6, frames[0].len, "frame[0] len must be 6");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
}

/* TC-15: FC04 response (byte_count=18, total 23 bytes) + FC04 request (8 bytes) with context hint.
 * Buffer from errors.csv row 99 (31 bytes total).
 * context_slave=0x83, context_fc=0x04 → Level 1 finds FC04 resp boundary directly.
 * FC04 resp: byte_count=0x12=18 → len = 3 + 18 + 2 = 23.
 * FC04 req:  fixed 8 bytes. */
void test_tc15_fc04_response_plus_fc04_request_with_context(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-15: FC04 resp (23b, byte_count=18) + FC04 req, context_fc=0x04 (Level 1)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC04 response, byte_count=0x12=18, total 23 bytes */
        0x83, 0x04, 0x12,
        0x10, 0x5D, 0x0C, 0x09, 0x07, 0x17, 0xFF, 0xFE,
        0xFF, 0xFE, 0x01, 0xD2, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x92, 0x07, 0x4E,
        /* frame 1: FC04 request, 8 bytes */
        0x83, 0x04, 0x01, 0x0E, 0x00, 0x0E, 0x0F, 0xD3
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0x83, 0x04, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(23, frames[0].len, "frame[0] len must be 23");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
}

/* TC-16: FC03 response (byte_count=4, total 9 bytes) + FC04 request (8 bytes) with context hint.
 * Buffer from errors.csv row 98 (17 bytes total).
 * context_slave=0x83, context_fc=0x03 → Level 1 finds FC03 resp boundary directly.
 * FC03 resp: byte_count=4 → len = 3 + 4 + 2 = 9.
 * FC04 req:  fixed 8 bytes. */
void test_tc16_fc03_response_plus_fc04_request_with_context(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-16: FC03 resp (9b, byte_count=4) + FC04 req, context_fc=0x03 (Level 1)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC03 response, byte_count=4, total 9 bytes */
        0x83, 0x03, 0x04, 0x00, 0x03, 0x00, 0x1E, 0x28, 0x33,
        /* frame 1: FC04 request, 8 bytes */
        0x83, 0x04, 0x00, 0x03, 0x00, 0x09, 0xDE, 0x2E
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0x83, 0x03, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(9, frames[0].len, "frame[0] len must be 9");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
}

/* TC-17: FC06 echo response (8 bytes) + FM Event Config packet (FC=0x46, 19 bytes) glued.
 * Buffer from errors.csv row 66 (27 bytes total).
 * FC06 echo: standard 8-byte frame with valid CRC.
 * FM 0x46 packet: slave=0x83 fc=0x46 len=19, CRC found via Level 3 scan. */
void test_tc17_fc06_echo_plus_fm_event_config(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-17: FC06 echo (8b) + FM Event Config FC=0x46 (19b) glued");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* frame 0: FC06 echo, 8 bytes */
        0x83, 0x06, 0x00, 0xF5, 0x00, 0x00, 0x87, 0xDA,
        /* frame 1: FM Event Config, FC=0x46, 19 bytes */
        0x83, 0x46, 0x18, 0x0D, 0x04, 0x01, 0x18, 0x04,
        0x01, 0x00, 0x00, 0x01, 0x0F, 0x00, 0x00, 0x01,
        0x00, 0xD2, 0x1F
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[0].len, "frame[0] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(19, frames[1].len, "frame[1] len must be 19");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "frame[1] CRC must be valid");
}

/* TC-18: FC10 request (13 bytes) + FC10 response (8 bytes) = 21 bytes.
 * FC10 Write Multiple Registers:
 *   request: slave=0x01, FC=0x10, start=0x0001, count=0x0002, byte_count=4,
 *            data={0x00,0x01,0x00,0x02}, CRC=0xE2 0x62 → 13 bytes total.
 *   response: slave=0x01, FC=0x10, start=0x0001, count=0x0002, CRC=0x10 0x08 → 8 bytes. */
void test_tc18_fc10_request_plus_fc10_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-18: FC10 request (13b) + FC10 response (8b) = 21 bytes");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* FC10 request: 7 header bytes + 4 data bytes + 2 CRC = 13 bytes */
        0x01, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04,
        0x00, 0x01, 0x00, 0x02,
        0xE2, 0x62,
        /* FC10 response: 6 bytes + 2 CRC = 8 bytes */
        0x01, 0x10, 0x00, 0x01, 0x00, 0x02, 0x10, 0x08
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-18: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(13, frames[0].len, "TC-18: frame[0] len must be 13");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-18: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "TC-18: frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "TC-18: frame[1] CRC must be valid");
}

/* TC-19: FC0F request (10 bytes) + FC0F response (8 bytes) = 18 bytes.
 * FC0F Write Multiple Coils:
 *   request: slave=0x01, FC=0x0F, start=0x0000, count=0x0008, byte_count=1,
 *            data={0xFF}, CRC=0xBE 0xD5 → 10 bytes total.
 *   response: slave=0x01, FC=0x0F, start=0x0000, count=0x0008, CRC=0x54 0x0D → 8 bytes. */
void test_tc19_fc0f_request_plus_fc0f_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-19: FC0F request (10b) + FC0F response (8b) = 18 bytes");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        /* FC0F request: 7 header bytes + 1 data byte + 2 CRC = 10 bytes */
        0x01, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x01,
        0xFF,
        0xBE, 0xD5,
        /* FC0F response: 6 bytes + 2 CRC = 8 bytes */
        0x01, 0x0F, 0x00, 0x00, 0x00, 0x08, 0x54, 0x0D
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-19: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(10, frames[0].len, "TC-19: frame[0] len must be 10");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-19: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "TC-19: frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "TC-19: frame[1] CRC must be valid");
}

/* TC-20: STREAM_SPLITTER_MAX_FRAMES limit — 16 consecutive FC06 frames (128 bytes total).
 * Each frame: 83 06 00 72 00 01 F6 33 (8 bytes, valid CRC).
 * The splitter must return n <= STREAM_SPLITTER_MAX_FRAMES without crashing.
 * Note: the defensive tail path emits the last 8 bytes (frame 16) with crc_valid=false
 * because the loop exits at STREAM_SPLITTER_MAX_FRAMES-1 to reserve the tail slot.
 * So only the first n-1 frames are guaranteed to have valid CRC. */
void test_tc20_stream_splitter_max_frames_limit(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-20: 16 FC06 frames (128 bytes) — must not exceed STREAM_SPLITTER_MAX_FRAMES");
    LOG_MESSAGE();

    /* Standard FC06 frame with valid CRC: 83 06 00 72 00 01 F6 33 */
    static const uint8_t fc06_frame[] = {0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33};
    uint8_t buf[STREAM_SPLITTER_MAX_FRAMES * 8];

    for (int i = 0; i < STREAM_SPLITTER_MAX_FRAMES; i++) {
        memcpy(&buf[i * 8], fc06_frame, 8);
    }

    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    /* Must not exceed the maximum and must produce at least one frame */
    TEST_ASSERT_EQUAL_INT_MESSAGE(STREAM_SPLITTER_MAX_FRAMES, n,
        "TC-20: exactly STREAM_SPLITTER_MAX_FRAMES frames expected at boundary");
    /* All frames except the last defensive-tail slot must have valid CRC */
    for (int i = 0; i < n - 1; i++) {
        TEST_ASSERT_TRUE_MESSAGE(frames[i].crc_valid,
            "TC-20: all frames except the last must have valid CRC");
    }
}

/* TC-21: Level-3 CRC scan finds a frame of exactly MODBUS_RTU_MAX_FRAME_LEN (256) bytes.
 * slave=0x01, FC=0x99 (unknown/exception), 252 bytes payload 0xAB, CRC=0x5D 0xFA.
 * Level-2 probes exception length 5; CRC at 5 is wrong; Level-3 must scan to 256. */
void test_tc21_level3_scan_at_max_frame_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-21: Level-3 CRC scan finds frame at MODBUS_RTU_MAX_FRAME_LEN boundary (256 bytes)");
    LOG_MESSAGE();

    /* 256-byte frame: 0x01 0x99 + 252 * 0xAB + CRC(0x5D, 0xFA) */
    static uint8_t buf[256];
    buf[0] = 0x01;  /* slave id */
    buf[1] = 0x99;  /* unknown FC with high bit set (treated as exception by Level-2) */
    memset(&buf[2], 0xAB, 252);  /* payload */
    buf[254] = 0x5D;  /* CRC low byte */
    buf[255] = 0xFA;  /* CRC high byte */

    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, n, "TC-21: expected exactly 1 frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(256, frames[0].len, "TC-21: frame len must be 256");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-21: CRC must be valid");
}

/* TC-22: FC04 large response (251 bytes, byte_count=246) found by Level-2 despite cap.
 * Followed by FC06 request (8 bytes). Total 259 bytes > MODBUS_RTU_MAX_FRAME_LEN.
 * Verifies cap does not block Level-2 detection of near-maximum-size frames. */
void test_tc22_large_frame_found_by_level2_despite_cap(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-22: FC04 large response (251b) + FC06 req (8b) — Level-2 finds 251b, cap irrelevant");
    LOG_MESSAGE();

    /* FC04 response: slave=0x83, fc=0x04, byte_count=246, data=246*0xAA, CRC=0x69 0x55 */
    static uint8_t buf[259];
    buf[0] = 0x83;
    buf[1] = 0x04;
    buf[2] = 0xF6;  /* byte_count = 246 */
    memset(&buf[3], 0xAA, 246);
    buf[249] = 0x69;  /* CRC low */
    buf[250] = 0x55;  /* CRC high */
    /* FC06 request (known valid): 0x83 0x06 0x00 0x72 0x00 0x01 0xF6 0x33 */
    static const uint8_t fc06[] = {0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33};
    memcpy(&buf[251], fc06, 8);

    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-22: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(251, frames[0].len, "TC-22: frame[0] len must be 251");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-22: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "TC-22: frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "TC-22: frame[1] CRC must be valid");
}

/* TC-23: 300-byte buffer of 0xAA garbage — no valid CRC in 4..256, scan capped.
 * The whole buffer is emitted as one broken frame (crc_valid=false).
 * Verifies the cap prevents scanning beyond MODBUS_RTU_MAX_FRAME_LEN. */
void test_tc23_oversized_garbage_emitted_as_broken_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-23: 300-byte 0xAA garbage — scan capped at 256, whole buffer broken frame");
    LOG_MESSAGE();

    static uint8_t buf[300];
    memset(buf, 0xAA, sizeof(buf));

    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, n, "TC-23: expected 1 broken frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(300, frames[0].len, "TC-23: broken frame must cover all 300 bytes");
    TEST_ASSERT_FALSE_MESSAGE(frames[0].crc_valid, "TC-23: broken frame crc_valid must be false");
}

/* TC-24: FC06 request (8b, valid) + 270-byte 0xBB garbage tail.
 * First frame found by Level-2; Level-3 scans tail up to 256, no match,
 * emits 270-byte broken frame. */
void test_tc24_valid_frame_then_garbage_tail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-24: FC06 (8b valid) + 270-byte 0xBB garbage — frame[0] OK, frame[1] broken");
    LOG_MESSAGE();

    static uint8_t buf[278];
    static const uint8_t fc06[] = {0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33};
    memcpy(&buf[0], fc06, 8);
    memset(&buf[8], 0xBB, 270);

    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-24: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[0].len, "TC-24: frame[0] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-24: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(270, frames[1].len, "TC-24: frame[1] (broken tail) len must be 270");
    TEST_ASSERT_FALSE_MESSAGE(frames[1].crc_valid, "TC-24: frame[1] crc_valid must be false");
}

/* TC-25: exactly MODBUS_RTU_MAX_FRAME_LEN+1 = 257-byte buffer of 0xCC garbage.
 * No valid CRC in 4..256 (scan_limit). Whole buffer emitted as broken frame.
 * Regression: if '<' in scan_limit formula is changed to '<=', this test catches it. */
void test_tc25_boundary_257_bytes_garbage(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-25: 257-byte 0xCC garbage (MAX+1 boundary) — scan capped at 256, broken frame");
    LOG_MESSAGE();

    static uint8_t buf[257];
    memset(buf, 0xCC, sizeof(buf));

    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, n, "TC-25: expected 1 broken frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(257, frames[0].len, "TC-25: broken frame must cover all 257 bytes");
    TEST_ASSERT_FALSE_MESSAGE(frames[0].crc_valid, "TC-25: broken frame crc_valid must be false");
}

void test_tc26_fm_frame_exact_fit_boundary(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-26: FM frame (FD/0x46/0x03, 10b) exactly fills buffer — exact-fit boundary");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0xFD, 0x46, 0x03, 0x11,  /* FM header: slave=0xFD, FC=0x46, subcmd=0x03 */
        0x10, 0xA1,              /* CRC16 of bytes[0..3] — valid 6-byte prefix (mutant trap) */
        0xAA, 0xBB,              /* filler */
        0x3E, 0xD3               /* CRC16 of bytes[0..7] — valid full 10-byte frame */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, n, "TC-26: expected exactly 1 frame (FM exact-fit)");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(10, frames[0].len,
        "TC-26: frame[0] len must be 10 (full FM frame), not a Level-3 mis-split");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-26: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf, frames[0].data, "TC-26: frame[0] must start at buf[0]");
}

void test_tc26_exception_response_length_via_length_table(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-26: Modbus exception response (5b, fc|0x80) via Level-2 length table + FC06 req (8b)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x86, 0xE1, 0x22, 0x00,                   /* exception response, 5 bytes */
        0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33  /* FC06 request, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-26: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(5, frames[0].len,
        "TC-26: exception frame length must be 5 (slave+fc|0x80+exc+2 CRC)");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-26: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "TC-26: frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "TC-26: frame[1] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(sizeof(buf), frames[0].len + frames[1].len,
        "TC-26: sum of frame lengths must equal buffer size");
}

void test_tc26_level3_scan_floor_4byte_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-26: Level-3 scan floor — 4-byte frame (2 data + 2 CRC) + FC06 echo (8b)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x99, 0xA0, 0xEA,                          /* minimal 4-byte RTU frame */
        0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33   /* FC06 echo, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-26: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(4, frames[0].len, "TC-26: frame[0] len must be 4 (scan floor)");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-26: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[1].len, "TC-26: frame[1] len must be 8");
    TEST_ASSERT_TRUE_MESSAGE(frames[1].crc_valid, "TC-26: frame[1] CRC must be valid");
}

void test_tc26_empty_buffer_fallback_crc_valid_false(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-26: empty buffer (len=0) — fallback frame crc_valid must be false");
    LOG_MESSAGE();

    static const uint8_t buf[] = { 0x00 };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];
    memset(frames, 0xFF, sizeof(frames));

    int n = stream_split(buf, 0, 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, n, "TC-26: count==0 fallback must return exactly 1 frame");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, frames[0].len, "TC-26: fallback frame len must be 0");
    TEST_ASSERT_FALSE_MESSAGE(frames[0].crc_valid,
        "TC-26: fallback frame crc_valid must be false (frame_crc_ok returns false for len<4)");
}

void test_tc26_level1_context_hint_fires_for_fc10_response(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "TC-26: ambiguous FC10 req/resp — context_fc=0x10 must trigger Level-1 (8b resp)");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0x83, 0x10, 0x00, 0x01, 0x00, 0x02, 0x0E, 0x2A,   /* 8-byte response prefix, CRC OK */
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x11, 0x11, 0x11, 0x67, 0xF7,         /* ...ends with request CRC */
        0x83, 0x06, 0x00, 0x72, 0x00, 0x01, 0xF6, 0x33    /* FC06 echo, 8 bytes */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0x83, 0x10, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, n, "TC-26: expected 2 frames");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[0].len,
        "TC-26: Level-1 must pick the 8-byte FC10 response (mutant picks 23-byte request)");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "TC-26: frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(23, frames[1].len, "TC-26: remainder must be the 23-byte broken tail");
    TEST_ASSERT_FALSE_MESSAGE(frames[1].crc_valid, "TC-26: tail must be a broken frame");
}

/* FM bus-arbitration run must be emitted as its OWN separate frame, between the
 * FM Event Request and the FM No-Events response — not lumped together with the
 * response. The 19-byte blob:
 *   FD 46 10 00 4F 00 00 C9 7D | FF FF FF FF FF | FD 46 12 52 5D
 * must split into exactly THREE frames:
 *   frame[0]: FD 46 10 00 4F 00 00 C9 7D  (len 9, crc_valid=true)  — FM Event Request
 *   frame[1]: FF FF FF FF FF              (len 5, crc_valid=false) — arbitration run
 *   frame[2]: FD 46 12 52 5D              (len 5, crc_valid=true)  — FM No-Events */
void test_fm_arbitration_run_emitted_as_separate_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE,
        "FM arbitration run (FF*5) must be emitted as its own frame between request and response");
    LOG_MESSAGE();

    static const uint8_t buf[] = {
        0xFD,0x46,0x10,0x00,0x4F,0x00,0x00,0xC9,0x7D,  /* FM Event Request, valid CRC */
        0xFF,0xFF,0xFF,0xFF,0xFF,                       /* FM bus arbitration run     */
        0xFD,0x46,0x12,0x52,0x5D                        /* FM No-Events, valid CRC     */
    };
    stream_frame_t frames[STREAM_SPLITTER_MAX_FRAMES];

    int n = stream_split(buf, sizeof(buf), 0, 0, frames);

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, n, "expected 3 frames (request, arbitration, response)");

    TEST_ASSERT_EQUAL_size_t_MESSAGE(9, frames[0].len, "frame[0] len must be 9 (FM Event Request)");
    TEST_ASSERT_TRUE_MESSAGE(frames[0].crc_valid, "frame[0] CRC must be valid");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf, frames[0].data, "frame[0] must start at buf[0]");

    TEST_ASSERT_EQUAL_size_t_MESSAGE(5, frames[1].len, "frame[1] len must be 5 (arbitration run)");
    TEST_ASSERT_FALSE_MESSAGE(frames[1].crc_valid, "frame[1] (arbitration) CRC must be invalid");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf + 9, frames[1].data, "frame[1] must start at buf[9]");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, frames[1].data[0], "frame[1] data[0] must be 0xFF");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, frames[1].data[1], "frame[1] data[1] must be 0xFF");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, frames[1].data[2], "frame[1] data[2] must be 0xFF");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, frames[1].data[3], "frame[1] data[3] must be 0xFF");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFF, frames[1].data[4], "frame[1] data[4] must be 0xFF");

    TEST_ASSERT_EQUAL_size_t_MESSAGE(5, frames[2].len, "frame[2] len must be 5 (FM No-Events)");
    TEST_ASSERT_TRUE_MESSAGE(frames[2].crc_valid, "frame[2] CRC must be valid");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(buf + 14, frames[2].data, "frame[2] must start at buf[14]");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_single_valid_frame_no_split);
    RUN_TEST(test_two_fc06_frames_glued);
    RUN_TEST(test_fc03_response_plus_fc04_request);
    RUN_TEST(test_fc03_response_plus_fc04_request_with_context);
    RUN_TEST(test_two_fc01_frames_glued);
    RUN_TEST(test_fc04_large_response_plus_fc04_request);
    RUN_TEST(test_context_hint_fc_mismatch_still_splits_correctly);
    RUN_TEST(test_crc_scan_fallback_level3);
    RUN_TEST(test_too_short_buffer);
    RUN_TEST(test_empty_buffer_no_crash);
    RUN_TEST(test_tc10_fc03_large_response_plus_fc06_request);
    RUN_TEST(test_tc11_fc03_short_response_plus_fc04_request);
    RUN_TEST(test_tc12_fc04_large_response_plus_fc01_request);
    RUN_TEST(test_tc13_three_fc06_frames_consecutive);
    RUN_TEST(test_tc14_fc01_short_response_plus_fc03_request);
    RUN_TEST(test_tc15_fc04_response_plus_fc04_request_with_context);
    RUN_TEST(test_tc16_fc03_response_plus_fc04_request_with_context);
    RUN_TEST(test_tc17_fc06_echo_plus_fm_event_config);
    RUN_TEST(test_tc18_fc10_request_plus_fc10_response);
    RUN_TEST(test_tc19_fc0f_request_plus_fc0f_response);
    RUN_TEST(test_tc20_stream_splitter_max_frames_limit);
    RUN_TEST(test_tc21_level3_scan_at_max_frame_boundary);
    RUN_TEST(test_tc22_large_frame_found_by_level2_despite_cap);
    RUN_TEST(test_tc23_oversized_garbage_emitted_as_broken_frame);
    RUN_TEST(test_tc24_valid_frame_then_garbage_tail);
    RUN_TEST(test_tc25_boundary_257_bytes_garbage);
    RUN_TEST(test_tc26_fm_frame_exact_fit_boundary);
    RUN_TEST(test_tc26_exception_response_length_via_length_table);
    RUN_TEST(test_tc26_level3_scan_floor_4byte_frame);
    RUN_TEST(test_tc26_empty_buffer_fallback_crc_valid_false);
    RUN_TEST(test_tc26_level1_context_hint_fires_for_fc10_response);
    RUN_TEST(test_fm_arbitration_run_emitted_as_separate_frame);

    return UNITY_END();
}
