#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "bridge/fast_modbus.h"

#include <string.h>

#define MODBUS_MGE_DETECT_FCODE             0x47

#define TRANSACTION_ID                      0x1234
#define PROTOCOL_ID                         0xABCD
#define PROBE_LENGTH                        17
#define UNIT_ID                             0

esp_err_t mock_tcp_send_result = ESP_OK;

bool malloc_should_fail = false;

void setUp(void)
{

}

void tearDown(void)
{

}

// Тестируем успешную отправку ответа на запрос Быстрого Modbus
void test_fast_modbus_send_probe_response_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - success case");
    LOG_MESSAGE();

    mb_tcp_task_ctx_t test_ctx = {0};

    mb_tcp_header_t test_request = {
        .transaction_id = TRANSACTION_ID,
        .protocol_id = PROTOCOL_ID,
        .length = PROBE_LENGTH,
        .unit_id = UNIT_ID,
        .function = MODBUS_MGE_DETECT_FCODE
    };

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(&test_ctx, &test_request);
    TEST_ASSERT_EQUAL(FAST_MODBUS_PROBE_SUCCESS, result);
}

// Тестируем случай, когда запрос не является запросом Быстрого Modbus (другой код функции)
void test_fast_modbus_send_probe_response_not_probe_function(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - not probe function");
    LOG_MESSAGE();

    mb_tcp_task_ctx_t test_ctx = {0};

    mb_tcp_header_t test_request = {
        .transaction_id = TRANSACTION_ID,
        .protocol_id = PROTOCOL_ID,
        .length = PROBE_LENGTH,
        .unit_id = UNIT_ID,
        .function = 0x03
    };

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(&test_ctx, &test_request);
    TEST_ASSERT_EQUAL(FAST_MODBUS_NOT_PROBE, result);
}

// Тестируем случай, когда запрос не является запросом Быстрого Modbus (другой Unit ID)
void test_fast_modbus_send_probe_response_not_probe_unit_id(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - not probe unit ID");
    LOG_MESSAGE();

    mb_tcp_task_ctx_t test_ctx = {0};

    mb_tcp_header_t test_request = {
        .transaction_id = TRANSACTION_ID,
        .protocol_id = PROTOCOL_ID,
        .length = PROBE_LENGTH,
        .unit_id = 0x01,
        .function = MODBUS_MGE_DETECT_FCODE
    };

    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(&test_ctx, &test_request);
    TEST_ASSERT_EQUAL(FAST_MODBUS_NOT_PROBE, result);
}

// Тестируем случай, когда tcp_server_send возвращает ошибку
void test_fast_modbus_send_probe_response_send_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - send fail");
    LOG_MESSAGE();

    mb_tcp_task_ctx_t test_ctx = {0};

    mb_tcp_header_t test_request = {
        .transaction_id = TRANSACTION_ID,
        .protocol_id = PROTOCOL_ID,
        .length = PROBE_LENGTH,
        .unit_id = UNIT_ID,
        .function = MODBUS_MGE_DETECT_FCODE
    };

    mock_tcp_send_result = ESP_FAIL;
    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(&test_ctx, &test_request);
    TEST_ASSERT_EQUAL(FAST_MODBUS_PROBE_SEND_FAIL, result);
    mock_tcp_send_result = ESP_OK;
}

// Тестируем случай, когда malloc возвращает NULL
void test_fast_modbus_send_probe_response_malloc_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_send_probe_response - malloc fail");
    LOG_MESSAGE();

    mb_tcp_task_ctx_t test_ctx = {0};

    mb_tcp_header_t test_request = {
        .transaction_id = TRANSACTION_ID,
        .protocol_id = PROTOCOL_ID,
        .length = PROBE_LENGTH,
        .unit_id = UNIT_ID,
        .function = MODBUS_MGE_DETECT_FCODE
    };

    malloc_should_fail = true;
    enum fast_modbus_probe_result result = fast_modbus_send_probe_response(&test_ctx, &test_request);
    TEST_ASSERT_EQUAL(FAST_MODBUS_PROBE_MALLOC_FAIL, result);
    malloc_should_fail = false;
}

// Тестируем функцию fast_modbus_truncate_ff с буфером без байтов 0xFF
void test_fast_modbus_truncate_ff_no_ff_bytes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - no FF bytes");
    LOG_MESSAGE();

    uint8_t test_data[] = {0x01, 0x03, 0x00, 0x01, 0x84, 0x0A};
    uint8_t *original_ptr = test_data;
    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(ARRAY_SIZE(test_data), result_len);
    TEST_ASSERT_EQUAL_PTR(original_ptr, data_ptr);
    TEST_ASSERT_EQUAL(0x01, *data_ptr);
}

// Тестируем функцию fast_modbus_truncate_ff с ведущими байтами 0xFF
void test_fast_modbus_truncate_ff_with_leading_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - with leading FF");
    LOG_MESSAGE();

    uint8_t test_data[] = {0xFF, 0xFF, 0xFF, 0x01, 0x03, 0x00, 0x01, 0x84, 0x0A};
    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(6, result_len);
    TEST_ASSERT_EQUAL_PTR(&test_data[3], data_ptr);
    TEST_ASSERT_EQUAL(0x01, *data_ptr);
}

// Тестируем функцию fast_modbus_truncate_ff, где все байты 0xFF
void test_fast_modbus_truncate_ff_all_ff_bytes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - all FF bytes");
    LOG_MESSAGE();

    uint8_t test_data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(0, result_len);
    TEST_ASSERT_EQUAL_PTR(&test_data[5], data_ptr);
}

// Тестируем функцию fast_modbus_truncate_ff с пустым буфером
void test_fast_modbus_truncate_ff_empty_buffer(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - empty buffer");
    LOG_MESSAGE();

    uint8_t test_data[] = {0x01};
    uint8_t *data_ptr = test_data;
    size_t len = 0;

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(0, result_len);
    TEST_ASSERT_EQUAL_PTR(test_data, data_ptr);
}

// Тестируем функцию fast_modbus_truncate_ff со смешанным паттерном 0xFF
void test_fast_modbus_truncate_ff_mixed_pattern(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - mixed pattern");
    LOG_MESSAGE();

    uint8_t test_data[] = {0xFF, 0xFF, 0x01, 0xFF, 0x03};
    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(3, result_len);
    TEST_ASSERT_EQUAL_PTR(&test_data[2], data_ptr);
    TEST_ASSERT_EQUAL(0x01, *data_ptr);
}

// Тестируем функцию fast_modbus_truncate_ff с реальным примером Modbus RTU
void test_fast_modbus_truncate_ff_real_modbus_example(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - real Modbus example");
    LOG_MESSAGE();

    uint8_t test_data[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFD, 0x46, 0x03, 0x00, 0x01, 0xEB, 0x37, 0x0C, 0xCE, 0xDC
    };
    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(10, result_len);
    TEST_ASSERT_EQUAL_PTR(&test_data[9], data_ptr);
    TEST_ASSERT_EQUAL(0xFD, *data_ptr);
    TEST_ASSERT_EQUAL(0x46, *(data_ptr + 1));
}

// Тестируем функцию fast_modbus_truncate_ff с буфером, содержащим только один байт 0xFF
void test_fast_modbus_truncate_ff_single_ff_byte(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - single FF byte");
    LOG_MESSAGE();

    uint8_t test_data[] = {0xFF};
    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(0, result_len);
    TEST_ASSERT_EQUAL_PTR(&test_data[1], data_ptr);
}

// Тестируем функцию fast_modbus_truncate_ff с буфером, содержащим 15 байт 0xFF
void test_fast_modbus_truncate_ff_many_leading_ff(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test fast_modbus_truncate_ff - many leading FF");
    LOG_MESSAGE();

    uint8_t test_data[20];
    memset(test_data, 0xFF, 15);
    test_data[15] = 0x01;
    test_data[16] = 0x03;
    test_data[17] = 0x00;
    test_data[18] = 0x01;
    test_data[19] = 0x84;

    uint8_t *data_ptr = test_data;
    size_t len = ARRAY_SIZE(test_data);

    size_t result_len = fast_modbus_truncate_ff(&data_ptr, len);

    TEST_ASSERT_EQUAL(5, result_len);
    TEST_ASSERT_EQUAL_PTR(&test_data[15], data_ptr);
    TEST_ASSERT_EQUAL(0x01, *data_ptr);
}

int main(void)
{
    UNITY_BEGIN();

    // Тестируем fast_modbus_send_probe_response
    RUN_TEST(test_fast_modbus_send_probe_response_success);
    RUN_TEST(test_fast_modbus_send_probe_response_not_probe_function);
    RUN_TEST(test_fast_modbus_send_probe_response_not_probe_unit_id);
    RUN_TEST(test_fast_modbus_send_probe_response_send_fail);
    RUN_TEST(test_fast_modbus_send_probe_response_malloc_fail);

    // Тестируем fast_modbus_truncate_ff
    RUN_TEST(test_fast_modbus_truncate_ff_no_ff_bytes);
    RUN_TEST(test_fast_modbus_truncate_ff_with_leading_ff);
    RUN_TEST(test_fast_modbus_truncate_ff_all_ff_bytes);
    RUN_TEST(test_fast_modbus_truncate_ff_empty_buffer);
    RUN_TEST(test_fast_modbus_truncate_ff_single_ff_byte);
    RUN_TEST(test_fast_modbus_truncate_ff_mixed_pattern);
    RUN_TEST(test_fast_modbus_truncate_ff_real_modbus_example);
    RUN_TEST(test_fast_modbus_truncate_ff_many_leading_ff);

    return UNITY_END();
}
