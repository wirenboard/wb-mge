#include "port_manager.h"
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

// Hardware-logic headers: needed by both builds. In QEMU these resolve to the
// virtual IO bus (gpio_expander.h symbols come from virtual_io_qemu.c).
#include "rs485_control.h"
#include "mio_control.h"
#include "update_rs485_mio_gpio_states.h"
#include "indication.h"
#include "gpio_expander.h"

// QEMU build conditional includes
#if (QEMU_BUILD)
    #include "wifi_qemu_mock.h"
    #include "virtual_io_qemu.h"
#else
    #include "esp_io_expander_tca95xx_16bit.h"
    #include "driver/gpio.h"
#endif


#define STATUS_LED_REGULAR_BLINK_PERIOD_MS          1000
#define STATUS_LED_FACTORY_RESET_BLINK_PERIOD_MS    200
#define STATUS_LED_FACTORY_RESET_BLINK_COUNT        5

#define CONFIG_BTN_FACTORY_RESET_HOLD_TIME_MS       5000


static const char *TAG = "main";


// Available in both builds: config button + factory reset only touch
// setting_items/settings_update/indication, all of which run in QEMU too.
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

#if (!QEMU_BUILD)
    // System voltage monitoring event (voltage_monitor is excluded from QEMU).
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

    // Initialize GPIO expander before voltage monitoring
    // to reset all GPIOs to safe state anyway.
    // In QEMU these resolve to the virtual IO bus (RAM-backed expander).
    gpio_expander_init(NULL);
    rs485_control_init();
    mio_control_init();

    #if (!QEMU_BUILD)
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

    update_io_bus_control();

    print_setting_items();

    ESP_ERROR_CHECK(network_init());

    // Bring up the network-independent subsystems BEFORE the HTTP server starts: the sniffer
    // (queue, per-port response timers, WS mutex, WS task), the multimaster cache mutex, the
    // repeater mutex and the RS-485 monitors. http_server_init() registers URI handlers that
    // reach straight into those FreeRTOS handles — the sniffer WS endpoint is one "enable" +
    // "disable" message away from xTimerStop() on the response timer — and FreeRTOS
    // configASSERTs on a NULL handle, which on this build is a panic and a reboot, not a failed
    // request. Keep this call above http_server_init(), and do not fold it back into
    // port_manager_init(): that one stays behind the wait-for-network loop at the end of this
    // function, because the rest of it (cache Modbus server, TCP bridges) needs an interface to
    // bind to. The order is still an invariant.
    //
    // It is no longer the only thing holding this up, though. Every public entry point of the
    // sniffer now checks the handle it is about to use and degrades instead of panicking — the
    // WS endpoint answers 503 — the way cache_multimaster has always done with its mutex. The
    // order keeps the feature working; the checks keep a stray early request from rebooting the
    // device.
    //
    // How wide that window really is, since the loop below invites a wrong reading: it is
    // released by sys_info flags that network.c sets on LINK/ASSOCIATION events —
    // ETHERNET_EVENT_CONNECTED, WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_AP_STACONNECTED — and NOT
    // on IP_EVENT_ETH_GOT_IP / IP_EVENT_STA_GOT_IP, which only fill in the address strings.
    // Under SoftAP the window is therefore narrower — the counter is bumped when a client
    // associates, before it has finished DHCP and asked for a page — but narrower is not
    // closed: the loop below only samples that counter once a second
    // (vTaskDelay(pdMS_TO_TICKS(1000))), so the association happening early buys nothing
    // against a client that skips DHCP. One with a static address or a cached lease can
    // request a page almost anywhere inside that second. The real exposure is Ethernet/STA
    // on a static address (eth_dhcpc off), where the interface starts answering the instant
    // the link comes up while this task is still up to a poll interval from noticing. A web
    // UI tab left open somewhere, retrying its WebSocket, lands inside that second easily.
    //
    // Deliberately NOT ESP_ERROR_CHECK, for the same reason as http_server_init() below: the
    // only way any of this fails is out of memory. These subsystems used to be brought up
    // behind the wait-for-network loop, so a device with no network never reached them at all
    // and still served its configuration interface over SoftAP quite happily. Aborting here
    // would put a fresh boot-loop trigger exactly where the old code degraded — and a device
    // that cannot spare a mutex has no better luck on the next boot. Continuing is safe
    // precisely because of the checks described above: a handler that arrives now gets a
    // refusal, not a NULL handle.
    esp_err_t subsys_ret = port_manager_init_subsystems();
    if (subsys_ret != ESP_OK) {
        ESP_LOGE(TAG, "port_manager_init_subsystems failed: %s - continuing without the "
                      "affected subsystem, the gateway keeps running", esp_err_to_name(subsys_ret));
    }

    // Deliberately NOT ESP_ERROR_CHECK: a web server that will not start must not abort the boot.
    // This device is a Modbus gateway first — routing RS-485/TCP traffic is what it is installed
    // for, and it does that with no web interface at all. Nothing below needs a running httpd
    // either: every URI handler is registered inside http_server_init() itself, and
    // port_manager_init() (the gateway) does not touch it. The opposite direction — what those
    // handlers need from the rest of the boot — is what the call above takes care of.
    // An abort() here panics and reboots, and every cause that can make the start fail — too
    // little heap, no free LWIP socket, a web_port already taken by a bridge gateway, a refused
    // auth/wifi_scan init — survives the reboot and meets the next boot the same way: a panic
    // loop that takes the gateway down too, instead of one degraded feature. This matches how a
    // failed start is handled at runtime in settings_update.c: log it, carry on, leave the web
    // interface down until the device is power-cycled.
    esp_err_t http_ret = http_server_init();
    if (http_ret != ESP_OK) {
        ESP_LOGE(TAG, "http_server_init failed: %s - continuing without the web interface, "
                      "the gateway keeps running", esp_err_to_name(http_ret));
    }

    #if (QEMU_BUILD)
        // Bring up the virtual IO state bus after the network is up and BEFORE
        // indication/button init, so the bus is ready to capture LED task activity.
        virtual_io_init();
    #endif // QEMU_BUILD

    update_rs485_control();
    indication_init();
    indication_status_led_blink(STATUS_LED_REGULAR_BLINK_PERIOD_MS);
    config_button_init();
    config_button_set_longpress_callback(config_button_longpress_callback, CONFIG_BTN_FACTORY_RESET_HOLD_TIME_MS);

    ESP_LOGI("main", "Firmware version: %s", FIRMWARE_VERSION);

    while (1)
    {
        if ((sys_info.wifi_ap_connections_count > 0) ||
            sys_info.eth_is_connected ||
            sys_info.wifi_sta_is_connected)
        {
            ESP_ERROR_CHECK(port_manager_init());
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "Waiting for network connection");
        }
    }
}
