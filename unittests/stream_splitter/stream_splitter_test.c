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

    return UNITY_END();
}
