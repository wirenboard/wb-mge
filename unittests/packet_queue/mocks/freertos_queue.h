#pragma once

#include "freertos/FreeRTOS.h"

// Mock control variables
extern QueueHandle_t mock_xQueueCreate_return_value;
extern BaseType_t mock_xQueueReceive_return_value;
extern UBaseType_t mock_uxQueueSpacesAvailable_return_value;
extern BaseType_t mock_xQueueSend_return_value;

// Mock call counters
extern int mock_xQueueCreate_call_count;
extern int mock_vQueueDelete_call_count;
extern int mock_xQueueReceive_call_count;
extern int mock_uxQueueSpacesAvailable_call_count;
extern int mock_xQueueSend_call_count;

// Mock parameter capture
extern size_t mock_xQueueCreate_last_queue_length;
extern size_t mock_xQueueCreate_last_item_size;
extern QueueHandle_t mock_vQueueDelete_last_queue;
extern QueueHandle_t mock_xQueueReceive_last_queue;
extern TickType_t mock_xQueueReceive_last_timeout;
extern QueueHandle_t mock_uxQueueSpacesAvailable_last_queue;
extern QueueHandle_t mock_xQueueSend_last_queue;
extern TickType_t mock_xQueueSend_last_timeout;

// Mock functions
QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize);
void vQueueDelete(QueueHandle_t xQueue);
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait);
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue);
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait);

// Mock control functions
void mock_freertos_queue_reset(void);
void mock_xQueueReceive_set_data(void *data, size_t size);
void mock_xQueueReceive_set_return_sequence(BaseType_t *sequence, int length);
