#include "system_voltage.h"

#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

static const char *TAG = "system_voltage";

#define VOLTAGE_DIVIDER_R1     33000.0f  // 33k ohm
#define VOLTAGE_DIVIDER_R2     3300.0f   // 3.3k ohm
#define VOLTAGE_ADC_CHANNEL    ADC_CHANNEL_7  // GPIO35
#define VOLTAGE_ADC_UNIT       ADC_UNIT_1
#define VOLTAGE_ADC_ATTEN      ADC_ATTEN_DB_12
#define VOLTAGE_ADC_BITWIDTH   ADC_BITWIDTH_12
#define VOLTAGE_ADC_REF_MV     3300.0f   // Reference voltage in mV for 11dB attenuation
#define VOLTAGE_ADC_MAX_VALUE  ((1 << 12) - 1)  // 4095 for 12-bit ADC

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc1_cali_handle = NULL;
static bool adc_calibration_init = false;

// Convert ADC raw reading to voltage in mV using linear conversion
static inline float adc_raw_to_voltage_mv(int adc_reading)
{
    return (adc_reading * VOLTAGE_ADC_REF_MV) / VOLTAGE_ADC_MAX_VALUE;
}

esp_err_t system_voltage_init(void)
{
    if (adc1_handle != NULL) {
        return ESP_OK;  // Already initialized
    }

    // Initialize ADC1
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = VOLTAGE_ADC_UNIT,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure ADC1 channel
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = VOLTAGE_ADC_BITWIDTH,
        .atten = VOLTAGE_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(adc1_handle, VOLTAGE_ADC_CHANNEL, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize calibration
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = VOLTAGE_ADC_UNIT,
        .atten = VOLTAGE_ADC_ATTEN,
        .bitwidth = VOLTAGE_ADC_BITWIDTH,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, &adc1_cali_handle);
    if (ret == ESP_OK) {
        adc_calibration_init = true;
        ESP_LOGI(TAG, "ADC calibration scheme initialized");
    } else {
        ESP_LOGW(TAG, "ADC calibration init failed: %s", esp_err_to_name(ret));
        adc_calibration_init = false;
    }

    ESP_LOGI(TAG, "System voltage monitoring initialized on GPIO35");
    return ESP_OK;
}

float system_voltage_read(void)
{
    if (adc1_handle == NULL) {
        ESP_LOGE(TAG, "System voltage not initialized, call system_voltage_init() first");
        return 0.0;
    }

    int adc_reading = 0;
    esp_err_t ret = adc_oneshot_read(adc1_handle, VOLTAGE_ADC_CHANNEL, &adc_reading);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC read failed: %s", esp_err_to_name(ret));
        return 0.0;
    }

    float voltage_mv = 0.0;
    if (adc_calibration_init) {
        // Convert raw reading to voltage in mV using calibration
        int voltage_mv_int = 0;
        ret = adc_cali_raw_to_voltage(adc1_cali_handle, adc_reading, &voltage_mv_int);
        if (ret == ESP_OK) {
            voltage_mv = (float)voltage_mv_int;
        } else {
            ESP_LOGW(TAG, "ADC calibration failed, using linear conversion");
            voltage_mv = adc_raw_to_voltage_mv(adc_reading);
        }
    } else {
        // Linear conversion fallback
        voltage_mv = adc_raw_to_voltage_mv(adc_reading);
    }

    // Apply voltage divider formula: Vin = Vout * (R1 + R2) / R2
    float system_voltage = voltage_mv * (VOLTAGE_DIVIDER_R1 + VOLTAGE_DIVIDER_R2) / VOLTAGE_DIVIDER_R2 / 1000.0;

    ESP_LOGD(TAG, "ADC raw: %d, ADC voltage: %.1f mV, System voltage: %.2f V",
             adc_reading, voltage_mv, system_voltage);

    return system_voltage;
}

#ifdef __unittest_env__
    void system_voltage_reset(void)
    {
        adc1_handle = NULL;
        adc1_cali_handle = NULL;
        adc_calibration_init = false;
    }
#endif
