#pragma once

#include "tcp_desc.h"

#include "esp_err.h"
#include <stdint.h>

esp_err_t tcp_client_init(uint32_t host_ip, uint16_t host_port,
                          tcp_receive_handler_t tcpc_receive_handler, tcp_desc_t **out_desc);
/* Send on the current outbound connection. Client mode has exactly one, so there is no
 * socket argument to get wrong: the target is read and used under conn_lock, with a
 * bounded wait, so a reconnect cannot swap the socket out mid-send.
 * Mirrors tcp_server_send_to_current_client() — transparent_tcp calls both through the
 * same function pointer. */
esp_err_t tcp_client_send_to_current_client(tcp_desc_t *desc, uint8_t *data, size_t len);
esp_err_t tcp_client_connected(tcp_desc_t *desc);

esp_err_t tcp_client_deinit(tcp_desc_t *desc);
