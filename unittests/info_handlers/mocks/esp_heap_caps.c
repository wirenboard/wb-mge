/* esp_heap_caps mock for the info_handlers unit test. Used only by
 * info_get_handler (not the function under test); link-only stubs. */

#include "esp_heap_caps.h"

size_t heap_caps_get_total_size(uint32_t caps)        { (void)caps; return 0; }
size_t heap_caps_get_free_size(uint32_t caps)         { (void)caps; return 0; }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { (void)caps; return 0; }
