#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

typedef struct tcp_desc_t tcp_desc_t;

typedef void (*tcp_receive_handler_t)(struct tcp_desc_t *desc, int client_sock, uint8_t *, size_t);

/* Optional: invoked by the receiver task when a client connection closes, so a
 * handler can release any per-connection state (e.g. reassembly buffers). */
typedef void (*tcp_close_handler_t)(struct tcp_desc_t *desc, int client_sock);

typedef struct tcp_desc_t {
    int listen_sock;                        // set to -1 in case of client
    int last_client_sock;                   // last client socket that sent data (updated by receiver_task)
    uint32_t remote_ip;
    int port;
    tcp_receive_handler_t receive_handler;
    tcp_close_handler_t close_handler;      // optional, may be NULL
    volatile uint32_t active_connections;   // per-server active connections, only for server mode; volatile uint32_t required by Atomic_Increment/Decrement_u32
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
} tcp_desc_t;
