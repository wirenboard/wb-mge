#include "esp_err.h"
#include "esp_io_expander.h"
#include <string.h>
#include "call_sequence.h"

mock_esp_io_expander_print_state_t mock_esp_io_expander_print_state_data = {0};
mock_esp_io_expander_set_dir_t mock_esp_io_expander_set_dir_data = {0};
mock_esp_io_expander_set_level_t mock_esp_io_expander_set_level_data = {0};
mock_esp_io_expander_get_level_t mock_esp_io_expander_get_level_data = {0};
mock_esp_io_expander_del_t mock_esp_io_expander_del_data = {0};

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle)
{
    mock_esp_io_expander_print_state_data.called++;
    mock_esp_io_expander_print_state_data.call_seq = call_sequence_get_call_id();
    mock_esp_io_expander_print_state_data.handle = handle;

    if (mock_esp_io_expander_print_state_data.should_fail) {
        return ESP_FAIL;
    }

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t esp_io_expander_set_dir(esp_io_expander_handle_t handle, uint32_t pin_num_mask, esp_io_expander_dir_t direction)
{
    if (mock_esp_io_expander_set_dir_data.called < MAX_FUNCTION_CALLS) {
        mock_esp_io_expander_set_dir_data.masks[mock_esp_io_expander_set_dir_data.called] = pin_num_mask;
        mock_esp_io_expander_set_dir_data.directions[mock_esp_io_expander_set_dir_data.called] = direction;
    }

    mock_esp_io_expander_set_dir_data.called++;
    mock_esp_io_expander_set_dir_data.call_seq = call_sequence_get_call_id();
    mock_esp_io_expander_set_dir_data.handle = handle;

    if (mock_esp_io_expander_set_dir_data.should_fail) {
        return ESP_FAIL;
    }

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t esp_io_expander_set_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint8_t level)
{
    if (mock_esp_io_expander_set_level_data.called < MAX_FUNCTION_CALLS) {
        mock_esp_io_expander_set_level_data.masks[mock_esp_io_expander_set_level_data.called] = pin_num_mask;
        mock_esp_io_expander_set_level_data.levels[mock_esp_io_expander_set_level_data.called] = level;
    }

    mock_esp_io_expander_set_level_data.called++;
    mock_esp_io_expander_set_level_data.call_seq = call_sequence_get_call_id();
    mock_esp_io_expander_set_level_data.handle = handle;

    if (mock_esp_io_expander_set_level_data.should_fail) {
        return ESP_FAIL;
    }

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t esp_io_expander_get_level(esp_io_expander_handle_t handle, uint32_t pin_num_mask, uint32_t *level_mask)
{
    if (mock_esp_io_expander_get_level_data.called < MAX_FUNCTION_CALLS) {
        mock_esp_io_expander_get_level_data.masks[mock_esp_io_expander_get_level_data.called] = pin_num_mask;
    }

    mock_esp_io_expander_get_level_data.called++;
    mock_esp_io_expander_get_level_data.call_seq = call_sequence_get_call_id();
    mock_esp_io_expander_get_level_data.handle = handle;

    if (mock_esp_io_expander_get_level_data.should_fail) {
        return ESP_FAIL;
    }

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (level_mask == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *level_mask = mock_esp_io_expander_get_level_data.levels_setup & pin_num_mask;
    return ESP_OK;
}

esp_err_t esp_io_expander_del(esp_io_expander_handle_t handle)
{
    mock_esp_io_expander_del_data.called++;
    mock_esp_io_expander_del_data.call_seq = call_sequence_get_call_id();
    mock_esp_io_expander_del_data.handle = handle;

    if (mock_esp_io_expander_del_data.should_fail) {
        return ESP_FAIL;
    }

    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void mock_esp_io_expander_reset(void)
{
    memset(&mock_esp_io_expander_print_state_data, 0, sizeof(mock_esp_io_expander_print_state_data));
    memset(&mock_esp_io_expander_set_dir_data, 0, sizeof(mock_esp_io_expander_set_dir_data));
    memset(&mock_esp_io_expander_set_level_data, 0, sizeof(mock_esp_io_expander_set_level_data));
    memset(&mock_esp_io_expander_get_level_data, 0, sizeof(mock_esp_io_expander_get_level_data));
    memset(&mock_esp_io_expander_del_data, 0, sizeof(mock_esp_io_expander_del_data));
}
