#include "packet_queue.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <esp_log.h>

//------------------------------------------------------------------------------

typedef struct {
    size_t packet_len;
    uint8_t* data_buf;
} packet_queue_elem_t;

//------------------------------------------------------------------------------

static const char* TAG = "packet_queue";

//------------------------------------------------------------------------------

packet_queue_handle packet_queue_create(const size_t max_len)
{
    QueueHandle_t queue = xQueueCreate(max_len, sizeof(packet_queue_elem_t));
    if (!queue) {
        ESP_LOGE(TAG, "Unable to create packet queue");
    }
    return queue;
}

//------------------------------------------------------------------------------

void packet_queue_delete(const packet_queue_handle handle)
{
    if (!handle) {
        return;
    }

    packet_queue_clear(handle);
    vQueueDelete(handle);
}

//------------------------------------------------------------------------------

size_t packet_queue_count(const packet_queue_handle handle)
{
    if (!handle) {
        return 0;
    }

    UBaseType_t count = uxQueueMessagesWaiting(handle);
    return count;
}

//------------------------------------------------------------------------------

void packet_queue_clear(const packet_queue_handle handle)
{
    if (!handle) {
        return;
    }

    packet_queue_elem_t queue_elem = {0};

    while (xQueueReceive(handle, &queue_elem, 0) == pdPASS) {
        free(queue_elem.data_buf);
    }
}

//------------------------------------------------------------------------------

int packet_queue_push(const packet_queue_handle handle, const uint8_t* data, const size_t len)
{
    if (!handle) {
        return -1;
    }

    UBaseType_t space_avail = uxQueueSpacesAvailable(handle);
    if (!space_avail) {
        ESP_LOGW(TAG, "No space in the queue");
        return -1;
    }

    packet_queue_elem_t queue_elem = {0};
    queue_elem.data_buf = malloc(len);
    if (!queue_elem.data_buf) {
        ESP_LOGE(TAG, "Unable to allocate memory for packet data");
        return -1;
    }

    memcpy(queue_elem.data_buf, data, len);
    queue_elem.packet_len = len;

    if (xQueueSend(handle, &queue_elem, 0) != pdPASS) {
        ESP_LOGE(TAG, "Queue overflow");
        free(queue_elem.data_buf);
        return -1;
    }

    return 0;
}

//------------------------------------------------------------------------------

size_t packet_queue_pop(const packet_queue_handle handle, uint8_t** buf_ptr, TickType_t timeout_ticks)
{
    if (!handle) {
        return -1;
    }

    packet_queue_elem_t queue_elem = {0};
    BaseType_t result = xQueueReceive(handle, &queue_elem, timeout_ticks);
    if (result != pdPASS) {
        return 0;
    }

    if (buf_ptr) {
        *buf_ptr = queue_elem.data_buf;
    } else {
        free(queue_elem.data_buf);
    }

    return queue_elem.packet_len;
}

//------------------------------------------------------------------------------
