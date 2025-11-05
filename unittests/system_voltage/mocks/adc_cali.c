#include "adc_cali.h"

#define MOCK_ADC_CALI_COEFF             1.05f

int mock_adc_cali_raw_to_voltage_called = 0;
adc_cali_handle_t mock_adc_cali_raw_to_voltage_handle = NULL;
int mock_adc_cali_raw_to_voltage_raw_value = 0;
esp_err_t mock_adc_cali_raw_to_voltage_return_value = ESP_OK;

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage)
{
    mock_adc_cali_raw_to_voltage_handle = handle;
    mock_adc_cali_raw_to_voltage_raw_value = raw;

    if (mock_adc_cali_raw_to_voltage_return_value == ESP_OK && voltage != NULL) {
        *voltage = (int)(raw * MOCK_ADC_CALI_COEFF);
    }

    mock_adc_cali_raw_to_voltage_called++;
    return mock_adc_cali_raw_to_voltage_return_value;
}

void mock_adc_cali_reset(void)
{
    mock_adc_cali_raw_to_voltage_called = 0;
    mock_adc_cali_raw_to_voltage_handle = NULL;
    mock_adc_cali_raw_to_voltage_raw_value = 0;
    mock_adc_cali_raw_to_voltage_return_value = ESP_OK;
}
