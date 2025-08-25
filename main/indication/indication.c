#include "esp_io_expander_tca95xx_16bit.h"
#include "leds_control.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

//------------------------------------------------------------------------------

#define INDICATION_TASK_STACK_SIZE      2048
#define INDICATION_TASK_PRIORITY        1

#define STATUS_LED_BLINK_PERIOD         1000    // ms

//------------------------------------------------------------------------------

static const char* TAG = "indication";

//------------------------------------------------------------------------------

static void status_led_control(unsigned long sys_time)
{
    static const unsigned long t_on = STATUS_LED_BLINK_PERIOD / 2;
    static const unsigned long t_off = STATUS_LED_BLINK_PERIOD - t_on;
    static bool init = 1;
    static unsigned long time_stamp = 0;
    static bool led_state = 0;

    if (init) {
        time_stamp = sys_time;
        led_state = 0;
        init = 0;
    }

    unsigned long delta_time = sys_time - time_stamp;

    if (!led_state) {
        if (delta_time > t_on) {
            led_state = 1;
            time_stamp += delta_time;
        }
    } else {
        if (delta_time > t_off) {
            led_state = 0;
            time_stamp += delta_time;
        }
    }

    leds_control_set_status_led(led_state);
}

static void ethernet_led_control(unsigned long sys_time)
{

}

static void wifi_led_control(unsigned long sys_time)
{

}

//------------------------------------------------------------------------------

static void indication_task(void *arg)
{
    ESP_LOGD(TAG, "Started indication_task()");

    while (1)
    {
        unsigned long sys_time = pdTICKS_TO_MS(xTaskGetTickCount());
        status_led_control(sys_time);
        ethernet_led_control(sys_time);
        wifi_led_control(sys_time);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

//------------------------------------------------------------------------------

esp_err_t indication_init(esp_io_expander_handle_t io_expander_handle)
{
    if (!io_expander_handle) {
        ESP_LOGE(TAG, "IO expander handle is NULL");
        return ESP_FAIL;
    }

    leds_control_init(io_expander_handle);
    xTaskCreate(indication_task, "indication_task", INDICATION_TASK_STACK_SIZE, NULL, INDICATION_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Indication initialized");

    return ESP_OK;
}

//------------------------------------------------------------------------------
