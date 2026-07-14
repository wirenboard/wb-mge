// Mock implementation of rs485_stats for the repeater unit tests.
// Everything is a no-op except rs485_busy_monitor_update_activity(), whose calls are recorded
// per port index: the repeater drives the RX/TX activity indicators through it, so a test must
// be able to assert that no TX activity is reported on a peer that transmitted nothing.

#include <stdbool.h>
#include <string.h>

#include "rs485_stats.h"
#include "bridge.h"   // BRIDGES_COUNT

int mock_rs485_busy_monitor_activity_called[BRIDGES_COUNT] = {0};

void mock_rs485_stats_reset(void)
{
    memset(mock_rs485_busy_monitor_activity_called, 0,
           sizeof(mock_rs485_busy_monitor_activity_called));
}

void rs485_busy_monitor_init(void)
{
}

void rs485_busy_monitor_update_activity(unsigned index)
{
    if (index < BRIDGES_COUNT) {
        mock_rs485_busy_monitor_activity_called[index]++;
    }
}

void rs485_busy_monitor_reset(unsigned index)
{
    (void)index;
}

void rs485_stats_init(void)
{
}

void rs485_stats_update(unsigned index, bool success)
{
    (void)index;
    (void)success;
}

void rs485_stats_reset(unsigned index)
{
    (void)index;
}
