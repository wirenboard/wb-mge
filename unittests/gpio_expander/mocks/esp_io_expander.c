#include "esp_err.h"
#include "esp_io_expander.h"

esp_err_t mock_esp_io_expander_print_state_return = ESP_OK;
int mock_esp_io_expander_print_state_called = 0;
esp_io_expander_handle_t mock_esp_io_expander_print_state_handle = NULL;

esp_err_t esp_io_expander_print_state(esp_io_expander_handle_t handle)
{
    mock_esp_io_expander_print_state_called++;
    mock_esp_io_expander_print_state_handle = handle;
    return mock_esp_io_expander_print_state_return;
}
