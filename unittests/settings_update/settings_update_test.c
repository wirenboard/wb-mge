#include "unity.h"
#include "console_log.h"

#include "settings_update.h"
#include "network.h"
#include "http_server.h"
#include "update_rs485_mio_gpio_states.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SETTINGS_UPDATE_TASK_STACK_SIZE         6144
#define SETTINGS_UPDATE_TASK_PRIORITY           5

#define HTTP_NETWORK_UPDATE_DELAY_MS            1000

void settings_update_reset(void);

void setUp(void)
{
    mock_network_reset();
    mock_http_server_reset();
    mock_update_rs485_mio_gpio_states_reset();
    mock_freertos_task_reset();
    settings_update_reset();
}

void tearDown(void)
{

}

void execute_task_function()
{
    mock_xTaskCreate_data.pvTaskCode(mock_xTaskCreate_data.pvParameters);
}

static void verify_settings_update_checks(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_rs485_control_called, "update_rs485_control should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_io_bus_control_called, "update_io_bus_control should be called once");

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_mdns_settings_changed_called, "mDNS check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_http_server_check_settings_changed_called, "HTTP server check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_eth_settings_changed_called, "Ethernet check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_wifi_settings_changed_called, "WiFi check should be called once");
}

static void verify_task_created(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "xTaskCreate should be called once");

    // pvTaskCode проверяется внутри xTaskCreate()

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "settings_update_task",
        mock_xTaskCreate_data.pcName,
        "Task name should be 'settings_update_task'"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SETTINGS_UPDATE_TASK_STACK_SIZE,
        mock_xTaskCreate_data.usStackDepth,
        "Task stack depth should be 6144"
    );

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_xTaskCreate_data.pvParameters, "Task parameters should not be NULL");
    TEST_ASSERT_EQUAL_MESSAGE(SETTINGS_UPDATE_TASK_PRIORITY, mock_xTaskCreate_data.uxPriority, "Task priority should be 5");
}

static void verify_updates(
    bool expect_mdns,
    bool expect_http,
    bool expect_eth,
    bool expect_wifi
)
{
    int expected_mdns = expect_mdns ? 1 : 0;
    int expected_http = expect_http ? 1 : 0;
    int expected_eth = expect_eth ? 1 : 0;
    int expected_wifi = expect_wifi ? 1 : 0;

    TEST_ASSERT_EQUAL_MESSAGE(expected_mdns, mock_network_update_mdns_settings_called,
        expect_mdns ? "mDNS update should be called" : "mDNS update should not be called");

    TEST_ASSERT_EQUAL_MESSAGE(expected_http, mock_http_server_deinit_called,
        expect_http ? "HTTP server deinit should be called" : "HTTP server deinit should not be called");
    TEST_ASSERT_EQUAL_MESSAGE(expected_http, mock_http_server_init_called,
        expect_http ? "HTTP server init should be called" : "HTTP server init should not be called");

    TEST_ASSERT_EQUAL_MESSAGE(expected_eth, mock_network_update_eth_settings_called,
        expect_eth ? "Ethernet update should be called" : "Ethernet update should not be called");

    TEST_ASSERT_EQUAL_MESSAGE(expected_wifi, mock_network_update_wifi_settings_called,
        expect_wifi ? "WiFi update should be called" : "WiFi update should not be called");
}

static void verify_delay_before_network_updates(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelay_data.called, "vTaskDelay should be called once before network updates");
    TEST_ASSERT_EQUAL_MESSAGE(
        pdMS_TO_TICKS(HTTP_NETWORK_UPDATE_DELAY_MS),
        mock_vTaskDelay_data.xTicksToDelay,
        "vTaskDelay should be called with the correct delay before network updates"
    );
}

static void verify_task_deleted(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelete_data.called, "vTaskDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(NULL, mock_vTaskDelete_data.xTaskToDelete, "Task should delete itself (NULL)");
}

// Тестируем случай, когда никакие настройки не изменились
void test_settings_update_no_changes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - no changes");
    LOG_MESSAGE();

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_data.called, "xTaskCreate should not be called when no changes");
    verify_updates(false, false, false, false);
}

// Тестируем обновление настроек mDNS
void test_settings_update_mdns_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - mDNS changed");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");
    verify_updates(true, false, false, false);
    verify_task_deleted();
}

// Тестируем обновление настроек HTTP сервера
void test_settings_update_http_server_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - HTTP server changed");
    LOG_MESSAGE();

    mock_http_server_check_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(false, true, false, false);
    verify_task_deleted();
}

// Тестируем обновление настроек Ethernet
void test_settings_update_ethernet_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - Ethernet changed");
    LOG_MESSAGE();

    mock_network_check_eth_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(false, false, true, false);
    verify_task_deleted();
}

// Тестируем обновление настроек WiFi
void test_settings_update_wifi_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - WiFi changed");
    LOG_MESSAGE();

    mock_network_check_wifi_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, true);
    verify_task_deleted();
}

// Тестируем обновление всех настроек одновременно
void test_settings_update_all_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - all settings changed");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;
    mock_http_server_check_settings_changed_return_value = true;
    mock_network_check_eth_settings_changed_return_value = true;
    mock_network_check_wifi_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    execute_task_function();
    verify_delay_before_network_updates();
    verify_updates(true, true, true, true);
    verify_task_deleted();
}

// Тестируем создание задачи с ошибкой
void test_settings_update_task_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - task creation failure");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;
    mock_xTaskCreate_data.should_fail = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_FAIL, result, "Settings update should fail");

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_data.called, "xTaskCreate should be called");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");
    verify_updates(false, false, false, false);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelete_data.called, "vTaskDelete should not be called when task creation fails");
}

// Тестируем повторный запуск settings_update когда задача еще не завершилась
void test_settings_update_task_already_running(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - task already running");
    LOG_MESSAGE();

    mock_http_server_check_settings_changed_return_value = true;

    esp_err_t result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");

    verify_settings_update_checks();
    verify_task_created();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelay_data.called, "vTaskDelay should not be called");

    mock_http_server_check_settings_changed_return_value = false;
    mock_vTaskDelay_data.task_handle_reset_on_count = 3;

    result = settings_update();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Settings update should succeed");
    TEST_ASSERT_EQUAL_MESSAGE(
        mock_vTaskDelay_data.task_handle_reset_on_count, mock_vTaskDelay_data.called, "vTaskDelay should be called 3 times"
    );
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_settings_update_no_changes);
    RUN_TEST(test_settings_update_mdns_changed);
    RUN_TEST(test_settings_update_http_server_changed);
    RUN_TEST(test_settings_update_ethernet_changed);
    RUN_TEST(test_settings_update_wifi_changed);
    RUN_TEST(test_settings_update_all_changed);
    RUN_TEST(test_settings_update_task_creation_failure);
    RUN_TEST(test_settings_update_task_already_running);

    return UNITY_END();
}
