#pragma once

#include <esp_adc/adc_cali.h>

extern int mock_adc_cali_raw_to_voltage_called;
extern adc_cali_handle_t mock_adc_cali_raw_to_voltage_handle;
extern int mock_adc_cali_raw_to_voltage_raw_value;
extern esp_err_t mock_adc_cali_raw_to_voltage_return_value;

void mock_adc_cali_reset(void);
