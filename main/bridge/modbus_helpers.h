#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

// Заголовок пакета Modbus RTU
typedef struct __attribute__((packed)) {
    uint8_t slave_id;
    uint8_t function;
} mb_rtu_header_t;

// Заголовок пакета Modbus TCP
typedef struct __attribute__((packed)) {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
    uint8_t function;
} mb_tcp_header_t;

// Конвертация 16-битного слова (регистра) Little Endian <-> Big Endian
static inline uint16_t modbus_swap16(uint16_t x) {return (x >> 8) | (x << 8);}

// Расчет CRC16 для пакета Modbus RTU
// Возвращает результат сразу в формате Big Endian
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);

// Проверка корректности запроса Modbus RTU (длина, CRC)
// Возвращает ESP_OK в случае успеха
esp_err_t modbus_rtu_check_request(const uint8_t *data, size_t len);

// Проверка ответа Modbus RTU (длина, CRC)
// Если передан заголовок RTU-запроса rtu_req_header, то дополнительно проверяются и slave_id и код функции
// Возвращает ESP_OK в случае успеха
esp_err_t modbus_rtu_check_response(const uint8_t *data, size_t len, const mb_rtu_header_t* rtu_req_header);

// Проверка запроса Modbus TCP (длина, ID протокола)
// Возвращает ESP_OK в случае успеха
esp_err_t modbus_tcp_check_request(const uint8_t *data, size_t len);

// Преобразование пакета Modbus TCP в Modbus RTU
// Пакет должен быть предварительно проверен функцией check_tcp_request() или check_tcp_response()
// Возвращает длину данных, записанных в буфер out_buf, в случае успешного завершения или 0 в случае ошибки
size_t modbus_rtu_from_tcp(const uint8_t *data, uint8_t* out_buf, size_t out_buf_size);

// Преобразование пакета Modbus RTU в Modbus TCP
// Пакет должен быть предварительно проверен функцией check_rtu_request() или check_rtu_response()
// Возвращает длину данных, записанных в буфер out_buf, в случае успешного завершения или 0 в случае ошибки
size_t modbus_tcp_from_rtu(uint16_t transaction_id, const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size);
