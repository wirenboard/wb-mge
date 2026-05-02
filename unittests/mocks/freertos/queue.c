#include "unity.h"

#include "queue.h"

#include <stdlib.h>
#include <string.h>

mock_xQueueCreate_t mock_xQueueCreate_data = {0};
mock_vQueueDelete_t mock_vQueueDelete_data = {0};
mock_xQueueReceive_t mock_xQueueReceive_data = {0};
mock_uxQueueSpacesAvailable_t mock_uxQueueSpacesAvailable_data = {0};
mock_xQueueSend_t mock_xQueueSend_data = {0};
mock_uxQueueMessagesWaiting_t mock_uxQueueMessagesWaiting_data = {0};
mock_xQueueReset_t mock_xQueueReset_data = {0};

static QueueHandle_t created_queue = NULL;

static void free_queue(QueueHandle_t queue)
{
    if (queue == NULL) {
        return;
    }

    for (size_t i = 0; i < queue->max_items; i++) {
        if (queue->items[i]) {
            free(queue->items[i]);
        }
    }

    free(queue->items);
    free(queue);
}

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize)
{
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, uxQueueLength, "xQueueCreate called with zero uxQueueLength");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, uxItemSize, "xQueueCreate called with zero uxItemSize");

    mock_xQueueCreate_data.called++;
    mock_xQueueCreate_data.max_len = uxQueueLength;
    mock_xQueueCreate_data.item_size = uxItemSize;

    if (mock_xQueueCreate_data.should_fail) {
        return NULL;
    }

    QueueHandle_t queue = malloc(sizeof(*queue));
    if (!queue) {
        return NULL;
    }

    queue->items = malloc(sizeof(void*) * uxQueueLength);
    if (!queue->items) {
        free(queue);
        return NULL;
    }

    for (size_t i = 0; i < uxQueueLength; i++) {
        queue->items[i] = NULL;
    }

    queue->max_items = uxQueueLength;
    queue->item_size = uxItemSize;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;

    created_queue = queue;

    return queue;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "vQueueDelete called with NULL xQueue");

    mock_vQueueDelete_data.called++;
    mock_vQueueDelete_data.handle = xQueue;

    free_queue(xQueue);

    if (created_queue == xQueue) {
        created_queue = NULL;
    }
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "xQueueReceive called with NULL xQueue");
    TEST_ASSERT_NOT_NULL_MESSAGE(pvBuffer, "xQueueReceive called with NULL pvBuffer");

    mock_xQueueReceive_data.called++;
    mock_xQueueReceive_data.handle = xQueue;
    mock_xQueueReceive_data.ticks = xTicksToWait;

    // If mock data is provided, copy it directly to pvBuffer
    if (mock_xQueueReceive_data.array_len && mock_xQueueReceive_data.buffer_size_arr && mock_xQueueReceive_data.pvBuffer_arr) {
        if (mock_xQueueReceive_data.called <= mock_xQueueReceive_data.array_len) {
            memcpy(
                pvBuffer,
                mock_xQueueReceive_data.pvBuffer_arr[mock_xQueueReceive_data.called - 1],
                mock_xQueueReceive_data.buffer_size_arr[mock_xQueueReceive_data.called - 1]
            );
            return pdPASS;
        } else {
            return pdFAIL;
        }
    }

    if (xQueue->count == 0) {
        return pdFAIL;
    }

    void *item = xQueue->items[xQueue->head];
    memcpy(pvBuffer, item, xQueue->item_size);

    free(item);
    xQueue->items[xQueue->head] = NULL;

    xQueue->head = (xQueue->head + 1) % xQueue->max_items;
    xQueue->count--;

    return pdPASS;
}

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "uxQueueSpacesAvailable called with NULL xQueue");

    mock_uxQueueSpacesAvailable_data.called++;
    mock_uxQueueSpacesAvailable_data.handle = xQueue;

    return (UBaseType_t)(xQueue->max_items - xQueue->count);
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "xQueueSend called with NULL xQueue");
    TEST_ASSERT_NOT_NULL_MESSAGE(pvItemToQueue, "xQueueSend called with NULL pvItemToQueue");

    mock_xQueueSend_data.called++;
    mock_xQueueSend_data.handle = xQueue;
    mock_xQueueSend_data.ticks = xTicksToWait;

    if (mock_xQueueSend_data.should_fail) {
        return pdFAIL;
    }

    if (xQueue->count >= xQueue->max_items) {
        return pdFAIL;
    }

    void *item = malloc(xQueue->item_size);
    if (!item) {
        return pdFAIL;
    }

    memcpy(item, pvItemToQueue, xQueue->item_size);

    xQueue->items[xQueue->tail] = item;
    xQueue->tail = (xQueue->tail + 1) % xQueue->max_items;
    xQueue->count++;

    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "uxQueueMessagesWaiting called with NULL xQueue");

    mock_uxQueueMessagesWaiting_data.called++;
    mock_uxQueueMessagesWaiting_data.handle = xQueue;

    return (UBaseType_t)xQueue->count;
}

BaseType_t xQueueReset(QueueHandle_t xQueue)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "xQueueReset called with NULL xQueue");

    mock_xQueueReset_data.called++;
    mock_xQueueReset_data.handle = xQueue;

    return pdPASS;
}

void mock_freertos_queue_reset(void)
{
    free_queue(created_queue);
    created_queue = NULL;

    memset(&mock_xQueueCreate_data, 0, sizeof(mock_xQueueCreate_data));
    memset(&mock_vQueueDelete_data, 0, sizeof(mock_vQueueDelete_data));
    memset(&mock_xQueueReceive_data, 0, sizeof(mock_xQueueReceive_data));
    memset(&mock_uxQueueSpacesAvailable_data, 0, sizeof(mock_uxQueueSpacesAvailable_data));
    memset(&mock_xQueueSend_data, 0, sizeof(mock_xQueueSend_data));
    memset(&mock_uxQueueMessagesWaiting_data, 0, sizeof(mock_uxQueueMessagesWaiting_data));
    memset(&mock_xQueueReset_data, 0, sizeof(mock_xQueueReset_data));
}

QueueHandle_t mock_get_last_created_queue(void)
{
    return created_queue;
}
