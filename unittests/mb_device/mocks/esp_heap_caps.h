#pragma once

/* Controllable mock for the ESP-IDF heap-capabilities API used by mb_device.c.
 * Only the free/total *size* getters are needed here (no allocation tracking).
 * Tests set the returned sizes via mock_heap_set_sizes(). */

#include <stddef.h>
#include <stdint.h>

/* Capability flag — value is irrelevant, the mock ignores it. */
#define MALLOC_CAP_INTERNAL  (1u << 11)

extern size_t mock_heap_free_size;
extern size_t mock_heap_total_size;

size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);

void mock_heap_set_sizes(size_t total, size_t free);
void mock_heap_reset(void);
