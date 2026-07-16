#pragma once

#include "tcp_server.h"
#include <stdbool.h>

typedef struct {
    int init_called;
    int send_called;
    int connected_called;
    int deinit_called;
    bool init_should_fail;
    // Observation of tcp_server_set_max_connections (transparent mode caps at 1).
    int set_max_connections_called;
    uint32_t set_max_connections_value;
    // Controllable return value of tcp_server_connected (ESP_OK by default).
    esp_err_t connected_ret;
    // Observation of tcp_server_send (used to verify serial -> TCP relay).
    int send_last_client_sock;
    uint8_t *send_last_data;
    size_t send_last_len;
} mock_tcp_server_calls_t;

extern mock_tcp_server_calls_t mock_tcp_server_calls;

// Handler registered by tcp_server_init (transparent_tcp process_data_from_tcp callback).
extern tcp_receive_handler_t mock_tcp_server_registered_handler;
// Descriptor handed back from tcp_server_init, so a test can drive the captured handler.
tcp_desc_t *mock_tcp_server_get_desc(void);

// Reproduce what the real tcp_server does when it ADMITS a connection: the acceptor
// registers the socket in last_client_sock and then publishes the connection by
// incrementing active_connections (the receiver task re-asserts the same value once it
// is scheduled) — so a client that has connected but not yet sent anything is already
// reachable.  Mirroring that contract here is what lets these tests exercise
// transparent_tcp against realistic descriptor state; the real registration itself is
// covered by the tcp_server suite.
void mock_tcp_server_simulate_client_admitted(int client_sock);

void mock_tcp_server_reset(void);
