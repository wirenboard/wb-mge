#include "unity.h"
#include "console_log.h"

#include "rs485_control.h"
#include "gpio_expander_mock.h"
#include "freertos/semphr.h"
#include "array_size.h"

#include <stdbool.h>
#include <string.h>

#define TERM485_1_PIN                   IO_EXPANDER_PIN_NUM_0
#define TERM485_2_PIN                   IO_EXPANDER_PIN_NUM_1
#define FAILSAFE_485_1_PIN              IO_EXPANDER_PIN_NUM_2
#define FAILSAFE_485_2_PIN              IO_EXPANDER_PIN_NUM_3
#define VOUT_485_PIN                    IO_EXPANDER_PIN_NUM_6

void rs485_control_test_reset(void);

void setUp(void)
{
    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();
    rs485_control_test_reset();
}

void tearDown(void)
{

}

static const char* bool_to_str(bool value)
{
    if (value) {
        return "true";
    } else {
        return "false";
    }
}

bool find_gpio_expander_set_out_dir_and_level_call(uint32_t pin_num_mask, uint8_t level)
{
    for (int i = 0; i < mock_gpio_expander_set_out_dir_and_level_data.called; i++) {
        if (mock_gpio_expander_set_out_dir_and_level_data.masks[i] != pin_num_mask) {
            continue;
        }
        if (mock_gpio_expander_set_out_dir_and_level_data.levels[i] != level) {
            continue;
        }
        return true;
    }
    return false;
}

bool find_gpio_expander_set_level_call(uint32_t pin_num_mask, uint8_t level)
{
    for (int i = 0; i < mock_gpio_expander_set_level_data.called; i++) {
        if (mock_gpio_expander_set_level_data.masks[i] != pin_num_mask) {
            continue;
        }
        if (mock_gpio_expander_set_level_data.levels[i] != level) {
            continue;
        }
        return true;
    }
    return false;
}

void validate_rs485_control_init_run(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        5,
        mock_gpio_expander_set_out_dir_and_level_data.called,
        "gpio_expander_set_out_dir_and_level() should be called 5 times"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_out_dir_and_level_call(TERM485_1_PIN, 0),
        "gpio_expander_set_out_dir_and_level() should be called with pin_num_mask = TERM485_1_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_out_dir_and_level_call(TERM485_2_PIN, 0),
        "gpio_expander_set_out_dir_and_level() should be called with pin_num_mask = TERM485_2_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_out_dir_and_level_call(FAILSAFE_485_1_PIN, 0),
        "gpio_expander_set_out_dir_and_level() should be called with pin_num_mask = FAILSAFE_485_1_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_out_dir_and_level_call(FAILSAFE_485_2_PIN, 0),
        "gpio_expander_set_out_dir_and_level() should be called with pin_num_mask = FAILSAFE_485_2_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_out_dir_and_level_call(VOUT_485_PIN, 0),
        "gpio_expander_set_out_dir_and_level() should be called with pin_num_mask = VOUT_485_PIN and level = 0"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        5,
        mock_gpio_expander_set_level_data.called,
        "gpio_expander_set_level() should be called 5 times"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_level_call(TERM485_1_PIN, 0),
        "gpio_expander_set_level() should be called with pin_num_mask = TERM485_1_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_level_call(TERM485_2_PIN, 0),
        "gpio_expander_set_level() should be called with pin_num_mask = TERM485_2_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_level_call(FAILSAFE_485_1_PIN, 0),
        "gpio_expander_set_level() should be called with pin_num_mask = FAILSAFE_485_1_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_level_call(FAILSAFE_485_2_PIN, 0),
        "gpio_expander_set_level() should be called with pin_num_mask = FAILSAFE_485_2_PIN and level = 0"
    );
    TEST_ASSERT_TRUE_MESSAGE(
        find_gpio_expander_set_level_call(VOUT_485_PIN, 0),
        "gpio_expander_set_level() should be called with pin_num_mask = VOUT_485_PIN and level = 0"
    );

    // rs485_bus_vout_on_off() specific calls
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreTake_called, "xSemaphoreTake() must be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreGive_called, "xSemaphoreGive() must be called once");

    // Unnecessary calls
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_init_data.called, "gpio_expander_init() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_dir_data.called, "gpio_expander_set_dir() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_get_level_data.called, "gpio_expander_get_level() should NOT be called");
}

// Тестируем успешную инициализацию rs485_control_init()
void test_rs485_control_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init() - success case");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should be called once");
    validate_rs485_control_init_run();
}

// Тестируем rs485_control_init() с ошибкой xSemaphoreCreateMutex()
void test_rs485_control_init_mutex_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init() - xSemaphoreCreateMutex() fail");
    LOG_MESSAGE();

    mock_xSemaphoreCreateMutex_return_value = NULL;

    esp_err_t result = rs485_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_control_init() should return ESP_FAIL");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete() should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_init_data.called, "gpio_expander_init() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_dir_data.called, "gpio_expander_set_dir() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_level_data.called, "gpio_expander_set_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_out_dir_and_level_data.called, "gpio_expander_set_out_dir_and_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_get_level_data.called, "gpio_expander_get_level() should NOT be called");
}

// Тестируем rs485_control_init() с ошибкой gpio_expander_set_out_dir_and_level()
void test_rs485_control_init_set_out_dir_and_level_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init() - gpio_expander_set_out_dir_and_level() fail");
    LOG_MESSAGE();

    mock_gpio_expander_set_out_dir_and_level_data.should_fail = true;

    esp_err_t result = rs485_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_control_init() should return ESP_FAIL");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should be called once");
    validate_rs485_control_init_run();
}

// Тестируем rs485_control_init() с ошибкой gpio_expander_set_level()
void test_rs485_control_init_set_level_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init() - gpio_expander_set_level() fail");
    LOG_MESSAGE();

    mock_gpio_expander_set_level_data.should_fail = true;

    esp_err_t result = rs485_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_control_init() should return ESP_FAIL");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should be called once");
    validate_rs485_control_init_run();
}

// Тестируем повторный вызов rs485_control_init()
void test_rs485_control_init_repeat_call(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init() - repeat call");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should be called once");
    validate_rs485_control_init_run();

    // Repeat call
    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    result = rs485_control_init();

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should NOT be called");
    validate_rs485_control_init_run();
}

// Валидация работы rs485_term_on_off() и rs485_pupd_on_off()
void validate_rs485_term_pupd_on_off_run(uint32_t pin_num_mask, bool on)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_gpio_expander_set_level_data.called, "gpio_expander_set_level() should be called once");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        pin_num_mask,
        mock_gpio_expander_set_level_data.masks[0],
        "gpio_expander_set_level() should be called with provided pin_num_mask value"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        (uint8_t)on,
        mock_gpio_expander_set_level_data.levels[0],
        "gpio_expander_set_level() should be called with provided level value"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete() should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_init_data.called, "gpio_expander_init() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_dir_data.called, "gpio_expander_set_dir() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_out_dir_and_level_data.called, "gpio_expander_set_out_dir_and_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_get_level_data.called, "gpio_expander_get_level() should NOT be called");
}

// Тестируем успешное выполнение rs485_term_on_off()
void test_rs485_term_on_off_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_term_on_off() - success case");
    LOG_MESSAGE();

    static struct {
        rs485_port_t port;
        bool on;
        uint32_t pin_num_mask;
    } test_data[] = {
        {.port = RS485_2,   .on = true,     .pin_num_mask = TERM485_2_PIN},
        {.port = RS485_1,   .on = true,     .pin_num_mask = TERM485_1_PIN},
        {.port = RS485_2,   .on = false,    .pin_num_mask = TERM485_2_PIN},
        {.port = RS485_1,   .on = false,    .pin_num_mask = TERM485_1_PIN},
    };

    for (unsigned i = 0; i < ARRAY_SIZE(test_data); i++) {
        setUp();
        LOG_INFO(
            "Testing with port = %s, on = %s",
            (test_data[i].port == RS485_1) ? "RS485_1" : "RS485_2",
            bool_to_str(test_data[i].on)
         );

         esp_err_t result = rs485_term_on_off(test_data[i].port, test_data[i].on);

         TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_term_on_off() should return ESP_OK");
         validate_rs485_term_pupd_on_off_run(test_data[i].pin_num_mask, test_data[i].on);
    }
}

// Тестируем rs485_term_on_off() с ошибкой gpio_expander_set_level()
void test_rs485_term_on_off_set_level_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_term_on_off() - gpio_expander_set_level() fail");
    LOG_MESSAGE();

    static struct {
        rs485_port_t port;
        bool on;
        uint32_t pin_num_mask;
    } test_data[] = {
        {.port = RS485_2,   .on = true,     .pin_num_mask = TERM485_2_PIN},
        {.port = RS485_1,   .on = true,     .pin_num_mask = TERM485_1_PIN},
        {.port = RS485_2,   .on = false,    .pin_num_mask = TERM485_2_PIN},
        {.port = RS485_1,   .on = false,    .pin_num_mask = TERM485_1_PIN},
    };

    for (unsigned i = 0; i < ARRAY_SIZE(test_data); i++) {
        setUp();
        mock_gpio_expander_set_level_data.should_fail = true;
        LOG_INFO(
            "Testing with port = %s, on = %s",
            (test_data[i].port == RS485_1) ? "RS485_1" : "RS485_2",
            bool_to_str(test_data[i].on)
         );

         esp_err_t result = rs485_term_on_off(test_data[i].port, test_data[i].on);

         TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_term_on_off() should return ESP_FAIL");
         validate_rs485_term_pupd_on_off_run(test_data[i].pin_num_mask, test_data[i].on);
    }
}

// Валидация работы rs485_term_on_off() и rs485_pupd_on_off() с некорректным номером порта
void validate_rs485_term_pupd_on_off_incorrect_port()
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete() should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_init_data.called, "gpio_expander_init() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_dir_data.called, "gpio_expander_set_dir() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_level_data.called, "gpio_expander_set_level() should NOT be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_out_dir_and_level_data.called, "gpio_expander_set_out_dir_and_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_get_level_data.called, "gpio_expander_get_level() should NOT be called");
}

// Тестируем rs485_term_on_off() с некорректным номером порта
void test_rs485_term_on_off_incorrect_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_term_on_off() - incorrect port");
    LOG_MESSAGE();

    // Minimum limit
    int port = RS485_1 - 1;
    LOG_INFO("Testing with port = %d", port);

    esp_err_t result = rs485_term_on_off(port, false);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_term_on_off() should return ESP_FAIL");
    validate_rs485_term_pupd_on_off_incorrect_port();

    // Maximum limit
    setUp();

    port = RS485_2 + 1;
    LOG_INFO("Testing with port = %d", port);

    result = rs485_term_on_off(port, false);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_term_on_off() should return ESP_FAIL");
    validate_rs485_term_pupd_on_off_incorrect_port();
}

// Тестируем успешное выполнение rs485_pupd_on_off()
void test_rs485_pupd_on_off_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_pupd_on_off() - success case");
    LOG_MESSAGE();

    static struct {
        rs485_port_t port;
        bool on;
        uint32_t pin_num_mask;
    } test_data[] = {
        {.port = RS485_2,   .on = true,     .pin_num_mask = FAILSAFE_485_2_PIN},
        {.port = RS485_1,   .on = true,     .pin_num_mask = FAILSAFE_485_1_PIN},
        {.port = RS485_2,   .on = false,    .pin_num_mask = FAILSAFE_485_2_PIN},
        {.port = RS485_1,   .on = false,    .pin_num_mask = FAILSAFE_485_1_PIN},
    };

    for (unsigned i = 0; i < ARRAY_SIZE(test_data); i++) {
        setUp();
        LOG_INFO(
            "Testing with port = %s, on = %s",
            (test_data[i].port == RS485_1) ? "RS485_1" : "RS485_2",
            bool_to_str(test_data[i].on)
         );

         esp_err_t result = rs485_pupd_on_off(test_data[i].port, test_data[i].on);

         TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_pupd_on_off() should return ESP_OK");
         validate_rs485_term_pupd_on_off_run(test_data[i].pin_num_mask, test_data[i].on);
    }
}

// Тестируем rs485_pupd_on_off() с ошибкой gpio_expander_set_level()
void test_rs485_pupd_on_off_set_level_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_pupd_on_off() - gpio_expander_set_level() fail");
    LOG_MESSAGE();

    static struct {
        rs485_port_t port;
        bool on;
        uint32_t pin_num_mask;
    } test_data[] = {
        {.port = RS485_2,   .on = true,     .pin_num_mask = FAILSAFE_485_2_PIN},
        {.port = RS485_1,   .on = true,     .pin_num_mask = FAILSAFE_485_1_PIN},
        {.port = RS485_2,   .on = false,    .pin_num_mask = FAILSAFE_485_2_PIN},
        {.port = RS485_1,   .on = false,    .pin_num_mask = FAILSAFE_485_1_PIN},
    };

    for (unsigned i = 0; i < ARRAY_SIZE(test_data); i++) {
        setUp();
        mock_gpio_expander_set_level_data.should_fail = true;
        LOG_INFO(
            "Testing with port = %s, on = %s",
            (test_data[i].port == RS485_1) ? "RS485_1" : "RS485_2",
            bool_to_str(test_data[i].on)
         );

         esp_err_t result = rs485_pupd_on_off(test_data[i].port, test_data[i].on);

         TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_pupd_on_off() should return ESP_FAIL");
         validate_rs485_term_pupd_on_off_run(test_data[i].pin_num_mask, test_data[i].on);
    }
}

// Тестируем rs485_pupd_on_off() с некорректным номером порта
void test_rs485_pupd_on_off_incorrect_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_pupd_on_off() - incorrect port");
    LOG_MESSAGE();

    // Minimum limit
    int port = RS485_1 - 1;
    LOG_INFO("Testing with port = %d", port);

    esp_err_t result = rs485_pupd_on_off(port, false);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_pupd_on_off() should return ESP_FAIL");
    validate_rs485_term_pupd_on_off_incorrect_port();

    // Maximum limit
    setUp();

    port = RS485_2 + 1;
    LOG_INFO("Testing with port = %d", port);

    result = rs485_pupd_on_off(port, false);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_pupd_on_off() should return ESP_FAIL");
    validate_rs485_term_pupd_on_off_incorrect_port();
}

// Валидация выполнения rs485_bus_vout_on_off() и rs485_bus_vout_set_allowed()
void validate_rs485_bus_vout_run(bool out_level)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreTake_called, "xSemaphoreTake() should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreTake_Handle,
        "xSemaphoreTake() should be called with correct handle"
    );
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        portMAX_DELAY,
        mock_xSemaphoreTake_xTicksToWait,
        "xSemaphoreTake() should be called with portMAX_DELAY timeout value"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_gpio_expander_set_level_data.called, "gpio_expander_set_level() should be called once");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(
        VOUT_485_PIN,
        mock_gpio_expander_set_level_data.masks[0],
        "gpio_expander_set_level() should be called with pin_num_mask = VOUT_485_PIN"
    );
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        (uint8_t)out_level,
        mock_gpio_expander_set_level_data.levels[0],
        "gpio_expander_set_level() should be equal to provided value"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreGive_called, "xSemaphoreGive() should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_SEMAPHORE_HANDLE_T,
        mock_xSemaphoreGive_Handle,
        "xSemaphoreGive() should be called with correct handle"
    );

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete() should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_init_data.called, "gpio_expander_init() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_dir_data.called, "gpio_expander_set_dir() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_out_dir_and_level_data.called, "gpio_expander_set_out_dir_and_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_get_level_data.called, "gpio_expander_get_level() should NOT be called");

    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_xSemaphoreTake_call_seq,
        mock_gpio_expander_set_level_data.call_seq[0],
        "gpio_expander_set_level() should be called after xSemaphoreTake()"
    );
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        mock_gpio_expander_set_level_data.call_seq[0],
        mock_xSemaphoreGive_call_seq,
        "xSemaphoreGive() should be called after gpio_expander_set_level()"
    );
}

// Тестируем успешное выполнение rs485_bus_vout_on_off()
void test_rs485_bus_vout_on_off_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_on_off() - success case");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    static struct {
        bool allowed;
        bool enabled;
        bool vout_state;
    } test_data[] = {
        {.allowed = true,   .enabled = true,    .vout_state = true},
        {.allowed = true,   .enabled = false,   .vout_state = false},
        {.allowed = false,  .enabled = false,   .vout_state = false},
        {.allowed = false,  .enabled = true,    .vout_state = false},
    };

    for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
        LOG_INFO("Testing with allowed = %s, enabled = %s", bool_to_str(test_data[i].allowed), bool_to_str(test_data[i].enabled));

        result = rs485_bus_vout_set_allowed(test_data[i].allowed);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_set_allowed() should return ESP_OK");

        mock_gpio_expander_reset();
        mock_freertos_semaphore_reset();

        result = rs485_bus_vout_on_off(test_data[i].enabled);

        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off() should return ESP_OK");
        validate_rs485_bus_vout_run(test_data[i].vout_state);
    }
}

// Тестируем rs485_bus_vout_on_off() с ошибкой выполнения gpio_expander_set_level()
void test_rs485_bus_vout_on_off_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_on_off() - gpio_expander_set_level() fail");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    static struct {
        bool allowed;
        bool enabled;
        bool vout_state;
    } test_data[] = {
        {.allowed = true,   .enabled = true,    .vout_state = true},
        {.allowed = true,   .enabled = false,   .vout_state = false},
        {.allowed = false,  .enabled = false,   .vout_state = false},
        {.allowed = false,  .enabled = true,    .vout_state = false},
    };

    for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
        LOG_INFO("Testing with allowed = %s, enabled = %s", bool_to_str(test_data[i].allowed), bool_to_str(test_data[i].enabled));

        mock_gpio_expander_set_level_data.should_fail = false;
        result = rs485_bus_vout_set_allowed(test_data[i].allowed);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_set_allowed() should return ESP_OK");

        mock_gpio_expander_reset();
        mock_freertos_semaphore_reset();
        mock_gpio_expander_set_level_data.should_fail = true;

        result = rs485_bus_vout_on_off(test_data[i].enabled);

        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_bus_vout_on_off() should return ESP_FAIL");
        validate_rs485_bus_vout_run(test_data[i].vout_state);
    }
}

// Валидация выполнения rs485_bus_vout_on_off() и rs485_bus_vout_set_allowed() без инициализации модуля
void validate_rs485_bus_vout_no_init(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called, "xSemaphoreTake() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_level_data.called, "gpio_expander_set_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called, "xSemaphoreGive() should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called, "xSemaphoreCreateMutex() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_vSemaphoreDelete_called, "vSemaphoreDelete() should NOT be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_init_data.called, "gpio_expander_init() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_dir_data.called, "gpio_expander_set_dir() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_set_out_dir_and_level_data.called, "gpio_expander_set_out_dir_and_level() should NOT be called");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_gpio_expander_get_level_data.called, "gpio_expander_get_level() should NOT be called");
}

// Тестируем rs485_bus_vout_on_off() без инициализации модуля
void test_rs485_bus_vout_on_off_no_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_on_off() - module not initialized");
    LOG_MESSAGE();

    bool on = true;
    LOG_INFO("Testing with on = true");

    esp_err_t result = rs485_bus_vout_on_off(on);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_bus_vout_on_off() should return ESP_FAIL");
    validate_rs485_bus_vout_no_init();

    // Second check with another value
    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    on = false;
    LOG_INFO("Testing with on = false");

    result = rs485_bus_vout_on_off(on);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_bus_vout_on_off() should return ESP_FAIL");
    validate_rs485_bus_vout_no_init();
}

// Тестируем успешное выполнение rs485_bus_vout_set_allowed()
void test_rs485_bus_vout_set_allowed_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_set_allowed() - success case");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    static struct {
        bool enabled;
        bool allowed;
        bool vout_state;
    } test_data[] = {
        {.enabled = false,  .allowed = true,    .vout_state = false},
        {.enabled = false,  .allowed = false,   .vout_state = false},
        {.enabled = true,   .allowed = false,   .vout_state = false},
        {.enabled = true,   .allowed = true,    .vout_state = true},
    };

    for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
        LOG_INFO("Testing with enabled = %s, allowed = %s", bool_to_str(test_data[i].enabled), bool_to_str(test_data[i].allowed));

        result = rs485_bus_vout_on_off(test_data[i].enabled);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off() should return ESP_OK");

        mock_gpio_expander_reset();
        mock_freertos_semaphore_reset();

        result = rs485_bus_vout_set_allowed(test_data[i].allowed);

        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_set_allowed() should return ESP_OK");
        validate_rs485_bus_vout_run(test_data[i].vout_state);
    }
}

// Тестируем rs485_bus_vout_set_allowed() с ошибкой выполнения gpio_expander_set_level()
void test_rs485_bus_vout_set_allowed_fail(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_set_allowed() - gpio_expander_set_level() fail");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    static struct {
        bool enabled;
        bool allowed;
        bool vout_state;
    } test_data[] = {
        {.enabled = false,  .allowed = true,    .vout_state = false},
        {.enabled = false,  .allowed = false,   .vout_state = false},
        {.enabled = true,   .allowed = false,   .vout_state = false},
        {.enabled = true,   .allowed = true,    .vout_state = true},
    };

    for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
        LOG_INFO("Testing with enabled = %s, allowed = %s", bool_to_str(test_data[i].enabled), bool_to_str(test_data[i].allowed));

        mock_gpio_expander_set_level_data.should_fail = false;
        result = rs485_bus_vout_on_off(test_data[i].enabled);
        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off() should return ESP_OK");

        mock_gpio_expander_reset();
        mock_freertos_semaphore_reset();
        mock_gpio_expander_set_level_data.should_fail = true;

        result = rs485_bus_vout_set_allowed(test_data[i].allowed);

        TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_bus_vout_set_allowed() should return ESP_FAIL");
        validate_rs485_bus_vout_run(test_data[i].vout_state);
    }
}

// Тестируем rs485_bus_vout_set_allowed() без инициализации модуля
void test_rs485_bus_vout_set_allowed_no_init(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_set_allowed() - module not initialized");
    LOG_MESSAGE();

    bool allowed = false;
    LOG_INFO("Testing with allowed = false");

    esp_err_t result = rs485_bus_vout_set_allowed(allowed);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_bus_vout_set_allowed() should return ESP_FAIL");
    validate_rs485_bus_vout_no_init();

    // Second check with another value
    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    allowed = true;
    LOG_INFO("Testing with allowed = true");

    result = rs485_bus_vout_set_allowed(allowed);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_bus_vout_set_allowed() should return ESP_FAIL");
    validate_rs485_bus_vout_no_init();
}

// Тестируем значение enabled для Vout после инициализации
void test_default_vout_enabled_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test default enabled value for Vout after init");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    bool allowed = true;
    LOG_INFO("Testing with allowed = %s", bool_to_str(allowed));

    result = rs485_bus_vout_set_allowed(allowed);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_set_allowed() should return ESP_OK");
    validate_rs485_bus_vout_run(false);

    // Second check with another value
    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    allowed = false;
    LOG_INFO("Testing with allowed = %s", bool_to_str(allowed));

    result = rs485_bus_vout_set_allowed(allowed);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_set_allowed() should return ESP_OK");
    validate_rs485_bus_vout_run(false);
}

// Тестируем значение allowed для Vout после инициализации
void test_default_vout_allowed_value(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test default allowed value for Vout after init");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init() should return ESP_OK");

    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    bool enabled = true;
    LOG_INFO("Testing with enabled = %s", bool_to_str(enabled));

    result = rs485_bus_vout_on_off(enabled);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off() should return ESP_OK");
    validate_rs485_bus_vout_run(enabled);

    // Second check with another value
    mock_gpio_expander_reset();
    mock_freertos_semaphore_reset();

    enabled = false;
    LOG_INFO("Testing with enabled = %s", bool_to_str(enabled));

    result = rs485_bus_vout_on_off(enabled);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off() should return ESP_OK");
    validate_rs485_bus_vout_run(enabled);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rs485_control_init_success);
    RUN_TEST(test_rs485_control_init_mutex_fail);
    RUN_TEST(test_rs485_control_init_set_out_dir_and_level_fail);
    RUN_TEST(test_rs485_control_init_set_level_fail);
    RUN_TEST(test_rs485_control_init_repeat_call);

    RUN_TEST(test_rs485_term_on_off_success);
    RUN_TEST(test_rs485_term_on_off_set_level_fail);
    RUN_TEST(test_rs485_term_on_off_incorrect_port);

    RUN_TEST(test_rs485_pupd_on_off_success);
    RUN_TEST(test_rs485_pupd_on_off_set_level_fail);
    RUN_TEST(test_rs485_pupd_on_off_incorrect_port);

    RUN_TEST(test_rs485_bus_vout_on_off_success);
    RUN_TEST(test_rs485_bus_vout_on_off_fail);
    RUN_TEST(test_rs485_bus_vout_on_off_no_init);

    RUN_TEST(test_rs485_bus_vout_set_allowed_success);
    RUN_TEST(test_rs485_bus_vout_set_allowed_fail);
    RUN_TEST(test_rs485_bus_vout_set_allowed_no_init);

    RUN_TEST(test_default_vout_enabled_value);
    RUN_TEST(test_default_vout_allowed_value);

    return UNITY_END();
}
