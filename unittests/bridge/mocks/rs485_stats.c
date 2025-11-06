#include "unity.h"
#include "rs485_stats.h"
#include <string.h>

mock_rs485_stats_t mock_rs485_stats;

void rs485_busy_monitor_init(void)
{
    mock_rs485_stats.busy_monitor_init_called++;
}

void rs485_busy_monitor_reset(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "rs485_busy_monitor_reset called with invalid index");

    mock_rs485_stats.busy_monitor_reset_called[index]++;
}

void rs485_stats_init(void)
{
    mock_rs485_stats.stats_init_called++;
}

void rs485_stats_reset(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "rs485_stats_reset called with invalid index");

    mock_rs485_stats.stats_reset_called[index]++;
}

void mock_rs485_stats_reset(void)
{
    memset(&mock_rs485_stats, 0, sizeof(mock_rs485_stats));
}
