#include "esp_err.h"
#include "esp_io_expander.h"

esp_err_t mock_esp_io_expander_print_state_return = ESP_OK;
int mock_esp_io_expander_print_state_called = 0;
esp_io_expander_handle_t mock_esp_io_expander_print_state_handle = NULL;

int mock_esp_io_expander_set_dir_called = 0;
esp_io_expander_handle_t mock_esp_io_expander_set_dir_handle = NULL;
uint32_t mock_esp_io_expander_set_dir_pin_mask = 0;
esp_io_expander_dir_t mock_esp_io_expander_set_dir_direction = IO_EXPANDER_INPUT;

uint32_t mock_esp_io_expander_set_dir_pin_masks[MAX_CALLS] = {0};
esp_io_expander_dir_t mock_esp_io_expander_set_dir_directions[MAX_CALLS] = {0};

int mock_esp_io_expander_set_level_called = 0;
esp_io_expander_handle_t mock_esp_io_expander_set_level_handle = NULL;
uint32_t mock_esp_io_expander_set_level_pin_mask = 0;
uint8_t mock_esp_io_expander_set_level_level = 0;

uint32_t mock_esp_io_expander_set_level_pin_masks[MAX_CALLS] = {0};
uint8_t mock_esp_io_expander_set_level_levels[MAX_CALLS] = {0};

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle)
{
    mock_esp_io_expander_print_state_called++;
    mock_esp_io_expander_print_state_handle = handle;
    return mock_esp_io_expander_print_state_return;
}

esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle, uint32_t pin_num_mask, esp_io_expander_dir_t direction)
{
    mock_esp_io_expander_set_dir_handle = handle;
    mock_esp_io_expander_set_dir_pin_mask = pin_num_mask;
    mock_esp_io_expander_set_dir_direction = direction;

    if (mock_esp_io_expander_set_dir_called < MAX_CALLS) {
        mock_esp_io_expander_set_dir_pin_masks[mock_esp_io_expander_set_dir_called] = pin_num_mask;
        mock_esp_io_expander_set_dir_directions[mock_esp_io_expander_set_dir_called] = direction;
    }

    mock_esp_io_expander_set_dir_called++;
    return ESP_OK;
}

esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint8_t level)
{
    mock_esp_io_expander_set_level_handle = handle;
    mock_esp_io_expander_set_level_pin_mask = pin_num_mask;
    mock_esp_io_expander_set_level_level = level;

    if (mock_esp_io_expander_set_level_called < MAX_CALLS) {
        mock_esp_io_expander_set_level_pin_masks[mock_esp_io_expander_set_level_called] = pin_num_mask;
        mock_esp_io_expander_set_level_levels[mock_esp_io_expander_set_level_called] = level;
    }

    mock_esp_io_expander_set_level_called++;
    return ESP_OK;
}
