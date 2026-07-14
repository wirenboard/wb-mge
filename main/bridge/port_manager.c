#include "port_manager.h"
#include "bridge.h"
#include "repeater.h"
#include "sniffer.h"
#include "cache_multimaster.h"
#include "cache_modbus_server.h"
#include "serial.h"
#include "setting_items.h"
#include "rs485_stats.h"
#include "auth.h"
#include "json_utils.h"
#include "modbus_helpers.h"

#include "esp_check.h"
#include "esp_log.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>

/* Allow unit tests to access helper functions that are otherwise static */
#ifdef __unittest_env__
#define PORT_MANAGER_STATIC
#else
#define PORT_MANAGER_STATIC static
#endif

static const char *TAG = "port_manager";

// Per-port runtime context.
typedef struct {
    pm_mode_t       mode;               // Currently active transport mode.
    bool            cache_overlay;      // Persisted cache-overlay state for this port.
                                        // Orthogonal to transport mode; survives mode changes.
    serial_desc_t  *serial_desc;        // Non-NULL only for PASSIVE mode.
                                        // For TCP_BRIDGE the serial_desc lives inside bridge_ctx.
    serial_config_t serial_cfg_at_init; // Serial config snapshot taken at port init time,
                                        // used to detect serial parameter changes for
                                        // PASSIVE mode.
    SemaphoreHandle_t init_mutex;       // Serialises port_init_mode/port_deinit_mode against
                                        // races between the HTTP set_mode handler and the
                                        // async settings_update_task (both can trigger a
                                        // port reinit; without serialisation they collide on
                                        // uart_driver_install and the second one gets
                                        // ESP_FAIL with "UART driver already installed").
} pm_ctx_t;

static pm_ctx_t pm_ctx[BRIDGES_COUNT] = {0};

// Set while the factory 100 kHz clock-out test owns the RS-485 TX/DE pins.
// The test puts both ports into DISABLED transiently (runtime only — NVS keeps
// the user's configured mode), which makes the runtime mode differ from NVS.
// Without this flag check_settings_changed() would report "changed" for both
// ports, so ANY unrelated POST /settings would run apply_settings() and re-init
// the ports — re-grabbing the TX/DE pins that the factory test is currently
// driving (the LEDC on the TX lines, plain GPIO writes on the DE lines).
// While frozen, check_settings_changed() reports no change, apply_settings() is a
// no-op and a persisting set_mode() (POST /ports/N/mode) is refused with the
// dedicated PM_ERR_PORTS_FROZEN, so the ports stay down for the whole test. Only the
// transient set_mode path stays open — that is how the test disables them. The
// test clears the flag on exit and then calls apply_settings() itself to restore
// both ports from NVS (which also picks up any settings written during the test).
//
// Locking contract. The flag is written by the httpd task (the wb_test handler) and
// read from several tasks, so it is accessed with GCC atomics (SEQ_CST) — same
// convention as tcp_desc.active_connections. Readers fall into two groups:
//
//  * Port paths — port_set_mode_impl(), port_manager_apply_settings() and
//    port_manager_check_settings_changed() — read it while holding that port's
//    pm_lock, together with the runtime mode it guards. Since the test only starts
//    the LEDC after set_mode_transient() has taken (and released) both pm_locks, a
//    reader that saw the flag as false is by then either already finished or still
//    holding the lock the test is waiting on: it can no longer re-init a port after
//    the waveform is live. This is the ordering that matters — these are the paths
//    that would hand the TX/DE pins back to the UART.
//
//  * Non-port paths — settings_update() gating update_rs485_control() (V-out) and
//    update_serial_tx_disabled() — read it atomically with NO lock held. There is no
//    pm_lock to take there: those calls drive the GPIO expander and the serial layer,
//    not pm_ctx, and taking a port lock would only give a false sense of mutual
//    exclusion against a test that does not hold it either. update_io_bus_control()
//    (MIO reset) is NOT in this list: it is no longer gated by the flag at all and is
//    applied unconditionally, because the test does not own the I/O bus. The residual
//    window: settings_update() reads the flag as false, is preempted, the test freezes
//    the ports and starts, and settings_update() resumes and re-applies the configured
//    V-out / tx_disabled on top of the running test (tx_disabled lands on ports that
//    are DISABLED by then, so it is a no-op). It is accepted, not proved impossible:
//    the window is the few instructions between the read and the expander write, it
//    has to be hit by a concurrent POST /settings or a factory-reset button long-press
//    (main.c) on a board that is at that exact moment entering the factory test, and
//    the damage is bounded — nothing is persisted, and switching the test off
//    re-applies both settings anyway.
//    Closing it properly needs a lock the non-port paths and wb_test can share (see
//    the note in settings_update.c), not a wider pm_lock.
static bool s_ports_frozen = false;

static inline bool ports_frozen(void)
{
    return __atomic_load_n(&s_ports_frozen, __ATOMIC_SEQ_CST);
}

// Take the per-port init mutex. Lazily creates it on first use so we don't depend
// on init order (port_manager_init may not have run yet when an early handler is
// dispatched). portMAX_DELAY because the critical section is the entire reinit and
// callers genuinely need exclusive access.
static void pm_lock(unsigned index)
{
    if (pm_ctx[index].init_mutex == NULL) {
        pm_ctx[index].init_mutex = xSemaphoreCreateMutex();
    }
    if (pm_ctx[index].init_mutex) {
        xSemaphoreTake(pm_ctx[index].init_mutex, portMAX_DELAY);
    }
}

static void pm_unlock(unsigned index)
{
    if (pm_ctx[index].init_mutex) {
        xSemaphoreGive(pm_ctx[index].init_mutex);
    }
}

static SemaphoreHandle_t s_cache_decision_mutex; // lazily created; serialises global-cache lifetime decisions

static void cache_decision_lock(void)
{
    if (s_cache_decision_mutex == NULL) {
        s_cache_decision_mutex = xSemaphoreCreateMutex();
    }
    if (s_cache_decision_mutex) {
        xSemaphoreTake(s_cache_decision_mutex, portMAX_DELAY);
    }
}
static void cache_decision_unlock(void)
{
    if (s_cache_decision_mutex) {
        xSemaphoreGive(s_cache_decision_mutex);
    }
}

// Bring the global cache pool in line with persisted per-port intent.
// The pool is enabled iff at least one port has its cache overlay set, and is
// touched ONLY on a real have!=want transition — so a live pool is never wiped
// by a redundant enable(), and never freed while another port still wants it.
// Serialised so concurrent per-port operations cannot race the global decision.
// Must NOT take any pm_lock (it only reads cache_overlay) to keep lock ordering
// pm_lock→cache_decision_mutex and avoid deadlock.
static void cache_sync_global(void)
{
    cache_decision_lock();
    bool want = false;
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (pm_ctx[i].cache_overlay) { want = true; break; }
    }
    bool have = cache_multimaster_is_enabled();
    if (want && !have) {
        cache_multimaster_enable();
    } else if (!want && have) {
        cache_multimaster_disable();
    }
    cache_decision_unlock();
}

// ────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────

const char *port_manager_mode_to_str(pm_mode_t mode)
{
    switch (mode) {
    case PM_MODE_DISABLED:   return PORT_MODE_DISABLED_STR;
    case PM_MODE_TCP_BRIDGE: return PORT_MODE_TCP_BRIDGE_STR;
    case PM_MODE_PASSIVE:    return PORT_MODE_PASSIVE_STR;
    case PM_MODE_REPEATER:   return PORT_MODE_REPEATER_STR;
    default:                 return "unknown";
    }
}

static pm_mode_t str_to_pm_mode(const char *str)
{
    if (!str) {
        return PM_MODE_DISABLED;
    }
    if (strncmp(str, PORT_MODE_TCP_BRIDGE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return PM_MODE_TCP_BRIDGE;
    }
    if (strncmp(str, PORT_MODE_PASSIVE_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return PM_MODE_PASSIVE;
    }
    if (strncmp(str, PORT_MODE_REPEATER_STR, SETTING_ITEM_MAX_STR_LEN) == 0) {
        return PM_MODE_REPEATER;
    }
    return PM_MODE_DISABLED;
}

// Return the NVS key for the per-port cache overlay setting (cache_en_1 / cache_en_2).
static const char *cache_en_nvs_key(unsigned index)
{
    static const char *keys[BRIDGES_COUNT] = {KEY_CACHE_EN_1, KEY_CACHE_EN_2};
    if (index >= BRIDGES_COUNT) {
        return KEY_CACHE_EN_1;
    }
    return keys[index];
}

// Return the NVS key for the port mode setting (port_mode_1 / port_mode_2).
static const char *port_mode_nvs_key(unsigned index)
{
    static const char *keys[BRIDGES_COUNT] = {KEY_PORT_MODE1, KEY_PORT_MODE2};
    if (index >= BRIDGES_COUNT) {
        return KEY_PORT_MODE1;
    }
    return keys[index];
}

// Read the port mode from NVS for the given port index.
static pm_mode_t read_port_mode_from_nvs(unsigned index)
{
    char value[SETTING_ITEM_MAX_STR_LEN] = {0};
    esp_err_t ret = setting_items_read(port_mode_nvs_key(index), value);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Port[%u]: Failed to read port mode from NVS, defaulting to disabled", index + 1);
        return PM_MODE_DISABLED;
    }
    return str_to_pm_mode(value);
}

// Return the serial_desc for the given port regardless of which mode owns it.
// Returns NULL if the port has no active serial port (disabled or not running).
static serial_desc_t *get_port_serial_desc(unsigned index)
{
    switch (pm_ctx[index].mode) {
    case PM_MODE_TCP_BRIDGE:
        return bridge_get_serial_desc(index);
    case PM_MODE_PASSIVE:
        return pm_ctx[index].serial_desc;
    case PM_MODE_REPEATER:
        return pm_ctx[index].serial_desc;
    default:
        return NULL;
    }
}

// ────────────────────────────────────────────────────────────────
// Per-port init / deinit
// ────────────────────────────────────────────────────────────────

static esp_err_t port_init_mode(unsigned index, pm_mode_t mode)
{
    ESP_LOGI(TAG, "Port[%u]: Initializing mode '%s'", index + 1, port_manager_mode_to_str(mode));

    switch (mode) {
    case PM_MODE_DISABLED:
        // Nothing to do; serial port stays closed.
        break;

    case PM_MODE_TCP_BRIDGE:
        // bridge_port_init() opens serial + starts TCP subsystem.
        ESP_RETURN_ON_ERROR(bridge_port_init(index),
                            TAG, "Port[%u]: bridge_port_init failed", index + 1);
        // Attach sniffer so that WebSocket clients can passively observe traffic
        // when they connect.  sniffer_enable() is NOT called here — the WS
        // connection handler does that on demand.
        {
            serial_desc_t *sd = bridge_get_serial_desc(index);
            if (sd) {
                // RX timeout is owned by the transport mode. The TCP bridge uses the
                // longer PROXY inter-frame timeout; the sniffer/cache overlay must not
                // change it. serial_init() defaults to the short value, so set it here.
                serial_set_rx_timeout(sd, SERIAL_RX_TOUT_PROXY);
                sniffer_attach(index, sd);
            } else {
                ESP_LOGW(TAG, "Port[%u]: TCP bridge has no serial_desc (inner bridge_mode may be disabled), sniffer not attached", index + 1);
            }
        }
        break;

    case PM_MODE_PASSIVE:
        // Open serial-only (no TCP layer). The sniffer is attached but NOT
        // forced on — it only runs when a reason (WS display or cache) is set.
        ESP_RETURN_ON_ERROR(bridge_port_init_serial_only(index, &pm_ctx[index].serial_desc),
                            TAG, "Port[%u]: bridge_port_init_serial_only failed", index + 1);
        // Passive listener: use the short sniffer inter-frame timeout for clean Modbus
        // framing. RX timeout is owned by the transport mode, not the sniffer overlay.
        serial_set_rx_timeout(pm_ctx[index].serial_desc, SERIAL_RX_TOUT_SNIFFER);
        sniffer_attach(index, pm_ctx[index].serial_desc);
        // Save the serial config used at init so we can detect changes later.
        bridge_read_serial_config(index, &pm_ctx[index].serial_cfg_at_init);
        break;

    case PM_MODE_REPEATER: {
        serial_config_t cfg = {0};
        ESP_RETURN_ON_ERROR(bridge_read_serial_config(index, &cfg),
                            TAG, "Port[%u]: Failed to read serial config", index + 1);
        ESP_RETURN_ON_ERROR(repeater_init_port(index, &cfg, &pm_ctx[index].serial_desc),
                            TAG, "Port[%u]: repeater_init_port failed", index + 1);
        // Transparent low-latency passthrough: use the PROXY inter-frame RX timeout.
        serial_set_rx_timeout(pm_ctx[index].serial_desc, SERIAL_RX_TOUT_PROXY);
        // Attach the sniffer/cache overlay (orthogonal), same as PASSIVE/TCP_BRIDGE.
        sniffer_attach(index, pm_ctx[index].serial_desc);
        // Snapshot serial config to detect later parameter changes.
        bridge_read_serial_config(index, &pm_ctx[index].serial_cfg_at_init);
        break;
    }

    default:
        ESP_LOGE(TAG, "Port[%u]: Unknown mode %d", index + 1, (int)mode);
        return ESP_ERR_INVALID_ARG;
    }

    pm_ctx[index].mode = mode;

    // Re-apply the persisted cache overlay now that serial is (re)opened.
    // The sniffer must already be attached (done above) before enabling the CACHE reason.
    // Ensure the global pool matches persisted intent (allocates it if needed),
    // then wire this port's serial data into it.
    cache_sync_global();
    if (pm_ctx[index].cache_overlay && get_port_serial_desc(index) != NULL) {
        sniffer_enable(index, SNIFF_REASON_CACHE);
    }

    // Apply tx_disabled setting immediately after serial init
    static const char * const tx_disabled_nvs_keys[BRIDGES_COUNT] = {
        KEY_485_TX_DISABLED_1, KEY_485_TX_DISABLED_2
    };
    bool tx_disabled = setting_items_read_bool(tx_disabled_nvs_keys[index]);
    if (tx_disabled) {
        serial_desc_t *sd = get_port_serial_desc(index);
        if (sd != NULL) {
            serial_set_tx_disabled(sd, true);
        }
    }

    return ESP_OK;
}

static void port_deinit_mode(unsigned index)
{
    pm_mode_t mode = pm_ctx[index].mode;
    ESP_LOGI(TAG, "Port[%u]: Deinitializing mode '%s'", index + 1, port_manager_mode_to_str(mode));

    switch (mode) {
    case PM_MODE_DISABLED:
        // Nothing to deinit.
        break;

    case PM_MODE_TCP_BRIDGE:
        // Detach sniffer (clears all reasons incl. CACHE) before the serial port
        // is destroyed to prevent use-after-free.
        sniffer_detach(index);
        bridge_port_deinit(index);
        // bridge_port_deinit() clears bridge_ctx[index].serial_desc internally.
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    case PM_MODE_PASSIVE:
        // sniffer_detach() clears all reasons and the sniff_handler pointer.
        sniffer_detach(index);
        serial_deinit(pm_ctx[index].serial_desc);
        pm_ctx[index].serial_desc = NULL;
        memset(&pm_ctx[index].serial_cfg_at_init, 0, sizeof(pm_ctx[index].serial_cfg_at_init));
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    case PM_MODE_REPEATER:
        sniffer_detach(index);
        repeater_deinit_port(index);
        pm_ctx[index].serial_desc = NULL;
        memset(&pm_ctx[index].serial_cfg_at_init, 0, sizeof(pm_ctx[index].serial_cfg_at_init));
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    default:
        ESP_LOGW(TAG, "Port[%u]: Unknown mode %d during deinit — skipping", index + 1, (int)mode);
        break;
    }

    // The cache overlay setting (pm_ctx[index].cache_overlay) is intentionally NOT
    // cleared here: it must survive transport-mode changes.
    pm_ctx[index].mode = PM_MODE_DISABLED;

    // The global cache pool is intentionally NOT freed here. cache_overlay is
    // unchanged, so the pool (and its accumulated data) must persist across a
    // transport re-init. sniffer_detach(index) above already cleared this port's
    // CACHE reason, stopping data flow while serial is down — that is sufficient.
}

// ────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────

esp_err_t port_manager_init(void)
{
    // Initialise shared RS-485 infrastructure (previously done inside bridge_init()).
    rs485_busy_monitor_init();
    rs485_stats_init();

    // Create the repeater-global mutex before any port can enter repeater mode,
    // so the cross-port data path is protected from the first init onward.
    repeater_init();

    // Initialise global subsystems once.
    ESP_RETURN_ON_ERROR(sniffer_init(), TAG, "sniffer_init failed");
    ESP_RETURN_ON_ERROR(cache_multimaster_init(), TAG, "cache_multimaster_init failed");
    // Start cache Modbus TCP server only if enabled in NVS settings.
    bool cache_server_enabled = setting_items_read_bool(KEY_CACHE_MODBUS_SERVER_ENABLED);
    if (cache_server_enabled) {
        int cache_port = setting_items_read_int(KEY_CACHE_MODBUS_PORT);
        if (cache_port <= 0) cache_port = CACHE_MODBUS_SERVER_PORT;
        ESP_RETURN_ON_ERROR(cache_modbus_server_init(cache_port),
                            TAG, "cache_modbus_server_init failed");
    } else {
        ESP_LOGI(TAG, "Cache Modbus TCP server is disabled by settings");
    }

    // Bring up each port in the mode stored in NVS.
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        // Read the persisted cache overlay intent for this port.
        pm_ctx[i].cache_overlay = setting_items_read_bool(cache_en_nvs_key(i));
        pm_mode_t mode = read_port_mode_from_nvs(i);
        esp_err_t ret = port_init_mode(i, mode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Port[%u]: Initialization failed (mode '%s'): %s",
                     i + 1, port_manager_mode_to_str(mode), esp_err_to_name(ret));
            // Continue with remaining ports rather than aborting.
        }
    }

    ESP_LOGI(TAG, "Port manager initialized");
    return ESP_OK;
}

pm_mode_t port_manager_get_mode(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return PM_MODE_DISABLED;
    }
    return pm_ctx[port_index].mode;
}

esp_err_t port_manager_set_tx_disabled(unsigned port_index, bool disabled)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    // Hold pm_lock across both the serial_desc lookup and its use: it prevents
    // apply_settings()/set_mode() from freeing/recreating serial_desc concurrently
    // (use-after-free). Mirrors port_manager_set_cache()'s lock discipline.
    pm_lock(port_index);
    serial_desc_t *sd = get_port_serial_desc(port_index);
    if (sd == NULL) {
        // Port not running — setting will be applied on next port_init_mode()
        pm_unlock(port_index);
        return ESP_OK;
    }
    esp_err_t ret = serial_set_tx_disabled(sd, disabled);
    pm_unlock(port_index);
    return ret;
}

esp_err_t port_manager_send_raw(unsigned port_index, const uint8_t *data, size_t len)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    // Hold pm_lock across both the serial_desc lookup and serial_send(): it prevents
    // apply_settings()/set_mode() from freeing/recreating serial_desc concurrently
    // (use-after-free). Mirrors port_manager_set_cache()'s lock discipline.
    pm_lock(port_index);
    serial_desc_t *sd = get_port_serial_desc(port_index);
    if (!sd) {
        ESP_LOGW(TAG, "Port[%u]: no serial_desc, cannot send raw bytes", port_index + 1);
        pm_unlock(port_index);
        return ESP_FAIL;
    }
    /* TX visibility for the sniffer/cache is now centralized in serial_send(): it feeds
     * the per-port sniff_handler after a successful transmit (and skips it when TX is
     * disabled or the write was partial). Injecting here too would double-count the
     * transmitted frame (R5: a byte reaches the sniffer exactly once per direction). */
    esp_err_t ret = serial_send(sd, (uint8_t *)data, len);
    pm_unlock(port_index);
    return ret;
}

// Shared implementation of the mode switch. When persist is true the new mode is
// written to NVS after a successful init (the normal REST/settings path). When it
// is false the switch is runtime-only: NVS keeps the user's configured mode, so a
// reboot (or port_manager_apply_settings()) restores it. The transient path exists
// for temporary overrides such as the factory 100 kHz test, which must not clobber
// the persisted port configuration if power is lost while the test is running.
static esp_err_t port_set_mode_impl(unsigned port_index, pm_mode_t mode, bool persist)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    pm_lock(port_index);

    // While the factory test owns the TX/DE pins, a persisting mode change
    // (POST /ports/N/mode) must not go through: port_init_mode() would hand the
    // pins the LEDC is driving back to the UART, and the new mode would be written
    // to NVS on top of the user's configuration. Reject it with the dedicated
    // PM_ERR_PORTS_FROZEN — the REST handler maps that (and only that) to 409.
    // A generic ESP_ERR_INVALID_STATE would not do: port_init_mode() can return the
    // same code for an unrelated reason (bridge_port_init() refuses a tcp_bridge with
    // an invalid/legacy bridge_mode), and that must not be reported as a test conflict.
    // The transient path stays open: it is how the test disables the ports in the
    // first place.
    if (persist && ports_frozen()) {
        pm_unlock(port_index);
        ESP_LOGW(TAG, "Port[%u]: mode change rejected, ports frozen by factory test",
                 port_index + 1);
        return PM_ERR_PORTS_FROZEN;
    }

    // Remember the current (presumed-working) mode so we can roll back if the
    // new mode fails to initialise.
    pm_mode_t prev_mode = pm_ctx[port_index].mode;

    port_deinit_mode(port_index);

    esp_err_t init_ret = port_init_mode(port_index, mode);

    if (init_ret == ESP_OK) {
        // Persist the new mode ONLY after a successful init (persist-2). If we
        // persisted before init and init failed, NVS would diverge from the
        // runtime mode (port_init_mode leaves it DISABLED on failure) and
        // port_manager_check_settings_changed() would report a permanent
        // mismatch, making settings_update_task re-apply (and re-fail) on every
        // subsequent settings write.
        esp_err_t save_ret = persist
            ? setting_items_save(port_mode_nvs_key(port_index),
                                 port_manager_mode_to_str(mode))
            : ESP_OK;
        if (save_ret != ESP_OK) {
            ESP_LOGE(TAG, "Port[%u]: Failed to save port mode to NVS: %s",
                     port_index + 1, esp_err_to_name(save_ret));
            // Init succeeded but persistence failed: the mode is live now, but
            // NVS still holds the old mode. The next settings cycle will see the
            // mismatch (check_settings_changed) and apply_settings() will revert
            // the live mode back to the persisted one — and a reboot would do the
            // same. Surface the failure to the caller instead of reporting
            // success (persist-6); the REST handler maps it to an API error.
            init_ret = save_ret;
        }
    } else {
        // Init of the requested mode failed. NVS still holds prev_mode, so it
        // stays consistent. Roll the runtime back to prev_mode immediately so
        // the port is not left DISABLED until the next settings cycle.
        ESP_LOGW(TAG, "Port[%u]: init of '%s' failed (%s); rolling back to '%s'",
                 port_index + 1, port_manager_mode_to_str(mode),
                 esp_err_to_name(init_ret), port_manager_mode_to_str(prev_mode));
        if (prev_mode != PM_MODE_DISABLED) {
            esp_err_t rb = port_init_mode(port_index, prev_mode);
            if (rb != ESP_OK) {
                // The previous mode also failed to re-init (e.g. its resources
                // are genuinely unavailable). Leave the port DISABLED; NVS still
                // holds prev_mode, so the next apply will retry it.
                ESP_LOGE(TAG, "Port[%u]: rollback to '%s' also failed (%s)",
                         port_index + 1, port_manager_mode_to_str(prev_mode),
                         esp_err_to_name(rb));
            }
        }
    }

    pm_unlock(port_index);
    return init_ret;
}

esp_err_t port_manager_set_mode(unsigned port_index, pm_mode_t mode)
{
    return port_set_mode_impl(port_index, mode, true);
}

esp_err_t port_manager_set_mode_transient(unsigned port_index, pm_mode_t mode)
{
    return port_set_mode_impl(port_index, mode, false);
}

bool port_manager_get_cache(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return false;
    }
    return pm_ctx[port_index].cache_overlay;
}

esp_err_t port_manager_set_cache(unsigned port_index, bool enabled)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    pm_lock(port_index);

    pm_ctx[port_index].cache_overlay = enabled;
    esp_err_t ret = setting_items_save_bool(cache_en_nvs_key(port_index), enabled);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to save cache overlay to NVS: %s",
                 port_index + 1, esp_err_to_name(ret));
        // The live overlay is still applied below, but a reboot would silently
        // revert it. The failure is surfaced via the return value (persist-6).
    }

    // Update the global pool to match the new intent (serialised, wipe-safe).
    cache_sync_global();
    // Wire/unwire this port's live data flow if its serial port is open.
    if (get_port_serial_desc(port_index) != NULL) {
        if (enabled) {
            sniffer_enable(port_index, SNIFF_REASON_CACHE);
        } else {
            sniffer_disable(port_index, SNIFF_REASON_CACHE);
        }
    }

    pm_unlock(port_index);
    // Return the persistence result: live state was applied regardless, but the
    // caller must know if the change will not survive a reboot (persist-6).
    return ret;
}

void port_manager_set_ports_frozen(bool frozen)
{
    __atomic_store_n(&s_ports_frozen, frozen, __ATOMIC_SEQ_CST);
    ESP_LOGI(TAG, "Ports %s", frozen ? "frozen (factory test active)" : "unfrozen");
}

bool port_manager_ports_frozen(void)
{
    return ports_frozen();
}

esp_err_t port_manager_apply_settings(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    pm_lock(port_index);

    // While the factory test owns the TX/DE pins, never bring a port back up.
    // Checked INSIDE the lock: read outside it, settings_update_task could see
    // false, get preempted before pm_lock, and resume after the test has frozen the
    // ports and started the LEDC — re-initialising the port on top of the running
    // waveform. Under the lock the test cannot slip in between the check and the
    // re-init, and a set_mode_transient() waiting on this lock only proceeds once
    // this apply_settings() is done.
    if (ports_frozen()) {
        pm_unlock(port_index);
        ESP_LOGW(TAG, "Port[%u]: apply_settings skipped, ports frozen by factory test",
                 port_index + 1);
        return ESP_OK;
    }

    port_deinit_mode(port_index);

    // Re-read the mode from NVS (it may have been changed externally).
    pm_mode_t mode = read_port_mode_from_nvs(port_index);
    esp_err_t ret = port_init_mode(port_index, mode);
    pm_unlock(port_index);
    return ret;
}

// Compare the running port against NVS. Caller must hold pm_lock(port_index): the
// runtime mode read here is the same one port_set_mode_impl()/apply_settings()
// mutate under that lock. Takes no other port_manager lock, and neither
// bridge_port_check_settings_changed() nor bridge_read_serial_config() re-enters
// port_manager, so the pm_lock→bridge order matches port_init_mode()'s.
static bool port_settings_changed_locked(unsigned port_index)
{
    pm_mode_t current_mode = pm_ctx[port_index].mode;

    // Always check whether the port mode itself has changed.
    pm_mode_t nvs_mode = read_port_mode_from_nvs(port_index);
    if (nvs_mode != current_mode) {
        ESP_LOGD(TAG, "Port[%u]: Mode changed from '%s' to '%s'",
                 port_index + 1,
                 port_manager_mode_to_str(current_mode),
                 port_manager_mode_to_str(nvs_mode));
        return true;
    }

    // For TCP_BRIDGE delegate to the bridge module which compares all TCP/serial params.
    if (current_mode == PM_MODE_TCP_BRIDGE) {
        return bridge_port_check_settings_changed(port_index);
    }

    // For PASSIVE and REPEATER compare only the serial parameters.
    // bridge_port_check_settings_changed() must NOT be used here because
    // bridge_ctx[index].initialized is always false for these modes, which
    // causes that function to return incorrect results.
    if (current_mode == PM_MODE_PASSIVE || current_mode == PM_MODE_REPEATER) {
        serial_config_t nvs_cfg = {0};
        // If reading fails, assume changed to trigger re-init.
        if (bridge_read_serial_config(port_index, &nvs_cfg) != ESP_OK) {
            return true;
        }
        return memcmp(&pm_ctx[port_index].serial_cfg_at_init, &nvs_cfg, sizeof(serial_config_t)) != 0;
    }

    // PM_MODE_DISABLED — nothing to check.
    return false;
}

bool port_manager_check_settings_changed(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return false;
    }

    pm_lock(port_index);

    // The factory test holds both ports DISABLED at runtime while NVS still has
    // the user's mode, so a naive compare would report "changed" for every port
    // and let any unrelated settings write re-init them mid-test. Report no
    // change instead; the test restores the ports from NVS when it finishes.
    // Read under the lock, together with the runtime mode it is guarding.
    bool changed = ports_frozen() ? false : port_settings_changed_locked(port_index);

    pm_unlock(port_index);
    return changed;
}

// ────────────────────────────────────────────────────────────────
// HTTP handlers
// ────────────────────────────────────────────────────────────────

/* Decode hex string to bytes — returns byte count or -1 on invalid input */
PORT_MANAGER_STATIC int hex_str_to_bytes(const char *hex, uint8_t *out, size_t out_max)
{
    size_t hex_len = strlen(hex);
    if ((hex_len % 2) != 0 || (hex_len / 2) > out_max) {
        return -1;
    }
    for (size_t i = 0; i < hex_len; i += 2) {
        unsigned byte_val;
        int chars_read = 0;
        /* Use %n to verify that exactly 2 hex characters were consumed */
        if (sscanf(hex + i, "%02x%n", &byte_val, &chars_read) != 1 || chars_read != 2) {
            return -1;
        }
        out[i / 2] = (uint8_t)byte_val;
    }
    return (int)(hex_len / 2);
}

static esp_err_t port_send_handler(httpd_req_t *req, unsigned port_index)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *req_json = json_utils_receive_json(req);
    if (!req_json) {
        return json_utils_send_error(req, "Invalid JSON");
    }

    cJSON *hex_item = cJSON_GetObjectItem(req_json, "hex");
    if (!hex_item || !cJSON_IsString(hex_item)) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Missing or invalid 'hex' field");
    }

    const char *hex_str = hex_item->valuestring;
    size_t hex_len = strlen(hex_str);

    /* Max 512 hex chars = 256 bytes = MODBUS_RTU_MAX_FRAME_LEN */
    if ((hex_len % 2) != 0) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Hex string length must be even");
    }
    if (hex_len > (MODBUS_RTU_MAX_FRAME_LEN * 2)) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Hex string too long (max 512 hex chars / 256 bytes)");
    }

    uint8_t bytes[MODBUS_RTU_MAX_FRAME_LEN];
    int byte_count = hex_str_to_bytes(hex_str, bytes, sizeof(bytes));
    if (byte_count < 0) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Invalid hex string");
    }

    esp_err_t ret = port_manager_send_raw(port_index, bytes, (size_t)byte_count);
    if (ret != ESP_OK) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, esp_err_to_name(ret));
    }

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Failed to create response");
    }
    cJSON_AddNumberToObject(resp, "sent", byte_count);
    json_utils_send_response(req, req_json, resp);
    return ESP_OK;
}

static esp_err_t port1_send_handler(httpd_req_t *req)
{
    return port_send_handler(req, 0);
}

static esp_err_t port2_send_handler(httpd_req_t *req)
{
    return port_send_handler(req, 1);
}

PORT_MANAGER_STATIC esp_err_t port_set_mode_handler(httpd_req_t *req, unsigned port_index)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *req_json = json_utils_receive_json(req);
    if (!req_json) {
        return json_utils_send_error(req, "Invalid JSON");
    }

    cJSON *mode_item = cJSON_GetObjectItem(req_json, "mode");
    if (!mode_item || !cJSON_IsString(mode_item)) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Missing or invalid 'mode' field");
    }

    pm_mode_t new_mode = str_to_pm_mode(mode_item->valuestring);

    // Reject unknown mode strings (str_to_pm_mode maps unknown → disabled,
    // but the caller might have sent a genuinely invalid string).
    if (new_mode == PM_MODE_DISABLED &&
        strncmp(mode_item->valuestring, PORT_MODE_DISABLED_STR, SETTING_ITEM_MAX_STR_LEN) != 0) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Unknown mode value");
    }

    esp_err_t ret = port_manager_set_mode(port_index, new_mode);
    if (ret == PM_ERR_PORTS_FROZEN) {
        // The factory clock-out test owns the port's TX/DE pins right now, so the
        // port cannot be reconfigured. 409: the request is fine, the resource state
        // is not — retry once the test is switched off (POST /wb_test {"clock_out":false}).
        // Only the dedicated freeze code lands here. ESP_ERR_INVALID_STATE must NOT:
        // port_init_mode() returns it for an unrelated reason too (a tcp_bridge whose
        // persisted bridge_mode is invalid/legacy — bridge.c), and answering that with
        // "the clock_out test is active" would be a false status and a false diagnosis.
        cJSON_Delete(req_json);
        return json_utils_send_error_status(req, "409 Conflict",
            "Port mode is locked while the clock_out factory test is active");
    }
    if (ret != ESP_OK) {
        // The request body was fully validated above (the port index comes from the URI
        // registration, an unknown mode string was already rejected with 400), so any
        // remaining failure is server-side, not a bad request. Sources: the port refused
        // to initialise in the requested mode (bridge_port_init() rejecting a tcp_bridge
        // whose persisted bridge_mode is invalid/legacy, serial_init() failing with
        // "UART driver already installed", repeater_init_port(), ESP_ERR_NO_MEM), or the
        // mode was applied but could not be persisted to NVS (persist-6). On an init
        // failure the port is rolled back to its previous mode (or left disabled if the
        // rollback failed too); on a persist failure the new mode is live but NVS still
        // holds the old one. Report 500 and name the real cause — never 400.
        cJSON_Delete(req_json);
        return json_utils_send_error_status(req, "500 Internal Server Error",
                                            esp_err_to_name(ret));
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddStringToObject(resp, "mode", port_manager_mode_to_str(new_mode));
    }
    json_utils_send_response(req, req_json, resp);
    return ESP_OK;
}

// User-facing port numbers are 1-based (Port 1, Port 2).
// Handlers convert to 0-based index before calling port_manager_set_mode().
static esp_err_t port1_set_mode_handler(httpd_req_t *req)
{
    return port_set_mode_handler(req, 0);
}

static esp_err_t port2_set_mode_handler(httpd_req_t *req)
{
    return port_set_mode_handler(req, 1);
}

static esp_err_t port_set_cache_handler(httpd_req_t *req, unsigned port_index)
{
    if (!auth_middleware_check(req)) {
        return ESP_OK;
    }

    cJSON *req_json = json_utils_receive_json(req);
    if (!req_json) {
        return json_utils_send_error(req, "Invalid JSON");
    }

    cJSON *enabled_item = cJSON_GetObjectItem(req_json, "enabled");
    if (!enabled_item || !cJSON_IsBool(enabled_item)) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, "Missing or invalid 'enabled' field");
    }

    bool enabled = cJSON_IsTrue(enabled_item);
    esp_err_t ret = port_manager_set_cache(port_index, enabled);
    if (ret != ESP_OK) {
        cJSON_Delete(req_json);
        return json_utils_send_error(req, esp_err_to_name(ret));
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp) {
        cJSON_AddBoolToObject(resp, "cache_enabled", enabled);
    }
    json_utils_send_response(req, req_json, resp);
    return ESP_OK;
}

static esp_err_t port1_set_cache_handler(httpd_req_t *req)
{
    return port_set_cache_handler(req, 0);
}

static esp_err_t port2_set_cache_handler(httpd_req_t *req)
{
    return port_set_cache_handler(req, 1);
}

static const httpd_uri_t uri_port1_mode = {
    .uri     = "/ports/1/mode",
    .method  = HTTP_POST,
    .handler = port1_set_mode_handler,
};

static const httpd_uri_t uri_port2_mode = {
    .uri     = "/ports/2/mode",
    .method  = HTTP_POST,
    .handler = port2_set_mode_handler,
};

static const httpd_uri_t uri_port1_send = {
    .uri     = "/ports/1/send",
    .method  = HTTP_POST,
    .handler = port1_send_handler,
};

static const httpd_uri_t uri_port2_send = {
    .uri     = "/ports/2/send",
    .method  = HTTP_POST,
    .handler = port2_send_handler,
};

static const httpd_uri_t uri_port1_cache = {
    .uri     = "/ports/1/cache",
    .method  = HTTP_POST,
    .handler = port1_set_cache_handler,
};

static const httpd_uri_t uri_port2_cache = {
    .uri     = "/ports/2/cache",
    .method  = HTTP_POST,
    .handler = port2_set_cache_handler,
};

esp_err_t port_manager_register_handlers(httpd_handle_t server)
{
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port1_mode),
                        TAG, "Failed to register POST /ports/1/mode");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port2_mode),
                        TAG, "Failed to register POST /ports/2/mode");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port1_send),
                        TAG, "Failed to register POST /ports/1/send");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port2_send),
                        TAG, "Failed to register POST /ports/2/send");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port1_cache),
                        TAG, "Failed to register POST /ports/1/cache");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri_port2_cache),
                        TAG, "Failed to register POST /ports/2/cache");

    ESP_LOGI(TAG, "HTTP handlers registered");
    return ESP_OK;
}

#ifdef __unittest_env__
void port_manager_reset_for_test(void)
{
    memset(pm_ctx, 0, sizeof(pm_ctx));
    __atomic_store_n(&s_ports_frozen, false, __ATOMIC_SEQ_CST);
}
#endif /* __unittest_env__ */
