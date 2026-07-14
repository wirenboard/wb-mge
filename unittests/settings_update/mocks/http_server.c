#include "http_server.h"
#include "call_sequence.h"
// The web UI socket must not be torn down before the delay that lets the response to the
// current POST /settings go out, so the mock records how many delays it had seen by then.
#include "freertos/task.h"

int mock_http_server_init_called = 0;
esp_err_t mock_http_server_init_return_value = ESP_OK;

int mock_http_server_deinit_called = 0;
esp_err_t mock_http_server_deinit_return_value = ESP_OK;

int mock_http_server_check_settings_changed_called = 0;
bool mock_http_server_check_settings_changed_return_value = false;

unsigned mock_http_server_init_call_seq = 0;
unsigned mock_http_server_deinit_call_seq = 0;

int mock_http_server_delays_before_deinit = -1;

esp_err_t http_server_init(void)
{
    if (mock_http_server_init_call_seq == 0) {
        mock_http_server_init_call_seq = call_sequence_get_call_id();
    }
    mock_http_server_init_called++;
    return mock_http_server_init_return_value;
}

esp_err_t http_server_deinit(void)
{
    if (mock_http_server_deinit_call_seq == 0) {
        mock_http_server_deinit_call_seq = call_sequence_get_call_id();
        mock_http_server_delays_before_deinit = mock_vTaskDelay_data.called;
    }
    mock_http_server_deinit_called++;
    return mock_http_server_deinit_return_value;
}

bool http_server_check_settings_changed(void)
{
    mock_http_server_check_settings_changed_called++;
    return mock_http_server_check_settings_changed_return_value;
}

void mock_http_server_reset(void)
{
    mock_http_server_init_called = 0;
    mock_http_server_init_return_value = ESP_OK;

    mock_http_server_deinit_called = 0;
    mock_http_server_deinit_return_value = ESP_OK;

    mock_http_server_check_settings_changed_called = 0;
    mock_http_server_check_settings_changed_return_value = false;

    mock_http_server_init_call_seq = 0;
    mock_http_server_deinit_call_seq = 0;
    mock_http_server_delays_before_deinit = -1;
}
