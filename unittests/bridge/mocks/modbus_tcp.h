#pragma once

#include "bridge.h"
#include "serial.h"
#include "tcp_desc.h"
#include "esp_err.h"

// active_connections the "fail late" init mode leaves on the descriptor it hands back.
// Any value but 0 will do; it only has to be distinguishable from the 0 that a correctly
// unpublished context must report.
#define MOCK_TCP_FAIL_LATE_ACTIVE_CONNS 7

typedef struct {
    serial_config_t *config;
    bridge_mode_t mode;
    int port;
    uint32_t ip;
    serial_desc_t **serial_desc;
    tcp_desc_t **tcp_desc;
} mock_modbus_tcp_t;

typedef struct {
    int init_port_called;
    int deinit_port_called;
    // What bridge.c's own readers saw at the moment this module would have freed the
    // descriptors — see the observation comments in modbus_tcp.c. Written by the mock,
    // asserted by the tests; -1 means "the observation point was never reached".
    int init_fail_observed_active_conns;
    int deinit_observed_active_conns;
    // No sentinel of its own: after the reset memset this is NULL, which is also the value
    // the ordering test demands, so a NULL here does not by itself prove the observation
    // point was reached. Reachability is carried by deinit_observed_active_conns above — the
    // mock writes it in the statement immediately before this one, and its -1 fails the
    // neighbouring assertion if the point was missed. Do not assert this field alone.
    const serial_desc_t *deinit_observed_serial_desc;
} mock_modbus_tcp_calls_t;

extern mock_modbus_tcp_t mock_modbus_tcp[BRIDGES_COUNT];
extern mock_modbus_tcp_calls_t mock_modbus_tcp_calls[BRIDGES_COUNT];
extern bool mock_modbus_tcp_init_port_should_fail;
extern bool mock_modbus_tcp_init_port_should_fail_late;

esp_err_t modbus_tcp_init_port(unsigned index, serial_config_t *config,
                                bridge_mode_t mode, int port, uint32_t ip,
                                serial_desc_t **serial_desc, tcp_desc_t **tcp_desc);

esp_err_t modbus_tcp_deinit_port(unsigned index);

void mock_modbus_tcp_reset(void);
