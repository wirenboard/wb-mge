#include "esp_timer.h"

/* Current mock return value — tests set this before calling code under test. */
uint64_t mock_esp_timer_get_time_value = 0;

int64_t esp_timer_get_time(void)
{
    return (int64_t)mock_esp_timer_get_time_value;
}

/* Reset the mock to its default state (return value 0). */
void mock_esp_timer_reset(void)
{
    mock_esp_timer_get_time_value = 0;
}
