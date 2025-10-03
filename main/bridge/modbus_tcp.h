#pragma once

#include "esp_err.h"
#include "serial.h"
#include "setting_items.h"
#include "tcp_desc.h"
#include "bridge.h"
#include "packet_queue.h"

// Инициализация порта в режиме Modbus TCP
esp_err_t modbus_tcp_init_port(unsigned index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc);

// Деинициализация ранее инициализированного порта
esp_err_t modbus_tcp_deinit_port(unsigned index);
