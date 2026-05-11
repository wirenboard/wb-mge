#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "http_server.h"
#include "nv_storage.h"
#include "setting_items.h"
#include "sys_info.h"
#include "config_button.h"
#include "voltage_monitor.h"
#include "network.h"
#include "settings_update.h"
#include "debug_log.h"

// QEMU build conditional includes
#if (QEMU_BUILD)
    #include "wifi_qemu_mock.h"
#else
    #include "esp_io_expander_tca95xx_16bit.h"
    #include "driver/gpio.h"
    #include "rs485_control.h"
    #include "mio_control.h"
    #include "update_rs485_mio_gpio_states.h"
    #include "indication.h"
    #include "gpio_expander.h"
#endif


#define STATUS_LED_REGULAR_BLINK_PERIOD_MS          1000
#define STATUS_LED_FACTORY_RESET_BLINK_PERIOD_MS    200
#define STATUS_LED_FACTORY_RESET_BLINK_COUNT        5

#define CONFIG_BTN_FACTORY_RESET_HOLD_TIME_MS       5000


static const char *TAG = "main";


#if (!QEMU_BUILD)
    static void factory_reset(void)
    {
        ESP_LOGI(TAG, "Resetting all settings to factory defaults...");
        ESP_ERROR_CHECK(setting_items_set_defaults(false));

        ESP_LOGI(TAG, "Factory reset completed! Settings will revert to defaults.");
        ESP_LOGI(TAG, "Device will continue running with default configuration.");
    }

    // Button long press callback for factory reset
    static void config_button_longpress_callback(unsigned press_time_ms)
    {
        ESP_LOGW(TAG, "Factory reset triggered by 5-second config button hold!");
        indication_status_led_blink_n_times(STATUS_LED_FACTORY_RESET_BLINK_PERIOD_MS, STATUS_LED_FACTORY_RESET_BLINK_COUNT);
        factory_reset();
        settings_update();
    }

    // System voltage monitoring event
    static void sys_voltage_event_callback(float voltage, bool is_ok)
    {
        rs485_bus_vout_set_allowed(is_ok);
        if (!is_ok) {
            ESP_LOGW(TAG, "System voltage protection alert, voltage: %.2f V", voltage);
        } else {
            ESP_LOGI(TAG, "System voltage protection release, voltage: %.2f V", voltage);
        }
    }
#endif


// Prints all settings to log
static inline void print_setting_items(void)
{
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};

    ESP_LOGI(TAG, "=== Current Settings ===");

    size_t count = setting_items_get_count();
    for (size_t i = 0; i < count; i++) {
        const char *key = setting_items_get_key_at(i);
        if (key) {
            // Skip printing any setting that contains 'pass' for security
            if ((key != NULL) && (strstr(key, "pass") != NULL)) {
                ESP_LOGI(TAG, "%s: [HIDDEN]", key);
                continue;
            }

            if (setting_items_read(key, value) == ESP_OK) {
                ESP_LOGI(TAG, "%s: %s", key, value);
            } else {
                ESP_LOGW(TAG, "%s: [not found]", key);
            }
        }
    }

    ESP_LOGI(TAG, "=== Settings printed (passwords hidden for security) ===");
}


void app_main(void)
{
    debug_log_init();

    #if (!QEMU_BUILD)
        // Initialize GPIO expander before voltage monitoring
        // to reset all GPIOs to safe state anyway
        gpio_expander_init(NULL);
        rs485_control_init();
        mio_control_init();

        ESP_ERROR_CHECK(voltage_monitor_init(sys_voltage_event_callback));
        float voltage = voltage_monitor_get_sys_voltage();
        if (!voltage_monitor_sys_voltage_is_ok()) {
            ESP_LOGW(TAG, "System voltage is out of working range, voltage: %.2f V", voltage);
        } else {
            ESP_LOGI(TAG, "System voltage: %.2f V", voltage);
        }
    #endif // QEMU_BUILD

    ESP_ERROR_CHECK(sys_info_init());

    ESP_ERROR_CHECK(nvs_init());
    ESP_ERROR_CHECK(setting_items_init());

    #if (!QEMU_BUILD)
        update_io_bus_control();
    #endif // QEMU_BUILD

    print_setting_items();

    ESP_ERROR_CHECK(network_init());
    ESP_ERROR_CHECK(http_server_init());

    #if (!QEMU_BUILD)
        update_rs485_control();
        indication_init();
        indication_status_led_blink(STATUS_LED_REGULAR_BLINK_PERIOD_MS);
        config_button_init();
        config_button_set_longpress_callback(config_button_longpress_callback, CONFIG_BTN_FACTORY_RESET_HOLD_TIME_MS);
    #endif // QEMU_BUILD

    ESP_LOGI("main", "Firmware version: %s", FIRMWARE_VERSION);
}
