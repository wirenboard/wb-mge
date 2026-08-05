#include "esp_heap_caps.h"

/* Defaults chosen so the unclamped path is exercised unless a test overrides. */
size_t mock_heap_free_size  = 0;
size_t mock_heap_total_size = 0;

size_t heap_caps_get_free_size(uint32_t caps)
{
    (void)caps;
    return mock_heap_free_size;
}

size_t heap_caps_get_total_size(uint32_t caps)
{
    (void)caps;
    return mock_heap_total_size;
}

void mock_heap_set_sizes(size_t total, size_t free)
{
    mock_heap_total_size = total;
    mock_heap_free_size  = free;
}

void mock_heap_reset(void)
{
    mock_heap_free_size  = 0;
    mock_heap_total_size = 0;
}
