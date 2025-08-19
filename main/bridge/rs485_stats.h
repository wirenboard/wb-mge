#pragma once

void rs485_busy_monitor_init(void);
void rs485_busy_monitor_update_activity(int index);

void rs485_stats_init(void);
void rs485_stats_update(int index, unsigned count, unsigned success);
