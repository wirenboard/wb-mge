#include "esp_heap_caps.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

int      mock_heap_caps_fail_count = 0;
int      mock_heap_caps_alloc_calls = 0;
uint32_t mock_heap_caps_last_caps = 0;

static bool mock_heap_caps_should_fail(uint32_t caps)
{
    mock_heap_caps_alloc_calls++;
    mock_heap_caps_last_caps = caps;
    if (mock_heap_caps_fail_count > 0) {
        mock_heap_caps_fail_count--;
        return true;
    }
    return false;
}

void *heap_caps_malloc(size_t size, uint32_t caps)
{
    return mock_heap_caps_should_fail(caps) ? NULL : malloc(size);
}

void *heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
    return mock_heap_caps_should_fail(caps) ? NULL : calloc(n, size);
}

void *heap_caps_malloc_prefer(size_t size, size_t num, ...)
{
    uint32_t first_caps = 0;
    if (num > 0) {
        va_list args;
        va_start(args, num);
        first_caps = va_arg(args, uint32_t);
        va_end(args);
    }
    return mock_heap_caps_should_fail(first_caps) ? NULL : malloc(size);
}

void heap_caps_free(void *ptr)
{
    free(ptr);
}

void mock_heap_caps_reset(void)
{
    mock_heap_caps_fail_count = 0;
    mock_heap_caps_alloc_calls = 0;
    mock_heap_caps_last_caps = 0;
}
