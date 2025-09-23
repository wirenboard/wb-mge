#include "modbus_helpers.h"

#include <string.h>
#include <esp_log.h>

#define MODBUS_EXCEPTION_FLAG                   0x80        // Флаг исключения Modbus (ставится по OR с кодом функции)
#define MODBUS_TCP_PROTOCOL_ID                  0x0000      // ID протокола Modbus TCP

#define MODBUS_RTU_CRC_BASE                     0xFFFF
#define MODBUS_RTU_CRC16_LEN                    sizeof(uint16_t)
#define MODBUS_RTU_REQUEST_MIN_LEN              5
#define MODBUS_RTU_EXCEPTION_RESPONSE_MIN_LEN   (sizeof(mb_rtu_header_t) + sizeof(uint8_t) + MODBUS_RTU_CRC16_LEN)
#define MODBUS_RTU_NORMAL_RESPONSE_MIN_LEN      (sizeof(mb_rtu_header_t) + MODBUS_RTU_CRC16_LEN + sizeof(uint16_t))

#define MODBUS_TCP_REQUEST_MIN_LEN              sizeof(mb_tcp_header_t)

static const char *TAG = "modbus_helpers";


uint16_t modbus_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = MODBUS_RTU_CRC_BASE;

    if (!data) {
        return crc;
    }

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

esp_err_t modbus_rtu_check_request(const uint8_t *data, size_t len)
{
    if (!data) {
        return ESP_FAIL;
    }

    mb_rtu_header_t* header = (mb_rtu_header_t*)data;

    if (len < MODBUS_RTU_REQUEST_MIN_LEN) {
        ESP_LOGW(TAG, "Incorrect RTU request length: %u, expected >= %u", (unsigned)len, MODBUS_RTU_REQUEST_MIN_LEN);
        return ESP_FAIL;
    }
    if (header->function & MODBUS_EXCEPTION_FLAG) {
        ESP_LOGW(TAG, "Incorrect RTU request function: 0x%02X", header->function);
        return ESP_FAIL;
    }

    uint16_t calc_crc = modbus_crc16(data, len - MODBUS_RTU_CRC16_LEN);
    uint16_t packet_crc = *(uint16_t*)&data[len - MODBUS_RTU_CRC16_LEN];
    if (calc_crc != packet_crc) {
        ESP_LOGW(TAG, "Incorrect RTU request CRC: 0x%04x, expected: 0x%04x", packet_crc, calc_crc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t modbus_rtu_check_response(const uint8_t *data, size_t len, const mb_rtu_header_t* rtu_req_header)
{
    if (!data) {
        return ESP_FAIL;
    }

    mb_rtu_header_t* header = (mb_rtu_header_t*)data;

    size_t min_len = 0;
    if (header->function & MODBUS_EXCEPTION_FLAG) {
        min_len = MODBUS_RTU_EXCEPTION_RESPONSE_MIN_LEN;
    } else {
        min_len = MODBUS_RTU_NORMAL_RESPONSE_MIN_LEN;
    }

    if (len < min_len) {
        ESP_LOGW(TAG, "Incorrect RTU response length: %u, expected >= %u", (unsigned)len, (unsigned)min_len);
        return ESP_FAIL;
    }

    uint16_t calc_crc = modbus_crc16(data, len - MODBUS_RTU_CRC16_LEN);
    uint16_t packet_crc = *(uint16_t*)&data[len - MODBUS_RTU_CRC16_LEN];
    if (calc_crc != packet_crc) {
        ESP_LOGW(TAG, "Incorrect RTU response CRC: 0x%04x, expected: 0x%04x", packet_crc, calc_crc);
        return ESP_FAIL;
    }

    if (rtu_req_header) {
        if (header->slave_id != rtu_req_header->slave_id) {
            ESP_LOGW(TAG, "Incorrect RTU response Slave ID: %u, expected: %u", header->slave_id, rtu_req_header->slave_id);
            return ESP_FAIL;
        }
        uint8_t func = header->function & ~MODBUS_EXCEPTION_FLAG;
        if (func != rtu_req_header->function) {
            ESP_LOGW(TAG, "Incorrect RTU response Function: 0x%02X, expected: 0x%02X or 0x%02X",
                    func, rtu_req_header->function, rtu_req_header->function | MODBUS_EXCEPTION_FLAG);
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

esp_err_t modbus_tcp_check_request(const uint8_t *data, size_t len)
{
    if (!data) {
        return ESP_FAIL;
    }

    if (len < MODBUS_TCP_REQUEST_MIN_LEN) {
        ESP_LOGW(
            TAG, "Incorrect TCP request ADU length: %u, expected >= %u",
            (unsigned)len, (unsigned)MODBUS_TCP_REQUEST_MIN_LEN
        );
        return ESP_FAIL;
    }

    mb_tcp_header_t* header = (mb_tcp_header_t*)data;

    uint16_t pid = modbus_swap16(header->protocol_id);
    if (pid != MODBUS_TCP_PROTOCOL_ID) {
        ESP_LOGW(TAG, "Incorrect TCP request protocol ID: 0x%04X, expected 0x%04X", pid, MODBUS_TCP_PROTOCOL_ID);
        return ESP_FAIL;
    }

    uint16_t req_packet_len = modbus_swap16(header->length) + offsetof(mb_tcp_header_t, unit_id);
    if (req_packet_len != len) {
        ESP_LOGW(TAG, "Incorrect TCP request ADU length: %u, expected: %u", (unsigned)len, req_packet_len);
        return ESP_FAIL;
    }

    return ESP_OK;
}

size_t modbus_rtu_from_tcp(const uint8_t *data, uint8_t* out_buf, size_t out_buf_size)
{
    if (!data) {
        return 0;
    }

    if (!out_buf) {
        return 0;
    }

    mb_tcp_header_t* header = (mb_tcp_header_t*)data;

    uint16_t rtu_len = modbus_swap16(header->length) + MODBUS_RTU_CRC16_LEN;
    if (out_buf_size < rtu_len) {
        ESP_LOGE(TAG, "Output RTU packet buffer is too small, size: %u, required: %u", (unsigned)out_buf_size, rtu_len);
        return 0;
    }

    uint16_t rtu_len_excl_crc = rtu_len - MODBUS_RTU_CRC16_LEN;
    memcpy(out_buf, &data[offsetof(mb_tcp_header_t, unit_id)], rtu_len_excl_crc);
    uint16_t crc = modbus_crc16(out_buf, rtu_len_excl_crc);
    *(uint16_t*)&out_buf[rtu_len_excl_crc] = crc;

    return rtu_len;
}


size_t modbus_tcp_from_rtu(uint16_t transaction_id, const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size)
{
    if (!data) {
        return 0;
    }

    if (!out_buf) {
        return 0;
    }

    size_t tcp_len = len + sizeof(mb_tcp_header_t) - sizeof(mb_rtu_header_t) - MODBUS_RTU_CRC16_LEN;

    if (out_buf_size < tcp_len) {
        ESP_LOGE(
            TAG, "Output TCP packet buffer is too small, size: %u, required: %u",
            (unsigned)out_buf_size, (unsigned)tcp_len
        );
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
