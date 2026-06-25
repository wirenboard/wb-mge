#pragma once

#ifdef __unittest_env__
#include <stdint.h>
#include <stddef.h>
#include "packet_queue.h"
#include "tcp_desc.h"

/* Initialize ctx[ctx_idx]: clear reasm slots, set queue handle and tcp_desc, no mutex. */
void modbus_tcp_test_init_ctx(unsigned ctx_idx, packet_queue_handle queue, tcp_desc_t *tcp_desc);

/* Try to get/allocate reasm slot for sock in ctx[ctx_idx].
 * Returns 1 on success (slot found or created), 0 if table is full. */
int modbus_tcp_test_reasm_get(unsigned ctx_idx, int sock);

/* Free reasm slot for sock in ctx[ctx_idx]. No-op if not found. */
void modbus_tcp_test_reasm_free(unsigned ctx_idx, int sock);

/* Returns 1 if a reasm slot for sock exists in ctx[ctx_idx], 0 if not. */
int modbus_tcp_test_reasm_has_slot(unsigned ctx_idx, int sock);

/* Returns number of accumulated bytes for sock in ctx[ctx_idx], 0 if no slot. */
size_t modbus_tcp_test_reasm_pending_bytes(unsigned ctx_idx, int sock);

/* Parse MBAP header at buf (must have >= 6 bytes) and return total ADU length. */
size_t modbus_tcp_test_frame_total_len(const uint8_t *buf);

/* Call separate_and_push_requests_from_tcp_with_client for ctx[ctx_idx].
 * Returns number of frames successfully pushed to queue. */
unsigned modbus_tcp_test_push_data(unsigned ctx_idx, int client_sock,
                                    const uint8_t *data, size_t len);

/* Simulate connection close for client_sock using ctx[ctx_idx]'s tcp_desc. */
void modbus_tcp_test_conn_close(unsigned ctx_idx, int client_sock);

/* Run the static self-device handler (Unit ID 0xFF local dispatch) for ctx[ctx_idx]. */
void modbus_tcp_test_handle_self_device_request(unsigned ctx_idx, int client_sock,
                                                uint8_t *tcp_req_buf, size_t tcp_req_len);

/* Seed / inspect the in-flight RTU request bookkeeping (pending_*), used to drive
 * and verify the on_tcp_conn_close() stale-pending-reset path. */
void     modbus_tcp_test_set_pending(unsigned ctx_idx, uint16_t tid,
                                     uint8_t slave_id, int client_sock);
uint16_t modbus_tcp_test_get_pending_tid(unsigned ctx_idx);
uint8_t  modbus_tcp_test_get_pending_slave_id(unsigned ctx_idx);
int      modbus_tcp_test_get_pending_client_sock(unsigned ctx_idx);

/* Expose calc_response_timeout_ticks() for unit tests (R1). */
unsigned modbus_tcp_test_calc_timeout(unsigned baudrate);

#endif /* __unittest_env__ */
