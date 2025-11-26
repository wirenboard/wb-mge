#pragma once

#include "esp_err.h"

// Automatic initialization if the timer has not been initialized
esp_err_t settings_save_timer_auto_init(void);

// Wait for the specified time before the next settings write
esp_err_t settings_save_timer_wait(void);
