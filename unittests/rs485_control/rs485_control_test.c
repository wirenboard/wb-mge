#include "unity.h"
#include "console_log.h"

#include "rs485_control.h"
#include "esp_io_expander.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <string.h>

#define TERM485_1_PIN                   IO_EXPANDER_PIN_NUM_0
#define TERM485_2_PIN                   IO_EXPANDER_PIN_NUM_1
#define FAILSAFE_485_1_PIN              IO_EXPANDER_PIN_NUM_2
#define FAILSAFE_485_2_PIN              IO_EXPANDER_PIN_NUM_3
#define VOUT_485_PIN                    IO_EXPANDER_PIN_NUM_6

#define RS485_PINS_COUNT                5

void rs485_control_test_reset(void);

void setUp(void)
{
    mock_esp_io_expander_set_dir_called = 0;
    mock_esp_io_expander_set_dir_handle = NULL;
    memset(mock_esp_io_expander_set_dir_pin_masks, 0, sizeof(mock_esp_io_expander_set_dir_pin_masks));
    memset(mock_esp_io_expander_set_dir_directions, 0, sizeof(mock_esp_io_expander_set_dir_directions));

    mock_esp_io_expander_set_level_called = 0;
    mock_esp_io_expander_set_level_handle = NULL;
    memset(mock_esp_io_expander_set_level_pin_masks, 0, sizeof(mock_esp_io_expander_set_level_pin_masks));
    memset(mock_esp_io_expander_set_level_levels, 0, sizeof(mock_esp_io_expander_set_level_levels));

    mock_xSemaphoreCreateMutex_should_fail = false;
    mock_xSemaphoreCreateMutex_called = 0;
    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreGive_called = 0;

    rs485_control_test_reset();
}

void tearDown(void)
{

}

static bool find_pin_with_direction(uint32_t pin_mask, esp_io_expander_dir_t expected_dir)
{
    for (int i = 0; i < RS485_PINS_COUNT; i++) {
        if (mock_esp_io_expander_set_dir_pin_masks[i] == pin_mask) {
            if (mock_esp_io_expander_set_dir_directions[i] == expected_dir) {
                return true;
            }
        }
    }
    return false;
}

static bool find_pin_with_level(uint32_t pin_mask, uint8_t level)
{
    for (int i = 0; i < RS485_PINS_COUNT; i++) {
        if (mock_esp_io_expander_set_level_pin_masks[i] == pin_mask) {
            if (mock_esp_io_expander_set_level_levels[i] == level) {
                return true;
            }
        }
    }
    return false;
}

static void verify_rs485_term_on_off(rs485_port_t port, bool on, uint32_t expected_pin, const char *port_name)
{
    mock_esp_io_expander_set_level_called = 0;
    memset(mock_esp_io_expander_set_level_pin_masks, 0, sizeof(mock_esp_io_expander_set_level_pin_masks));
    memset(mock_esp_io_expander_set_level_levels, 0, sizeof(mock_esp_io_expander_set_level_levels));

    esp_err_t result = rs485_term_on_off(port, on);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_term_on_off should return ESP_OK");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_set_level_called, "esp_io_expander_set_level should be called once"
    );

    char msg[128];

    snprintf(msg, sizeof(msg), "%s should be set", port_name);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected_pin, mock_esp_io_expander_set_level_pin_masks[0], msg);

    snprintf(msg, sizeof(msg), "%s should be set to %s", port_name, on ? "HIGH (1)" : "LOW (0)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(on ? 1 : 0, mock_esp_io_expander_set_level_levels[0], msg);
}

static void verify_rs485_pupd_on_off(rs485_port_t port, bool on, uint32_t expected_pin, const char *port_name)
{
    mock_esp_io_expander_set_level_called = 0;
    memset(mock_esp_io_expander_set_level_pin_masks, 0, sizeof(mock_esp_io_expander_set_level_pin_masks));
    memset(mock_esp_io_expander_set_level_levels, 0, sizeof(mock_esp_io_expander_set_level_levels));

    esp_err_t result = rs485_pupd_on_off(port, on);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_pupd_on_off should return ESP_OK");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        1, mock_esp_io_expander_set_level_called, "esp_io_expander_set_level should be called once"
    );

    char msg[128];

    snprintf(msg, sizeof(msg), "%s should be set", port_name);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(expected_pin, mock_esp_io_expander_set_level_pin_masks[0], msg);

    snprintf(msg, sizeof(msg), "%s should be set to %s", port_name, on ? "HIGH (1)" : "LOW (0)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(on ? 1 : 0, mock_esp_io_expander_set_level_levels[0], msg);
}

static void verify_rs485_bus_vout_on_off(bool on)
{
    mock_esp_io_expander_set_level_called = 0;
    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreGive_called = 0;
    memset(mock_esp_io_expander_set_level_pin_masks, 0, sizeof(mock_esp_io_expander_set_level_pin_masks));
    memset(mock_esp_io_expander_set_level_levels, 0, sizeof(mock_esp_io_expander_set_level_levels));

    esp_err_t result = rs485_bus_vout_on_off(on);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreTake_called,
        "xSemaphoreTake should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreGive_called,
        "xSemaphoreGive should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called once");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(VOUT_485_PIN, mock_esp_io_expander_set_level_pin_masks[0],
        "VOUT_485_PIN should be set");

    char msg[128];
    snprintf(msg, sizeof(msg), "VOUT_485_PIN should be set to %s", on ? "HIGH (1)" : "LOW (0)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(on ? 1 : 0, mock_esp_io_expander_set_level_levels[0], msg);
}

static void verify_rs485_bus_vout_set_allowed(bool allowed, uint8_t expected_level)
{
    mock_esp_io_expander_set_level_called = 0;
    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreGive_called = 0;
    memset(mock_esp_io_expander_set_level_pin_masks, 0, sizeof(mock_esp_io_expander_set_level_pin_masks));
    memset(mock_esp_io_expander_set_level_levels, 0, sizeof(mock_esp_io_expander_set_level_levels));

    esp_err_t result = rs485_bus_vout_set_allowed(allowed);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_set_allowed should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreTake_called,
        "xSemaphoreTake should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreGive_called,
        "xSemaphoreGive should be called once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called once");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(VOUT_485_PIN, mock_esp_io_expander_set_level_pin_masks[0],
        "VOUT_485_PIN should be set");

    char msg[128];
    snprintf(msg, sizeof(msg), "VOUT_485_PIN should be set to %d", expected_level);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected_level, mock_esp_io_expander_set_level_levels[0], msg);
}

// Тестируем успешную инициализацию rs485_control_init
void test_rs485_control_init_success(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - success case");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called once");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_PINS_COUNT, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should be called 5 times");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_direction(VOUT_485_PIN, IO_EXPANDER_OUTPUT),
        "VOUT_485_PIN should be configured as OUTPUT");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_direction(TERM485_1_PIN, IO_EXPANDER_OUTPUT),
        "TERM485_1_PIN should be configured as OUTPUT");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_direction(TERM485_2_PIN, IO_EXPANDER_OUTPUT),
        "TERM485_2_PIN should be configured as OUTPUT");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_direction(FAILSAFE_485_1_PIN, IO_EXPANDER_OUTPUT),
        "FAILSAFE_485_1_PIN should be configured as OUTPUT");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_direction(FAILSAFE_485_2_PIN, IO_EXPANDER_OUTPUT),
        "FAILSAFE_485_2_PIN should be configured as OUTPUT");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_PINS_COUNT * 2, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called 10 times (5 init + 5 from on_off functions)");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_level(VOUT_485_PIN, 0),
        "VOUT_485_PIN should be set to level 0");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_level(TERM485_1_PIN, 0),
        "TERM485_1_PIN should be set to level 0");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_level(TERM485_2_PIN, 0),
        "TERM485_2_PIN should be set to level 0");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_level(FAILSAFE_485_1_PIN, 0),
        "FAILSAFE_485_1_PIN should be set to level 0");

    TEST_ASSERT_TRUE_MESSAGE(
        find_pin_with_level(FAILSAFE_485_2_PIN, 0),
        "FAILSAFE_485_2_PIN should be set to level 0");
}

// Тестируем инициализацию с NULL handle
void test_rs485_control_init_null_handle(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - NULL handle");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(NULL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_control_init should return ESP_FAIL with NULL handle");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should not be called with NULL handle");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should not be called with NULL handle");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called with NULL handle");
}

// Тестируем повторную инициализацию (мьютекс уже создан)
void test_rs485_control_init_mutex_already_created(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - mutex already created");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called on first init");

    mock_esp_io_expander_set_dir_called = 0;
    mock_esp_io_expander_set_level_called = 0;

    result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should not be called on second init (mutex already exists)");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_PINS_COUNT, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should still be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_PINS_COUNT * 2, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should still be called");
}

// Тестируем инициализацию с ошибкой создания мьютекса
void test_rs485_control_init_mutex_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_control_init - mutex creation failure");
    LOG_MESSAGE();

    mock_xSemaphoreCreateMutex_should_fail = true;

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK even if mutex creation fails");

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mock_xSemaphoreCreateMutex_called,
        "xSemaphoreCreateMutex should be called");

    TEST_ASSERT_EQUAL_INT_MESSAGE(RS485_PINS_COUNT, mock_esp_io_expander_set_dir_called,
        "esp_io_expander_set_dir should still be called (init continues on mutex failure)");

    TEST_ASSERT_EQUAL_INT_MESSAGE(9, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should be called 9 times (5 init + 4 from term/pupd, bus_vout fails)");
}

// Тестируем rs485_term_on_off - неинициализированный io_expander
void test_rs485_term_on_off_handler_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_term_on_off - not initialized");
    LOG_MESSAGE();

    esp_err_t result = rs485_term_on_off(RS485_1, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_term_on_off should return ESP_FAIL when not initialized");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when not initialized");
}

// Тестируем rs485_term_on_off - недопустимый номер порта
void test_rs485_term_on_off_invalid_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_term_on_off - invalid port number");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    mock_esp_io_expander_set_level_called = 0;

    result = rs485_term_on_off((rs485_port_t)0, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result, "rs485_term_on_off should return ESP_FAIL with invalid port");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called with invalid port");
}

// Тестируем rs485_term_on_off - включить и выключить для обоих портов
void test_rs485_term_on_off_switch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_term_on_off - turn on and off for both ports");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    verify_rs485_term_on_off(RS485_1, true, TERM485_1_PIN, "TERM485_1_PIN");
    verify_rs485_term_on_off(RS485_1, false, TERM485_1_PIN, "TERM485_1_PIN");

    verify_rs485_term_on_off(RS485_2, true, TERM485_2_PIN, "TERM485_2_PIN");
    verify_rs485_term_on_off(RS485_2, false, TERM485_2_PIN, "TERM485_2_PIN");
}

// Тестируем rs485_pupd_on_off - неинициализированный io_expander
void test_rs485_pupd_on_off_handler_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_pupd_on_off - not initialized");
    LOG_MESSAGE();

    esp_err_t result = rs485_pupd_on_off(RS485_1, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result,
        "rs485_pupd_on_off should return ESP_FAIL when not initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when not initialized");
}

// Тестируем rs485_pupd_on_off - недопустимый номер порта
void test_rs485_pupd_on_off_invalid_port(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_pupd_on_off - invalid port number");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    mock_esp_io_expander_set_level_called = 0;

    result = rs485_pupd_on_off((rs485_port_t)5, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result,
        "rs485_pupd_on_off should return ESP_FAIL with invalid port");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called with invalid port");
}

// Тестируем rs485_pupd_on_off - включить и выключить для обоих портов
void test_rs485_pupd_on_off_switch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_pupd_on_off - turn on and off for both ports");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    verify_rs485_pupd_on_off(RS485_1, true, FAILSAFE_485_1_PIN, "FAILSAFE_485_1_PIN");
    verify_rs485_pupd_on_off(RS485_1, false, FAILSAFE_485_1_PIN, "FAILSAFE_485_1_PIN");

    verify_rs485_pupd_on_off(RS485_2, true, FAILSAFE_485_2_PIN, "FAILSAFE_485_2_PIN");
    verify_rs485_pupd_on_off(RS485_2, false, FAILSAFE_485_2_PIN, "FAILSAFE_485_2_PIN");
}

// Тестируем rs485_bus_vout_on_off - неинициализированный io_expander
void test_rs485_bus_vout_on_off_handler_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_on_off - handler not initialized");
    LOG_MESSAGE();

    esp_err_t result = rs485_bus_vout_on_off(true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result,
        "rs485_bus_vout_on_off should return ESP_FAIL when not initialized");

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when not initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called,
        "xSemaphoreTake should not be called when not initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called,
        "xSemaphoreGive should not be called when not initialized");
}

// Тестируем rs485_bus_vout_on_off - неинициализированный мьютекс
void test_rs485_bus_vout_on_off_mutex_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_on_off - mutex not initialized");
    LOG_MESSAGE();

    mock_xSemaphoreCreateMutex_should_fail = true;
    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    mock_esp_io_expander_set_level_called = 0;
    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreGive_called = 0;

    result = rs485_bus_vout_on_off(true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result,
        "rs485_bus_vout_on_off should return ESP_FAIL when mutex is NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when mutex is NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called,
        "xSemaphoreTake should not be called when mutex is NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called,
        "xSemaphoreGive should not be called when mutex is NULL");
}

// Тестируем rs485_bus_vout_on_off - включить и выключить
void test_rs485_bus_vout_on_off_switch(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_on_off - turn on and off");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    verify_rs485_bus_vout_on_off(true);
    verify_rs485_bus_vout_on_off(false);
}

// Тестируем rs485_bus_vout_set_allowed - неинициализированный io_expander
void test_rs485_bus_vout_set_allowed_handler_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_set_allowed - handler not initialized");
    LOG_MESSAGE();

    esp_err_t result = rs485_bus_vout_set_allowed(true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result,
        "rs485_bus_vout_set_allowed should return ESP_FAIL when not initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when not initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called,
        "xSemaphoreTake should not be called when not initialized");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called,
        "xSemaphoreGive should not be called when not initialized");
}

// Тестируем rs485_bus_vout_set_allowed - неинициализированный мьютекс
void test_rs485_bus_vout_set_allowed_mutex_not_initialized(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_set_allowed - mutex not initialized");
    LOG_MESSAGE();

    mock_xSemaphoreCreateMutex_should_fail = true;
    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    mock_esp_io_expander_set_level_called = 0;
    mock_xSemaphoreTake_called = 0;
    mock_xSemaphoreGive_called = 0;

    result = rs485_bus_vout_set_allowed(true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_FAIL, result,
        "rs485_bus_vout_set_allowed should return ESP_FAIL when mutex is NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_esp_io_expander_set_level_called,
        "esp_io_expander_set_level should not be called when mutex is NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreTake_called,
        "xSemaphoreTake should not be called when mutex is NULL");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mock_xSemaphoreGive_called,
        "xSemaphoreGive should not be called when mutex is NULL");
}

// Тестируем rs485_bus_vout_set_allowed - различные комбинации allowed и enabled
void test_rs485_bus_vout_set_allowed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test rs485_bus_vout_set_allowed - various combinations");
    LOG_MESSAGE();

    esp_err_t result = rs485_control_init(MOCK_IO_EXPANDER_HANDLE);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_control_init should return ESP_OK");

    verify_rs485_bus_vout_set_allowed(true, 0);
    verify_rs485_bus_vout_set_allowed(false, 0);

    result = rs485_bus_vout_on_off(true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, result, "rs485_bus_vout_on_off should return ESP_OK");

    verify_rs485_bus_vout_set_allowed(true, 1);
    verify_rs485_bus_vout_set_allowed(false, 0);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_rs485_control_init_success);
    RUN_TEST(test_rs485_control_init_null_handle);
    RUN_TEST(test_rs485_control_init_mutex_already_created);
    RUN_TEST(test_rs485_control_init_mutex_creation_failure);

    RUN_TEST(test_rs485_term_on_off_handler_not_initialized);
    RUN_TEST(test_rs485_term_on_off_invalid_port);
    RUN_TEST(test_rs485_term_on_off_switch);

    RUN_TEST(test_rs485_pupd_on_off_handler_not_initialized);
    RUN_TEST(test_rs485_pupd_on_off_invalid_port);
    RUN_TEST(test_rs485_pupd_on_off_switch);

    RUN_TEST(test_rs485_bus_vout_on_off_handler_not_initialized);
    RUN_TEST(test_rs485_bus_vout_on_off_mutex_not_initialized);
    RUN_TEST(test_rs485_bus_vout_on_off_switch);

    RUN_TEST(test_rs485_bus_vout_set_allowed_handler_not_initialized);
    RUN_TEST(test_rs485_bus_vout_set_allowed_mutex_not_initialized);
    RUN_TEST(test_rs485_bus_vout_set_allowed);

    return UNITY_END();
}
