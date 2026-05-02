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

    return UNITY_END();
}
