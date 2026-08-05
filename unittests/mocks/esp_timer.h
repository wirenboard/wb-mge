#pragma once
#include <stdint.h>

/* Controllable mock for esp_timer_get_time().
 * Set mock_esp_timer_get_time_value before calling the code under test to
 * control the value returned by esp_timer_get_time(). */
extern uint64_t mock_esp_timer_get_time_value;

int64_t esp_timer_get_time(void);
void    mock_esp_timer_reset(void);
