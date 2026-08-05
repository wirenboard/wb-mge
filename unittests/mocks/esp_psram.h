#pragma once

#include <stdbool.h>
#include <stddef.h>

// Mock control variables (writable from tests)
extern bool mock_esp_psram_is_initialized_return;  // default: false
extern size_t mock_esp_psram_get_size_return;       // default: 0

bool esp_psram_is_initialized(void);
size_t esp_psram_get_size(void);

void mock_esp_psram_reset(void);
