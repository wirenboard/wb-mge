/* Stub for settings_save_timer — there is no flash write to pace in the tests. */

#include "settings_save_timer.h"

esp_err_t settings_save_timer_auto_init(void)
{
    return ESP_OK;
}

esp_err_t settings_save_timer_wait(void)
{
    return ESP_OK;
}
