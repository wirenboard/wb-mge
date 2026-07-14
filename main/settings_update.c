#include "esp_log.h"
#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_server.h"
#include "port_manager.h"
#include "network.h"
#include "setting_items.h"
#include "update_rs485_mio_gpio_states.h"
#include "cache_modbus_server.h"


#define SETTINGS_UPDATE_TASK_STACK_SIZE     (6 * 1024)
#define SETTINGS_UPDATE_TASK_PRIORITY       5

#define BRIDGE_FLAGS_BASE                   BIT0
#define MDNS_FLAG                           BIT8
#define HTTP_SERVER_FLAG                    BIT9
#define ETHERNET_FLAG                       BIT10
#define WIFI_FLAG                           BIT11
#define CACHE_MODBUS_FLAG                   BIT12

#define HTTP_NETWORK_UPDATE_DELAY_MS        1000            // Delay before updating HTTP / Ethernet / WiFi settings


static const char *TAG = "settings_update";

static TaskHandle_t update_task_handle = NULL;


// ── Cache Modbus TCP server ──────────────────────────────────────────────────
// cache_modbus_server exposes init/deinit/get_port, so its check/release/acquire trio is built
// here on top of that public API — the same shape port_manager and http_server provide for
// themselves. The lifecycle used to live inline in the POST /settings handler
// (settings_manager.c), which ran it BEFORE settings_update() had touched the RS-485 ports or the
// web server: no port could ever be handed over between them.

// The port NVS asks the server to listen on; 0 means "must be stopped". That is also what
// cache_modbus_server_get_port() reports for a stopped server, so one comparison of the two covers
// every transition there is: start, stop and port change.
static int cache_modbus_wanted_port(void)
{
    if (!setting_items_read_bool(KEY_CACHE_MODBUS_SERVER_ENABLED)) {
        return 0;
    }
    int port = setting_items_read_int(KEY_CACHE_MODBUS_PORT);
    if (port <= 0) {
        port = CACHE_MODBUS_SERVER_PORT;    // unset / invalid: the compiled-in default
    }
    return port;
}

static bool cache_modbus_server_check_settings_changed(void)
{
    return cache_modbus_wanted_port() != cache_modbus_server_get_port();
}

// Release half: give up the listening socket when the server must stop or move to another port.
// Returns the port that was released, or 0 when the server keeps (or never had) its socket. The
// released port is handed to cache_modbus_server_acquire() so a failed start can roll back to it.
static int cache_modbus_server_release(void)
{
    int running_port = cache_modbus_server_get_port();

    if (running_port <= 0 || running_port == cache_modbus_wanted_port()) {
        return 0;       // not running, or already listening where it should — nothing to give up
    }

    esp_err_t ret = cache_modbus_server_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cache_modbus_server_deinit failed: %s", esp_err_to_name(ret));
        return 0;       // still listening on running_port; acquire() detects that and stays away
    }
    return running_port;
}

// Acquire half: start the server on the port NVS asks for. released_port is what
// cache_modbus_server_release() stopped (0 = nothing was stopped).
static void cache_modbus_server_acquire(int released_port)
{
    int wanted_port = cache_modbus_wanted_port();
    int running_port = cache_modbus_server_get_port();

    if (wanted_port == 0 || wanted_port == running_port) {
        return;         // must stay stopped, or already listening on the wanted port
    }

    if (running_port > 0) {
        // The release phase failed to stop the old listener. Starting a second one would orphan it
        // (deinit only frees the latest descriptor), so leave the server as it is.
        ESP_LOGE(TAG, "cache Modbus server still listening on port %d, not starting it on %d",
                 running_port, wanted_port);
        return;
    }

    esp_err_t ret = cache_modbus_server_init(wanted_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cache_modbus_server_init(%d) failed: %s", wanted_port, esp_err_to_name(ret));
        // Roll back to the port the server was serving so it does not stay down entirely.
        if (released_port > 0) {
            esp_err_t rb = cache_modbus_server_init(released_port);
            if (rb != ESP_OK) {
                ESP_LOGE(TAG, "Rollback to port %d also failed: %s", released_port, esp_err_to_name(rb));
            }
        }
    }
}


// ── HTTP server ──────────────────────────────────────────────────────────────
// http_server provides its own check (http_server_check_settings_changed) and init/deinit, but
// not the release/acquire pair the two-phase apply needs, so it is built here on top of that
// public API — the same shape as the cache Modbus server's above.

// Release half: give up the web UI's listening socket, so a subsystem that is moving onto
// web_port can bind it in the acquire phase.
static void http_server_release(void)
{
    esp_err_t ret = http_server_deinit();
    if (ret != ESP_OK) {
        // Defensive only: http_server_deinit() drops its handle whatever httpd_stop() says (and
        // httpd_stop() can only fail on a NULL handle, which cannot happen here). So the server is
        // NOT listening any more; we simply do not know what became of its socket.
        ESP_LOGE(TAG, "http_server_deinit failed: %s", esp_err_to_name(ret));
    }
}

// Acquire half: start the web UI on the port NVS asks for.
static void http_server_acquire(void)
{
    esp_err_t ret = http_server_init();
    if (ret != ESP_OK) {
        // The result used to be discarded entirely, which is how a settings write could take the
        // web interface down without a trace in the log.
        ESP_LOGE(TAG, "http_server_init failed: %s", esp_err_to_name(ret));
    }
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

    // Two-phase apply for every subsystem that owns a TCP listening socket — the web server
    // (web_port), the cache Modbus TCP server and the RS-485 gateways: FIRST all of them give up
    // the sockets that have to change (release), THEN all of them bind the new ones (acquire).
    //
    // Applying subsystem by subsystem could not express a port hand-over. Two reproducible ways to
    // kill a port with a single POST /settings:
    //   - {web_port: 8080, rs485_1.bridge.port: 80} while web=80 and bridge1=8080. Validation
    //     compares the NEW values and passes. The port was then re-initialized while httpd was
    //     still listening on 80 -> EADDRINUSE, and port_manager_apply_settings() has no rollback,
    //     so RS-485-1 stayed dead until the next settings write or a reboot.
    //   - {cache_modbus_port: 8080, rs485_1.bridge.port: 502} while cache=502 (on) and
    //     bridge1=8080. Same thing in both directions at once: the cache server could not bind
    //     8080 (the bridge still had it), rolled back to 502 — which the bridge was by then trying
    //     to take. One dead port, one server on the wrong port, and NVS matching neither.
    //
    // The price, paid knowingly: the outage window is wider. Applying subsystem by subsystem
    // brought port 1 back up before port 2 was even touched, and each subsystem was down only for
    // its own restart. Now every subsystem whose settings changed is down for the WHOLE
    // release->acquire window, so both RS-485 gateways, the cache server and the web UI can be off
    // the air at the same time; traffic arriving in that window is lost (it was lost across the old
    // per-subsystem restart too — the window is just longer now, on the order of the port re-init
    // time, not a new class of loss).
    //
    // That is the unavoidable cost of a correct hand-over: a socket can only move between two
    // subsystems if the giver closed it before the taker binds it, and nothing here can know which
    // subsystems are trading ports without the release phase having happened first. A settings
    // write is a rare, user-initiated event; a few hundred milliseconds of extra downtime on it is
    // cheaper than a port that stays dead until the next one. Do not "optimise" this back into a
    // per-subsystem apply.
    if (flags & HTTP_SERVER_FLAG) {
        ESP_LOGD(TAG, "Releasing the HTTP server socket");
        http_server_release();
    }

    int cache_released_port = 0;
    if (flags & CACHE_MODBUS_FLAG) {
        ESP_LOGD(TAG, "Releasing the cache Modbus TCP server socket");
        cache_released_port = cache_modbus_server_release();
    }

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        if (flags & (BRIDGE_FLAGS_BASE << index)) {
            ESP_LOGD(TAG, "Releasing port %u via port_manager", index + 1);
            port_manager_release(index);
        }
    }

    for (unsigned index = 0; index < BRIDGES_COUNT; index++) {
        if (flags & (BRIDGE_FLAGS_BASE << index)) {
            ESP_LOGD(TAG, "Applying new settings to port %u via port_manager", index + 1);
            port_manager_apply_settings(index);
        }
    }

    if (flags & CACHE_MODBUS_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to the cache Modbus TCP server");
        cache_modbus_server_acquire(cache_released_port);
    }

    if (flags & HTTP_SERVER_FLAG) {
        ESP_LOGD(TAG, "Applying new settings to HTTP server");
        http_server_acquire();
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
    update_task_handle = NULL;
    vTaskDelete(NULL);
}


esp_err_t settings_update(void)
{
    // The factory clock_out test owns part of the RS-485 hardware while it runs: it forces
    // V-out on, drives the TX pins of BOTH ports with the LEDC, and holds the DE pin of
    // both ports as a plain GPIO — port 1's HIGH (that driver transmits), port 2's LOW
    // (that driver stays in receive, so the shared RS-485-2 pair is not driven).
    // Re-applying those two settings here would undo that:
    //   - update_rs485_control() would push the configured vout value over the test's;
    //   - update_serial_tx_disabled() is NOT the pure software flag it looks like:
    //     serial_set_tx_disabled() does gpio_reset_pin()/gpio_set_level()/
    //     gpio_set_direction() on the port's dir_pin (or, for tx_disabled=false,
    //     uart_set_pin() back to the UART) — and those dir pins are exactly the DE pins
    //     the test is holding: SERIAL_IO_PIN_1, kept HIGH for port 1, and SERIAL_IO_PIN_2,
    //     parked LOW for port 2. It would drop the port-1 driver mid-waveform, or hand the
    //     parked port-2 pin back to the UART. Today it happens to be harmless only
    //     because the frozen ports sit in PM_MODE_DISABLED, so port_manager_set_tx_disabled()
    //     finds no serial_desc and returns early — an accident of the disable order, not a
    //     property of the call. Gate it rather than depend on that.
    // Skipped while the ports are frozen, exactly as the port re-init below is skipped.
    // Nothing is lost: wb_test's exit path calls update_rs485_control() itself, and
    // port_manager_apply_settings() re-applies tx_disabled from NVS when it brings each
    // port back up — so settings written during the test take effect when the test ends.
    //
    // update_io_bus_control() is deliberately NOT gated. The MIO controller shares the
    // RS-485-2 pair, but the test never drives that pair: it toggles only the logic-side
    // TX (DI) line of port 2 to blink LED2, and it holds that transceiver's DE line
    // (CLK_OUT_DE_PARK_PIN = SERIAL_IO_PIN_2) driven LOW for the whole test. That LOW is
    // the FIRMWARE's doing, not the hardware's: disabling a port never releases its dir
    // pin (serial_deinit() does not gpio_reset_pin() it), so the board's weak pulldown
    // never gets a say — wb_test takes the pin and drives it. With DE low the port-2
    // driver stays in receive, the RS-485-2 pair is silent, and MIO owns the bus alone, so
    // taking MIO in or out of reset collides with nothing. Gating it would only mean an
    // io_bus_enabled written during the test never reached the hardware, since wb_test's
    // exit path does not re-apply it.
    //
    // The flag is read here without any lock (see the locking contract in port_manager.c):
    // unlike the port re-init below, these calls do not touch pm_ctx, so there is no
    // pm_lock that would exclude them against wb_test. That leaves a narrow window — read
    // false, get preempted, the test starts, resume and re-apply V-out / tx_disabled on top
    // of it. settings_update() has three callers: the httpd task — POST /settings
    // (settings_manager.c) and POST /cmd "set_default_settings" (cmd_handler.c) — and the
    // button task (main.c, factory reset on long press). So it is a real window, just a very
    // small one. Closing it needs a lock shared with wb_test's entry/exit sequences (held
    // across "check frozen + apply" here and across "freeze + disable the ports + start
    // LEDC" there); it would take no other lock inside, so it cannot deadlock with pm_lock.
    if (!port_manager_ports_frozen()) {
        update_rs485_control();
        update_serial_tx_disabled();
    }

    // Independent of the freeze: the I/O bus is not part of what the test owns.
    update_io_bus_control();

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

    if (cache_modbus_server_check_settings_changed()) {
        ESP_LOGD(TAG, "Cache Modbus TCP server settings were changed");
        flags |= CACHE_MODBUS_FLAG;
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
