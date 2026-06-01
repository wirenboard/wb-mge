#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Gateway's own Modbus Unit ID (0xFF), per the Modbus Messaging Implementation Guide. */
#define MB_DEVICE_UNIT_ID 0xFFu

/* True if unit_id addresses the gateway itself (0xFF). */
bool mb_device_is_self(uint8_t unit_id);

/*
 * Build a Modbus TCP read response for a request addressed to the gateway (unit 0xFF).
 * Supports FC03 (holding) and FC04 (input). Fills the MBAP header + payload into resp_buf.
 *   transaction_id_net  : transaction ID in NETWORK byte order (echoed verbatim).
 *   task_stack_size_bytes: total stack size (bytes) of the CALLING task, for the
 *                          stack-usage registers; pass 0 if unknown.
 *   resp_buf            : output buffer, at least 260 bytes.
 *   exc_out             : on failure, set to a Modbus exception code.
 * Returns total wire length on success, or 0 on failure (then *exc_out is set:
 *   0x01 illegal function, 0x02 illegal data address, 0x03 illegal data value).
 */
size_t mb_device_build_read_response(uint8_t unit_id, uint8_t fc,
                                     uint16_t transaction_id_net,
                                     uint16_t start_addr, uint16_t count,
                                     uint16_t task_stack_size_bytes,
                                     uint8_t *resp_buf, uint8_t *exc_out);

/*
 * Handle a Modbus TCP request addressed to the gateway itself (Unit ID 0xFF).
 * Parses the request frame, validates function code / length / quantity, and
 * builds EITHER a normal FC03/FC04 read response OR a Modbus exception ADU into
 * resp_buf. Always produces a ready-to-send ADU (never returns 0).
 *   req                  : full TCP frame starting at the MBAP header (>= header bytes).
 *   req_len              : total length of req in bytes.
 *   task_stack_size_bytes: total stack size of the calling task (for stack-usage
 *                          registers; pass 0 if unknown).
 *   resp_buf             : output buffer, at least 260 bytes.
 * Returns the total wire length of the ADU written to resp_buf (always > 0).
 */
size_t mb_device_handle_self_request(const uint8_t *req, size_t req_len,
                                     uint16_t task_stack_size_bytes,
                                     uint8_t *resp_buf);
