#pragma once

#include "tcp_desc.h"

#include "esp_err.h"
#include <stdint.h>

/* Stack size (in bytes) of the tasks tcp_server creates. Part of the public
 * contract: the tcp_receive_handler_t callback runs inside the per-connection
 * receiver task, so a handler that needs to report or budget against its own
 * stack must use this value rather than hard-coding a copy. */
#define TCP_SERVER_TASK_STACK_SIZE      4096u

esp_err_t tcp_server_init(int port, tcp_receive_handler_t tcps_receive_handler, tcp_desc_t **out_desc);
void tcp_server_set_max_connections(tcp_desc_t *desc, uint32_t max_connections);
esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len);
esp_err_t tcp_server_connected(tcp_desc_t *desc);
esp_err_t tcp_server_deinit(tcp_desc_t *desc);

#ifdef __unittest_env__
void tcp_server_run_receiver_for_test(tcp_desc_t *desc, int client_sock);
#endif /* __unittest_env__ */
