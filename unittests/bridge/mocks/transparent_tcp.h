#pragma once

#include "bridge.h"
#include "serial.h"
#include "tcp_desc.h"
#include "esp_err.h"

#define MOCK_TRANSPARENT_TCP_MAX_CALLS 8

extern int mock_transparent_tcp_init_port_called;
extern esp_err_t mock_transparent_tcp_init_port_return_value;
extern unsigned mock_transparent_tcp_init_port_indices[MOCK_TRANSPARENT_TCP_MAX_CALLS];

extern int mock_transparent_tcp_deinit_port_called;
extern unsigned mock_transparent_tcp_deinit_port_indices[MOCK_TRANSPARENT_TCP_MAX_CALLS];

void mock_transparent_tcp_reset(void);

esp_err_t transparent_tcp_init_port(unsigned index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc);

esp_err_t transparent_tcp_deinit_port(unsigned index);
