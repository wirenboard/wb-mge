#include "esp_log.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_server.h"
#include "mb_role.h"
#include "network.h"
#include "setting_items.h"
#include "update_rs485_mio_gpio_states.h"
#include "wb_test.h"

// The Modbus stack of the built role. Both the slave and the master are restarted the same
// way — stop, then start, under the same non-recursive ownership lock — so the apply below
// is written once and only the names differ. In the "none" role there is no stack to
// restart and MODBUS_STACK_FLAG is never raised.
#if WB_MB_ROLE_SLAVE
    #include "mb_slave.h"
    #define MB_STACK_LOCK_WAIT_FOREVER  MB_SLAVE_LOCK_WAIT_FOREVER
    #define MB_STACK_LOCK_TAKE(ms)      mb_slave_lock_take(ms)
    #define MB_STACK_LOCK_GIVE()        mb_slave_lock_give()
    #define MB_STACK_STOP()             mb_slave_stop()
    #define MB_STACK_START()            mb_slave_start()
    #define MB_STACK_CHECK_CHANGED()    mb_slave_check_settings_changed()
#elif WB_MB_ROLE_MASTER
    #include "mb_master.h"
    #define MB_STACK_LOCK_WAIT_FOREVER  MB_MASTER_LOCK_WAIT_FOREVER
    #define MB_STACK_LOCK_TAKE(ms)      mb_master_lock_take(ms)
    #define MB_STACK_LOCK_GIVE()        mb_master_lock_give()
    #define MB_STACK_STOP()             mb_master_stop()
    #define MB_STACK_START()            mb_master_start()
    #define MB_STACK_CHECK_CHANGED()    mb_master_check_settings_changed()
#else
    #define MB_STACK_CHECK_CHANGED()    false
#endif


#define SETTINGS_UPDATE_TASK_STACK_SIZE     (6 * 1024)
#define SETTINGS_UPDATE_TASK_PRIORITY       5

#define MDNS_FLAG                           BIT8
#define HTTP_SERVER_FLAG                    BIT9
#define ETHERNET_FLAG                       BIT10
#define WIFI_FLAG                           BIT11
#define MODBUS_STACK_FLAG                   BIT12

#define HTTP_NETWORK_UPDATE_DELAY_MS        1000            // Delay before updating HTTP / Ethernet / WiFi settings


static const char *TAG = "settings_update";

// The in-flight apply: its task handle (NULL = none) and the flags it was started with.
//
// Both are written here and read from other tasks — settings_update_in_progress() and
// settings_update_restarts_web_server() are called from the httpd task — so the reads and
// the writes go through the atomic builtins rather than being plain loads and stores. The
// one exception is the handle's initial write, which FreeRTOS performs through the
// pxCreatedTask pointer handed to xTaskCreate(); it happens inside xTaskCreate(), before
// the new task is added to the ready list, so the handle is visible from the moment the
// task can first run.
//
// The flags are published BEFORE xTaskCreate() for the same reason: by the time a reader
// can see a non-NULL handle, the flags that go with it are already there.
static TaskHandle_t update_task_handle = NULL;
static uint32_t     update_task_flags = 0;

// Serialises the spawn decision in settings_update(): the "is one already in flight?" wait
// and the xTaskCreate() that follows it are one indivisible step.
//
// settings_update() runs on more than one task — the two httpd handlers
// (settings_manager.c, cmd_handler.c) and config_button_task, through the factory reset in
// main.c — so without this two callers could both pass the wait and both spawn a task. The
// second one's handle would overwrite the first's, the first would clear it on the way out,
// and settings_update_in_progress() would report "idle" with an apply still live: exactly
// the answer the factory clock_out test uses to decide it may take the RS-485 hardware.
//
// This is NOT the lock that guards the Modbus stop/start pair; that one is
// mb_slave_lock_take(), and it must stay separate. This one is held across a wait for the
// in-flight task to finish, and that task needs the mb_slave lock to do its work — one
// mutex for both jobs would deadlock the moment a second caller arrived during an apply
// that restarts the Modbus slave.
static SemaphoreHandle_t spawn_mutex;
static StaticSemaphore_t spawn_mutex_buffer;


// ── HTTP server ──────────────────────────────────────────────────────────────
// http_server provides its own check (http_server_check_settings_changed) and init/deinit, but
// not the release/acquire pair the two-phase apply needs, so it is built here on top of that
// public API.

// Release half: give up the web UI's listening socket, so a subsystem that is moving onto web_port
// can bind it in the acquire phase. Returns the port that was released, or 0 when the server was
// not running (or could not be stopped). The released port is handed to http_server_acquire() so a
// failed start on the new port can roll back to it.
static uint16_t http_server_release(void)
{
    uint16_t running_port = http_server_get_port();

    esp_err_t ret = http_server_deinit();
    if (ret != ESP_OK) {
        // Defensive only: http_server_deinit() drops its handle whatever httpd_stop() says (and
        // httpd_stop() can only fail on a NULL handle, which cannot happen here). So the server is
        // NOT listening any more, we simply do not know what happened to its socket — which is
        // exactly why running_port is not offered as a rollback target. The web UI is down at this
        // point; http_server_acquire() takes it from there (a failed start on the new port then
        // falls back to the default port).
        ESP_LOGE(TAG, "http_server_deinit failed: %s", esp_err_to_name(ret));
        return 0;
    }
    return running_port;
}

// Acquire half: start the web UI on the port NVS asks for. released_port is what
// http_server_release() stopped (0 = nothing was stopped).
//
// A start that fails and is left failed takes the web interface down until the power is pulled:
// http_server_check_settings_changed() reports "no change" while the server is stopped, so
// HTTP_SERVER_FLAG is never raised again and no later settings write can bring the server back —
// and the API that would fix the setting IS the web server. That was the path to a bricked device:
// POST {web_port: <a port another local listener already holds>} → no validation of web_port at the
// time → NVS written → deinit freed 80 → init on the busy port failed → the web UI was gone for
// good. Hence the ladder below: configured port → the port we just gave up → the default port.
//
// If none of them binds, that is the end of it: log and return. NO REBOOT — do not add one back.
// The ladder tells its rungs apart only by "ret != ESP_OK", while http_server_init_port() collapses
// every reason for a refusal into a single ESP_FAIL: out of heap, LWIP out of sockets (httpd alone
// takes up to MAX_OPEN_SOCKETS of CONFIG_LWIP_MAX_SOCKETS), a refused wifi_scan_init()/auth_init().
// Those causes sink every rung alike, so "no port bound" says nothing about the ports. A reboot
// also cannot repair a shortage that outlives it: the boot path calls http_server_init() again and
// meets the same refusal.
//
// What is left when the ladder runs out is a device that keeps running with a dead web UI until it
// is power-cycled.
static void http_server_acquire(uint16_t released_port)
{
    esp_err_t ret = http_server_init();
    if (ret == ESP_OK) {
        return;
    }
    ESP_LOGE(TAG, "http_server_init failed: %s", esp_err_to_name(ret));

    // Fallback 1: roll back to the port the web UI was serving. A web UI that is down on both the
    // old and the new port leaves the user with no way to undo the setting that broke it — worse
    // than any single failed port change. The rolled-back port deliberately diverges from NVS, so
    // http_server_check_settings_changed() keeps reporting a change and the next settings write
    // retries the move — as does the default-port fallback below, for the same reason: any port
    // other than the configured one keeps that flag raised.
    if (released_port != 0) {
        ESP_LOGW(TAG, "Rolling the HTTP server back to port %u", released_port);
        esp_err_t rb = http_server_init_port(released_port);
        if (rb == ESP_OK) {
            return;
        }
        ESP_LOGE(TAG, "Rollback to port %u also failed: %s", released_port, esp_err_to_name(rb));
    }

    // Fallback 2: the default port. Neither the configured port nor the one we vacated can be
    // bound, so try the one port that is not derived from the settings that just broke the server.
    // It may well be the port http_server_init() already tried (when NVS holds the default anyway)
    // — one wasted bind() attempt is a cheap price for not having to guess.
    if (released_port != HTTP_SERVER_DEFAULT_PORT) {
        ESP_LOGW(TAG, "Falling back to the default HTTP port %u", HTTP_SERVER_DEFAULT_PORT);
        if (http_server_init_port(HTTP_SERVER_DEFAULT_PORT) == ESP_OK) {
            return;
        }
        ESP_LOGE(TAG, "Fallback to the default port %u also failed", HTTP_SERVER_DEFAULT_PORT);
    }

    // Out of fallbacks. The web interface stays down until the device is power-cycled; the device
    // itself keeps running. See the comment above this function for why nothing more is attempted
    // here — in particular, why this must not become a reboot.
    ESP_LOGE(TAG, "HTTP server could not be started on any port, the web interface stays down "
                  "until the device is power-cycled");
}


static void settings_update_task(void *arg)
{
    uint32_t flags = (uint32_t)(uintptr_t)arg;
    ESP_LOGI(TAG, "Updating settings...");

    if (flags & (HTTP_SERVER_FLAG | ETHERNET_FLAG | WIFI_FLAG)) {
        // Small delay to let the response to the current POST /settings reach the client. It has
        // to happen BEFORE anything is torn down, because the web server itself is now released in
        // the phase below: a delay placed after the teardown would come too late, and the client's
        // POST would be answered by a closed socket.
        vTaskDelay(pdMS_TO_TICKS(HTTP_NETWORK_UPDATE_DELAY_MS));
    }

    // The web server owns a TCP listening socket, so moving it to another port is a release
    // followed by an acquire: the old socket has to be closed before the new one is bound.
    //
    // The Modbus slave owns one too (mb_tcp_port), which is why the two are applied as one
    // two-phase pass — every release first, then every acquire — rather than each closing
    // and reopening on its own. A single request can swap web_port and mb_tcp_port, and an
    // acquire that ran before the other side's release would meet EADDRINUSE on a port that
    // was about to become free. The web server is acquired LAST, as it always has been.
    uint16_t http_released_port = 0;
    if (flags & HTTP_SERVER_FLAG) {
        ESP_LOGD(TAG, "Releasing the HTTP server socket");
        http_released_port = http_server_release();
    }

    // Stop and restart, rather than reconfigure in place: the serial parameters live in the
    // UART driver esp-modbus installs at create time, and the unit id and TCP port are
    // create-time options of the controller instances, so there is nothing to change on a
    // running instance. mb_slave_start() re-reads all of them from NVS.
    //
    // Not while the factory clock-out test is running, though: it stopped the slave itself
    // precisely to take both TX lines for its LEDC waveform and both DE lines as plain
    // GPIOs, so mb_slave_start() here would re-install the UART drivers and call
    // uart_set_pin() on pads the test is driving — two owners on the same pins, and the
    // RS-485-2 bus the test must keep silent would go live. Deferring costs nothing: the
    // test's exit path runs mb_slave_start(), which re-reads every one of these settings
    // from NVS.
    //
    // Reading that guard and acting on it is ONE step, under the mb_slave lock: the test
    // raises the guard under the same lock, so it cannot slip in between the check and the
    // stop and end up tearing the same esp-modbus instances down from two tasks at once.
    // Waiting forever for the lock is safe — its only other holder is a clock_out
    // transition on the httpd task, which is bounded and never waits on this task.
#if (!WB_MB_ROLE_NONE)
    if (flags & MODBUS_STACK_FLAG) {
        if (!MB_STACK_LOCK_TAKE(MB_STACK_LOCK_WAIT_FOREVER)) {
            ESP_LOGE(TAG, "Modbus restart skipped: the ownership lock is unavailable");
        } else {
            if (wb_test_clock_out_active()) {
                ESP_LOGW(TAG, "Modbus restart deferred: the clock_out test owns the RS-485 pins");
            } else {
                ESP_LOGD(TAG, "Restarting the Modbus stack with the new settings");
                MB_STACK_STOP();
                MB_STACK_START();
            }
            MB_STACK_LOCK_GIVE();
        }
    }
#endif

    if (flags & HTTP_SERVER_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to HTTP server");
        http_server_acquire(http_released_port);
    }

    if (flags & MDNS_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to mDNS");
        network_update_mdns_settings();
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
    // The flags first, the handle last: the handle is what says "an apply is in flight", so
    // it must be the last thing to go. A reader that catches the intermediate state sees an
    // apply in flight with no flags — which is only ever read as "this apply is not about to
    // restart the web server", and by then it is not.
    __atomic_store_n(&update_task_flags, 0u, __ATOMIC_SEQ_CST);
    __atomic_store_n(&update_task_handle, (TaskHandle_t)NULL, __ATOMIC_SEQ_CST);
    vTaskDelete(NULL);
}


void settings_update_init(void)
{
    // Idempotent, and deliberately not re-creating an existing mutex: a second create would
    // hand back a new handle and strand whatever task is holding the old one.
    if (spawn_mutex != NULL) {
        return;
    }
    // Static allocation cannot fail, so there is no error path for app_main to handle.
    spawn_mutex = xSemaphoreCreateMutexStatic(&spawn_mutex_buffer);
}


bool settings_update_in_progress(void)
{
    return (__atomic_load_n(&update_task_handle, __ATOMIC_SEQ_CST) != NULL);
}


bool settings_update_restarts_web_server(void)
{
    // The handle first: the flags of a finished apply are cleared before the handle, so a
    // non-NULL handle is what makes the flags below meaningful.
    if (__atomic_load_n(&update_task_handle, __ATOMIC_SEQ_CST) == NULL) {
        return false;
    }
    return ((__atomic_load_n(&update_task_flags, __ATOMIC_SEQ_CST) & HTTP_SERVER_FLAG) != 0);
}


esp_err_t settings_update(void)
{
    // The factory clock-out test force-enables RS-485 bus V-out for the whole of its run,
    // on top of whatever KEY_485_VOUT says, so re-applying the stored state here would
    // switch V-out off under the measurement. The test restores it itself on the way out
    // by calling update_rs485_control(), so this is deferred, not dropped.
    if (!wb_test_clock_out_active()) {
        update_rs485_control();
    } else {
        ESP_LOGW(TAG, "RS485 control update deferred: the clock_out test owns V-out");
    }
    // Not guarded, and neither needs to be. update_serial_tx_disabled() ends in
    // mb_slave_set_tx_disabled(), which refuses to touch a DE pin while the serial half is
    // down — and it is down for the whole test, which is what released those pins to it in
    // the first place; mb_slave_start() re-applies the NVS value on the way out. The I/O
    // bus shares no pin with the test at all (the test never drives the RS-485-2 pair the
    // MIO controller hangs off), so its setting must reach the hardware immediately.
    update_serial_tx_disabled();
    update_io_bus_control();

    // From here to the xTaskCreate() below is one critical section: the wait for an
    // in-flight apply and the decision to spawn a new one have to be indivisible, or two
    // callers on two tasks both pass the wait and both create a task (see spawn_mutex).
    if ((spawn_mutex == NULL) || (xSemaphoreTake(spawn_mutex, portMAX_DELAY) != pdTRUE)) {
        // settings_update_init() was never called, or the take failed — neither is
        // reachable with a statically created mutex and portMAX_DELAY, but running the
        // spawn decision unserialised is not the way to find out.
        ESP_LOGE(TAG, "Settings update spawn lock unavailable, skipping the async update");
        return ESP_FAIL;
    }

    if (settings_update_in_progress()) {
        ESP_LOGW(TAG, "Previous settings have not yet been applied, waiting for setting update task finished");
        while (settings_update_in_progress()) {
            vTaskDelay(10);
        }
    }

    uint32_t flags = 0;

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

    // In the "none" role this collapses to `if (false)`, so the flag is never raised and
    // the apply above compiles out with nothing left to call.
    if (MB_STACK_CHECK_CHANGED()) {
        ESP_LOGD(TAG, "Modbus settings were changed");
        flags |= MODBUS_STACK_FLAG;
    }

    if (flags) {
        ESP_LOGI(TAG, "Some settings were changed, starting settings update task");
        // Published before the task exists, so no reader can see the handle without them.
        __atomic_store_n(&update_task_flags, flags, __ATOMIC_SEQ_CST);
        BaseType_t ret = xTaskCreate(settings_update_task, "settings_update_task", SETTINGS_UPDATE_TASK_STACK_SIZE,
                                    (void*)(uintptr_t)flags, SETTINGS_UPDATE_TASK_PRIORITY, &update_task_handle);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Unable to create settings update task");
            __atomic_store_n(&update_task_flags, 0u, __ATOMIC_SEQ_CST);
            xSemaphoreGive(spawn_mutex);
            return ESP_FAIL;
        }
    }

    xSemaphoreGive(spawn_mutex);
    return ESP_OK;
}

#ifdef __unittest_env__
    void settings_update_reset(void)
    {
        update_task_handle = NULL;
        update_task_flags = 0;
    }
#endif
