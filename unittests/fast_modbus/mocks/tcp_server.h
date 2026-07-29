#pragma once

#include "esp_err.h"
#include "bridge/tcp_desc.h"
#include <stdint.h>

typedef struct {
    int called;
    tcp_desc_t* desc;
    int client_sock;
    /* Connection generation the module passed to tcp_server_send_to_captured_client().
     * fast_modbus answers a probe from modbus_tcp_server_task(), not from the connection's
     * receiver task, so the reply must be addressed by the (socket, generation) pair that
     * came off the packet queue — the generation is half of that address. */
    uint32_t generation;
    size_t len;
    esp_err_t result;
    uint8_t last_data[64]; /* copy of data buffer before free() */
} tcp_server_send_mock_t;

extern tcp_server_send_mock_t tcp_server_send_mock;

esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len);

/* The real implementation validates (client_sock, generation) against the descriptor under
 * its connection lock and only then sends; that logic belongs to tcp_server and is covered
 * by its own suite. Here the mock only records what fast_modbus addressed the probe
 * response to, which is what these tests are about. */
esp_err_t tcp_server_send_to_captured_client(tcp_desc_t *desc, int client_sock,
                                             uint32_t generation, uint8_t *data, size_t len);

void mock_tcp_server_reset(void);
