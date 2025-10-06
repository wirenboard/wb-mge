#include "freertos/FreeRTOS.h"
#include "esp_log.h"


#define SETTING_SAVE_TIMER_INTERVAL_MS          1000    // Минимальный интервал между сохранением настроек
#define EVENT_BIT_READY                         BIT0    // Бит (флаг) готовности таймера к следующему сохранению настроек


static const char* TAG = "settings_save_timer";

static EventGroupHandle_t event_group = NULL;
static TimerHandle_t timer = NULL;


static void timer_callback(TimerHandle_t pxTimer)
{
    if (event_group) {
        ESP_LOGD(TAG, "Set READY flag");
        xEventGroupSetBits(event_group, EVENT_BIT_READY);
    }
}


esp_err_t settings_save_timer_auto_init(void)
{
    if (event_group == NULL) {
        event_group = xEventGroupCreate();
        if (event_group == NULL) {
            ESP_LOGE(TAG, "Unable to create event group");
            return ESP_FAIL;
        }
        xEventGroupSetBits(event_group, EVENT_BIT_READY);
    }
    if (timer == NULL) {
        timer = xTimerCreate(TAG, pdMS_TO_TICKS(SETTING_SAVE_TIMER_INTERVAL_MS), pdFALSE, NULL, timer_callback);
        if (timer == NULL) {
            ESP_LOGE(TAG, "Unable to create timer");
            if (event_group != NULL) {
                vEventGroupDelete(event_group);
                event_group = NULL;
            }
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}


esp_err_t settings_save_timer_wait(void)
{
    if (event_group == NULL || timer == NULL) {
        // Fail-safe mode
        ESP_LOGW(TAG, "Using fail-safe delay %d ms", SETTING_SAVE_TIMER_INTERVAL_MS);
        vTaskDelay(pdMS_TO_TICKS(SETTING_SAVE_TIMER_INTERVAL_MS));
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    ESP_LOGD(TAG, "Waiting for READY flag ...");
    EventBits_t bits = xEventGroupWaitBits(event_group, EVENT_BIT_READY, pdTRUE, pdTRUE, pdMS_TO_TICKS(SETTING_SAVE_TIMER_INTERVAL_MS));
    ESP_LOGD(TAG, "Waiting for READY flag finished");
    if (!(bits & EVENT_BIT_READY)) {
        ESP_LOGW(TAG, "Wait timeout occurred");
        result = ESP_ERR_TIMEOUT;
    }

    xTimerStart(timer, 0);
    return result;
}
