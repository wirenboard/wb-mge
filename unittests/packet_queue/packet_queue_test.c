#include "unity.h"
#include "console_log.h"

#include "bridge/packet_queue.h"
#include "freertos/queue.h"

#include <stdbool.h>

extern bool malloc_should_fail;

typedef struct {
    size_t packet_len;
    uint8_t* data_buf;
} packet_queue_elem_t;

static packet_queue_handle g_test_handle = NULL;

void setUp(void)
{
    mock_freertos_queue_reset();
    malloc_should_fail = false;
}

void tearDown(void)
{
    if (g_test_handle) {
        packet_queue_delete(g_test_handle);
        g_test_handle = NULL;
    }
}

// Тестируем успешное создание и удаление очереди пакетов
void test_packet_queue_create_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_create and packet_queue_delete - success case");
    LOG_MESSAGE();

    const size_t max_len = 10;
    g_test_handle = packet_queue_create(max_len);

    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_create_call_count, "xQueueCreate should be called once");

    packet_queue_delete(g_test_handle);
    g_test_handle = NULL; // Clear since we manually deleted
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_delete_call_count, "vQueueDelete should be called once");
}

// Тестируем неуспешное создание очереди пакетов
void test_packet_queue_create_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_create - failure case");
    LOG_MESSAGE();

    const size_t max_len = 0;
    packet_queue_handle test_handle = packet_queue_create(max_len);

    TEST_ASSERT_NULL_MESSAGE(test_handle, "Queue handle should be NULL on failure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_create_call_count, "xQueueCreate should be called once");
}

// Тестируем удаление очереди пакетов с NULL дескриптором
void test_packet_queue_delete_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_delete - NULL handle");
    LOG_MESSAGE();

    packet_queue_delete(NULL);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_queue_delete_call_count, "vQueueDelete should not be called for NULL handle");
}

// Тестируем очистку очереди пакетов с валидным дескриптором
void test_packet_queue_clear_valid_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_clear - valid handle with items in queue");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data1[] = {0x01, 0x02, 0x03};
    const uint8_t test_data2[] = {0x04, 0x05, 0x06, 0x07};
    const uint8_t test_data3[] = {0x08, 0x09};

    esp_err_t result1 = packet_queue_push(g_test_handle, test_data1, sizeof(test_data1));
    esp_err_t result2 = packet_queue_push(g_test_handle, test_data2, sizeof(test_data2));
    esp_err_t result3 = packet_queue_push(g_test_handle, test_data3, sizeof(test_data3));

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result1, "First push should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result2, "Second push should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result3, "Third push should succeed");

    UBaseType_t spaces_before = uxQueueSpacesAvailable(g_test_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, spaces_before, "Queue should have 2 spaces left (5 - 3 items)");

    packet_queue_clear(g_test_handle);

    UBaseType_t spaces_after = uxQueueSpacesAvailable(g_test_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(max_len, spaces_after, "Queue should be completely empty after clear");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(3, g_queue_receive_call_count, "xQueueReceive should be called multiple times");
}

// Тестируем очистку очереди пакетов с NULL дескриптором
void test_packet_queue_clear_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_clear - NULL handle");
    LOG_MESSAGE();

    packet_queue_clear(NULL);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_queue_receive_call_count, "xQueueReceive should not be called for NULL handle");
}

// Тестируем успешное добавление пакета в очередь
void test_packet_queue_push_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push - success case");
    LOG_MESSAGE();

    const size_t max_len = 10;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    const size_t test_len = sizeof(test_data);

    esp_err_t result = packet_queue_push(g_test_handle, test_data, test_len);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "Push should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_space_call_count, "uxQueueSpacesAvailable should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_send_call_count, "xQueueSend should be called once");

    UBaseType_t spaces = uxQueueSpacesAvailable(g_test_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(max_len - 1, spaces, "Queue should have one item");
}

// Тестируем добавление пакета в очередь с NULL дескриптором
void test_packet_queue_push_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push - NULL handle");
    LOG_MESSAGE();

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    esp_err_t result = packet_queue_push(NULL, test_data, test_len);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "Push should fail with NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_queue_space_call_count, "uxQueueSpacesAvailable should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_queue_send_call_count, "xQueueSend should not be called");
}

// Тестируем добавление пакета в заполненную очередь
void test_packet_queue_push_no_space(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push - no space in queue");
    LOG_MESSAGE();

    const size_t max_len = 2;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    esp_err_t result1 = packet_queue_push(g_test_handle, test_data, test_len);
    esp_err_t result2 = packet_queue_push(g_test_handle, test_data, test_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result1, "First push should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result2, "Second push should succeed");

    esp_err_t result3 = packet_queue_push(g_test_handle, test_data, test_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result3, "Third push should fail when queue is full");

    UBaseType_t spaces = uxQueueSpacesAvailable(g_test_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, spaces, "Queue should be full");
}

// Тестируем добавление пакета с ошибкой выделения памяти
void test_packet_queue_push_malloc_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push - malloc failure");
    LOG_MESSAGE();

    const size_t max_len = 2;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    malloc_should_fail = true;

    esp_err_t result = packet_queue_push(g_test_handle, test_data, test_len);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "Push should fail when malloc fails");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_space_call_count, "uxQueueSpacesAvailable should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_queue_send_call_count, "xQueueSend should not be called when malloc fails");
}

// Тестируем добавление пакета с ошибкой отправки в очередь
void test_packet_queue_push_queue_send_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push - queue send failure");
    LOG_MESSAGE();

    const size_t max_len = 2;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    g_queue_send_return_value = pdFAIL;

    esp_err_t result = packet_queue_push(g_test_handle, test_data, test_len);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "Push should fail when queue send fails");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_space_call_count, "uxQueueSpacesAvailable should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_send_call_count, "xQueueSend should be called once");
}

// Тестируем успешное извлечение пакета из очереди
void test_packet_queue_pop_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop - success case");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    const size_t test_len = sizeof(test_data);

    esp_err_t push_result = packet_queue_push(g_test_handle, test_data, test_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, push_result, "Push should succeed");

    uint8_t* received_buf = NULL;
    size_t result = packet_queue_pop(g_test_handle, &received_buf, 0);

    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "Should return correct packet length");
    TEST_ASSERT_NOT_NULL_MESSAGE(received_buf, "Buffer pointer should not be NULL");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(test_data, received_buf, test_len, "Received data should match sent data");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_receive_call_count, "xQueueReceive should be called once");

    free(received_buf);
}

// Тестируем извлечение пакета из очереди с NULL дескриптором
void test_packet_queue_pop_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop - NULL handle");
    LOG_MESSAGE();

    uint8_t* received_buf = NULL;

    size_t result = packet_queue_pop(NULL, &received_buf, 0);

    TEST_ASSERT_EQUAL_MESSAGE(0, result, "Should return 0 for NULL handle");
    TEST_ASSERT_NULL_MESSAGE(received_buf, "Buffer pointer should remain NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_queue_receive_call_count, "xQueueReceive should not be called");
}

// Тестируем извлечение пакета из пустой очереди
void test_packet_queue_pop_empty_queue(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop - empty queue");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    uint8_t* received_buf = NULL;

    size_t result = packet_queue_pop(g_test_handle, &received_buf, 0);

    TEST_ASSERT_EQUAL_MESSAGE(0, result, "Should return 0 for empty queue");
    TEST_ASSERT_NULL_MESSAGE(received_buf, "Buffer pointer should remain NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_receive_call_count, "xQueueReceive should be called once");
}

// Тестируем извлечение пакета с NULL указателем на буфер
void test_packet_queue_pop_null_buffer_ptr(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop - NULL buffer pointer");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x11, 0x22, 0x33};
    const size_t test_len = sizeof(test_data);

    esp_err_t push_result = packet_queue_push(g_test_handle, test_data, test_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, push_result, "Push should succeed");

    size_t result = packet_queue_pop(g_test_handle, NULL, 0);

    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "Should return correct packet length even with NULL buf_ptr");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_queue_receive_call_count, "xQueueReceive should be called once");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_packet_queue_create_success);
    RUN_TEST(test_packet_queue_create_failure);
    RUN_TEST(test_packet_queue_delete_null_handle);

    RUN_TEST(test_packet_queue_clear_valid_handle);
    RUN_TEST(test_packet_queue_clear_null_handle);

    RUN_TEST(test_packet_queue_push_success);
    RUN_TEST(test_packet_queue_push_null_handle);
    RUN_TEST(test_packet_queue_push_no_space);
    RUN_TEST(test_packet_queue_push_malloc_fail);
    RUN_TEST(test_packet_queue_push_queue_send_fail);

    RUN_TEST(test_packet_queue_pop_success);
    RUN_TEST(test_packet_queue_pop_null_handle);
    RUN_TEST(test_packet_queue_pop_empty_queue);
    RUN_TEST(test_packet_queue_pop_null_buffer_ptr);

    return UNITY_END();
}
