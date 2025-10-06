#pragma once

#include "tcp_desc.h"
#include <stdbool.h>

enum fast_modbus_probe_result
{
    FAST_MODBUS_PROBE_SUCCESS,
    FAST_MODBUS_NOT_PROBE,
    FAST_MODBUS_PROBE_MALLOC_FAIL,
    FAST_MODBUS_PROBE_SEND_FAIL
};

// Проверка запроса на поддержку Быстрого Modbus и отправка ответа, что устройство его поддерживает
enum fast_modbus_probe_result fast_modbus_send_probe_response(uint8_t port, tcp_desc_t *tcp_desc, uint8_t *tcp_req_buf);

// Удаляем байты 0xFF, предшествующие началу пакета в быстром Modbus.
// Data является in/out параметром, который изменяется внутри функции
size_t fast_modbus_truncate_ff(uint8_t **data, size_t len);
