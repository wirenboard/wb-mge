#pragma once

#include "esp_err.h"
#include "bridge/tcp_desc.h"
#include <stdint.h>

/* Minimal tcp_server stub for sniffer unit tests */
esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len);

/* The suite links bridge/fast_modbus.c (for fast_modbus_truncate_ff), and its probe-response
 * path now sends through the captured-connection entry point. Nothing here exercises it —
 * this only satisfies the linker. */
esp_err_t tcp_server_send_to_captured_client(tcp_desc_t *desc, int client_sock,
                                             uint32_t generation, uint8_t *data, size_t len);
