#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* Minimal serial.h stub for unit tests */
typedef struct serial_desc_t serial_desc_t;

typedef void (*serial_receive_handler_t)(serial_desc_t *desc, uint8_t *data, size_t len);

struct serial_desc_t {
    serial_receive_handler_t sniff_handler;
};

/* RX timeout constants matching production serial.h */
#define SERIAL_RX_TOUT_SNIFFER  3
#define SERIAL_RX_TOUT_PROXY    10

esp_err_t serial_set_rx_timeout(serial_desc_t *desc, uint8_t tout_symbols);

/* Stub for esp_err_to_name — declared here so sniffer.c can link */
const char *esp_err_to_name(esp_err_t code);
