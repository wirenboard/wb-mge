#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "http_server.h"
#include "mb_role.h"
#include "nv_storage.h"
#include "setting_items.h"
#include "sys_info.h"
#include "config_button.h"
#include "voltage_monitor.h"
#include "network.h"
#include "settings_update.h"
#include "debug_log.h"

// The Modbus stack of the built role, and the example task that goes with it. In the "none"
// role neither header exists on this build and none of the calls below is made: the two
// UARTs are simply left alone.
#if WB_MB_ROLE_SLAVE
    #include "mb_slave.h"
    #include "user_app.h"
#elif WB_MB_ROLE_MASTER
    #include "mb_master.h"
    #include "user_app.h"
#endif

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

    // The two locks that keep the tasks below out of each other's way: the Modbus stack
    // ownership lock (the stop/start pair is run from the httpd task, settings_update_task
    // and, on a factory reset, config_button_task) and the one that serialises the spawn
    // decision in settings_update(). Created here, before any of those tasks exists;
    // neither call can fail.
    #if WB_MB_ROLE_SLAVE
        mb_slave_lock_init();
    #elif WB_MB_ROLE_MASTER
        mb_master_lock_init();
    #endif
    settings_update_init();

    update_io_bus_control();

    print_setting_items();

    // Both RS-485 transceivers into receive before anything else touches those pins. A no-op
    // unless this is a "none" build; see the comment on the function for why that role needs
    // it and the other two must not have it.
    park_serial_direction_pins();

    // The two RS-485 slaves come up HERE, before the network and before the web server.
    // Everything they need is the per-port serial parameters, which are readable the
    // moment setting_items_init() returns — and this device's primary use case is a board
    // sitting on a controller's RS-485 bus with no Ethernet cable and no Wi-Fi at all.
    // Gating them on a link, the way the TCP instance is gated at the end of this function,
    // would mean such a device never opened its ports.
    //
    // Deliberately NOT ESP_ERROR_CHECK: this is the one function the device is installed
    // for, so aborting on it takes that down instead of degrading it. Every cause that can
    // make a port fail — too little heap, a UART that could not be installed — survives a
    // reboot and meets the next boot the same way, so a panic loop is the one outcome to
    // avoid. mb_slave_start_serial() brings up each port independently and logs the ones
    // that did not make it.
    //
    // No mb_slave_lock_take() around this one, and that is not an oversight: nothing else
    // can be touching the Modbus instances yet. The web server (http_server_init() below)
    // and config_button_task (config_button_init() below) are the two tasks that reach
    // settings_update_task, and neither has been created at this point. The TCP start at
    // the end of app_main is a different matter — by then they both exist, so it takes the
    // lock.
    #if WB_MB_ROLE_SLAVE
        esp_err_t mb_serial_ret = mb_slave_start_serial();
        if (mb_serial_ret != ESP_OK) {
            ESP_LOGE(TAG, "mb_slave_start_serial failed: %s - continuing with whatever came up",
                     esp_err_to_name(mb_serial_ret));
        }
    #elif WB_MB_ROLE_MASTER
        // The master role opens the same two UARTs, at the same point and for the same
        // reason — it just drives the buses instead of answering them.
        esp_err_t mb_serial_ret = mb_master_start_serial();
        if (mb_serial_ret != ESP_OK) {
            ESP_LOGE(TAG, "mb_master_start_serial failed: %s - continuing with whatever came up",
                     esp_err_to_name(mb_serial_ret));
        }
    #endif

    // The example application task, and the file a DIY user replaces. Started here rather
    // than at the end of app_main for the same reason the serial half is: the wait loop
    // down there never exits on a device with no network, so anything after it would never
    // run.
    //
    // AFTER the Modbus bring-up above, and both roles need that, for their own reason:
    //   - slave: the task blocks on mb_slave_event_queue(), and that queue exists only once
    //     the call above has initialised the register framework — xQueueReceive() on a
    //     handle that does not exist yet is a FreeRTOS assert;
    //   - master: the task's first poll must meet instances that are already up, or it comes
    //     straight back with ESP_ERR_INVALID_STATE before a single frame goes on the bus.
    #if (!WB_MB_ROLE_NONE)
        user_app_start();
    #endif

    ESP_ERROR_CHECK(network_init());

    // Deliberately NOT ESP_ERROR_CHECK: a web server that will not start must not abort the boot.
    // An abort() here panics and reboots, and every cause that can make the start fail — too
    // little heap, no free LWIP socket, a web_port already held by another listener, a refused
    // auth/wifi_scan init — survives the reboot and meets the next boot the same way: a panic
    // loop instead of one degraded feature.
    esp_err_t http_ret = http_server_init();
    if (http_ret != ESP_OK) {
        ESP_LOGE(TAG, "http_server_init failed: %s - continuing without the web interface",
                 esp_err_to_name(http_ret));
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

    // Modbus TCP belongs to the slave role and to no other, so this whole wait exists only
    // there. In the master and "none" roles app_main simply returns here, and the FreeRTOS
    // main task ends — everything the device does from then on runs on the tasks started
    // above.
#if WB_MB_ROLE_SLAVE
    // Only the Modbus TCP instance waits here. The RS-485 pair has been answering since
    // long before this point, and nothing below it is needed by anything else — this loop
    // is the last thing app_main does, and on a device that never gets a link it simply
    // never ends.
    //
    // The flags are set by network.c on LINK/ASSOCIATION events (ETHERNET_EVENT_CONNECTED,
    // WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_AP_STACONNECTED), not on the IP_EVENTs that
    // only fill in the address strings.
    while (1)
    {
        if ((sys_info.wifi_ap_connections_count > 0) ||
            sys_info.eth_is_connected ||
            sys_info.wifi_sta_is_connected)
        {
            // Deliberately NOT ESP_ERROR_CHECK, for the same reason as the serial half
            // above: a TCP port another listener already holds, or a socket that could not
            // be allocated, survives a reboot and meets the next boot the same way. Losing
            // the TCP transport must not cost the device the two RS-485 ones as well.
            //
            // This is the only call, and the loop breaks either way — but a failure is not
            // final: mb_slave_start_tcp() records that a TCP instance was asked for, so
            // mb_slave_check_settings_changed() reports a pending change while it is down
            // and the next POST /settings retries the listener.
            //
            // Under the ownership lock, unlike the serial start earlier in this function:
            // the web server and config_button_task are both up by now, and that "records
            // that a TCP instance was asked for" is the hazard. mb_tcp_wanted is set at the
            // TOP of mb_slave_start_tcp(), so from that instant until mb_tcp_running goes
            // true, any POST /settings or button factory reset makes
            // mb_slave_check_settings_changed() answer true through "wanted and not
            // running" — and settings_update_task would then run its own stop/start over
            // the instance being created here. Whichever way that interleaves it ends
            // badly: a delete under the create, or the error path below deleting the
            // instance the apply had just started while mb_tcp_running stays true. The
            // latter is the unrecoverable one — "wanted and not running" is false, and the
            // port comparison sits behind "if (mb_tcp_running)", so no settings write can
            // ever see it, let alone repair it.
            //
            // Taken here rather than inside mb_slave_start_tcp(): the mutex is not
            // recursive, and mb_slave_start() calls that function while already holding it.
            bool mb_locked = mb_slave_lock_take(MB_SLAVE_LOCK_WAIT_FOREVER);
            if (!mb_locked) {
                // Unreachable: mb_slave_lock_init() is called unconditionally above.
                // Starting unguarded still beats not starting at all — without this call
                // mb_tcp_wanted is never set, and then no settings write ever retries the
                // listener either.
                ESP_LOGE(TAG, "Modbus ownership lock unavailable, starting the TCP slave "
                              "without it");
            }
            esp_err_t mb_tcp_ret = mb_slave_start_tcp();
            if (mb_locked) {
                mb_slave_lock_give();
            }
            if (mb_tcp_ret != ESP_OK) {
                ESP_LOGE(TAG, "mb_slave_start_tcp failed: %s - the RS-485 ports keep "
                              "running and the device stays reachable",
                         esp_err_to_name(mb_tcp_ret));
            }
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGW(TAG, "Waiting for network connection");
        }
    }
#endif // WB_MB_ROLE_SLAVE
}
