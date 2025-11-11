#pragma once

#include "bridge.h"

typedef struct {
    int busy_monitor_init_called;
    int busy_monitor_reset_called[BRIDGES_COUNT];
    int stats_init_called;
    int stats_reset_called[BRIDGES_COUNT];
} mock_rs485_stats_t;

extern mock_rs485_stats_t mock_rs485_stats;

void rs485_busy_monitor_init(void);
void rs485_busy_monitor_reset(unsigned index);
void rs485_stats_init(void);
void rs485_stats_reset(unsigned index);
void mock_rs485_stats_reset(void);
