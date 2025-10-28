#include "unity.h"
#include "console_log.h"

#include "system_voltage.h"
#include "adc_oneshot.h"
#include "adc_cali.h"
#include "adc_cali_line_fitting.h"

#define VOLTAGE_DIVIDER_R1              33000.0f
#define VOLTAGE_DIVIDER_R2              3300.0f
#define VOLTAGE_ADC_CHANNEL             ADC_CHANNEL_7
#define VOLTAGE_ADC_UNIT                ADC_UNIT_1
#define VOLTAGE_ADC_ATTEN               ADC_ATTEN_DB_12
#define VOLTAGE_ADC_BITWIDTH            ADC_BITWIDTH_12
#define VOLTAGE_ADC_REF_MV              3300.0f
#define VOLTAGE_ADC_MAX_VALUE           ((1 << 12) - 1)

#define VOLTAGE_ADC_CHANNEL_NUM         7   // ADC_CHANNEL_7
#define VOLTAGE_ADC_UNIT_NUM            0   // ADC_UNIT_1
#define VOLTAGE_ADC_ATTEN_NUM           3   // ADC_ATTEN_DB_12
#define VOLTAGE_ADC_BITWIDTH_NUM        12  // ADC_BITWIDTH_12

void system_voltage_reset(void);

void setUp(void)
{
    system_voltage_reset();
    mock_adc_oneshot_reset();
    mock_adc_cali_reset();
    mock_adc_cali_line_fitting_reset();
}

void tearDown(void)
{

}

static void verify_adc_unit_init(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_adc_oneshot_new_unit_called, "ADC unit init should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(VOLTAGE_ADC_UNIT_NUM, mock_adc_oneshot_new_unit_unit_id, "ADC unit ID mismatch");
}

static void verify_adc_channel_config(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_adc_oneshot_config_channel_called, "ADC channel config should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE((void *)MOCK_ADC_ONESHOT_HANDLE, mock_adc_oneshot_config_channel_handle, "ADC handle mismatch in config");
    TEST_ASSERT_EQUAL_MESSAGE(VOLTAGE_ADC_CHANNEL_NUM, mock_adc_oneshot_config_channel_channel, "ADC channel mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(VOLTAGE_ADC_BITWIDTH_NUM, mock_adc_oneshot_config_channel_bitwidth, "ADC bitwidth mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(VOLTAGE_ADC_ATTEN_NUM, mock_adc_oneshot_config_channel_atten, "ADC attenuation mismatch");
}

static void verify_calibration_config(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        1, mock_adc_cali_create_scheme_line_fitting_called, "Calibration init should be called once"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        VOLTAGE_ADC_UNIT_NUM, mock_adc_cali_create_scheme_line_fitting_unit_id, "Calibration unit ID mismatch"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        VOLTAGE_ADC_ATTEN_NUM, mock_adc_cali_create_scheme_line_fitting_atten, "Calibration attenuation mismatch"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        VOLTAGE_ADC_BITWIDTH_NUM, mock_adc_cali_create_scheme_line_fitting_bitwidth, "Calibration bitwidth mismatch"
    );
}

static void verify_adc_read(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_adc_oneshot_read_called, "ADC read should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        (void *)MOCK_ADC_ONESHOT_HANDLE,
        mock_adc_oneshot_read_handle,
        "ADC handle mismatch in read"
    );
    TEST_ASSERT_EQUAL_MESSAGE(VOLTAGE_ADC_CHANNEL_NUM, mock_adc_oneshot_read_channel, "ADC channel mismatch");
}

// Тестируем случай успешной инициализации system_voltage_init с калибровкой
void test_system_voltage_init_success_with_calibration(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_init - success with calibration");
    LOG_MESSAGE();

    esp_err_t ret = system_voltage_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "system_voltage_init should return ESP_OK");

    verify_adc_unit_init();
    verify_adc_channel_config();
    verify_calibration_config();
}

// Тестируем случай успешной инициализации system_voltage_init без калибровки
void test_system_voltage_init_success_without_calibration(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_init - success without calibration");
    LOG_MESSAGE();

    mock_adc_cali_create_scheme_line_fitting_return_value = ESP_FAIL;

    esp_err_t ret = system_voltage_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "system_voltage_init should return ESP_OK even without calibration");

    verify_adc_unit_init();
    verify_adc_channel_config();
    verify_calibration_config();
}

// Тестируем повторную инициализацию system_voltage_init
void test_system_voltage_init_already_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_init - already initialized");
    LOG_MESSAGE();

    esp_err_t ret = system_voltage_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "First init should return ESP_OK");

    verify_adc_unit_init();
    verify_adc_channel_config();
    verify_calibration_config();

    ret = system_voltage_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, ret, "Second init should return ESP_OK (idempotent)");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_adc_oneshot_new_unit_called, "ADC unit init should only be called once");
    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_adc_oneshot_config_channel_called,
        "ADC channel config should only be called once"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        1,
        mock_adc_cali_create_scheme_line_fitting_called,
        "Calibration init should only be called once"
    );
}

// Тестируем ошибки инициализации system_voltage_init
void test_system_voltage_init_adc_unit_init_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_init - ADC unit init failure");
    LOG_MESSAGE();

    mock_adc_oneshot_new_unit_return_value = ESP_FAIL;

    esp_err_t ret = system_voltage_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret, "system_voltage_init should return ESP_FAIL when ADC unit init fails");

    verify_adc_unit_init();

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_adc_oneshot_config_channel_called,
        "ADC channel config should not be called after unit init failure"
    );
    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_adc_cali_create_scheme_line_fitting_called,
        "Calibration init should not be called after unit init failure"
    );
}

void test_system_voltage_init_adc_channel_config_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_init - ADC channel config failure");
    LOG_MESSAGE();

    mock_adc_oneshot_config_channel_return_value = ESP_FAIL;

    esp_err_t ret = system_voltage_init();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, ret, "system_voltage_init should return ESP_FAIL when channel config fails");

    verify_adc_unit_init();
    verify_adc_channel_config();

    TEST_ASSERT_EQUAL_MESSAGE(
        0,
        mock_adc_cali_create_scheme_line_fitting_called,
        "Calibration init should not be called after channel config failure"
    );
}

// Тестируем успешное чтение напряжения с калибровкой
void test_system_voltage_read_success_with_calibration(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_read - success with calibration");
    LOG_MESSAGE();

    system_voltage_init();

    mock_adc_oneshot_read_out_raw_value = 2000;

    float voltage = system_voltage_read();
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(23.10f, voltage, "Voltage calculation with calibration incorrect");

    verify_adc_read();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_adc_cali_raw_to_voltage_called, "Calibration conversion should be called");
    TEST_ASSERT_EQUAL_PTR_MESSAGE((void *)MOCK_ADC_CALI_HANDLE, mock_adc_cali_raw_to_voltage_handle, "Calibration handle mismatch");
    TEST_ASSERT_EQUAL_MESSAGE(
        mock_adc_oneshot_read_out_raw_value,
        mock_adc_cali_raw_to_voltage_raw_value,
        "Raw ADC value passed to calibration incorrect"
    );
}

// Тестируем чтение напряжения без калибровки
void test_system_voltage_read_success_without_calibration(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_read - success without calibration");
    LOG_MESSAGE();

    mock_adc_cali_create_scheme_line_fitting_return_value = ESP_FAIL;

    system_voltage_init();

    mock_adc_oneshot_read_out_raw_value = 2000;

    float voltage = system_voltage_read();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01, 17.73f, voltage, "Voltage calculation without calibration incorrect");

    verify_adc_read();

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_adc_cali_raw_to_voltage_called, "Calibration should not be used when not initialized");
}

// Тестируем чтение напряжения с ошибкой калибровки
void test_system_voltage_read_calibration_failure_fallback(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_read - calibration failure fallback");
    LOG_MESSAGE();

    system_voltage_init();

    mock_adc_oneshot_read_out_raw_value = 2000;
    mock_adc_cali_raw_to_voltage_return_value = ESP_FAIL;

    float voltage = system_voltage_read();
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(
        0.01, 17.73f, voltage, "Voltage calculation with calibration fallback incorrect"
    );

    verify_adc_read();

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_adc_cali_raw_to_voltage_called, "Calibration conversion should be attempted");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        (void *)MOCK_ADC_CALI_HANDLE,
        mock_adc_cali_raw_to_voltage_handle,
        "Calibration handle mismatch"
    );
}

// Тестируем чтение напряжения без инициализации
void test_system_voltage_read_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_read - not initialized");
    LOG_MESSAGE();

    float voltage = system_voltage_read();
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, voltage, "Read without init should return 0.0");

    TEST_ASSERT_EQUAL_MESSAGE(0, mock_adc_oneshot_read_called, "ADC read should not be called when not initialized");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_adc_cali_raw_to_voltage_called, "Calibration conversion should not be attempted");
}

// Тестируем чтение напряжения с ошибкой чтения ADC
void test_system_voltage_read_adc_read_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_read - ADC read failure");
    LOG_MESSAGE();

    system_voltage_init();

    mock_adc_oneshot_read_return_value = ESP_FAIL;

    float voltage = system_voltage_read();
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, voltage, "Read failure should return 0.0");

    verify_adc_read();

    TEST_ASSERT_EQUAL_MESSAGE(
        0, mock_adc_cali_raw_to_voltage_called, "Calibration should not be called after ADC read failure"
    );
}

// Тестируем чтение напряжения с нулевым значением ADC
void test_system_voltage_read_zero_adc_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test system_voltage_read - zero ADC value");
    LOG_MESSAGE();

    system_voltage_init();

    mock_adc_oneshot_read_out_raw_value = 0;

    float voltage = system_voltage_read();
    TEST_ASSERT_EQUAL_FLOAT_MESSAGE(0.0f, voltage, "Zero ADC value should give 0.0V");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_system_voltage_init_success_with_calibration);
    RUN_TEST(test_system_voltage_init_success_without_calibration);
    RUN_TEST(test_system_voltage_init_already_initialized);
    RUN_TEST(test_system_voltage_init_adc_unit_init_failure);
    RUN_TEST(test_system_voltage_init_adc_channel_config_failure);

    RUN_TEST(test_system_voltage_read_success_with_calibration);
    RUN_TEST(test_system_voltage_read_success_without_calibration);
    RUN_TEST(test_system_voltage_read_calibration_failure_fallback);
    RUN_TEST(test_system_voltage_read_not_initialized);
    RUN_TEST(test_system_voltage_read_adc_read_failure);

    RUN_TEST(test_system_voltage_read_zero_adc_value);

    return UNITY_END();
}
