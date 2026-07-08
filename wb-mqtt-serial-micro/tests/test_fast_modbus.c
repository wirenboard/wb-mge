/*
 * Unit tests for Fast Modbus event frame build/parse (fast_modbus_events.c).
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "fast_modbus_events.h"
#include "modbus_frame.h"

#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

/* Append a valid CRC (low byte first) after an n-byte body; return total len. */
static int with_crc(uint8_t *buf, int n)
{
    uint16_t crc = modbus_crc16(buf, n);
    buf[n]     = (uint8_t)(crc & 0xFF);
    buf[n + 1] = (uint8_t)(crc >> 8);
    return n + 2;
}

void test_build_request(void)
{
    uint8_t out[16];
    int len = fm_build_request(out, sizeof(out), 0, 0x64, 0x0A, 0x01);
    TEST_ASSERT_EQUAL_INT(9, len);
    TEST_ASSERT_EQUAL_HEX8(0xFD, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x46, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x10, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x64, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[6]);
    uint16_t crc = modbus_crc16(out, 7);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc & 0xFF), out[7]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(crc >> 8),   out[8]);
}

void test_build_enable(void)
{
    uint8_t out[64];
    fm_enable_range_t r[2] = {
        { FM_TYPE_DISCRETE, 4,      3, 1 },
        { FM_TYPE_INPUT,    0x01D0, 1, 2 },
    };
    int len = fm_build_enable(out, sizeof(out), 0x0A, r, 2);
    TEST_ASSERT_EQUAL_INT(4 + (4 + 3) + (4 + 1) + 2, len);  /* = 18 */
    TEST_ASSERT_EQUAL_HEX8(0x0A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x46, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x18, out[2]);
    TEST_ASSERT_EQUAL_HEX8((4 + 3) + (4 + 1), out[3]);      /* settings len = 12 */
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_DISCRETE, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x04, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[7]);
    TEST_ASSERT_EQUAL_HEX8(1, out[8]);
    TEST_ASSERT_EQUAL_HEX8(1, out[9]);
    TEST_ASSERT_EQUAL_HEX8(1, out[10]);
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_INPUT, out[11]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[12]);
    TEST_ASSERT_EQUAL_HEX8(0xD0, out[13]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[14]);
    TEST_ASSERT_EQUAL_HEX8(2, out[15]);
}

void test_parse_doc_example(void)
{
    /* Doc body: 05 46 11 01 01 06 | 02 04 01 D0 04 00 | CRC */
    uint8_t buf[32] = {0x05,0x46,0x11,0x01,0x01,0x06,
                       0x02,0x04,0x01,0xD0,0x04,0x00};
    int len = with_crc(buf, 12);
    fm_event_t ev[4];
    uint8_t slave = 0, flag = 0;
    int n = fm_parse_response(buf, len, ev, 4, &slave, &flag);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX8(0x05, slave);
    TEST_ASSERT_EQUAL_HEX8(0x01, flag);
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_INPUT, ev[0].type);
    TEST_ASSERT_EQUAL_HEX16(0x01D0, ev[0].id);
    TEST_ASSERT_EQUAL_INT(2, ev[0].data_len);
    TEST_ASSERT_EQUAL_UINT32(0x0004, fm_event_value(&ev[0]));
}

void test_parse_two_events(void)
{
    /* header: 07 46 11 00 02 0B  | ev1: 02 04 01 00 01 00 | ev2: 01 01 00 05 01 */
    uint8_t buf[40] = {0x07,0x46,0x11,0x00,0x02,0x0B,
                       0x02,0x04,0x01,0x00,0x01,0x00,
                       0x01,0x01,0x00,0x05,0x01};
    int len = with_crc(buf, 17);
    fm_event_t ev[4];
    uint8_t slave = 0, flag = 0xAA;
    int n = fm_parse_response(buf, len, ev, 4, &slave, &flag);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_HEX8(0x00, flag);
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_INPUT, ev[0].type);
    TEST_ASSERT_EQUAL_HEX16(0x0100, ev[0].id);
    TEST_ASSERT_EQUAL_UINT32(1, fm_event_value(&ev[0]));
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_COIL, ev[1].type);
    TEST_ASSERT_EQUAL_HEX16(0x0005, ev[1].id);
    TEST_ASSERT_EQUAL_UINT32(1, fm_event_value(&ev[1]));
}

void test_parse_no_events(void)
{
    uint8_t buf[8] = {0xFD,0x46,0x12};
    int len = with_crc(buf, 3);
    fm_event_t ev[4];
    uint8_t s, f;
    TEST_ASSERT_EQUAL_INT(0, fm_parse_response(buf, len, ev, 4, &s, &f));
}

void test_parse_skips_ff_prefix(void)
{
    uint8_t frame[8] = {0xFD,0x46,0x12};
    int flen = with_crc(frame, 3);          /* 5-byte framed no-events packet */
    uint8_t wire[16] = {0xFF,0xFF,0xFF};
    memcpy(wire + 3, frame, (size_t)flen);
    fm_event_t ev[4];
    uint8_t s, f;
    TEST_ASSERT_EQUAL_INT(0, fm_parse_response(wire, 3 + flen, ev, 4, &s, &f));
}

void test_parse_bad_crc(void)
{
    uint8_t buf[8] = {0xFD,0x46,0x12, 0x00,0x00};  /* deliberately wrong CRC */
    fm_event_t ev[4];
    uint8_t s, f;
    TEST_ASSERT_EQUAL_INT(-1, fm_parse_response(buf, 5, ev, 4, &s, &f));
}

/* extra_len > FM_MAX_EVENT_DATA: payload copy must be capped to 8 bytes, but the
 * parser must still advance by the FULL extra length so the next event is not
 * desynced. */
void test_parse_caps_oversize_extra(void)
{
    /* header count=2 data_len=(4+10)+(4+1)=19; ev1 holding id2 extra=10; ev2 coil id9 */
    uint8_t buf[48] = {0x05,0x46,0x11,0x00,0x02,0x13,
                       0x0A,0x03,0x00,0x02, 1,2,3,4,5,6,7,8,9,10,
                       0x01,0x01,0x00,0x09, 0x01};
    int len = with_crc(buf, 25);
    fm_event_t ev[4];
    uint8_t s = 0, f = 0;
    int n = fm_parse_response(buf, len, ev, 4, &s, &f);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_HOLDING, ev[0].type);
    TEST_ASSERT_EQUAL_HEX16(0x0002, ev[0].id);
    TEST_ASSERT_EQUAL_INT(FM_MAX_EVENT_DATA, ev[0].data_len);  /* capped to 8 */
    /* Second event parsed correctly => p advanced by the full 10-byte payload */
    TEST_ASSERT_EQUAL_HEX8(FM_TYPE_COIL, ev[1].type);
    TEST_ASSERT_EQUAL_HEX16(0x0009, ev[1].id);
    TEST_ASSERT_EQUAL_UINT32(1, fm_event_value(&ev[1]));
}

/* An event whose extra length runs past the declared data area is dropped
 * (loop breaks) rather than reading out of bounds. */
void test_parse_truncated_event(void)
{
    /* data_len=6 but the single event claims extra=5 -> 4+5=9 > 6 -> break */
    uint8_t buf[16] = {0x05,0x46,0x11,0x00,0x01,0x06,
                       0x05,0x03,0x00,0x02,0x00,0x00};
    int len = with_crc(buf, 12);
    fm_event_t ev[4];
    uint8_t s, f;
    TEST_ASSERT_EQUAL_INT(0, fm_parse_response(buf, len, ev, 4, &s, &f));
}

/* max_events caps how many events are written even if more are present. */
void test_parse_respects_max_events(void)
{
    uint8_t buf[40] = {0x07,0x46,0x11,0x00,0x02,0x0B,
                       0x02,0x04,0x01,0x00,0x01,0x00,
                       0x01,0x01,0x00,0x05,0x01};
    int len = with_crc(buf, 17);
    fm_event_t ev[1];
    uint8_t s, f;
    int n = fm_parse_response(buf, len, ev, 1, &s, &f);   /* room for only 1 */
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_HEX16(0x0100, ev[0].id);
}

/* data_len that would extend past the CRC is rejected. */
void test_parse_datalen_exceeds_frame(void)
{
    uint8_t buf[16] = {0x05,0x46,0x11,0x00,0x01,0xFF, 0x00,0x00};
    int len = with_crc(buf, 8);
    fm_event_t ev[4];
    uint8_t s, f;
    TEST_ASSERT_EQUAL_INT(-1, fm_parse_response(buf, len, ev, 4, &s, &f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_request);
    RUN_TEST(test_build_enable);
    RUN_TEST(test_parse_doc_example);
    RUN_TEST(test_parse_two_events);
    RUN_TEST(test_parse_no_events);
    RUN_TEST(test_parse_skips_ff_prefix);
    RUN_TEST(test_parse_bad_crc);
    RUN_TEST(test_parse_caps_oversize_extra);
    RUN_TEST(test_parse_truncated_event);
    RUN_TEST(test_parse_respects_max_events);
    RUN_TEST(test_parse_datalen_exceeds_frame);
    return UNITY_END();
}
