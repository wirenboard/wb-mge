#include "gpio_expander_mock.h"

#include "call_sequence.h"
#include <string.h>

mock_gpio_expander_init_t mock_gpio_expander_init_data = {0};
mock_gpio_expander_set_dir_t mock_gpio_expander_set_dir_data = {0};
mock_gpio_expander_set_level_t mock_gpio_expander_set_level_data = {0};
mock_gpio_expander_set_out_dir_and_level_t mock_gpio_expander_set_out_dir_and_level_data = {0};
mock_gpio_expander_get_level_t mock_gpio_expander_get_level_data = {0};

esp_err_t gpio_expander_init(esp_io_expander_handle_t* handle)
{
    mock_gpio_expander_init_data.called++;
    mock_gpio_expander_init_data.call_seq = call_sequence_get_call_id();

    if (mock_gpio_expander_init_data.should_fail) {
        return ESP_FAIL;
    }

    if (handle != NULL) {
        *handle = MOCK_GPIO_EXPANDER_INIT_HANDLE;
    }

    return ESP_OK;
}

esp_err_t gpio_expander_set_dir(uint32_t pin_num_mask, esp_io_expander_dir_t direction)
{
    if (mock_gpio_expander_set_dir_data.called < MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS) {
        mock_gpio_expander_set_dir_data.masks[mock_gpio_expander_set_dir_data.called] = pin_num_mask;
        mock_gpio_expander_set_dir_data.directions[mock_gpio_expander_set_dir_data.called] = direction;
        mock_gpio_expander_set_dir_data.call_seq[mock_gpio_expander_set_dir_data.called] = call_sequence_get_call_id();
    }
    mock_gpio_expander_set_dir_data.called++;

    if (mock_gpio_expander_set_dir_data.should_fail) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t gpio_expander_set_level(uint32_t pin_num_mask, uint8_t level)
{
    if (mock_gpio_expander_set_level_data.called < MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS) {
        mock_gpio_expander_set_level_data.masks[mock_gpio_expander_set_level_data.called] = pin_num_mask;
        mock_gpio_expander_set_level_data.levels[mock_gpio_expander_set_level_data.called] = level;
        mock_gpio_expander_set_level_data.call_seq[mock_gpio_expander_set_level_data.called] = call_sequence_get_call_id();
    }
    mock_gpio_expander_set_level_data.called++;

    if (mock_gpio_expander_set_level_data.should_fail) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t gpio_expander_set_out_dir_and_level(uint32_t pin_num_mask, uint8_t level)
{
    if (mock_gpio_expander_set_out_dir_and_level_data.called < MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS) {
        mock_gpio_expander_set_out_dir_and_level_data.masks[mock_gpio_expander_set_out_dir_and_level_data.called] = pin_num_mask;
        mock_gpio_expander_set_out_dir_and_level_data.levels[mock_gpio_expander_set_out_dir_and_level_data.called] = level;
        mock_gpio_expander_set_out_dir_and_level_data.call_seq[mock_gpio_expander_set_out_dir_and_level_data.called] =
            call_sequence_get_call_id();
    }
    mock_gpio_expander_set_out_dir_and_level_data.called++;

    if (mock_gpio_expander_set_out_dir_and_level_data.should_fail) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t gpio_expander_get_level(uint32_t pin_num_mask, uint32_t *level_mask)
{
    if (mock_gpio_expander_get_level_data.called < MOCK_GPIO_EXPANDER_MAX_FUNC_CALLS) {
        mock_gpio_expander_get_level_data.masks[mock_gpio_expander_get_level_data.called] = pin_num_mask;
        mock_gpio_expander_get_level_data.call_seq[mock_gpio_expander_get_level_data.called] = call_sequence_get_call_id();
    }
    mock_gpio_expander_get_level_data.called++;

    if (mock_gpio_expander_get_level_data.should_fail) {
        return ESP_FAIL;
    }

    if (level_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *level_mask = mock_gpio_expander_get_level_data.level_setup & pin_num_mask;
    return ESP_OK;
}

void mock_gpio_expander_reset(void)
{
    memset(&mock_gpio_expander_init_data, 0, sizeof(mock_gpio_expander_init_data));
    memset(&mock_gpio_expander_set_dir_data, 0, sizeof(mock_gpio_expander_set_dir_data));
    memset(&mock_gpio_expander_set_level_data, 0, sizeof(mock_gpio_expander_set_level_data));
    memset(&mock_gpio_expander_set_out_dir_and_level_data, 0, sizeof(mock_gpio_expander_set_out_dir_and_level_data));
    memset(&mock_gpio_expander_get_level_data, 0, sizeof(mock_gpio_expander_get_level_data));
}
