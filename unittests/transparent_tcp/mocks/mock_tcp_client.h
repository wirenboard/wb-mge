#pragma once

#include "tcp_client.h"
#include <stdbool.h>

typedef struct {
    int init_called;
    int send_called;
    int connected_called;
    int deinit_called;
    bool init_should_fail;
} mock_tcp_client_calls_t;

extern mock_tcp_client_calls_t mock_tcp_client_calls;

void mock_tcp_client_reset(void);
