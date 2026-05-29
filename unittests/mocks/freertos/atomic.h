#pragma once

#include <stdint.h>

/* Mock of FreeRTOS freertos/atomic.h for unit-test builds.
 * On real hardware these are implemented with hardware atomics; here we use
 * GCC built-ins which are sufficient for single-threaded unit tests.
 * Both functions return the OLD value (before the operation), matching the
 * FreeRTOS API contract. */

static inline uint32_t Atomic_Increment_u32(volatile uint32_t *p)
{
    return __atomic_fetch_add(p, 1u, __ATOMIC_SEQ_CST);
}

static inline uint32_t Atomic_Decrement_u32(volatile uint32_t *p)
{
    return __atomic_fetch_sub(p, 1u, __ATOMIC_SEQ_CST);
}
