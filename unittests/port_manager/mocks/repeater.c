// Mock implementation of repeater for the port_manager unit tests.
// repeater_init_port() hands back the same per-port mock serial descriptor that
// the bridge mock uses (mock_serial_desc_instances[]), so the shared serial.c
// mock can map the descriptor back to its port index in serial_deinit() /
// serial_set_rx_timeout(). All functions are simple call-tracking stubs.

#include "unity.h"
#include "repeater.h"
#include "bridge.h"        // BRIDGES_COUNT
#include "bridge_mock.h"   // mock_serial_desc_instances[]
#include "repeater_mock.h"
#include <string.h>

mock_repeater_calls_t mock_repeater_calls[BRIDGES_COUNT] = {0};
bool mock_repeater_init_should_fail = false;

void repeater_init(void) {}

esp_err_t repeater_init_port(unsigned index, serial_config_t *config, serial_desc_t **serial_desc_out)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "repeater_init_port: invalid index");
    TEST_ASSERT_NOT_NULL(serial_desc_out);
    (void)config;
    mock_repeater_calls[index].init_called++;
    if (mock_repeater_init_should_fail) {
        return ESP_FAIL;
    }
    *serial_desc_out = &mock_serial_desc_instances[index];
    return ESP_OK;
}

esp_err_t repeater_deinit_port(unsigned index)
{
    TEST_ASSERT_LESS_THAN_UINT_MESSAGE(BRIDGES_COUNT, index, "repeater_deinit_port: invalid index");
    mock_repeater_calls[index].deinit_called++;
    /* Mirror the real repeater: tear down the underlying serial so the shared
     * serial.c mock records serial_deinit() against the right port index. */
    serial_deinit(&mock_serial_desc_instances[index]);
    return ESP_OK;
}

void repeater_get_stats(repeater_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
}

void repeater_reset_for_test(void)
{
    memset(mock_repeater_calls, 0, sizeof(mock_repeater_calls));
    mock_repeater_init_should_fail = false;
}

void mock_repeater_reset(void)
{
    repeater_reset_for_test();
}
