#pragma once

extern int mock_rs485_busy_monitor_init_called;

extern int mock_rs485_busy_monitor_reset_called;
extern unsigned mock_rs485_busy_monitor_reset_index;

extern int mock_rs485_stats_init_called;

extern int mock_rs485_stats_reset_called;
extern unsigned mock_rs485_stats_reset_index;

void rs485_busy_monitor_init(void);
void rs485_busy_monitor_reset(unsigned index);
void rs485_stats_init(void);
void rs485_stats_reset(unsigned index);
void mock_rs485_stats_reset(void);
