#include "unity.h"
#include "console_log.h"

#include "array_size.h"
#include "bridge/fast_modbus.h"

#include <string.h>

void setUp(void)
{

}

void tearDown(void)
{

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
