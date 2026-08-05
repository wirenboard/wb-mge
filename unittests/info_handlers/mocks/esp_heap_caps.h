#pragma once

/* Minimal esp_heap_caps.h stub for the info_handlers unit test. info_get_handler
 * reads heap statistics; the function under test (info_build_ap_clients_json)
 * does not, but the whole translation unit must link. Functions are stubbed in
 * mocks/esp_heap_caps.c. */

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_INTERNAL (1 << 11)

size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
