#pragma once

#include "esp_err.h"

esp_err_t settings_update(void);

/* Prime the MQTT-serial change-detection cache with the current (boot-time)
 * settings, so the first later change is detected. Call once at startup after
 * settings are loaded. No side effects (only seeds the cache). */
void settings_update_prime(void);
