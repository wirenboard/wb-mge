#include "esp_err.h"
#include "esp_io_expander.h"

esp_err_t mock_esp_io_expander_print_state_return = ESP_OK;
int mock_esp_io_expander_print_state_called = 0;

int mock_esp_io_expander_set_dir_called = 0;
esp_io_expander_handle_t mock_esp_io_expander_set_dir_handle = NULL;
uint32_t mock_esp_io_expander_set_dir_pin_mask = 0;
esp_io_expander_dir_t mock_esp_io_expander_set_dir_direction = IO_EXPANDER_INPUT;

int mock_esp_io_expander_set_level_called = 0;
esp_io_expander_handle_t mock_esp_io_expander_set_level_handle = NULL;
uint32_t mock_esp_io_expander_set_level_pin_mask = 0;
uint8_t mock_esp_io_expander_set_level_level = 0;

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle)
{
    (void)handle;
    mock_esp_io_expander_print_state_called++;
    return mock_esp_io_expander_print_state_return;
}

esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle, uint32_t pin_num_mask, esp_io_expander_dir_t direction)
{
    mock_esp_io_expander_set_dir_called++;
    mock_esp_io_expander_set_dir_handle = handle;
    mock_esp_io_expander_set_dir_pin_mask = pin_num_mask;
    mock_esp_io_expander_set_dir_direction = direction;
    return ESP_OK;
}

esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint8_t level)
{
    mock_esp_io_expander_set_level_called++;
    mock_esp_io_expander_set_level_handle = handle;
    mock_esp_io_expander_set_level_pin_mask = pin_num_mask;
    mock_esp_io_expander_set_level_level = level;
    return ESP_OK;
}
