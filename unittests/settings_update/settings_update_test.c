#include "unity.h"
#include "console_log.h"

#include "settings_update.h"
#include "bridge.h"
#include "network.h"
#include "http_server.h"
#include "update_rs485_mio_gpio_states.h"
#include "freertos/task.h"

#define SETTINGS_UPDATE_TASK_STACK_SIZE         6144
#define SETTINGS_UPDATE_TASK_PRIORITY           5

#define HTTP_NETWORK_UPDATE_DELAY_MS            1000

void settings_update_reset(void);

void setUp(void)
{
    mock_bridge_reset();
    mock_network_reset();
    mock_http_server_reset();
    mock_update_rs485_mio_gpio_states_reset();
    mock_freertos_task_reset();
    settings_update_reset();
}

void tearDown(void)
{

}

static void verify_settings_update_checks(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_rs485_control_called, "update_rs485_control should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_update_io_bus_control_called, "update_io_bus_control should be called once");

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        TEST_ASSERT_EQUAL_MESSAGE(1, mock_bridge_port_check_settings_changed_called[i], "Bridge check should be called once");
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_mdns_settings_changed_called, "mDNS check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_http_server_check_settings_changed_called, "HTTP server check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_eth_settings_changed_called, "Ethernet check should be called once");
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_network_check_wifi_settings_changed_called, "WiFi check should be called once");
}

static void verify_task_created(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_called, "xTaskCreate should be called once");
    TEST_ASSERT_NOT_NULL_MESSAGE(mock_xTaskCreate_pvTaskCode, "Task function should not be NULL");

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "settings_update_task",
        mock_xTaskCreate_pcName,
        "Task name should be 'settings_update_task'"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        SETTINGS_UPDATE_TASK_STACK_SIZE,
        mock_xTaskCreate_usStackDepth,
        "Task stack depth should be 6144"
    );

    TEST_ASSERT_NOT_NULL_MESSAGE(mock_xTaskCreate_pvParameters, "Task parameters should not be NULL");
    TEST_ASSERT_EQUAL_MESSAGE(SETTINGS_UPDATE_TASK_PRIORITY, mock_xTaskCreate_uxPriority, "Task priority should be 5");

    TEST_ASSERT_EQUAL_PTR_MESSAGE(
        MOCK_TASK_HANDLE_T,
        mock_xTaskCreate_pxCreatedTask,
        "Task handle should match the created task"
    );
}

static void verify_updates(
    bool expect_bridge0,
    bool expect_bridge1,
    bool expect_mdns,
    bool expect_http,
    bool expect_eth,
    bool expect_wifi
)
{
    bool bridge_expected[BRIDGES_COUNT] = {expect_bridge0, expect_bridge1};

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        int expected = bridge_expected[i] ? 1 : 0;

        TEST_ASSERT_EQUAL_MESSAGE(expected, mock_bridge_port_deinit_called[i],
            bridge_expected[i] ? "Bridge deinit should be called once" : "Bridge deinit should not be called");
        TEST_ASSERT_EQUAL_MESSAGE(expected, mock_bridge_port_init_called[i],
            bridge_expected[i] ? "Bridge init should be called once" : "Bridge init should not be called");

        if (bridge_expected[i]) {
            TEST_ASSERT_EQUAL_MESSAGE(i, mock_bridge_port_deinit_index[i], "Bridge deinit index should be correct");
            TEST_ASSERT_EQUAL_MESSAGE(i, mock_bridge_port_init_index[i], "Bridge init index should be correct");
        }
    }

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
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelay_called, "vTaskDelay should be called once before network updates");
    TEST_ASSERT_EQUAL_MESSAGE(
        HTTP_NETWORK_UPDATE_DELAY_MS,
        mock_vTaskDelay_xTicksToDelay,
        "vTaskDelay should be called with the correct delay before network updates"
    );
}

static void verify_task_deleted(void)
{
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_vTaskDelete_called, "vTaskDelete should be called once");
    TEST_ASSERT_EQUAL_PTR_MESSAGE(NULL, mock_vTaskDelete_xTaskToDelete, "Task should delete itself (NULL)");
}

// Тестируем случай, когда никакие настройки не изменились
void test_settings_update_no_changes(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - no changes");
    LOG_MESSAGE();

    settings_update();

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_xTaskCreate_called, "xTaskCreate should not be called when no changes");
    verify_updates(false, false, false, false, false, false);
}

// Тестируем обновление настроек мостов
void test_settings_update_bridge_ports_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - bridge ports changed");
    LOG_MESSAGE();

    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        setUp();

        mock_bridge_port_check_settings_changed_return_value[i] = true;

        settings_update();

        verify_settings_update_checks();
        verify_task_created();

        if (i == 0) {
            verify_updates(true, false, false, false, false, false);
        } else if (i == 1) {
            verify_updates(false, true, false, false, false, false);
        }

        verify_task_deleted();
    }
}

// Тестируем обновление настроек mDNS
void test_settings_update_mdns_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - mDNS changed");
    LOG_MESSAGE();

    mock_network_check_mdns_settings_changed_return_value = true;

    settings_update();

    verify_settings_update_checks();
    verify_task_created();
    verify_updates(false, false, true, false, false, false);
    verify_task_deleted();
}

// Тестируем обновление настроек HTTP сервера
void test_settings_update_http_server_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - HTTP server changed");
    LOG_MESSAGE();

    mock_http_server_check_settings_changed_return_value = true;

    settings_update();

    verify_settings_update_checks();
    verify_task_created();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, true, false, false);
    verify_task_deleted();
}

// Тестируем обновление настроек Ethernet
void test_settings_update_ethernet_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - Ethernet changed");
    LOG_MESSAGE();

    mock_network_check_eth_settings_changed_return_value = true;

    settings_update();

    verify_settings_update_checks();
    verify_task_created();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, false, true, false);
    verify_task_deleted();
}

// Тестируем обновление настроек WiFi
void test_settings_update_wifi_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - WiFi changed");
    LOG_MESSAGE();

    mock_network_check_wifi_settings_changed_return_value = true;

    settings_update();

    verify_settings_update_checks();
    verify_task_created();
    verify_delay_before_network_updates();
    verify_updates(false, false, false, false, false, true);
    verify_task_deleted();
}

// Тестируем обновление всех настроек одновременно
void test_settings_update_all_changed(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - all settings changed");
    LOG_MESSAGE();

    for (unsigned i = 0; i < BRIDGES_COUNT; ++i) {
        mock_bridge_port_check_settings_changed_return_value[i] = true;
    }

    mock_network_check_mdns_settings_changed_return_value = true;
    mock_http_server_check_settings_changed_return_value = true;
    mock_network_check_eth_settings_changed_return_value = true;
    mock_network_check_wifi_settings_changed_return_value = true;

    settings_update();

    verify_settings_update_checks();
    verify_task_created();
    verify_delay_before_network_updates();
    verify_updates(true, true, true, true, true, true);
    verify_task_deleted();
}

// Тестируем создание задачи с ошибкой
void test_settings_update_task_creation_failure(void)
{
    LOG_MESSAGE();
    LOG_COLORED_MESSAGE(CONS_COLOR_LIGHT_BLUE, "Test settings_update - task creation failure");
    LOG_MESSAGE();

    mock_bridge_port_check_settings_changed_return_value[0] = true;
    mock_xTaskCreate_return_value = pdFAIL;

    settings_update();

    verify_settings_update_checks();
    TEST_ASSERT_EQUAL_MESSAGE(1, mock_xTaskCreate_called, "xTaskCreate should be called");
    verify_updates(false, false, false, false, false, false);
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_vTaskDelete_called, "vTaskDelete should not be called when task creation fails");
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_settings_update_no_changes);
    RUN_TEST(test_settings_update_bridge_ports_changed);
    RUN_TEST(test_settings_update_mdns_changed);
    RUN_TEST(test_settings_update_http_server_changed);
    RUN_TEST(test_settings_update_ethernet_changed);
    RUN_TEST(test_settings_update_wifi_changed);
    RUN_TEST(test_settings_update_all_changed);
    RUN_TEST(test_settings_update_task_creation_failure);

    return UNITY_END();
}
