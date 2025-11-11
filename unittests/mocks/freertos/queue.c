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

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize)
{
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, uxQueueLength, "xQueueCreate called with zero uxQueueLength");

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

    return queue;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "vQueueDelete called with NULL xQueue");

    mock_vQueueDelete_data.called++;
    mock_vQueueDelete_data.handle = xQueue;

    for (size_t i = 0; i < xQueue->max_items; i++) {
        if (xQueue->items[i]) {
            free(xQueue->items[i]);
        }
    }

    free(xQueue->items);
    free(xQueue);
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
{
    TEST_ASSERT_NOT_NULL_MESSAGE(xQueue, "xQueueReceive called with NULL xQueue");
    TEST_ASSERT_NOT_NULL_MESSAGE(pvBuffer, "xQueueReceive called with NULL pvBuffer");

    mock_xQueueReceive_data.called++;
    mock_xQueueReceive_data.ticks = xTicksToWait;

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

void mock_freertos_queue_reset(void)
{
    memset(&mock_xQueueCreate_data, 0, sizeof(mock_xQueueCreate_data));
    memset(&mock_vQueueDelete_data, 0, sizeof(mock_vQueueDelete_data));
    memset(&mock_xQueueReceive_data, 0, sizeof(mock_xQueueReceive_data));
    memset(&mock_uxQueueSpacesAvailable_data, 0, sizeof(mock_uxQueueSpacesAvailable_data));
    memset(&mock_xQueueSend_data, 0, sizeof(mock_xQueueSend_data));
    memset(&mock_uxQueueMessagesWaiting_data, 0, sizeof(mock_uxQueueMessagesWaiting_data));
}
