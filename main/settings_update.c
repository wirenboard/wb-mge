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

// How long settings_update_with_status() waits for a previous apply to finish, and how often it
// looks. See the join in that function for why the wait is bounded and what the number is sized
// against. The poll interval is in MILLISECONDS on purpose: the loop used to pass a raw 10 to
// vTaskDelay(), i.e. 10 ticks, which is 10 ms at the QEMU CONFIG_FREERTOS_HZ=1000 but 20 ms on
// the device (CONFIG_FREERTOS_HZ=500) — a poll that silently means two different things, and
// which no elapsed-time bound could be built on.
#define SETTINGS_UPDATE_JOIN_TIMEOUT_MS     15000u
#define SETTINGS_UPDATE_JOIN_POLL_MS        10u


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
// POST {web_port: <a port a bridge gateway is already serving>} → no validation of web_port at the
// time → NVS written → deinit freed 80 → init on the busy port failed → the web UI was gone for
// good. Hence the ladder below: configured port → the port we just gave up → the default port.
//
// If none of them binds, that is the end of it: log and return. NO REBOOT — do not add one back.
// The ladder tells its rungs apart only by "ret != ESP_OK", while http_server_init_port() collapses
// every reason for a refusal into a single ESP_FAIL: out of heap, LWIP out of sockets (httpd alone
// takes up to MAX_OPEN_SOCKETS of CONFIG_LWIP_MAX_SOCKETS, and the two bridge TCP servers and the
// cache server hold theirs on top), a refused wifi_scan_init()/auth_init(). Those causes sink every
// rung alike, so "no port bound" says nothing about the ports — a busy gateway would reboot itself
// mid-Modbus-traffic on a plain "Save" click, when waiting would have been enough. A reboot also
// cannot repair a shortage that outlives it: the boot path calls http_server_init() again and meets
// the same refusal.
//
// What is left when the ladder runs out is the behaviour this code had before the ladder existed:
// the gateway keeps routing Modbus — its actual job — with a dead web UI until it is power-cycled.
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

    // Out of fallbacks. The web interface stays down until the device is power-cycled; the gateway
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
    uint16_t http_released_port = 0;
    if (flags & HTTP_SERVER_FLAG) {
        ESP_LOGD(TAG, "Releasing the HTTP server socket");
        http_released_port = http_server_release();
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
    update_task_handle = NULL;
    vTaskDelete(NULL);
}


esp_err_t settings_update_with_status(esp_err_t *cache_apply_err)
{
    if (cache_apply_err != NULL) {
        *cache_apply_err = ESP_OK;
    }

    // The factory clock_out test owns part of the RS-485 hardware while it runs: it forces
    // V-out on, drives the TX pins of BOTH ports with the LEDC, and holds the DE pin of
    // both ports as a plain GPIO — port 1's HIGH (that driver transmits), port 2's LOW
    // (that driver stays in receive, so the shared RS-485-2 pair is not driven).
    // Re-applying those two settings here would undo that:
    //   - update_rs485_control() would push the configured vout value over the test's;
    //   - update_serial_tx_disabled() is NOT the pure software flag it looks like:
    //     serial_set_tx_disabled() does gpio_reset_pin()/gpio_set_level()/
    //     gpio_set_direction() on the port's dir_pin (or, for tx_disabled=false,
    //     uart_set_pin() back to the UART, re-applying the port's TX and RX pins along
    //     with the dir pin) — and those pins are exactly the ones the test is holding:
    //     the DE pins SERIAL_IO_PIN_1, kept HIGH for port 1, and SERIAL_IO_PIN_2, parked
    //     LOW for port 2, plus the TX pins of both ports, driven by the LEDC. It would
    //     drop the port-1 driver mid-waveform, hand the parked port-2 pin back to the
    //     UART, or pull both TX lines out from under the LEDC. Today it happens to be
    //     harmless only because the frozen ports sit in PM_MODE_DISABLED, so
    //     port_manager_set_tx_disabled() finds no serial_desc and returns early — an
    //     accident of the disable order, not a property of the call. Gate it rather than
    //     depend on that.
    // Skipped while the ports are frozen, exactly as the port re-init below is skipped.
    // Nothing is lost: wb_test's exit path calls update_rs485_control() itself, and
    // port_manager_apply_settings() re-applies tx_disabled from NVS when it brings each
    // port back up — so settings written during the test take effect when the test ends.
    //
    // update_io_bus_control() is deliberately NOT gated. The MIO controller shares the
    // RS-485-2 pair, but the test never drives that pair: it toggles only the logic-side
    // TX (DI) line of port 2 to blink LED2, and it holds that transceiver's DE line
    // (CLK_OUT_DE_PARK_PIN = SERIAL_IO_PIN_2) driven LOW for the whole test. That LOW is the
    // FIRMWARE's doing, not the hardware's: wb_test takes the pin and drives it, whatever
    // disabling the port left on it. What disabling leaves there is IDF-version dependent —
    // serial_deinit() never gpio_reset_pin()s the dir pin, but from v5.4.2 on the
    // uart_driver_delete() it calls releases the UART's pins itself (uart_release_pin() ->
    // gpio_output_disable(rts_io_num)), which clears the output driver and leaves the level to
    // whatever else acts on the pad — the board, or an internal pull-up left by an earlier
    // gpio_reset_pin(). On v5.4.1 and older nothing is released, so the pin keeps whatever the
    // firmware last put there: the UART's idle level, or a driven LOW if tx_disabled was set.
    // The argument below rests on neither: the test owns the pin for its whole run. With DE
    // low the port-2 driver stays in receive, the RS-485-2 pair is silent, and MIO owns the
    // bus alone, so taking MIO in or out of reset collides with nothing. Gating it would only
    // mean an io_bus_enabled written during the test never reached the hardware, since wb_test's
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

    // Join the previous apply before starting another one. This runs in the CALLER's task, and
    // the caller that matters is the single esp_http_server worker (POST /settings, POST /cmd
    // "set_default_settings"), so this wait is the one place where a wedged settings_update_task
    // takes the whole web interface with it: while it spins, the worker cannot answer any
    // request at all — not /info, not GET /settings, not the very POST that would undo the
    // setting that wedged it.
    //
    // It used to spin with no bound, and that is exactly what turned one stuck subsystem
    // teardown into a device that answered nothing until it was power-cycled. Bounded now, and
    // the timeout is REPORTED rather than worked around: on expiry this returns without
    // touching the running subsystems, because the previous apply still owns them (it is inside
    // the release/acquire window, holding a pm_lock and mid-way through moving listening
    // sockets) and a second one running through it concurrently is the port corruption the
    // two-phase apply exists to prevent.
    //
    // Nothing is lost by giving up: NVS has already been written by the caller, so the values
    // ARE saved. What is skipped is only applying them to the live device, which the next
    // settings write retries — every check_settings_changed() below compares the running state
    // against NVS, so the work simply stays pending — and which a reboot performs from NVS
    // anyway.
    //
    // 15 s covers a healthy apply with room to spare: HTTP_NETWORK_UPDATE_DELAY_MS (1 s) plus
    // two port re-inits plus the cache and web servers, which the QEMU e2e suite measures in
    // the low seconds even under load, and each subsystem teardown inside it is itself capped
    // (TCP_SERVER_DEINIT_WAIT_MS). It also stays clear of the ~30 s HTTP client timeout the API
    // tests use, so the client gets a real response instead of giving up on the socket.
    if (update_task_handle != NULL) {
        ESP_LOGW(TAG, "Previous settings have not yet been applied, waiting for setting update task finished");
        uint32_t waited_ms = 0;
        while (update_task_handle != NULL) {
            if (waited_ms >= SETTINGS_UPDATE_JOIN_TIMEOUT_MS) {
                ESP_LOGE(TAG, "Previous settings update task has not finished in %ums; the values "
                              "are saved in NVS but were not applied to the running device",
                         (unsigned)SETTINGS_UPDATE_JOIN_TIMEOUT_MS);
                return ESP_ERR_TIMEOUT;
            }
            vTaskDelay(pdMS_TO_TICKS(SETTINGS_UPDATE_JOIN_POLL_MS));
            waited_ms += SETTINGS_UPDATE_JOIN_POLL_MS;
        }
    }

    // The runtime cache overlay, reconciled against the cache_en_N keys the settings write just
    // put in NVS. settings_manager maps rs485_N.cache_en straight onto those keys and stops
    // there, so before this call the overlay only ever moved through POST /ports/N/cache: a
    // device could report cache_en=true on port 2 in /settings, cache_enabled=true on port 1 in
    // /info, and packets_processed stuck at 0 forever — and nothing at runtime healed it.
    //
    // HERE, and not in settings_update_task, for two reasons.
    //   - The result has to reach the client. settings_update_task is created below and runs
    //     after this function has returned, by which time settings_process_request_json() has
    //     already built and sent the POST /settings response; a failure raised there could only
    //     ever be a log line. Run synchronously, it becomes a "warnings" entry the UI shows.
    //   - It owns no listening socket, so it has no business in the release/acquire two-phase
    //     apply the task performs. Running it BEFORE that task also means each port comes back
    //     up already knowing the final overlay: port_init_mode() arms SNIFF_REASON_CACHE from
    //     it, so a port whose serial parameters changed in the same request is not armed twice.
    //
    // Not gated on port_manager_ports_frozen() either, unlike the two calls above: this touches
    // no TX/DE pin (its only live action is sniffer_enable/disable on a port whose serial is
    // open, and a frozen port has none), so during the factory test it merely records the
    // intent, which wb_test's exit path then applies along with everything else.
    esp_err_t cache_ret = port_manager_apply_cache_settings();
    if (cache_ret != ESP_OK) {
        // Logged whether or not anyone is listening — the out-parameter is optional and the
        // factory-reset paths pass NULL.
        ESP_LOGE(TAG, "Failed to apply the cache overlay to the running ports: %s",
                 esp_err_to_name(cache_ret));
        if (cache_apply_err != NULL) {
            *cache_apply_err = cache_ret;
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

esp_err_t settings_update(void)
{
    return settings_update_with_status(NULL);
}

#ifdef __unittest_env__
    void settings_update_reset(void)
    {
        update_task_handle = NULL;
    }
#endif
