#define malloc test_malloc

#include "queue.h"

#include <stdlib.h>
#include <string.h>



int g_queue_create_call_count = 0;
int g_queue_delete_call_count = 0;
int g_queue_receive_call_count = 0;
int g_queue_space_call_count = 0;
int g_queue_send_call_count = 0;
BaseType_t g_queue_send_return_value = pdPASS;

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize)
{
    g_queue_create_call_count++;

    if (uxQueueLength == 0) {
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
    g_queue_delete_call_count++;

    if (!xQueue) {
        return;
    }

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
    (void)xTicksToWait;
    g_queue_receive_call_count++;

    if (!xQueue || !pvBuffer) {
        return pdFAIL;
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
    g_queue_space_call_count++;

    if (!xQueue) {
        return 0;
    }

    return (UBaseType_t)(xQueue->max_items - xQueue->count);
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    g_queue_send_call_count++;

    if (g_queue_send_return_value != pdPASS) {
        return g_queue_send_return_value;
    }

    if (!xQueue || !pvItemToQueue) {
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

void mock_freertos_queue_reset(void)
{
    g_queue_create_call_count = 0;
    g_queue_delete_call_count = 0;
    g_queue_receive_call_count = 0;
    g_queue_space_call_count = 0;
    g_queue_send_call_count = 0;
    g_queue_send_return_value = pdPASS;
}
