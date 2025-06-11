#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct tcp_desc_t tcp_desc_t;

typedef void (*tcp_receive_handler_t)(struct tcp_desc_t *desc, uint8_t *, size_t);

typedef struct tcp_desc_t {
    int listen_sock; // set to -1 in case of client
    int client_sock;
    int port;
    tcp_receive_handler_t receive_handler;
} tcp_desc_t;
