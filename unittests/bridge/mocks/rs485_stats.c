#include "rs485_stats.h"

int mock_rs485_busy_monitor_reset_called = 0;
unsigned mock_rs485_busy_monitor_reset_index = 0;

int mock_rs485_stats_init_called = 0;

int mock_rs485_stats_reset_called = 0;
unsigned mock_rs485_stats_reset_index = 0;

int mock_rs485_busy_monitor_init_called = 0;

void rs485_busy_monitor_init(void)
{
    mock_rs485_busy_monitor_init_called++;
}

void rs485_busy_monitor_reset(unsigned index)
{
    mock_rs485_busy_monitor_reset_called++;
    mock_rs485_busy_monitor_reset_index = index;
}

void rs485_stats_init(void)
{
    mock_rs485_stats_init_called++;
}

void rs485_stats_reset(unsigned index)
{
    mock_rs485_stats_reset_called++;
    mock_rs485_stats_reset_index = index;
}

void mock_rs485_stats_reset(void)
{
    mock_rs485_busy_monitor_reset_called = 0;
    mock_rs485_busy_monitor_reset_index = 0;

    mock_rs485_busy_monitor_init_called = 0;

    mock_rs485_stats_reset_called = 0;
    mock_rs485_stats_reset_index = 0;

    mock_rs485_stats_init_called = 0;
}
