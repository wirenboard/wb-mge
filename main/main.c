#include "bridge.h"
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "http_server.h"
#include "nv_storage.h"
#include "serial.h"
#include "setting_items.h"
#include "sys_info.h"
#include "config_button.h"
#include "voltage_monitor.h"
#include "network.h"
#include "settings_update.h"
#include "debug_log.h"
#include "knx_server.h"
#include "esp_mac.h"

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

/* Global MAC for knxd eibnetserver.cpp on ESP32 */
uint8_t g_knx_mac[6] = {0};

/* Cached config + counter for the supervisor that restarts knx_server
 * if the knxd task dies (e.g. after the NCN5120 TPUART, which is bus-
 * powered, goes silent during a KNX bus power loss). */
static knx_server_config_t s_knx_cfg;
static uint32_t s_knx_restart_count = 0;
uint32_t knx_get_restart_count(void) { return s_knx_restart_count; }

static void knx_supervisor_task(void *arg)
{
    (void)arg;
    /* Backoff between attempts: NCN5120 may stay silent until the bus
     * comes back, so retrying every 5s is enough — restart itself
     * (PBKDF2 cached, ev_loop, factories) takes ~1s. */
    const TickType_t poll_period = pdMS_TO_TICKS(5000);
    while (1) {
        vTaskDelay(poll_period);
        if (knx_server_is_running()) continue;
        ESP_LOGW(TAG, "KNX server not running, restarting (attempt #%lu)",
                 (unsigned long)(s_knx_restart_count + 1));
        esp_err_t err = knx_server_start(&s_knx_cfg);
        if (err == ESP_OK) {
            s_knx_restart_count++;
            ESP_LOGI(TAG, "KNX restarted");
        } else {
            ESP_LOGE(TAG, "KNX restart failed: %s", esp_err_to_name(err));
        }
    }
}


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

    while (1)
    {
        if ((sys_info.wifi_ap_connections_count > 0) ||
            sys_info.eth_is_connected ||
            sys_info.wifi_sta_is_connected)
        {
            // Claim UART1 for KNX before bridge starts (bridge will skip port 1)
            bool knx_enabled = setting_items_read_bool(KEY_KNX_ENABLED);
            if (knx_enabled) {
                uart_driver_install(KNX_UART_NUM, 512, 512, 0, NULL, 0);
            }

            ESP_ERROR_CHECK(bridge_init());

            if (knx_enabled) {
                s_knx_cfg.tcp_port = (uint16_t)setting_items_read_int(KEY_KNX_PORT);
                setting_items_read(KEY_KNX_DEVICE_AUTH, s_knx_cfg.device_auth);
                setting_items_read(KEY_KNX_USER_PASS, s_knx_cfg.user_password);

                esp_read_mac(s_knx_cfg.serial_number, ESP_MAC_ETH);
                memcpy(g_knx_mac, s_knx_cfg.serial_number, 6);

                // WBE2-I-KNX module: NCN5121 TPUART
                s_knx_cfg.uart_num = KNX_UART_NUM;
                s_knx_cfg.uart_rx_pin = KNX_UART_RX_PIN;
                s_knx_cfg.uart_tx_pin = KNX_UART_TX_PIN;
                s_knx_cfg.uart_baud = KNX_UART_BAUD;

                esp_err_t err = knx_server_start(&s_knx_cfg);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "KNX IP Secure server started on port %d", s_knx_cfg.tcp_port);
                } else {
                    ESP_LOGE(TAG, "KNX IP Secure server failed to start: %s", esp_err_to_name(err));
                }
                xTaskCreate(knx_supervisor_task, "knx_super", 3072, NULL, 4, NULL);
            }

            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "Waiting for network connection");
        }
    }
}
