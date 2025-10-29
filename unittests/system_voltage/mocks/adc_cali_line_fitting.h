#pragma once

#include <esp_adc/adc_cali_scheme.h>

#define MOCK_ADC_CALI_HANDLE            0x87654321

extern int mock_adc_cali_create_scheme_line_fitting_called;
extern adc_unit_t mock_adc_cali_create_scheme_line_fitting_unit_id;
extern adc_atten_t mock_adc_cali_create_scheme_line_fitting_atten;
extern adc_bitwidth_t mock_adc_cali_create_scheme_line_fitting_bitwidth;
extern esp_err_t mock_adc_cali_create_scheme_line_fitting_return_value;

void mock_adc_cali_line_fitting_reset(void);
