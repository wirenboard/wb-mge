#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef QueueHandle_t packet_queue_handle;


// Initialize queue with maximum length of max_len packets
// Returns handle of created queue
// Returns NULL on error
packet_queue_handle packet_queue_create(const size_t max_len);

// Delete packet queue
void packet_queue_delete(const packet_queue_handle handle);

// Get number of packets in the queue
size_t packet_queue_count(const packet_queue_handle handle);

// Remove all packets from the queue
void packet_queue_clear(const packet_queue_handle handle);

// Add packet to queue, packet data is copied
// data - packet data, len - packet length
// Returns ESP_OK on success
esp_err_t packet_queue_push(const packet_queue_handle handle, const uint8_t* data, const size_t len);

// Extract packet from queue with maximum wait of timeout_ticks RTOS ticks
// On success returns packet size and sets buf_ptr pointer to packet data
// Returns 0 if queue is empty or timeout occurred
// Buffer must be freed after use by calling free(buf_ptr)
size_t packet_queue_pop(const packet_queue_handle handle, uint8_t** buf_ptr, const TickType_t timeout_ticks);
