#include "adc_cali_line_fitting.h"

int mock_adc_cali_create_scheme_line_fitting_called = 0;
adc_unit_t mock_adc_cali_create_scheme_line_fitting_unit_id = 0;
adc_atten_t mock_adc_cali_create_scheme_line_fitting_atten = 0;
adc_bitwidth_t mock_adc_cali_create_scheme_line_fitting_bitwidth = 0;
esp_err_t mock_adc_cali_create_scheme_line_fitting_return_value = ESP_OK;

esp_err_t adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *config, adc_cali_handle_t *ret_handle)
{
    mock_adc_cali_create_scheme_line_fitting_unit_id = config->unit_id;
    mock_adc_cali_create_scheme_line_fitting_atten = config->atten;
    mock_adc_cali_create_scheme_line_fitting_bitwidth = config->bitwidth;

    if (mock_adc_cali_create_scheme_line_fitting_return_value == ESP_OK && ret_handle != NULL) {
        *ret_handle = (adc_cali_handle_t)MOCK_ADC_CALI_HANDLE;
    }

    mock_adc_cali_create_scheme_line_fitting_called++;
    return mock_adc_cali_create_scheme_line_fitting_return_value;
}

void mock_adc_cali_line_fitting_reset(void)
{
    mock_adc_cali_create_scheme_line_fitting_called = 0;
    mock_adc_cali_create_scheme_line_fitting_unit_id = 0;
    mock_adc_cali_create_scheme_line_fitting_atten = 0;
    mock_adc_cali_create_scheme_line_fitting_bitwidth = 0;
    mock_adc_cali_create_scheme_line_fitting_return_value = ESP_OK;
}
