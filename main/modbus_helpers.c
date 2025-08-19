#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <esp_log.h>
#include "modbus_helpers.h"

//------------------------------------------------------------------------------

static const char *TAG = "modbus_helpers";

//------------------------------------------------------------------------------

uint16_t modbus_crc16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)data[pos];
        for (int i = 0; i < 8; i++) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

//------------------------------------------------------------------------------

int modbus_rtu_check_request(const uint8_t *data, size_t len)
{
    mb_rtu_header_t* header = (mb_rtu_header_t*)data;

    if (len < 5) {
        ESP_LOGW(TAG, "Incorrect RTU request length: %u, expected >= 5", len);
        return -1;
    }
    if (header->function & MODBUS_EXCEPTION_FLAG) {
        ESP_LOGW(TAG, "Incorrect RTU request function: 0x%02X", header->function);
        return -1;
    }

    uint16_t calc_crc = modbus_crc16(data, len - 2);
    uint16_t packet_crc = *(uint16_t*)&data[len - 2];
    if (calc_crc != packet_crc) {
        ESP_LOGW(TAG, "Incorrect RTU request CRC: 0x%04x, expected: 0x%04x", packet_crc, calc_crc);
        return -1;
    }

    return 0;
}

//------------------------------------------------------------------------------

int modbus_rtu_check_response(const uint8_t *data, size_t len, const mb_rtu_header_t* rtu_req_header) {
    mb_rtu_header_t* header = (mb_rtu_header_t*)data;

    size_t min_len = (header->function & MODBUS_EXCEPTION_FLAG) ? 5 : 6;

    if (len < min_len) {
        ESP_LOGW(TAG, "Incorrect RTU response length: %u, expected >= %u", len, min_len);
        return -1;
    }

    uint16_t calc_crc = modbus_crc16(data, len - 2);
    uint16_t packet_crc = *(uint16_t*)&data[len - 2];
    if (calc_crc != packet_crc) {
        ESP_LOGW(TAG, "Incorrect RTU response CRC: 0x%04x, expected: 0x%04x", packet_crc, calc_crc);
        return -1;
    }

    if (rtu_req_header) {
        if (header->slave_id != rtu_req_header->slave_id) {
            ESP_LOGW(TAG, "Incorrect RTU response Slave ID: %u, expected: %u", header->slave_id, rtu_req_header->slave_id);
            return -1;
        }
        uint8_t func = header->function & ~MODBUS_EXCEPTION_FLAG;
        if (func != rtu_req_header->function) {
            ESP_LOGW(TAG, "Incorrect RTU response Function: 0x%02X, expected: 0x%02X or 0x%02X",
                    func, rtu_req_header->function, rtu_req_header->function | MODBUS_EXCEPTION_FLAG);
            return -1;
        }
    }

    return 0;
}

//------------------------------------------------------------------------------

int modbus_tcp_check_request(const uint8_t *data, size_t len)
{
    if (len < 8) {
        ESP_LOGW(TAG, "Incorrect TCP request ADU length: %u, expected >= 8", len);
        return -1;
    }

    mb_tcp_header_t* header = (mb_tcp_header_t*)data;

    uint16_t pid = modbus_swap16(header->protocol_id);
    if (pid != MODBUS_TCP_PROTOCOL_ID) {
        ESP_LOGW(TAG, "Incorrect TCP request protocol ID: 0x%04X, expected 0x%04X", pid, MODBUS_TCP_PROTOCOL_ID);
        return -1;
    }

    uint16_t req_packet_len = modbus_swap16(header->length) + offsetof(mb_tcp_header_t, unit_id);
    if (req_packet_len != len) {
        ESP_LOGW(TAG, "Incorrect TCP request ADU length: %u, expected: %u", len, req_packet_len);
        return -1;
    }

    return 0;
}

//------------------------------------------------------------------------------

int modbus_tcp_check_response(const uint8_t *data, size_t len, const mb_tcp_header_t* tcp_req_header)
{
    if (len < 8) {
        ESP_LOGW(TAG, "Incorrect TCP response ADU length: %u, expected >= 8", len);
        return -1;
    }

    mb_tcp_header_t* header = (mb_tcp_header_t*)data;

    uint16_t pid = modbus_swap16(header->protocol_id);
    if (pid != MODBUS_TCP_PROTOCOL_ID) {
        ESP_LOGW(TAG, "Incorrect TCP response protocol ID: 0x%04X, expected 0x%04X", pid, MODBUS_TCP_PROTOCOL_ID);
        return -1;
    }

    uint16_t req_packet_len = modbus_swap16(header->length) + offsetof(mb_tcp_header_t, unit_id);
    if (req_packet_len != len) {
        ESP_LOGW(TAG, "Incorrect TCP response ADU length: %u, expected: %u", len, req_packet_len);
        return -1;
    }

    if (tcp_req_header) {
        if (header->unit_id != tcp_req_header->unit_id) {
            ESP_LOGW(TAG, "Incorrect TCP response Unit ID: %u, expected: %u", header->unit_id, tcp_req_header->unit_id);
            return -1;
        }
        uint8_t func = header->function & ~MODBUS_EXCEPTION_FLAG;
        if (func != tcp_req_header->function) {
            ESP_LOGW(TAG, "Incorrect TCP response function: 0x%02X, expected: 0x%02X or 0x%02X",
                    header->function, tcp_req_header->function, tcp_req_header->function | MODBUS_EXCEPTION_FLAG);
            return -1;
        }
    }

    return 0;
}

//------------------------------------------------------------------------------

size_t modbus_rtu_from_tcp(const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size)
{
    mb_tcp_header_t* header = (mb_tcp_header_t*)data;

    uint16_t rtu_len = modbus_swap16(header->length) + 2;
    if (out_buf_size < rtu_len) {
        ESP_LOGE(TAG, "Output RTU packet buffer is too small, size: %u, required: %u", out_buf_size, rtu_len);
        return 0;
    }

    memcpy(out_buf, &data[offsetof(mb_tcp_header_t, unit_id)], rtu_len - 2);
    uint16_t crc = modbus_crc16(out_buf, rtu_len - 2);
    *(uint16_t*)&out_buf[rtu_len - 2] = crc;

    return rtu_len;
}

//------------------------------------------------------------------------------

size_t modbus_tcp_from_rtu(uint16_t transaction_id, const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size)
{
    size_t tcp_len = len + sizeof(mb_tcp_header_t) - sizeof(mb_rtu_header_t) - 2;

    if (out_buf_size < tcp_len) {
        ESP_LOGE(TAG, "Output TCP packet buffer is too small, size: %u, required: %u", out_buf_size, tcp_len);
        return 0;
    }

    mb_rtu_header_t* rtu_header = (mb_rtu_header_t*)data;
    mb_tcp_header_t* tcp_header = (mb_tcp_header_t*)out_buf;

    tcp_header->transaction_id = modbus_swap16(transaction_id);
    tcp_header->protocol_id = modbus_swap16(MODBUS_TCP_PROTOCOL_ID);
    tcp_header->length = modbus_swap16(tcp_len - offsetof(mb_tcp_header_t, unit_id));
    tcp_header->unit_id = rtu_header->slave_id;
    tcp_header->function = rtu_header->function;

    size_t data_len = tcp_len - sizeof(mb_tcp_header_t);
    memcpy(&out_buf[sizeof(mb_tcp_header_t)], &data[sizeof(mb_rtu_header_t)], data_len);

    return tcp_len;
}

//------------------------------------------------------------------------------

size_t modbus_rtu_exception_response(const mb_rtu_header_t* rtu_req_header, uint8_t exception_code, uint8_t* out_buf, size_t out_buf_size)
{
    if (!out_buf || (out_buf_size < (sizeof(mb_rtu_exception_response_t) + 2))) {
        return 0;
    }

    mb_rtu_exception_response_t* response = (mb_rtu_exception_response_t*)out_buf;
    memcpy(&response->header, rtu_req_header, sizeof(response->header));
    response->header.function |= MODBUS_EXCEPTION_FLAG;
    response->exception_code = exception_code;

    uint16_t crc = modbus_crc16(out_buf, sizeof(mb_rtu_exception_response_t));
    *(uint16_t*)&out_buf[sizeof(mb_rtu_exception_response_t)] = crc;

    return (sizeof(mb_rtu_exception_response_t) + 2);
}

//------------------------------------------------------------------------------

size_t modbus_tcp_exception_response(const mb_tcp_header_t* tcp_req_header, uint8_t exception_code, uint8_t* out_buf, size_t out_buf_size)
{
    if (!out_buf || (out_buf_size < sizeof(mb_tcp_exception_response_t))) {
        return 0;
    }

    mb_tcp_exception_response_t* response = (mb_tcp_exception_response_t*)out_buf;
    memcpy(&response->header, tcp_req_header, sizeof(response->header));
    response->header.length = modbus_swap16(sizeof(mb_tcp_exception_response_t) - offsetof(mb_tcp_header_t, unit_id));
    response->header.function |= MODBUS_EXCEPTION_FLAG;
    response->exception_code = exception_code;

    return sizeof(mb_tcp_exception_response_t);
}

//------------------------------------------------------------------------------
