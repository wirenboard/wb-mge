#include <stdbool.h>
#include "bridge.h"  /* for BRIDGES_COUNT (includes serial.h -> uart.h -> stdbool.h) */
#include "rs485_stats.h"
#include <string.h>

/* Call tracking variables exposed for test assertions */
int mock_rs485_busy_monitor_init_called = 0;
int mock_rs485_busy_monitor_reset_called[BRIDGES_COUNT];
int mock_rs485_stats_init_called = 0;
int mock_rs485_stats_reset_called[BRIDGES_COUNT];

void rs485_busy_monitor_init(void)
{
    mock_rs485_busy_monitor_init_called++;
}

void rs485_busy_monitor_update_activity(unsigned index)
{
    (void)index;
}

void rs485_busy_monitor_reset(unsigned index)
{
    if (index < BRIDGES_COUNT) {
        mock_rs485_busy_monitor_reset_called[index]++;
    }
}

void rs485_stats_init(void)
{
    mock_rs485_stats_init_called++;
}

void rs485_stats_update(unsigned index, bool success)
{
    (void)index;
    (void)success;
}

void rs485_stats_reset(unsigned index)
{
    if (index < BRIDGES_COUNT) {
        mock_rs485_stats_reset_called[index]++;
    }
}

void mock_rs485_stats_reset_all(void)
{
    mock_rs485_busy_monitor_init_called = 0;
    memset(mock_rs485_busy_monitor_reset_called, 0, sizeof(mock_rs485_busy_monitor_reset_called));
    mock_rs485_stats_init_called = 0;
    memset(mock_rs485_stats_reset_called, 0, sizeof(mock_rs485_stats_reset_called));
}
