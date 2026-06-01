#include "cache_multimaster.h"
#include <stdint.h>

/* ---- Mock state ---------------------------------------------------------- */

/* Fields returned by cache_multimaster_get_stats(); tests set them via
 * mock_cache_stats_set(). */
static cache_multimaster_stats_t mock_stats = {0};

/* ---- Mock implementations ------------------------------------------------ */

void cache_multimaster_get_stats(cache_multimaster_stats_t *out)
{
    if (out != NULL) {
        *out = mock_stats;
    }
}

/* ---- Test helpers -------------------------------------------------------- */

void mock_cache_stats_set(uint32_t packets_processed, uint32_t last_packet_age_s,
                          uint32_t map_age_s, uint16_t devices_on_bus)
{
    mock_stats.packets_processed = packets_processed;
    mock_stats.last_packet_age_s = last_packet_age_s;
    mock_stats.map_age_s         = map_age_s;
    mock_stats.devices_on_bus    = devices_on_bus;
}

void mock_cache_stats_reset(void)
{
    mock_stats.packets_processed = 0;
    mock_stats.last_packet_age_s = 0;
    mock_stats.map_age_s         = 0;
    mock_stats.devices_on_bus    = 0;
}
