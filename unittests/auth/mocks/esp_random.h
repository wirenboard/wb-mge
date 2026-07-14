#pragma once

#include <stdint.h>

/* Deterministic stand-in for the hardware RNG: returns the values queued with
 * mock_esp_random_push(), then falls back to an incrementing counter. */
uint32_t esp_random(void);

void mock_esp_random_reset(void);
void mock_esp_random_push(uint32_t value);
