#pragma once

#include "esp_err.h"

// Автоматическая инициализация, если таймер не был инициализирован
esp_err_t settings_save_timer_auto_init(void);

// Ожидание заданного времени перед следующей записью настроек
esp_err_t settings_save_timer_wait(void);
