#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

/* Maximum Modbus RTU ADU length: 1 slave + 253 PDU + 2 CRC = 256 bytes */
#define MODBUS_RTU_MAX_FRAME_LEN 256u

// Modbus RTU packet header
typedef struct __attribute__((packed)) {
    uint8_t slave_id;
    uint8_t function;
} mb_rtu_header_t;

// Modbus TCP packet header
typedef struct __attribute__((packed)) {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
    uint8_t function;
} mb_tcp_header_t;

// Canonical Modbus exception codes (shared across the gateway)
#define MODBUS_EXC_ILLEGAL_FUNCTION   0x01u  // Illegal function
#define MODBUS_EXC_ILLEGAL_ADDRESS    0x02u  // Illegal data address
#define MODBUS_EXC_ILLEGAL_DATA_VALUE 0x03u  // Illegal data value
#define MODBUS_EXC_GW_TARGET_FAILED   0x0Bu  // Gateway target device failed to respond

// Canonical Modbus read function codes (shared across the gateway)
#define MODBUS_FC_READ_COILS           0x01u  // FC01: read coils
#define MODBUS_FC_READ_DISCRETE_INPUTS 0x02u  // FC02: read discrete inputs
#define MODBUS_FC_READ_HOLDING_REGS    0x03u  // FC03: read holding registers
#define MODBUS_FC_READ_INPUT_REGS      0x04u  // FC04: read input registers

// Convert 16-bit word (register) Little Endian <-> Big Endian
static inline uint16_t modbus_swap16(uint16_t x) {return (x >> 8) | (x << 8);}

// Calculate CRC16 for Modbus RTU packet
// Returns result in Big Endian format
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);

// Check Modbus RTU request validity (length, CRC)
// Returns ESP_OK on success
esp_err_t modbus_rtu_check_request(const uint8_t *data, size_t len);

// Check Modbus RTU response (length, CRC)
// If RTU request header rtu_req_header is provided, additionally checks slave_id and function code
// Returns ESP_OK on success
esp_err_t modbus_rtu_check_response(const uint8_t *data, size_t len, const mb_rtu_header_t* rtu_req_header);

// Check Modbus TCP request (length, protocol ID)
// Returns ESP_OK on success
esp_err_t modbus_tcp_check_request(const uint8_t *data, size_t len);

// Convert Modbus TCP packet to Modbus RTU
// Packet must be pre-validated by check_tcp_request() or check_tcp_response() function
// Returns length of data written to out_buf buffer on success, or 0 on error
size_t modbus_rtu_from_tcp(const uint8_t *data, uint8_t* out_buf, size_t out_buf_size);

// Convert Modbus RTU packet to Modbus TCP
// Packet must be pre-validated by check_rtu_request() or check_rtu_response() function
// Returns length of data written to out_buf buffer on success, or 0 on error
size_t modbus_tcp_from_rtu(uint16_t transaction_id, const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size);

// Build a Modbus TCP exception ADU into buf: MBAP header (length=3) followed by
// the exception code byte. tid_net is the transaction id in network byte order,
// echoed verbatim. The PDU function byte is set to (fc | 0x80).
// Returns the total wire length (sizeof(mb_tcp_header_t) + 1).
size_t modbus_pdu_build_exception(uint8_t *buf, uint16_t tid_net, uint8_t unit_id, uint8_t fc, uint8_t exc);

// Parse the big-endian start address and count from a Modbus read-request PDU
// body (the 4 bytes immediately after the MBAP/RTU function byte:
// [start_hi][start_lo][count_hi][count_lo]). Caller must ensure pdu has 4 bytes.
void modbus_pdu_parse_read_request(const uint8_t *pdu, uint16_t *start_addr, uint16_t *count);
