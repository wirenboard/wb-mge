#pragma once

#include <esp_err.h>
#include <stdbool.h>

#define BRIDGES_COUNT                 2

extern int mock_bridge_port_init_called[BRIDGES_COUNT];
extern unsigned mock_bridge_port_init_index[BRIDGES_COUNT];
extern esp_err_t mock_bridge_port_init_return_value[BRIDGES_COUNT];

extern int mock_bridge_port_deinit_called[BRIDGES_COUNT];
extern unsigned mock_bridge_port_deinit_index[BRIDGES_COUNT];
extern esp_err_t mock_bridge_port_deinit_return_value[BRIDGES_COUNT];

extern int mock_bridge_port_check_settings_changed_called[BRIDGES_COUNT];
extern bool mock_bridge_port_check_settings_changed_return_value[BRIDGES_COUNT];

void mock_bridge_reset(void);

esp_err_t bridge_port_init(unsigned index);
esp_err_t bridge_port_deinit(unsigned index);
bool bridge_port_check_settings_changed(unsigned index);
