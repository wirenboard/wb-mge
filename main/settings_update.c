#include "esp_log.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"
#include "port_manager.h"
#include "network.h"
#include "update_rs485_mio_gpio_states.h"


#define SETTINGS_UPDATE_TASK_STACK_SIZE     (6 * 1024)
#define SETTINGS_UPDATE_TASK_PRIORITY       5

#define BRIDGE_FLAGS_BASE                   BIT0
#define MDNS_FLAG                           BIT8
#define HTTP_SERVER_FLAG                    BIT9
#define ETHERNET_FLAG                       BIT10
#define WIFI_FLAG                           BIT11

#define HTTP_NETWORK_UPDATE_DELAY_MS        1000            // Delay before updating HTTP / Ethernet / WiFi settings


static const char *TAG = "settings_update";

static TaskHandle_t update_task_handle = NULL;


static void settings_update_task(void *arg)
{
    uint32_t flags = (uint32_t)(uintptr_t)arg;
    ESP_LOGI(TAG, "Updating settings...");

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        if (flags & (BRIDGE_FLAGS_BASE << index)) {
            ESP_LOGD(TAG, "Applying new settings to port %u via port_manager", index + 1);
            port_manager_apply_settings(index);
        }
    }

    if (flags & MDNS_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to mDNS");
        network_update_mdns_settings();
    }

    if (flags & (HTTP_SERVER_FLAG | ETHERNET_FLAG | WIFI_FLAG)) {
        // Small delay to allow the response to be sent to the client
        vTaskDelay(pdMS_TO_TICKS(HTTP_NETWORK_UPDATE_DELAY_MS));
    }

    if (flags & HTTP_SERVER_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to HTTP server");
        http_server_deinit();
        http_server_init();
    }

    if (flags & ETHERNET_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to Ethernet");
        network_update_eth_settings();
    }

    if (flags & WIFI_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to WiFi");
        network_update_wifi_settings();
    }

    ESP_LOGI(TAG, "Settings update task finished");
    update_task_handle = NULL;
    vTaskDelete(NULL);
}


esp_err_t settings_update(void)
{
    update_rs485_control();
    update_io_bus_control();
    update_serial_tx_disabled();

    if (update_task_handle != NULL) {
        ESP_LOGW(TAG, "Previous settings have not yet been applied, waiting for setting update task finished");
        while (update_task_handle != NULL) {
            vTaskDelay(10);
        }
    }

    uint32_t flags = 0;

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        if (port_manager_check_settings_changed(index)) {
            ESP_LOGD(TAG, "Port %u settings were changed", index + 1);
            flags |= BRIDGE_FLAGS_BASE << index;
        }
    }

    if (network_check_mdns_settings_changed()) {
        ESP_LOGD(TAG, "mDNS settings were changed");
        flags |= MDNS_FLAG;
    }

    if (http_server_check_settings_changed()) {
        ESP_LOGD(TAG, "HTTP server settings were changed");
        flags |= HTTP_SERVER_FLAG;
    }

    if (network_check_eth_settings_changed()) {
        ESP_LOGD(TAG, "Ethernet settings were changed");
        flags |= ETHERNET_FLAG;
    }

    if (network_check_wifi_settings_changed()) {
        ESP_LOGD(TAG, "WiFi settings were changed");
        flags |= WIFI_FLAG;
    }

    if (flags) {
        ESP_LOGI(TAG, "Some settings were changed, starting settings update task");
        BaseType_t ret = xTaskCreate(settings_update_task, "settings_update_task", SETTINGS_UPDATE_TASK_STACK_SIZE,
                                    (void*)(uintptr_t)flags, SETTINGS_UPDATE_TASK_PRIORITY, &update_task_handle);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Unable to create settings update task");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

#ifdef __unittest_env__
    void settings_update_reset(void)
    {
        update_task_handle = NULL;
    }
#endif
