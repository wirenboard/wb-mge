#pragma once

#include <stdint.h>
#include <stddef.h>

//------------------------------------------------------------------------------

#define MODBUS_EXCEPTION_FLAG               0x80        // Флаг исключения Modbus (ставится по OR с кодом функции)

#define MODBUS_EXCEPTION_SLAVE_DEVICE_FAIL  0x04        // Код исключения: отказ slave-устройства (неверный CRC пакета или длина)
#define MODBUS_EXCEPTION_GATEWAY            0x0A        // Код исключения: ошибка шлюза
#define MODBUS_EXCEPTION_SLAVE_NOT_AVAIL    0x0B        // Код исключения: slave-устройство недоступно (тайм-аут)

#define MODBUS_TCP_PROTOCOL_ID              0x0000      // ID протокола Modbus TCP

//------------------------------------------------------------------------------

// Заголовок пакета Modbus RTU
typedef struct __attribute__((packed)) {
    uint8_t slave_id;
    uint8_t function;
} mb_rtu_header_t;

// Пакет ответа Modbus RTU с кодом ошибки
typedef struct __attribute__((packed)) {
    mb_rtu_header_t header;
    uint8_t exception_code;
} mb_rtu_exception_response_t;

// Заголовок пакета Modbus TCP
typedef struct __attribute__((packed)) {
    uint16_t transaction_id;
    uint16_t protocol_id;
    uint16_t length;
    uint8_t unit_id;
    uint8_t function;
} mb_tcp_header_t;

// Пакет ответа Modbus TCP с кодом ошибки
typedef struct __attribute__((packed)) {
    mb_tcp_header_t header;
    uint8_t exception_code;
} mb_tcp_exception_response_t;

//------------------------------------------------------------------------------

// Конвертация 16-битного слова (регистра) Little Endian <-> Big Endian
static inline uint16_t modbus_swap16(uint16_t x) {return (x >> 8) | (x << 8);}

// Расчет CRC16 для пакета Modbus RTU
// Возвращает результат сразу в формате Big Endian
uint16_t modbus_crc16(const uint8_t *data, uint16_t len);

// Проверка корректности запроса Modbus RTU (длина, CRC)
// Возвращает 0 в случае успеха
int modbus_rtu_check_request(const uint8_t *data, size_t len);

// Проверка ответа Modbus RTU (длина, CRC)
// Если передан заголовок RTU-запроса rtu_req_header, то дополнительно проверяются и slave_id и код функции
// Возвращает 0 в случае успеха
int modbus_rtu_check_response(const uint8_t *data, size_t len, const mb_rtu_header_t* rtu_req_header);

// Проверка запроса Modbus TCP (длина, ID протокола)
// Возвращает 0 в случае успеха
int modbus_tcp_check_request(const uint8_t *data, size_t len);

// Проверка ответа Modbus TCP (длина, ID протокола)
// Если передан заголовок TCP-запроса tcp_req_header, то дополнительно проверяются и unit_id и код функции
// Возвращает 0 в случае успеха
int modbus_tcp_check_response(const uint8_t *data, size_t len, const mb_tcp_header_t* tcp_req_header);

// Преобразование пакета Modbus TCP в Modbus RTU
// Пакет должен быть предварительно проверен функцией check_tcp_request() или check_tcp_response()
// Возвращает длину данных, записанных в буфер out_data или 0 в случае ошибки
size_t modbus_rtu_from_tcp(const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size);

// Преобразование пакета Modbus RTU в Modbus TCP
// Пакет должен быть предварительно проверен функцией check_rtu_request() или check_rtu_response()
// Возвращает длину данных, записанных в буфер out_data или 0 в случае ошибки
size_t modbus_tcp_from_rtu(uint16_t transaction_id, const uint8_t *data, size_t len, uint8_t* out_buf, size_t out_buf_size);

// Подготовка ответа Modbus RTU с кодом исключения (ошибки)
// Возвращает размер пакета с исключением (размер mb_rtu_exception_response_t + 2 байта CRC)
size_t modbus_rtu_exception_response(const mb_rtu_header_t* rtu_req_header, uint8_t exception_code, uint8_t* out_buf, size_t out_buf_size);

// Подготовка ответа Modbus TCP с кодом исключения (ошибки)
// Возвращает размер пакета с исключением (mb_tcp_exception_response_t)
size_t modbus_tcp_exception_response(const mb_tcp_header_t* tcp_req_header, uint8_t exception_code, uint8_t* out_buf, size_t out_buf_size);

//------------------------------------------------------------------------------
