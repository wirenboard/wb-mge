#pragma once

#ifdef __unittest_env__
#include <stdint.h>
#include <stddef.h>
#include "packet_queue.h"
#include "tcp_desc.h"
#include "mbtcp_reasm.h"

/* Initialize ctx[ctx_idx]: clear reasm slots, set queue handle and tcp_desc, no mutex. */
void modbus_tcp_test_init_ctx(unsigned ctx_idx, packet_queue_handle queue, tcp_desc_t *tcp_desc);

/* Slot allocation, slot release, buffered-byte accounting and MBAP length parsing
 * live in bridge/mbtcp_reasm and are covered by unittests/mbtcp_reasm. Tests that
 * need to inspect a port's reassembly state reach it through this accessor and
 * call the real mbtcp_reasm API — no per-function shims. */
mbtcp_reasm_t *modbus_tcp_test_get_reasm(unsigned ctx_idx);

/* Call separate_and_push_requests_from_tcp_with_client for ctx[ctx_idx].
 * Returns number of frames successfully pushed to queue. */
unsigned modbus_tcp_test_push_data(unsigned ctx_idx, int client_sock,
                                    const uint8_t *data, size_t len);

/* Simulate connection close for client_sock using ctx[ctx_idx]'s tcp_desc. */
void modbus_tcp_test_conn_close(unsigned ctx_idx, int client_sock);

/* Run the static self-device handler (Unit ID 0xFF local dispatch) for ctx[ctx_idx],
 * addressed by the (socket, generation) pair the request arrived with. */
void modbus_tcp_test_handle_self_device_request(unsigned ctx_idx, int client_sock,
                                                uint32_t conn_generation,
                                                uint8_t *tcp_req_buf, size_t tcp_req_len);

/* Take the next request off the queue exactly as modbus_tcp_server_task() does: pop the
 * packet together with the (socket, generation) pair enqueued with it and adopt that pair
 * as the port's in-flight request. Returns the packet length (0 if the queue is empty);
 * *tcp_req_buf must be free()d by the caller. */
size_t modbus_tcp_test_fetch_tcp_request(unsigned ctx_idx, uint8_t **tcp_req_buf,
                                         int *client_sock, uint32_t *conn_generation);

/* Seed / inspect the in-flight RTU request bookkeeping (pending_*), used to drive
 * and verify the on_tcp_conn_close() stale-pending-reset path. */
void     modbus_tcp_test_set_pending(unsigned ctx_idx, uint16_t tid,
                                     uint8_t slave_id, int client_sock);
/* Seed the connection generation captured alongside pending_client_sock — the other half
 * of the pair that identifies which connection the in-flight request belongs to. */
void     modbus_tcp_test_set_pending_generation(unsigned ctx_idx, uint32_t generation);
uint16_t modbus_tcp_test_get_pending_tid(unsigned ctx_idx);
uint8_t  modbus_tcp_test_get_pending_slave_id(unsigned ctx_idx);
int      modbus_tcp_test_get_pending_client_sock(unsigned ctx_idx);
uint32_t modbus_tcp_test_get_pending_generation(unsigned ctx_idx);

/* Run the RS-485 receive callback for ctx[ctx_idx], i.e. deliver an RTU response as the
 * UART event task would. This is the path that sends the reply back to the TCP client. */
void     modbus_tcp_test_process_data_from_serial(unsigned ctx_idx, uint8_t *data, size_t len);

/* The response-timeout arithmetic now lives in modbus_helpers
 * (modbus_rtu_response_timeout_ticks) and is tested directly there. */

#endif /* __unittest_env__ */
