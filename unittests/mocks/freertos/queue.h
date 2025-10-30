#pragma once

#include "freertos/FreeRTOS.h"

#include <stddef.h>

typedef struct {
    void **items;          // Array of pointers to queue items
    size_t max_items;      // Maximum number of items
    size_t item_size;      // Size of each item
    size_t head;           // Index of first item
    size_t tail;           // Index where next item will be added
    size_t count;          // Current number of items in queue
} *QueueHandle_t;

extern int g_queue_create_call_count;
extern BaseType_t g_queue_create_result;
extern UBaseType_t g_queue_max_len;
extern UBaseType_t g_queue_item_size;

extern int g_queue_delete_call_count;
extern QueueHandle_t g_queue_delete_handle;

extern int g_queue_receive_call_count;
extern TickType_t g_queue_receive_ticks;

extern int g_queue_space_call_count;
extern QueueHandle_t g_queue_spaces_handle;

extern int g_queue_send_call_count;
extern QueueHandle_t g_queue_send_handle;
extern TickType_t g_queue_send_ticks;
extern BaseType_t g_queue_send_return_value;

extern int g_queue_messages_waiting_call_count;
extern QueueHandle_t g_queue_messages_waiting_handle;

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize);
void vQueueDelete(QueueHandle_t xQueue);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait);
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait);
UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue);

void mock_freertos_queue_reset(void);
