#include "http_server.h"
#include "call_sequence.h"
// The web UI socket must not be torn down before the delay that lets the response to the
// current POST /settings go out, so the mock records how many delays it had seen by then.
#include "freertos/task.h"

// Port the mocked server is listening on; 0 = stopped.
static uint16_t mock_running_port = 0;

int mock_http_server_init_called = 0;
esp_err_t mock_http_server_init_return_value = ESP_OK;

int mock_http_server_deinit_called = 0;
esp_err_t mock_http_server_deinit_return_value = ESP_OK;

int mock_http_server_check_settings_changed_called = 0;
bool mock_http_server_check_settings_changed_return_value = false;

uint16_t mock_http_server_configured_port = 0;

uint16_t mock_http_server_init_fail_port = 0;
esp_err_t mock_http_server_init_fail_error = ESP_FAIL;

uint16_t mock_http_server_init_ok_port = 0;

uint16_t mock_http_server_init_ports[MOCK_HTTP_INIT_PORT_LOG_MAX] = {0};
uint16_t mock_http_server_init_last_port = 0;

unsigned mock_http_server_init_call_seq = 0;
unsigned mock_http_server_deinit_call_seq = 0;

int mock_http_server_delays_before_deinit = -1;

esp_err_t http_server_init(void)
{
    // The real http_server_init() takes the port from NVS; the mock takes it from the
    // "configured port" the test set up.
    return http_server_init_port(mock_http_server_configured_port);
}

esp_err_t http_server_init_port(uint16_t port)
{
    if (mock_http_server_init_called < MOCK_HTTP_INIT_PORT_LOG_MAX) {
        mock_http_server_init_ports[mock_http_server_init_called] = port;
    }
    if (mock_http_server_init_call_seq == 0) {
        mock_http_server_init_call_seq = call_sequence_get_call_id();
    }
    mock_http_server_init_called++;
    mock_http_server_init_last_port = port;

    // Per-port injections take precedence over the global error, the "this port works" one over the
    // "this port fails" one.
    if ((mock_http_server_init_ok_port != 0) && (port == mock_http_server_init_ok_port)) {
        mock_running_port = port;
        return ESP_OK;
    }
    if ((mock_http_server_init_fail_port != 0) && (port == mock_http_server_init_fail_port)) {
        return mock_http_server_init_fail_error;
    }
    if (mock_http_server_init_return_value != ESP_OK) {
        return mock_http_server_init_return_value;
    }
    mock_running_port = port;
    return ESP_OK;
}

esp_err_t http_server_deinit(void)
{
    if (mock_http_server_deinit_call_seq == 0) {
        mock_http_server_deinit_call_seq = call_sequence_get_call_id();
        mock_http_server_delays_before_deinit = mock_vTaskDelay_data.called;
    }
    mock_http_server_deinit_called++;

    // The server stops being reachable even when deinit reports a failure: the real
    // http_server_deinit() sets its handle to NULL whatever httpd_stop() returns, so
    // http_server_get_port() answers 0 from then on. The mock must not pretend the old port is
    // still being served — a rollback target that does not exist is exactly the state the
    // production code has to survive.
    mock_running_port = 0;
    return mock_http_server_deinit_return_value;
}

uint16_t http_server_get_port(void)
{
    return mock_running_port;
}

bool http_server_check_settings_changed(void)
{
    mock_http_server_check_settings_changed_called++;
    return mock_http_server_check_settings_changed_return_value;
}

void mock_http_server_reset(void)
{
    mock_running_port = 0;

    mock_http_server_init_called = 0;
    mock_http_server_init_return_value = ESP_OK;

    mock_http_server_deinit_called = 0;
    mock_http_server_deinit_return_value = ESP_OK;

    mock_http_server_check_settings_changed_called = 0;
    mock_http_server_check_settings_changed_return_value = false;

    mock_http_server_configured_port = 0;

    mock_http_server_init_fail_port = 0;
    mock_http_server_init_fail_error = ESP_FAIL;

    mock_http_server_init_ok_port = 0;

    for (int i = 0; i < MOCK_HTTP_INIT_PORT_LOG_MAX; i++) {
        mock_http_server_init_ports[i] = 0;
    }
    mock_http_server_init_last_port = 0;

    mock_http_server_init_call_seq = 0;
    mock_http_server_deinit_call_seq = 0;
    mock_http_server_delays_before_deinit = -1;
}

void mock_http_server_set_running_port(uint16_t port)
{
    mock_running_port = port;
}
