#pragma once

void rs485_busy_monitor_init(void);
void rs485_busy_monitor_update_activity(unsigned index);
void rs485_busy_monitor_reset(unsigned index);

void rs485_stats_init(void);
void rs485_stats_update(unsigned index, bool success);
void rs485_stats_reset(unsigned index);
