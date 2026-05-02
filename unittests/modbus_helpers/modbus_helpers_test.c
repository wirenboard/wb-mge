#include "unity.h"
#include "console_log.h"

#include "bridge/modbus_helpers.h"

#include <esp_err.h>
#include <string.h>

#define MODBUS_EXCEPTION_FLAG                       0x80
#define MODBUS_TCP_PROTOCOL_ID                      0x0000

#define MODBUS_RTU_CRC_BASE                         0xFFFF
#define MODBUS_RTU_CRC16_LEN                        sizeof(uint16_t)
#define MODBUS_RTU_REQUEST_MIN_LEN                  5
#define MODBUS_RTU_RESPONSE_MIN_LEN                 5

#define MODBUS_TCP_REQUEST_MIN_LEN                  8
#define MODBUS_TCP_TRANSACTION_ID                   0x1234

const uint8_t valid_rtu_request[] = { 0x9F, 0x03, 0x00, 0xC8, 0x00, 0x06, 0x58, 0x48 };
#define valid_rtu_request_len sizeof(valid_rtu_request)
const uint8_t valid_rtu_response[] = { 0x9F, 0x03, 0x06, 0x57, 0x42, 0x4D, 0x52, 0x36, 0x43, 0x54, 0x17 };
#define valid_rtu_response_len sizeof(valid_rtu_response)

const uint8_t valid_short_rtu_request[] = { 0xFD, 0x46, 0x01, 0x13, 0x90 };
#define valid_short_rtu_request_len sizeof(valid_short_rtu_request)
const uint8_t valid_short_rtu_response[] = { 0xFD, 0x46, 0x12, 0x52, 0x5D };
#define valid_short_rtu_response_len sizeof(valid_short_rtu_response)

const uint8_t valid_tcp_request[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x9F, 0x03, 0x00, 0xC8, 0x00, 0x06};
#define valid_tcp_request_len sizeof(valid_tcp_request)

void setUp(void)
{

}

void tearDown(void)
{

}

// Тестируем функцию modbus_crc16
void test_modbus_crc16_null_buffer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_crc16 - null buffer");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL_HEX16(MODBUS_RTU_CRC_BASE, modbus_crc16(NULL, 0));
}

void test_modbus_crc16_zero_length_nonnull(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_crc16 - zero length non-null");
    LOG_MESSAGE();

    uint8_t dummy = 0x42;
    TEST_ASSERT_EQUAL_HEX16(MODBUS_RTU_CRC_BASE, modbus_crc16(&dummy, 0));
}

void test_modbus_crc16_known_frame(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_crc16 - known frame");
    LOG_MESSAGE();

    /* Use enum to get a compile-time constant, avoiding VLA warnings */
    enum { buf_len = sizeof(valid_rtu_request) - sizeof(uint16_t) };
    uint8_t buf[buf_len];
    memcpy(buf, valid_rtu_request, buf_len);
    TEST_ASSERT_EQUAL_HEX16(0x4858, modbus_crc16(buf, buf_len));
}

void test_modbus_crc16_single_byte(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_crc16 - single byte");
    LOG_MESSAGE();

    uint8_t b = 0xFF;
    TEST_ASSERT_EQUAL_HEX16(0x00FF, modbus_crc16(&b, 1));
}

// Тестируем функцию modbus_rtu_check_request
void test_modbus_rtu_check_request_null(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_request - null buffer");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_request(NULL, valid_rtu_request_len));
}

void test_modbus_rtu_check_request_short_len_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_request - short length fail");
    LOG_MESSAGE();

    /* Use enum to get a compile-time constant, avoiding VLA warnings */
    enum { short_len = MODBUS_RTU_REQUEST_MIN_LEN - 1 };
    uint8_t buf[short_len];
    memcpy(buf, valid_short_rtu_request, short_len);

    // Make CRC16 valid
    uint16_t* crc = (uint16_t*)&buf[short_len - MODBUS_RTU_CRC16_LEN];
    *crc = modbus_crc16(buf, short_len - MODBUS_RTU_CRC16_LEN);

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_request(buf, short_len));
}

void test_modbus_rtu_check_request_short_len_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_request - short length ok");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, modbus_rtu_check_request(valid_short_rtu_request, valid_short_rtu_request_len));
}

void test_modbus_rtu_check_request_exception_func(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_request - exception function");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_request_len];
    memcpy(buf, valid_rtu_request, valid_rtu_request_len);

    mb_rtu_header_t* header = (mb_rtu_header_t*)buf;
    header->function |= MODBUS_EXCEPTION_FLAG;

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_request(buf, valid_rtu_request_len));
}

void test_modbus_rtu_check_request_crc_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_request - CRC mismatch");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_request_len];
    memcpy(buf, valid_rtu_request, valid_rtu_request_len);

    buf[valid_rtu_request_len - 1] ^= 0xFF;

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_request(buf, valid_rtu_request_len));
}

void test_modbus_rtu_check_request_valid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_request - valid request");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_request_len];
    memcpy(buf, valid_rtu_request, valid_rtu_request_len);

    TEST_ASSERT_EQUAL(ESP_OK, modbus_rtu_check_request(buf, valid_rtu_request_len));
}

// Тестируем функцию modbus_rtu_check_response
void test_modbus_rtu_check_response_null(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - null buffer");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_response(NULL, valid_rtu_response_len, NULL));
}

void test_modbus_rtu_check_response_short_len_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - short length fail");
    LOG_MESSAGE();

    size_t short_len = MODBUS_RTU_RESPONSE_MIN_LEN - 1;
    uint8_t buf_reg[short_len];
    memcpy(buf_reg, valid_short_rtu_response, short_len);

    // Make CRC16 valid
    uint16_t* crc = (uint16_t*)&buf_reg[short_len - MODBUS_RTU_CRC16_LEN];
    *crc = modbus_crc16(buf_reg, short_len - MODBUS_RTU_CRC16_LEN);

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_response(buf_reg, short_len, NULL));
}

void test_modbus_rtu_check_response_short_len_ok(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - short length ok");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, modbus_rtu_check_response(valid_short_rtu_response, valid_short_rtu_response_len, NULL));
}

void test_modbus_rtu_check_response_crc_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - CRC mismatch");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_response_len];
    memcpy(buf, valid_rtu_response, valid_rtu_response_len);
    buf[valid_rtu_response_len - 1] ^= 0xFF;

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_response(buf, valid_rtu_response_len, NULL));
}

void test_modbus_rtu_check_response_slave_id_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - slave ID mismatch");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_response_len];
    memcpy(buf, valid_rtu_response, valid_rtu_response_len);
    mb_rtu_header_t req = { .slave_id = buf[0] + 1, .function = buf[1] };
    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_response(buf, valid_rtu_response_len, &req));
}

void test_modbus_rtu_check_response_func_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - function mismatch");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_response_len];
    memcpy(buf, valid_rtu_response, valid_rtu_response_len);
    mb_rtu_header_t req = { .slave_id = buf[0], .function = buf[1] + 1 };
    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_rtu_check_response(buf, valid_rtu_response_len, &req));
}

void test_modbus_rtu_check_response_valid_normal(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_check_response - valid normal response");
    LOG_MESSAGE();

    uint8_t buf[valid_rtu_response_len];
    memcpy(buf, valid_rtu_response, valid_rtu_response_len);
    mb_rtu_header_t req = { .slave_id = buf[0], .function = buf[1] };
    TEST_ASSERT_EQUAL(ESP_OK, modbus_rtu_check_response(buf, valid_rtu_response_len, &req));
}

// Тестируем функцию modbus_tcp_check_request
void test_modbus_tcp_check_request_null(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_check_request - null buffer");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_tcp_check_request(NULL, valid_tcp_request_len));
}

void test_modbus_tcp_check_request_short_len(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_check_request - short length");
    LOG_MESSAGE();

    /* Use enum to get a compile-time constant, avoiding VLA warnings */
    enum { short_len = MODBUS_TCP_REQUEST_MIN_LEN - 1 };
    uint8_t buf[short_len];
    memcpy(buf, valid_tcp_request, short_len);

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_tcp_check_request(buf, short_len));
}

void test_modbus_tcp_check_request_protocol_pid_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_check_request - PID mismatch");
    LOG_MESSAGE();

    uint8_t buf[valid_tcp_request_len];
    memcpy(buf, valid_tcp_request, valid_tcp_request_len);

    mb_tcp_header_t* header = (mb_tcp_header_t*)buf;
    header->protocol_id = MODBUS_TCP_PROTOCOL_ID + 1;

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_tcp_check_request(buf, valid_tcp_request_len));
}

void test_modbus_tcp_check_request_length_mismatch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_check_request - length mismatch");
    LOG_MESSAGE();

    uint8_t buf[valid_tcp_request_len];
    memcpy(buf, valid_tcp_request, valid_tcp_request_len);

    mb_tcp_header_t* header = (mb_tcp_header_t*)buf;
    header->length = valid_tcp_request_len;

    TEST_ASSERT_EQUAL(ESP_FAIL, modbus_tcp_check_request(buf, valid_tcp_request_len));
}

void test_modbus_tcp_check_request_valid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_check_request - valid request");
    LOG_MESSAGE();

    TEST_ASSERT_EQUAL(ESP_OK, modbus_tcp_check_request(valid_tcp_request, valid_tcp_request_len));
}

// Тестируем функцию modbus_rtu_from_tcp
void test_modbus_rtu_from_tcp_null_args(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_from_tcp - null args");
    LOG_MESSAGE();

    uint8_t out[32];
    const size_t out_len = sizeof(out);
    uint8_t tcp[12];

    TEST_ASSERT_EQUAL_UINT32(0, modbus_rtu_from_tcp(NULL, out, out_len));
    TEST_ASSERT_EQUAL_UINT32(0, modbus_rtu_from_tcp(tcp, NULL, out_len));
}

void test_modbus_rtu_from_tcp_small_out_buf(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_from_tcp - small output buffer");
    LOG_MESSAGE();

    uint8_t out[4];

    TEST_ASSERT_EQUAL_UINT32(0, modbus_rtu_from_tcp(valid_tcp_request, out, sizeof(out)));
}

void test_modbus_rtu_from_tcp_valid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_rtu_from_tcp - valid conversion");
    LOG_MESSAGE();

    uint8_t out[32];
    const size_t rtu_len = modbus_rtu_from_tcp(valid_tcp_request, out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT32(8, rtu_len);
    TEST_ASSERT_EQUAL_HEX8(valid_tcp_request[6], out[0]);
    TEST_ASSERT_EQUAL_HEX8(valid_tcp_request[7], out[1]);
    TEST_ASSERT_EQUAL_HEX8(valid_tcp_request[8], out[2]);
    TEST_ASSERT_EQUAL_HEX8(valid_tcp_request[9], out[3]);
    TEST_ASSERT_EQUAL_HEX8(valid_tcp_request[10], out[4]);
    TEST_ASSERT_EQUAL_HEX8(valid_tcp_request[11], out[5]);
    TEST_ASSERT_EQUAL_HEX8(0x58, out[6]);
    TEST_ASSERT_EQUAL_HEX8(0x48, out[7]);
}

// Тестируем функцию modbus_tcp_from_rtu
void test_modbus_tcp_from_rtu_null_args(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_from_rtu - null args");
    LOG_MESSAGE();

    uint8_t out[32];
    uint8_t rtu[8];

    TEST_ASSERT_EQUAL_UINT32(0, modbus_tcp_from_rtu(MODBUS_TCP_TRANSACTION_ID, NULL, 8, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, modbus_tcp_from_rtu(MODBUS_TCP_TRANSACTION_ID, rtu, 8, NULL, sizeof(out)));
}

void test_modbus_tcp_from_rtu_small_out_buf(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_from_rtu - small output buffer");
    LOG_MESSAGE();

    uint8_t out[4];

    TEST_ASSERT_EQUAL_UINT32(
        0, modbus_tcp_from_rtu(MODBUS_TCP_TRANSACTION_ID, valid_rtu_response, 8, out, sizeof(out))
    );
}

void test_modbus_tcp_from_rtu_valid(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test modbus_tcp_from_rtu - valid conversion");
    LOG_MESSAGE();

    uint8_t out[32];
    const size_t tcp_len = modbus_tcp_from_rtu(
        MODBUS_TCP_TRANSACTION_ID, valid_rtu_response, valid_rtu_response_len, out, sizeof(out)
    );

    TEST_ASSERT_EQUAL_UINT32(15, tcp_len);
    TEST_ASSERT_EQUAL_HEX8(0x12, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x34, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x09, out[5]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[0], out[6]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[1], out[7]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[2], out[8]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[3], out[9]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[4], out[10]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[5], out[11]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[6], out[12]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[7], out[13]);
    TEST_ASSERT_EQUAL_HEX8(valid_rtu_response[8], out[14]);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_modbus_crc16_null_buffer);
    RUN_TEST(test_modbus_crc16_zero_length_nonnull);
    RUN_TEST(test_modbus_crc16_known_frame);
    RUN_TEST(test_modbus_crc16_single_byte);

    RUN_TEST(test_modbus_rtu_check_request_null);
    RUN_TEST(test_modbus_rtu_check_request_short_len_fail);
    RUN_TEST(test_modbus_rtu_check_request_short_len_ok);
    RUN_TEST(test_modbus_rtu_check_request_exception_func);
    RUN_TEST(test_modbus_rtu_check_request_crc_mismatch);
    RUN_TEST(test_modbus_rtu_check_request_valid);

    RUN_TEST(test_modbus_rtu_check_response_null);
    RUN_TEST(test_modbus_rtu_check_response_short_len_fail);
    RUN_TEST(test_modbus_rtu_check_response_short_len_ok);
    RUN_TEST(test_modbus_rtu_check_response_crc_mismatch);
    RUN_TEST(test_modbus_rtu_check_response_slave_id_mismatch);
    RUN_TEST(test_modbus_rtu_check_response_func_mismatch);
    RUN_TEST(test_modbus_rtu_check_response_valid_normal);

    RUN_TEST(test_modbus_tcp_check_request_null);
    RUN_TEST(test_modbus_tcp_check_request_short_len);
    RUN_TEST(test_modbus_tcp_check_request_protocol_pid_mismatch);
    RUN_TEST(test_modbus_tcp_check_request_length_mismatch);
    RUN_TEST(test_modbus_tcp_check_request_valid);

    RUN_TEST(test_modbus_rtu_from_tcp_null_args);
    RUN_TEST(test_modbus_rtu_from_tcp_small_out_buf);
    RUN_TEST(test_modbus_rtu_from_tcp_valid);

    RUN_TEST(test_modbus_tcp_from_rtu_null_args);
    RUN_TEST(test_modbus_tcp_from_rtu_small_out_buf);
    RUN_TEST(test_modbus_tcp_from_rtu_valid);

    return UNITY_END();
}
