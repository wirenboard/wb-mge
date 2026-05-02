#pragma once
#include <stdint.h>
#include <stddef.h>

/* Minimal serial.h stub for unit tests */
typedef struct serial_desc_s serial_desc_t;

typedef void (*serial_receive_handler_t)(serial_desc_t *desc, uint8_t *data, size_t len);

struct serial_desc_s {
    serial_receive_handler_t sniff_handler;
};
