#include "unity.h"
#include "console_log.h"

#include "bridge/packet_queue.h"
#include "freertos/queue.h"
#include "malloc.h"

// In unittest env, packet_queue.c maps free -> test_free, so callers that
// receive pointers back from pop must also use test_free to stay consistent
// with the allocation tracking done by the mock.
#define free test_free

// Must mirror the private packet_queue_elem_t in packet_queue.c so that the
// sizeof() check in test_packet_queue_create_success stays in sync.
typedef struct {
    size_t packet_len;
    uint8_t* data_buf;
    int client_sock;
} packet_queue_elem_t;

static packet_queue_handle g_test_handle = NULL;

void setUp(void)
{
    mock_freertos_queue_reset();
    reset_malloc_tracking();
}

void tearDown(void)
{
    if (g_test_handle) {
        packet_queue_delete(g_test_handle);
        g_test_handle = NULL;
    }
}

// Test successful creation and deletion of a packet queue
void test_packet_queue_create_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_create - success case");
    LOG_MESSAGE();

    const size_t max_len = 10;
    g_test_handle = packet_queue_create(max_len);

    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueCreate_data.called, "xQueueCreate should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(max_len, mock_xQueueCreate_data.max_len, "Queue max length should match requested length");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        sizeof(packet_queue_elem_t), mock_xQueueCreate_data.item_size, "Queue item size should match packet_queue_elem_t size"
    );
}

// Test unsuccessful creation of a packet queue
void test_packet_queue_create_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_create - failure case");
    LOG_MESSAGE();

    const size_t max_len = 10;
    mock_xQueueCreate_data.should_fail = true;
    packet_queue_handle test_handle = packet_queue_create(max_len);

    TEST_ASSERT_NULL_MESSAGE(test_handle, "Queue handle should be NULL on failure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueCreate_data.called, "xQueueCreate should be called once");
}

// Test successful deletion of a packet queue
void test_packet_queue_delete_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_delete - success case");
    LOG_MESSAGE();

    const size_t max_len = 5;
    packet_queue_handle test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(test_handle, "Queue handle should not be NULL");

    const uint8_t test_data1[] = {0x01, 0x02, 0x03};
    const uint8_t test_data2[] = {0x04, 0x05, 0x06, 0x07};

    esp_err_t result1 = packet_queue_push(test_handle, test_data1, sizeof(test_data1));
    esp_err_t result2 = packet_queue_push(test_handle, test_data2, sizeof(test_data2));

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result1, "First push should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result2, "Second push should succeed");

    packet_queue_delete(test_handle);

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, mock_xQueueReceive_data.called, "xQueueReceive should be called to clear items");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_vQueueDelete_data.called, "vQueueDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(test_handle, mock_vQueueDelete_data.handle, "vQueueDelete should be called with correct handle");
}

// Test packet queue deletion with a NULL handle
void test_packet_queue_delete_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_delete - NULL handle");
    LOG_MESSAGE();

    packet_queue_delete(NULL);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vQueueDelete_data.called, "vQueueDelete should not be called for NULL handle");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(NULL, mock_vQueueDelete_data.handle, "vQueueDelete should not be called for NULL handle");
}

// Test packet queue clearing with a valid handle
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

    void* buffer_ptr1 = get_allocated_ptr(0);
    void* buffer_ptr2 = get_allocated_ptr(1);
    void* buffer_ptr3 = get_allocated_ptr(2);

    UBaseType_t spaces_before = uxQueueSpacesAvailable(g_test_handle);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, spaces_before, "Queue should have 2 spaces left (5 - 3 items)");

    packet_queue_clear(g_test_handle);

    UBaseType_t spaces_after = uxQueueSpacesAvailable(g_test_handle);

    TEST_ASSERT_EQUAL_INT_MESSAGE(max_len, spaces_after, "Queue should be completely empty after clear");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, mock_xQueueReceive_data.called, "xQueueReceive should be called 4 times");

    TEST_ASSERT_TRUE_MESSAGE(was_ptr_freed(buffer_ptr1), "First allocated buffer should be freed");
    TEST_ASSERT_TRUE_MESSAGE(was_ptr_freed(buffer_ptr2), "Second allocated buffer should be freed");
    TEST_ASSERT_TRUE_MESSAGE(was_ptr_freed(buffer_ptr3), "Third allocated buffer should be freed");
}

// Test packet queue clearing with a NULL handle
void test_packet_queue_clear_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_clear - NULL handle");
    LOG_MESSAGE();

    packet_queue_clear(NULL);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueReceive_data.called, "xQueueReceive should not be called for NULL handle");
}

// Test getting the number of packets in the queue
void test_packet_queue_count(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_count");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, packet_queue_count(g_test_handle), "Empty queue should have count 0");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_uxQueueMessagesWaiting_data.called, "uxQueueMessagesWaiting should be called once");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};

    packet_queue_push(g_test_handle, test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, packet_queue_count(g_test_handle), "Queue should have 1 item");

    packet_queue_push(g_test_handle, test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, packet_queue_count(g_test_handle), "Queue should have 2 items");

    packet_queue_push(g_test_handle, test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, packet_queue_count(g_test_handle), "Queue should have 3 items");

    uint8_t* received_buf = NULL;
    packet_queue_pop(g_test_handle, &received_buf, 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, packet_queue_count(g_test_handle), "Queue should have 2 items after pop");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, packet_queue_count(NULL), "NULL handle should return 0");
}

// Test successful push of a packet to the queue
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
    TEST_ASSERT_EQUAL_size_t_MESSAGE(test_len, allocated_ptrs[0].size, "malloc should allocate test_len bytes for buffer");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_uxQueueSpacesAvailable_data.called, "uxQueueSpacesAvailable should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueSend_data.called, "xQueueSend should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueSend_data.ticks, "xQueueSend should be called with correct ticks");

    UBaseType_t spaces = uxQueueSpacesAvailable(g_test_handle);

    TEST_ASSERT_EQUAL_INT_MESSAGE(max_len - 1, spaces, "Queue should have one item");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        g_test_handle, mock_uxQueueSpacesAvailable_data.handle, "g_queue_spaces_handle should point to the correct queue"
    );
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        g_test_handle, mock_xQueueSend_data.handle, "g_queue_send_handle should point to the correct queue"
    );
}

// Test pushing a packet to the queue with a NULL handle
void test_packet_queue_push_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push - NULL handle");
    LOG_MESSAGE();

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    esp_err_t result = packet_queue_push(NULL, test_data, test_len);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "Push should fail with NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_uxQueueSpacesAvailable_data.called, "uxQueueSpacesAvailable should not be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueSend_data.called, "xQueueSend should not be called");
}

// Test pushing a packet to a full queue
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, mock_uxQueueSpacesAvailable_data.called, "uxQueueSpacesAvailable should be called 4 times");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, mock_xQueueSend_data.called, "xQueueSend should be called only 2 times");
}

// Test pushing a packet with memory allocation failure
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_uxQueueSpacesAvailable_data.called, "uxQueueSpacesAvailable should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueSend_data.called, "xQueueSend should not be called when malloc fails");
}

// Test pushing a packet with queue send failure
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

    mock_xQueueSend_data.should_fail = true;

    esp_err_t result = packet_queue_push(g_test_handle, test_data, test_len);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "Push should fail when queue send fails");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_uxQueueSpacesAvailable_data.called, "uxQueueSpacesAvailable should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueSend_data.called, "xQueueSend should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueSend_data.ticks, "xQueueSend should be called with correct ticks");
}

// Test successful pop of a packet from the queue
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueReceive_data.called, "xQueueReceive should be called once");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, packet_queue_count(g_test_handle), "Queue should be empty after pop");
}

// Test popping a packet from the queue with a NULL handle
void test_packet_queue_pop_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop - NULL handle");
    LOG_MESSAGE();

    uint8_t* received_buf = NULL;
    size_t result = packet_queue_pop(NULL, &received_buf, 0);

    TEST_ASSERT_EQUAL_MESSAGE(0, result, "Should return 0 for NULL handle");
    TEST_ASSERT_NULL_MESSAGE(received_buf, "Buffer pointer should remain NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueReceive_data.called, "xQueueReceive should not be called");
}

// Test popping a packet from an empty queue
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueReceive_data.called, "xQueueReceive should be called once");
}

// Test popping a packet with a NULL buffer pointer
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

    void* buffer_ptr = get_allocated_ptr(0);
    TEST_ASSERT_NOT_NULL_MESSAGE(buffer_ptr, "Allocated buffer should not be NULL");

    size_t result = packet_queue_pop(g_test_handle, NULL, 0);

    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "Should return correct packet length even with NULL buf_ptr");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueReceive_data.called, "xQueueReceive should be called once");
    TEST_ASSERT_TRUE_MESSAGE(was_ptr_freed(buffer_ptr), "The correct allocated buffer should be freed");
}

// Test popping a packet with various wait timeouts
void test_packet_queue_pop_with_timeout(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop - with timeout");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x11, 0x22, 0x33};
    const size_t test_len = sizeof(test_data);

    esp_err_t push_result = packet_queue_push(g_test_handle, test_data, test_len);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, push_result, "Push should succeed");

    uint8_t* received_buf = NULL;
    TickType_t wait_time = 0;
    size_t result = packet_queue_pop(g_test_handle, &received_buf, wait_time);
    TEST_ASSERT_EQUAL_MESSAGE(wait_time, mock_xQueueReceive_data.ticks, "xQueueReceive should be called with correct wait time");
    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "Should return correct packet length");

    wait_time = 1000;
    result = packet_queue_pop(g_test_handle, &received_buf, wait_time);
    TEST_ASSERT_EQUAL_MESSAGE(wait_time, mock_xQueueReceive_data.ticks, "xQueueReceive should be called with correct wait time");
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "Should return 0 for empty queue");

    wait_time = portMAX_DELAY;
    result = packet_queue_pop(g_test_handle, &received_buf, wait_time);
    TEST_ASSERT_EQUAL_MESSAGE(wait_time, mock_xQueueReceive_data.ticks, "xQueueReceive should be called with correct wait time");
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "Should return 0 for empty queue");
}

// Test successful push of a packet with client socket to the queue
void test_packet_queue_push_with_client_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push_with_client - success case");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);
    const int client_sock = 42;

    esp_err_t result = packet_queue_push_with_client(g_test_handle, test_data, test_len, client_sock);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "push_with_client should succeed");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(test_len, allocated_ptrs[0].size, "malloc should allocate test_len bytes for buffer");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xQueueSend_data.called, "xQueueSend should be called once");
}

// Test push with a NULL queue handle
void test_packet_queue_push_with_client_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push_with_client - NULL handle");
    LOG_MESSAGE();

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    esp_err_t result = packet_queue_push_with_client(NULL, test_data, test_len, 42);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "push_with_client should fail with NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueSend_data.called, "xQueueSend should not be called");
}

// Test push when the queue is full
void test_packet_queue_push_with_client_no_space(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push_with_client - no space in queue");
    LOG_MESSAGE();

    const size_t max_len = 2;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    esp_err_t result1 = packet_queue_push_with_client(g_test_handle, test_data, test_len, 1);
    esp_err_t result2 = packet_queue_push_with_client(g_test_handle, test_data, test_len, 2);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result1, "First push_with_client should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result2, "Second push_with_client should succeed");

    esp_err_t result3 = packet_queue_push_with_client(g_test_handle, test_data, test_len, 3);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result3, "Third push_with_client should fail when queue is full");
}

// Test push with memory allocation failure
void test_packet_queue_push_with_client_malloc_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_push_with_client - malloc failure");
    LOG_MESSAGE();

    const size_t max_len = 2;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    malloc_should_fail = true;

    esp_err_t result = packet_queue_push_with_client(g_test_handle, test_data, test_len, 42);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "push_with_client should fail when malloc fails");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueSend_data.called, "xQueueSend should not be called when malloc fails");
}

// Test successful pop of a packet with client socket
void test_packet_queue_pop_with_client_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop_with_client - success case");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0xAA, 0xBB, 0xCC};
    const size_t test_len = sizeof(test_data);
    const int push_sock = 42;

    esp_err_t push_result = packet_queue_push_with_client(g_test_handle, test_data, test_len, push_sock);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, push_result, "push_with_client should succeed");

    uint8_t *received_buf = NULL;
    int received_sock = -1;
    size_t result = packet_queue_pop_with_client(g_test_handle, &received_buf, 0, &received_sock);

    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "pop_with_client should return correct packet length");
    TEST_ASSERT_NOT_NULL_MESSAGE(received_buf, "Buffer pointer should not be NULL");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(test_data, received_buf, test_len, "Received data should match sent data");
    TEST_ASSERT_EQUAL_INT_MESSAGE(push_sock, received_sock, "Received socket should match pushed socket");

    free(received_buf);
}

// Test pop with a NULL queue handle
void test_packet_queue_pop_with_client_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop_with_client - NULL handle");
    LOG_MESSAGE();

    uint8_t *received_buf = NULL;
    int received_sock = -1;
    size_t result = packet_queue_pop_with_client(NULL, &received_buf, 0, &received_sock);

    TEST_ASSERT_EQUAL_MESSAGE(0, result, "pop_with_client should return 0 for NULL handle");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xQueueReceive_data.called, "xQueueReceive should not be called");
}

// Test pop with a NULL client_sock pointer — function must not crash
void test_packet_queue_pop_with_client_null_client_sock(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop_with_client - NULL client_sock pointer");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x01, 0x02, 0x03};
    const size_t test_len = sizeof(test_data);

    esp_err_t push_result = packet_queue_push_with_client(g_test_handle, test_data, test_len, 99);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, push_result, "push_with_client should succeed");

    uint8_t *received_buf = NULL;
    // Pass NULL as client_sock: function should not crash and still return length
    size_t result = packet_queue_pop_with_client(g_test_handle, &received_buf, 0, NULL);

    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "pop_with_client should return correct packet length even with NULL client_sock");
    TEST_ASSERT_NOT_NULL_MESSAGE(received_buf, "Buffer pointer should not be NULL");

    free(received_buf);
}

// Test pop_with_client with NULL buf_ptr — data should be freed, no crash
void test_packet_queue_pop_with_client_null_buffer_ptr(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop_with_client - NULL buffer pointer frees data");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t test_data[] = {0x11, 0x22, 0x33};
    const size_t test_len = sizeof(test_data);

    esp_err_t push_result = packet_queue_push_with_client(g_test_handle, test_data, test_len, 55);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, push_result, "push_with_client should succeed");

    /* Pass NULL buf_ptr: implementation must free the buffer instead of leaking it */
    int received_sock = -1;
    size_t result = packet_queue_pop_with_client(g_test_handle, NULL, 0, &received_sock);

    TEST_ASSERT_EQUAL_MESSAGE(test_len, result, "pop_with_client should return correct packet length even with NULL buf_ptr");
    TEST_ASSERT_EQUAL_INT_MESSAGE(55, received_sock, "client_sock should still be populated with NULL buf_ptr");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, packet_queue_count(g_test_handle), "Queue should be empty after pop");
}

// Test FIFO ordering preservation for client_sock with two packets
void test_packet_queue_pop_with_client_preserves_sock(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test packet_queue_pop_with_client - FIFO order preserves client_sock");
    LOG_MESSAGE();

    const size_t max_len = 5;
    g_test_handle = packet_queue_create(max_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_test_handle, "Queue handle should not be NULL");

    const uint8_t data1[] = {0x01, 0x02};
    const uint8_t data2[] = {0x03, 0x04, 0x05};
    const int sock1 = 33;
    const int sock2 = 77;

    esp_err_t r1 = packet_queue_push_with_client(g_test_handle, data1, sizeof(data1), sock1);
    esp_err_t r2 = packet_queue_push_with_client(g_test_handle, data2, sizeof(data2), sock2);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, r1, "First push_with_client should succeed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, r2, "Second push_with_client should succeed");

    uint8_t *buf1 = NULL;
    uint8_t *buf2 = NULL;
    int out_sock1 = -1;
    int out_sock2 = -1;

    size_t len1 = packet_queue_pop_with_client(g_test_handle, &buf1, 0, &out_sock1);
    size_t len2 = packet_queue_pop_with_client(g_test_handle, &buf2, 0, &out_sock2);

    TEST_ASSERT_EQUAL_MESSAGE(sizeof(data1), len1, "First pop should return correct length");
    TEST_ASSERT_EQUAL_MESSAGE(sizeof(data2), len2, "Second pop should return correct length");
    TEST_ASSERT_EQUAL_INT_MESSAGE(sock1, out_sock1, "First pop should have sock1");
    TEST_ASSERT_EQUAL_INT_MESSAGE(sock2, out_sock2, "Second pop should have sock2");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data1, buf1, sizeof(data1), "First pop data should match");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(data2, buf2, sizeof(data2), "Second pop data should match");

    free(buf1);
    free(buf2);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_packet_queue_create_success);
    RUN_TEST(test_packet_queue_create_failure);

    RUN_TEST(test_packet_queue_delete_success);
    RUN_TEST(test_packet_queue_delete_null_handle);

    RUN_TEST(test_packet_queue_clear_valid_handle);
    RUN_TEST(test_packet_queue_clear_null_handle);

    RUN_TEST(test_packet_queue_count);

    RUN_TEST(test_packet_queue_push_success);
    RUN_TEST(test_packet_queue_push_null_handle);
    RUN_TEST(test_packet_queue_push_no_space);
    RUN_TEST(test_packet_queue_push_malloc_fail);
    RUN_TEST(test_packet_queue_push_queue_send_fail);

    RUN_TEST(test_packet_queue_pop_success);
    RUN_TEST(test_packet_queue_pop_null_handle);
    RUN_TEST(test_packet_queue_pop_empty_queue);
    RUN_TEST(test_packet_queue_pop_null_buffer_ptr);
    RUN_TEST(test_packet_queue_pop_with_timeout);

    RUN_TEST(test_packet_queue_push_with_client_success);
    RUN_TEST(test_packet_queue_push_with_client_null_handle);
    RUN_TEST(test_packet_queue_push_with_client_no_space);
    RUN_TEST(test_packet_queue_push_with_client_malloc_fail);

    RUN_TEST(test_packet_queue_pop_with_client_success);
    RUN_TEST(test_packet_queue_pop_with_client_null_handle);
    RUN_TEST(test_packet_queue_pop_with_client_null_client_sock);
    RUN_TEST(test_packet_queue_pop_with_client_null_buffer_ptr);
    RUN_TEST(test_packet_queue_pop_with_client_preserves_sock);

    return UNITY_END();
}
