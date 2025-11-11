#pragma once

#include "bridge.h"
#include "serial.h"
#include "tcp_desc.h"
#include "esp_err.h"

typedef struct {
    serial_config_t *config;
    bridge_mode_t mode;
    int port;
    uint32_t ip;
    serial_desc_t **serial_desc;
    tcp_desc_t **tcp_desc;
} mock_transparent_tcp_t;

typedef struct {
    int init_port_called;
    int deinit_port_called;
} mock_transparent_tcp_calls_t;

extern mock_transparent_tcp_t mock_transparent_tcp[BRIDGES_COUNT];
extern mock_transparent_tcp_calls_t mock_transparent_tcp_calls[BRIDGES_COUNT];
extern bool mock_transparent_tcp_init_port_should_fail;

esp_err_t transparent_tcp_init_port(unsigned index, serial_config_t *config,
                                    bridge_mode_t mode, int port, uint32_t ip,
                                    serial_desc_t **serial_desc, tcp_desc_t **tcp_desc);

esp_err_t transparent_tcp_deinit_port(unsigned index);

void mock_transparent_tcp_reset(void);
