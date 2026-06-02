#pragma once

/* Mock tracking state for bridge functions — include from tests to access
 * call counters and control variables. */

#include "bridge.h"   /* for BRIDGES_COUNT, serial_desc_t, serial_config_t */
#include <stdbool.h>

typedef struct {
    int bridge_port_init_called;
    int bridge_port_deinit_called;
    int bridge_port_init_serial_only_called;
    int bridge_get_serial_desc_called;
    int bridge_read_serial_config_called;
} mock_bridge_calls_t;

extern mock_bridge_calls_t mock_bridge_calls[BRIDGES_COUNT];
extern bool mock_bridge_port_init_should_fail;
extern bool mock_bridge_port_init_serial_only_should_fail;
extern serial_desc_t *mock_bridge_serial_desc[BRIDGES_COUNT];
/* Raw descriptor instances — exposed so serial.c mock can map desc → port index */
extern serial_desc_t mock_serial_desc_instances[BRIDGES_COUNT];

void mock_bridge_reset(void);

/* R3: inject the serial_config that bridge_read_serial_config() returns for a port,
 * so a test can make the init-time snapshot and the later check see different values. */
void mock_bridge_set_serial_config(unsigned index, const serial_config_t *cfg);
