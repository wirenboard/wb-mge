#include "adc_oneshot.h"

int mock_adc_oneshot_new_unit_called = 0;
adc_unit_t mock_adc_oneshot_new_unit_unit_id = 0;
esp_err_t mock_adc_oneshot_new_unit_return_value = ESP_OK;

int mock_adc_oneshot_config_channel_called = 0;
adc_oneshot_unit_handle_t mock_adc_oneshot_config_channel_handle = NULL;
adc_channel_t mock_adc_oneshot_config_channel_channel = 0;
adc_bitwidth_t mock_adc_oneshot_config_channel_bitwidth = 0;
adc_atten_t mock_adc_oneshot_config_channel_atten = 0;
esp_err_t mock_adc_oneshot_config_channel_return_value = ESP_OK;

int mock_adc_oneshot_read_called = 0;
adc_oneshot_unit_handle_t mock_adc_oneshot_read_handle = NULL;
adc_channel_t mock_adc_oneshot_read_channel = 0;
int mock_adc_oneshot_read_out_raw_value = 0;
esp_err_t mock_adc_oneshot_read_return_value = ESP_OK;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit)
{
    mock_adc_oneshot_new_unit_unit_id = init_config->unit_id;

    if (mock_adc_oneshot_new_unit_return_value == ESP_OK && ret_unit != NULL) {
        *ret_unit = (adc_oneshot_unit_handle_t)MOCK_ADC_ONESHOT_HANDLE;
    }

    mock_adc_oneshot_new_unit_called++;
    return mock_adc_oneshot_new_unit_return_value;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel, const adc_oneshot_chan_cfg_t *config)
{
    mock_adc_oneshot_config_channel_handle = handle;
    mock_adc_oneshot_config_channel_channel = channel;
    mock_adc_oneshot_config_channel_bitwidth = config->bitwidth;
    mock_adc_oneshot_config_channel_atten = config->atten;

    mock_adc_oneshot_config_channel_called++;
    return mock_adc_oneshot_config_channel_return_value;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw)
{
    mock_adc_oneshot_read_handle = handle;
    mock_adc_oneshot_read_channel = chan;

    if (mock_adc_oneshot_read_return_value == ESP_OK && out_raw != NULL) {
        *out_raw = mock_adc_oneshot_read_out_raw_value;
    }

    mock_adc_oneshot_read_called++;
    return mock_adc_oneshot_read_return_value;
}

void mock_adc_oneshot_reset(void)
{
    mock_adc_oneshot_new_unit_called = 0;
    mock_adc_oneshot_new_unit_unit_id = 0;
    mock_adc_oneshot_new_unit_return_value = ESP_OK;

    mock_adc_oneshot_config_channel_called = 0;
    mock_adc_oneshot_config_channel_handle = NULL;
    mock_adc_oneshot_config_channel_channel = 0;
    mock_adc_oneshot_config_channel_bitwidth = 0;
    mock_adc_oneshot_config_channel_atten = 0;
    mock_adc_oneshot_config_channel_return_value = ESP_OK;

    mock_adc_oneshot_read_called = 0;
    mock_adc_oneshot_read_handle = NULL;
    mock_adc_oneshot_read_channel = 0;
    mock_adc_oneshot_read_out_raw_value = 0;
    mock_adc_oneshot_read_return_value = ESP_OK;
}
