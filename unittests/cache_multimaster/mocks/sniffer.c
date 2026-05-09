#include "sniffer.h"

/* Track calls to sniffer_set_cache_active for test assertions. */
int  mock_sniffer_set_cache_active_called     = 0;
bool mock_sniffer_set_cache_active_last_value = false;

void sniffer_set_cache_active(bool active)
{
    mock_sniffer_set_cache_active_called++;
    mock_sniffer_set_cache_active_last_value = active;
}

/* Reset sniffer mock state between tests. */
void mock_sniffer_reset(void)
{
    mock_sniffer_set_cache_active_called     = 0;
    mock_sniffer_set_cache_active_last_value = false;
}
