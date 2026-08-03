#pragma once

#include "tcp_desc.h"

#include "esp_err.h"
#include <stdint.h>

esp_err_t tcp_server_init(int port, tcp_receive_handler_t tcps_receive_handler, tcp_desc_t **out_desc);
void tcp_server_set_max_connections(tcp_desc_t *desc, uint32_t max_connections);
/* Send on a socket the caller owns — i.e. from the per-connection receiver task, which is
 * the task that closes that socket and only does so after the receive handler returns.
 * Any other caller must use one of the two functions below: sampling a socket and passing
 * it in is exactly the race they exist to close. */
esp_err_t tcp_server_send(tcp_desc_t *desc, int client_sock, uint8_t *data, size_t len);

/* Send to the currently registered client (transparent bridge, serial->TCP push).
 * Reads the target and sends under conn_lock, with a bounded wait: on timeout the packet
 * is dropped instead of blocking the calling (UART) task. */
esp_err_t tcp_server_send_to_current_client(tcp_desc_t *desc, uint8_t *data, size_t len);

/* Send to a connection captured earlier as a (client_sock, generation) pair — sample the
 * generation with tcp_desc_conn_generation() at capture time. The pair is validated and
 * the data sent under one conn_lock hold, so a socket closed (and its fd recycled) since
 * the capture can never receive it. Same bounded wait as above. */
esp_err_t tcp_server_send_to_captured_client(tcp_desc_t *desc, int client_sock,
                                             uint32_t generation, uint8_t *data, size_t len);
esp_err_t tcp_server_connected(tcp_desc_t *desc);
esp_err_t tcp_server_deinit(tcp_desc_t *desc);

#ifdef __unittest_env__
void tcp_server_run_receiver_for_test(tcp_desc_t *desc, int client_sock);
#endif /* __unittest_env__ */
