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
    // Socket a consumer should send unsolicited data to. In server mode the ACCEPTOR
    // registers it the moment a connection is admitted — before it publishes the
    // connection via active_connections — so a client that has not sent anything yet is
    // already reachable; the receiver task re-asserts it on every received packet. Set to
    // -1 ("no client") by tcp_server_init() and rolled back to -1 if the receiver task
    // fails to spawn (only when the field still points at that socket); an optional
    // close_handler may clear it back to -1 on disconnect.
    //
    // Well-defined ONLY on a capped single-client server (max_connections == 1), where it
    // is simply "the one admitted client". On an uncapped multi-client server
    // (max_connections == 0) the field is raced between the receivers AND the acceptor,
    // which re-registers on every admit: a newly connected silent client overwrites the
    // socket of a client that is actively sending. It therefore identifies neither the
    // last sender nor a stable peer, and no uncapped consumer reads it today
    // (modbus_tcp/cache reply via the client_sock passed to their receive handler).
    //
    // In client mode the field is owned by tcp_client.c and holds our outgoing socket.
    int last_client_sock;
    uint32_t remote_ip;
    int port;
    tcp_receive_handler_t receive_handler;
    tcp_close_handler_t close_handler;      // optional, may be NULL
    uint32_t max_connections;   // 0 = unlimited. When the cap is reached the acceptor rejects new clients and keeps the ones already being served (see tcp_server_task).
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
