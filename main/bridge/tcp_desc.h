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
    uint32_t max_connections;   // 0 = unlimited. Only 1 is supported: the acceptor admits a new client by dropping the single previously-served one (see tcp_server_task).
    // Per-server active connection count (server mode only).
    // Updated via GCC atomic builtins (__atomic_fetch_add/sub, __ATOMIC_SEQ_CST); polled
    // through plain volatile reads in deinit, so the volatile qualifier must stay.
    // WARNING: GCC/C11 atomic builtins rely on the Xtensa s32c1i instruction, which does
    // NOT work on external PSRAM — they compile fine but silently break (esp-idf #4635).
    // Therefore tcp_desc_t must live in internal RAM: never allocate it with MALLOC_CAP_SPIRAM.
    volatile uint32_t active_connections;
    TaskHandle_t task_handle;
    EventGroupHandle_t event_group;
} tcp_desc_t;
