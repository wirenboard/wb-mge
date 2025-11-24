#pragma once

#include "freertos/FreeRTOS.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void **items;          // Array of pointers to queue items
    size_t max_items;      // Maximum number of items
    size_t item_size;      // Size of each item
    size_t head;           // Index of first item
    size_t tail;           // Index where next item will be added
    size_t count;          // Current number of items in queue
} *QueueHandle_t;

typedef struct {
    int called;
    bool should_fail;
    UBaseType_t max_len;
    UBaseType_t item_size;
} mock_xQueueCreate_t;

typedef struct {
    int called;
    QueueHandle_t handle;
} mock_vQueueDelete_t;

typedef struct {
    int called;
    QueueHandle_t handle;
    TickType_t ticks;
    void *pvBuffer;
    size_t buffer_size;
} mock_xQueueReceive_t;

typedef struct {
    int called;
    QueueHandle_t handle;
} mock_uxQueueSpacesAvailable_t;

typedef struct {
    int called;
    QueueHandle_t handle;
    TickType_t ticks;
    bool should_fail;
} mock_xQueueSend_t;

typedef struct {
    int called;
    QueueHandle_t handle;
} mock_uxQueueMessagesWaiting_t;

typedef struct {
    int called;
    QueueHandle_t handle;
} mock_xQueueReset_t;

extern mock_xQueueCreate_t mock_xQueueCreate_data;
extern mock_vQueueDelete_t mock_vQueueDelete_data;
extern mock_xQueueReceive_t mock_xQueueReceive_data;
extern mock_uxQueueSpacesAvailable_t mock_uxQueueSpacesAvailable_data;
extern mock_xQueueSend_t mock_xQueueSend_data;
extern mock_uxQueueMessagesWaiting_t mock_uxQueueMessagesWaiting_data;
extern mock_xQueueReset_t mock_xQueueReset_data;

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize);
void vQueueDelete(QueueHandle_t xQueue);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait);
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait);
UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue);
BaseType_t xQueueReset(QueueHandle_t xQueue);

void mock_freertos_queue_reset(void);
