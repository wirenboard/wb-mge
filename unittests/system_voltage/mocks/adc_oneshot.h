#pragma once

#include <esp_adc/adc_oneshot.h>

#define MOCK_ADC_ONESHOT_HANDLE             0x12345678

extern int mock_adc_oneshot_new_unit_called;
extern adc_unit_t mock_adc_oneshot_new_unit_unit_id;
extern esp_err_t mock_adc_oneshot_new_unit_return_value;

extern int mock_adc_oneshot_config_channel_called;
extern adc_oneshot_unit_handle_t mock_adc_oneshot_config_channel_handle;
extern adc_channel_t mock_adc_oneshot_config_channel_channel;
extern adc_bitwidth_t mock_adc_oneshot_config_channel_bitwidth;
extern adc_atten_t mock_adc_oneshot_config_channel_atten;
extern esp_err_t mock_adc_oneshot_config_channel_return_value;

extern int mock_adc_oneshot_read_called;
extern adc_oneshot_unit_handle_t mock_adc_oneshot_read_handle;
extern adc_channel_t mock_adc_oneshot_read_channel;
extern int mock_adc_oneshot_read_out_raw_value;
extern esp_err_t mock_adc_oneshot_read_return_value;

void mock_adc_oneshot_reset(void);
