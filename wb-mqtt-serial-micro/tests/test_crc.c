/*
 * Unit tests for modbus CRC-16/IBM and frame building.
 * Uses Unity framework (ThrowTheSwitch).
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "modbus_frame.h"

#include <string.h>
#include <stdint.h>

void setUp(void)   {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Known-good test vectors for Modbus CRC-16/IBM                       */
/* ------------------------------------------------------------------ */

/*
 * FC03 request: slave=1, read 1 holding reg at addr 0
 * Frame:  01 03 00 00 00 01 | 84 0A
 * CRC over first 6 bytes = 0x0A84 (little-endian in frame: 84 0A)
 */
void test_crc_fc03_request(void)
{
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = modbus_crc16(frame, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX16(0x0A84, crc);
}

/*
 * FC04 request: slave=131 (0x83), read input reg 4, count 1
 * Frame: 83 04 00 04 00 01 | CRC
 * CRC of these 6 bytes = 0x296E (verified with modpoll / online CRC calculator)
 */
void test_crc_fc04_request(void)
{
    const uint8_t frame[] = {0x83, 0x04, 0x00, 0x04, 0x00, 0x01};
    uint16_t crc = modbus_crc16(frame, sizeof(frame));
    TEST_ASSERT_EQUAL_HEX16(0x296E, crc);
}

/* Empty buffer -> initial value 0xFFFF */
void test_crc_empty_buffer(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, modbus_crc16(NULL, 0));
}

/* Single byte 0x01 */
void test_crc_single_byte(void)
{
    const uint8_t b = 0x01;
    TEST_ASSERT_EQUAL_HEX16(0x807E, modbus_crc16(&b, 1));
}

/* ------------------------------------------------------------------ */
/* modbus_make_frame                                                    */
/* ------------------------------------------------------------------ */

/* Build FC03 request frame and verify CRC appended correctly */
void test_make_frame_fc03(void)
{
    /* PDU (after slave ID): FC=03, addr_hi=00, addr_lo=00, qty_hi=00, qty_lo=01 */
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t out[16];
    int len = modbus_make_frame(0x01, pdu, sizeof(pdu), out, sizeof(out));

    /* Frame: [01][03 00 00 00 01][CRC_lo CRC_hi] = 8 bytes */
    TEST_ASSERT_EQUAL_INT(8, len);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[0]);  /* slave */
    TEST_ASSERT_EQUAL_HEX8(0x03, out[1]);  /* FC    */
    /* Verify CRC: last 2 bytes should be CRC of first 6 */
    uint16_t crc_expected = modbus_crc16(out, 6);
    uint16_t crc_actual   = out[6] | ((uint16_t)out[7] << 8);
    TEST_ASSERT_EQUAL_HEX16(crc_expected, crc_actual);
}

/* Output buffer too small -> returns 0 */
void test_make_frame_buf_too_small(void)
{
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t out[4]; /* needs 8, only 4 available */
    int len = modbus_make_frame(0x01, pdu, sizeof(pdu), out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, len);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc_fc03_request);
    RUN_TEST(test_crc_fc04_request);
    RUN_TEST(test_crc_empty_buffer);
    RUN_TEST(test_crc_single_byte);

    RUN_TEST(test_make_frame_fc03);
    RUN_TEST(test_make_frame_buf_too_small);

    return UNITY_END();
}
