#pragma once

/* Stub for esp_heap_caps.h in unit test builds.
 * Redirects heap_caps_malloc to test_malloc so that allocation tracking works
 * and the unit tests can control malloc failures via malloc_should_fail. */

#include "malloc.h"

/* Dummy capability flag — not used in the test environment. */
#define MALLOC_CAP_8BIT  0u

/* Redirect heap_caps_malloc to tracked test_malloc so that allocation
 * failures can be injected via malloc_should_fail. */
#define heap_caps_malloc(size, caps)    test_malloc(size)
