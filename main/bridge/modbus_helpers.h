#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

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
