#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "serial.h"

esp_err_t sniffer_init(void);
void sniffer_attach(unsigned port_index, serial_desc_t *serial_desc);
void sniffer_enable(unsigned port_index);
void sniffer_disable(unsigned port_index);
esp_err_t sniffer_register_handlers(httpd_handle_t server);
