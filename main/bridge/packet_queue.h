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

// Add packet with the identity of the connection it came from to queue, packet data is copied
// data - packet data, len - packet length
// client_sock - originating TCP socket fd
// conn_generation - tcp_desc_conn_generation() of that socket's descriptor, sampled by the
//   PRODUCER (the receive handler, which runs in the connection's own receiver task and so
//   is looking at a connection that is provably still alive). The fd alone is not an
//   identity: a request may sit in the queue while its client disconnects and lwIP hands
//   the fd number to a new connection, and the consumer would then answer a stranger.
//   Sampling the generation on the consumer side instead of carrying it through the queue
//   reintroduces exactly that hole, so the pair must travel WITH the packet.
// Returns ESP_OK on success
esp_err_t packet_queue_push_with_client(const packet_queue_handle handle, const uint8_t* data, const size_t len,
                                        int client_sock, uint32_t conn_generation);

// Extract packet with the identity of the connection it came from
// On success returns packet size, sets buf_ptr pointer to packet data, and writes the
// (client_sock, conn_generation) pair pushed alongside it
// Returns 0 if queue is empty or timeout occurred
// Buffer must be freed after use by calling free(buf_ptr)
// client_sock is set to -1 if not available; conn_generation and client_sock may be NULL
size_t packet_queue_pop_with_client(const packet_queue_handle handle, uint8_t** buf_ptr, const TickType_t timeout_ticks,
                                    int* client_sock, uint32_t* conn_generation);
