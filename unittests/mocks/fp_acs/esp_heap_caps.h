#pragma once

/*
 * esp_heap_caps.h for the fingerprint access control suites: plain malloc/calloc/free underneath,
 * with a way to make the next allocations fail (no PSRAM, no memory).
 */

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_SPIRAM           (1 << 10)
#define MALLOC_CAP_INTERNAL         (1 << 11)
#define MALLOC_CAP_DEFAULT          (1 << 12)

void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_malloc_prefer(size_t size, size_t num, ...);
void  heap_caps_free(void *ptr);

// How many of the next allocations (any of the three calls) return NULL.
extern int mock_heap_caps_fail_count;
// Allocations asked for since the last reset, failed ones included.
extern int mock_heap_caps_alloc_calls;
// Caps of the last heap_caps_malloc()/heap_caps_calloc(), the first caps of heap_caps_malloc_prefer().
extern uint32_t mock_heap_caps_last_caps;

void mock_heap_caps_reset(void);
