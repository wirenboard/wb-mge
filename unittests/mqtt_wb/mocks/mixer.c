/* Mock of the mixer engine.
 *
 * The MQTT side reads one snapshot and makes two calls, so that is all there is here: a status a
 * test sets by hand, and a record of what was asked of the engine. The real engine is covered by
 * unittests/mixer_engine. */

#include "mock_mixer.h"

#include <string.h>

mixer_status_t mock_mixer_status;
bool mock_mixer_enabled;
uint8_t mock_mixer_program;

int mock_mixer_set_enabled_called;
bool mock_mixer_set_enabled_value;
esp_err_t mock_mixer_set_enabled_return = ESP_OK;

int mock_mixer_set_program_called;
uint8_t mock_mixer_set_program_value;
esp_err_t mock_mixer_set_program_return = ESP_OK;

void mock_mixer_reset(void)
{
    memset(&mock_mixer_status, 0, sizeof(mock_mixer_status));
    mock_mixer_enabled = false;
    mock_mixer_program = 0;

    mock_mixer_set_enabled_called = 0;
    mock_mixer_set_enabled_value = false;
    mock_mixer_set_enabled_return = ESP_OK;

    mock_mixer_set_program_called = 0;
    mock_mixer_set_program_value = 0;
    mock_mixer_set_program_return = ESP_OK;
}

void mixer_start(void)
{
}

void mixer_get_status(mixer_status_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = mock_mixer_status;
}

esp_err_t mixer_set_enabled(bool on)
{
    mock_mixer_set_enabled_called++;
    mock_mixer_set_enabled_value = on;
    if (mock_mixer_set_enabled_return == ESP_OK) {
        mock_mixer_enabled = on;
    }
    return mock_mixer_set_enabled_return;
}

esp_err_t mixer_set_program(uint8_t index)
{
    mock_mixer_set_program_called++;
    mock_mixer_set_program_value = index;
    if (mock_mixer_set_program_return == ESP_OK) {
        // The setting, which mixer_get_program() answers from, moves at once; the snapshot only
        // moves on the engine's tick - which is exactly the difference the real engine has.
        mock_mixer_program = index;
    }
    return mock_mixer_set_program_return;
}

bool mixer_is_enabled(void)
{
    return mock_mixer_enabled;
}

uint8_t mixer_get_program(void)
{
    return mock_mixer_program;
}

void mixer_settings_changed(void)
{
}
