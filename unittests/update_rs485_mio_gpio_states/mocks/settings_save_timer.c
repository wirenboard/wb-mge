#include "settings_save_timer.h"

int mock_settings_save_timer_wait_called = 0;

esp_err_t settings_save_timer_auto_init(void)
{
    return ESP_OK;
}

esp_err_t settings_save_timer_wait(void)
{
    mock_settings_save_timer_wait_called++;
    return ESP_OK;
}

void mock_settings_save_timer_reset(void)
{
    mock_settings_save_timer_wait_called = 0;
}
