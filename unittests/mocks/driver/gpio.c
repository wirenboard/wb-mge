#include "driver/gpio.h"

#include <string.h>

// Private monotonically-increasing call sequence used to verify ordering
// among GPIO calls. Kept self-contained (no shared call_sequence.c) so the
// mock stays standalone-includable.
static unsigned s_gpio_call_seq = 0;

mock_gpio_reset_pin_t     mock_gpio_reset_pin_data = {0};
mock_gpio_set_direction_t mock_gpio_set_direction_data = {0};
mock_gpio_set_level_t     mock_gpio_set_level_data = {0};

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    mock_gpio_reset_pin_data.called++;
    mock_gpio_reset_pin_data.gpio_num = gpio_num;
    mock_gpio_reset_pin_data.call_seq = ++s_gpio_call_seq;
    return ESP_OK;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    mock_gpio_set_direction_data.called++;
    mock_gpio_set_direction_data.gpio_num = gpio_num;
    mock_gpio_set_direction_data.mode = mode;
    mock_gpio_set_direction_data.call_seq = ++s_gpio_call_seq;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level)
{
    mock_gpio_set_level_data.called++;
    mock_gpio_set_level_data.gpio_num = gpio_num;
    mock_gpio_set_level_data.level = level;
    mock_gpio_set_level_data.call_seq = ++s_gpio_call_seq;
    return ESP_OK;
}

void mock_gpio_reset(void)
{
    memset(&mock_gpio_reset_pin_data, 0, sizeof(mock_gpio_reset_pin_data));
    memset(&mock_gpio_set_direction_data, 0, sizeof(mock_gpio_set_direction_data));
    memset(&mock_gpio_set_level_data, 0, sizeof(mock_gpio_set_level_data));
    s_gpio_call_seq = 0;
}
