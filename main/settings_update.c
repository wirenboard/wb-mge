#include <stdbool.h>
#include "esp_log.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "http_server.h"


#define SETTINGS_UPDATE_DEBUG_LOG_ENABLE    1               // TODO: Возможно, вынести в настройки

#define SETTINGS_UPDATE_TASK_STACK_SIZE     (6 * 1024)
#define SETTINGS_UPDATE_TASK_PRIORITY       5

#define HTTP_SERVER_FLAG                    BIT0

#define HTTP_SERVER_UPDATE_DELAY_MS         1000            // Задержка перед обновлением настроек HTTP сервера


static const char *TAG = "settings_update";


static void settings_update_task(void *arg)
{
    uint32_t flags = (uint32_t)arg;
    ESP_LOGI(TAG, "Updating settings...");

    if (flags & HTTP_SERVER_FLAG) {
        vTaskDelay(pdMS_TO_TICKS(HTTP_SERVER_UPDATE_DELAY_MS));
        http_server_deinit();
        http_server_init();
    }

    ESP_LOGD(TAG, "Settings update task finished");
    vTaskDelete(NULL);
}


void settings_update(void)
{
    static bool log_initialized = false;

    if (!log_initialized) {
        if (SETTINGS_UPDATE_DEBUG_LOG_ENABLE) {
            esp_log_level_set(TAG, ESP_LOG_DEBUG);
        }
        log_initialized = true;
    }

    uint32_t flags = 0;

    if (http_server_check_settings_changed()) {
        ESP_LOGD(TAG, "HTTP server settings were changed");
        flags |= HTTP_SERVER_FLAG;
    }

    if (flags) {
        ESP_LOGI(TAG, "Some settings were changed, starting settings update task");
        xTaskCreate(settings_update_task, "settings_update_task", SETTINGS_UPDATE_TASK_STACK_SIZE, (void*)flags, SETTINGS_UPDATE_TASK_PRIORITY, NULL);
    }
}
