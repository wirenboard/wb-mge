#pragma once

#include <stdbool.h>

#include "esp_err.h"

#define MOCK_MB_SLAVE_MAX_CALLS 8

extern int      mock_mb_slave_set_tx_disabled_called;
extern unsigned mock_mb_slave_set_tx_disabled_indexes[MOCK_MB_SLAVE_MAX_CALLS];
extern bool     mock_mb_slave_set_tx_disabled_values[MOCK_MB_SLAVE_MAX_CALLS];

void mock_mb_slave_reset(void);

esp_err_t mb_slave_start(void);
esp_err_t mb_slave_stop(void);
bool mb_slave_check_settings_changed(void);
esp_err_t mb_slave_set_tx_disabled(unsigned index, bool disabled);
