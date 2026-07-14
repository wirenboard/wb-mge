#pragma once

#include "esp_err.h"
#include <stdbool.h>

// Number of bridges — must match the value in bridge.h mock
#ifndef BRIDGES_COUNT
#define BRIDGES_COUNT 2
#endif

// Mock control variables
extern int mock_port_manager_check_settings_changed_called[BRIDGES_COUNT];
extern bool mock_port_manager_check_settings_changed_return_value[BRIDGES_COUNT];

extern int mock_port_manager_apply_settings_called[BRIDGES_COUNT];
extern unsigned mock_port_manager_apply_settings_index[BRIDGES_COUNT];
extern esp_err_t mock_port_manager_apply_settings_return_value[BRIDGES_COUNT];

// Global call id (call_sequence_get_call_id()) of the last apply_settings() call for this port.
// Lets a test assert the order of the phases against the cache Modbus server and the web server.
extern unsigned mock_port_manager_apply_settings_call_seq[BRIDGES_COUNT];

// port_manager_release(): the release half of the two-phase apply.
// The *_skipped counters count the calls that found the ports frozen by the factory clock-out test
// and left the port alone; those do NOT bump the *_called counters, which therefore mean "the port
// really was torn down / brought back up".
extern int mock_port_manager_release_called[BRIDGES_COUNT];
extern unsigned mock_port_manager_release_call_seq[BRIDGES_COUNT];
extern int mock_port_manager_release_skipped[BRIDGES_COUNT];
extern int mock_port_manager_apply_settings_skipped[BRIDGES_COUNT];

extern int mock_port_manager_ports_frozen_called;
extern bool mock_port_manager_ports_frozen_return_value;

bool port_manager_check_settings_changed(unsigned port_index);
esp_err_t port_manager_release(unsigned port_index);
esp_err_t port_manager_apply_settings(unsigned port_index);
bool port_manager_ports_frozen(void);

void mock_port_manager_reset(void);
