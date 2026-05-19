#pragma once

#include "tcp_server.h"
#include <stdbool.h>

typedef struct {
    int init_called;
    int send_called;
    int connected_called;
    int deinit_called;
    bool init_should_fail;
} mock_tcp_server_calls_t;

extern mock_tcp_server_calls_t mock_tcp_server_calls;

void mock_tcp_server_reset(void);
