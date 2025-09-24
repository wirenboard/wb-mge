#include "freertos_queue.h"
#include <string.h>

// Mock control variables
QueueHandle_t mock_xQueueCreate_return_value = (QueueHandle_t)0x12345678;
BaseType_t mock_xQueueReceive_return_value = pdPASS;
UBaseType_t mock_uxQueueSpacesAvailable_return_value = 10;
BaseType_t mock_xQueueSend_return_value = pdPASS;

// Support for changing return values after calls
static BaseType_t mock_xQueueReceive_return_sequence[10] = {pdPASS};
static int mock_xQueueReceive_sequence_index = 0;
static int mock_xQueueReceive_sequence_length = 0;

// Mock call counters
int mock_xQueueCreate_call_count = 0;
int mock_vQueueDelete_call_count = 0;
int mock_xQueueReceive_call_count = 0;
int mock_uxQueueSpacesAvailable_call_count = 0;
int mock_xQueueSend_call_count = 0;

// Mock parameter capture
size_t mock_xQueueCreate_last_queue_length = 0;
size_t mock_xQueueCreate_last_item_size = 0;
QueueHandle_t mock_vQueueDelete_last_queue = NULL;
QueueHandle_t mock_xQueueReceive_last_queue = NULL;
TickType_t mock_xQueueReceive_last_timeout = 0;
QueueHandle_t mock_uxQueueSpacesAvailable_last_queue = NULL;
QueueHandle_t mock_xQueueSend_last_queue = NULL;
TickType_t mock_xQueueSend_last_timeout = 0;

// Captured queue receive data
static void *mock_xQueueReceive_data_to_return = NULL;
static size_t mock_xQueueReceive_data_size = 0;

QueueHandle_t xQueueCreate(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize)
{
    mock_xQueueCreate_call_count++;
    mock_xQueueCreate_last_queue_length = uxQueueLength;
    mock_xQueueCreate_last_item_size = uxItemSize;
    return mock_xQueueCreate_return_value;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    mock_vQueueDelete_call_count++;
    mock_vQueueDelete_last_queue = xQueue;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
{
    mock_xQueueReceive_call_count++;
    mock_xQueueReceive_last_queue = xQueue;
    mock_xQueueReceive_last_timeout = xTicksToWait;

    BaseType_t return_value;
    if (mock_xQueueReceive_sequence_length > 0 && mock_xQueueReceive_sequence_index < mock_xQueueReceive_sequence_length) {
        return_value = mock_xQueueReceive_return_sequence[mock_xQueueReceive_sequence_index++];
    } else {
        return_value = mock_xQueueReceive_return_value;
    }

    if (return_value == pdPASS && mock_xQueueReceive_data_to_return && pvBuffer) {
        memcpy(pvBuffer, mock_xQueueReceive_data_to_return, mock_xQueueReceive_data_size);
    }

    return return_value;
}

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    mock_uxQueueSpacesAvailable_call_count++;
    mock_uxQueueSpacesAvailable_last_queue = xQueue;
    return mock_uxQueueSpacesAvailable_return_value;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait)
{
    mock_xQueueSend_call_count++;
    mock_xQueueSend_last_queue = xQueue;
    mock_xQueueSend_last_timeout = xTicksToWait;
    return mock_xQueueSend_return_value;
}

void mock_freertos_queue_reset(void)
{
    mock_xQueueCreate_return_value = (QueueHandle_t)0x12345678;
    mock_xQueueReceive_return_value = pdPASS;
    mock_uxQueueSpacesAvailable_return_value = 10;
    mock_xQueueSend_return_value = pdPASS;

    mock_xQueueCreate_call_count = 0;
    mock_vQueueDelete_call_count = 0;
    mock_xQueueReceive_call_count = 0;
    mock_uxQueueSpacesAvailable_call_count = 0;
    mock_xQueueSend_call_count = 0;

    mock_xQueueCreate_last_queue_length = 0;
    mock_xQueueCreate_last_item_size = 0;
    mock_vQueueDelete_last_queue = NULL;
    mock_xQueueReceive_last_queue = NULL;
    mock_xQueueReceive_last_timeout = 0;
    mock_uxQueueSpacesAvailable_last_queue = NULL;
    mock_xQueueSend_last_queue = NULL;
    mock_xQueueSend_last_timeout = 0;

    mock_xQueueReceive_data_to_return = NULL;
    mock_xQueueReceive_data_size = 0;

    mock_xQueueReceive_sequence_index = 0;
    mock_xQueueReceive_sequence_length = 0;
}

void mock_xQueueReceive_set_data(void *data, size_t size)
{
    mock_xQueueReceive_data_to_return = data;
    mock_xQueueReceive_data_size = size;
}

void mock_xQueueReceive_set_return_sequence(BaseType_t *sequence, int length)
{
    for (int i = 0; i < length && i < 10; i++) {
        mock_xQueueReceive_return_sequence[i] = sequence[i];
    }
    mock_xQueueReceive_sequence_length = length > 10 ? 10 : length;
    mock_xQueueReceive_sequence_index = 0;
}
