#include "port_manager.h"
#include "bridge.h"
#include "repeater.h"
#include "sniffer.h"
#include "cache_multimaster.h"
#include "cache_modbus_server.h"
#include "serial.h"
#include "setting_items.h"
#include "settings_manager.h"
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

// "port_manager_init_subsystems() has already been attempted" — NOT "it succeeded".
// Set before the first attempt runs, so every later call is a no-op whatever the outcome;
// see the function for why a retry is worse than living with the result.
// Plain bool, no atomics: it is written and read only from the main task during boot
// (main.c, then port_manager_init() from the wait-for-network loop in the same task).
// Nothing else calls either function — if that ever changes, this needs the same
// treatment as s_ports_frozen above.
static bool s_subsystems_ready = false;

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
//
// Returns cache_multimaster_enable()'s result — ESP_ERR_NO_MEM when the 32 KB pool
// would not allocate, ESP_ERR_INVALID_STATE when the cache module never initialised.
// A failed enable leaves the pool off while the overlay flag stays set, so want stays
// true and the NEXT sync retries it; every caller must decide whether to surface the
// failure (port_manager_set_cache(), where the caller asked for the cache) or to log
// it and carry on (port_init_mode(), where the caller asked for a transport mode).
// ESP_OK for the disable and the two no-op cases: disable() cannot fail, and there is
// nothing to report when the pool already matches the intent.
static esp_err_t cache_sync_global(void)
{
    cache_decision_lock();
    esp_err_t ret = ESP_OK;
    bool want = false;
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        if (pm_ctx[i].cache_overlay) { want = true; break; }
    }
    bool have = cache_multimaster_is_enabled();
    if (want && !have) {
        ret = cache_multimaster_enable();
    } else if (!want && have) {
        cache_multimaster_disable();
    }
    cache_decision_unlock();
    return ret;
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
    //
    // Logged, NOT propagated — the same log-and-continue policy as the subsystem init in
    // port_manager_init() below, and for a stronger reason here: this function's return
    // value is what port_set_mode_impl() rolls a port back on and what port_manager_init()
    // reports a dead port with. A 32 KB pool that would not allocate says nothing about the
    // transport the caller actually asked for, and turning it into a failure would tear a
    // working UART back down (or, at boot, leave the port down) over an overlay. The port
    // therefore comes up with the overlay flag still set and the pool off — a divergence
    // that IS visible (/info says cache_enabled true, /cache/status says enabled false) and
    // that heals on the next sync: the flag keeps want true, so the next cache_sync_global()
    // retries the allocation. There are exactly two call sites of it — this one and
    // port_manager_set_cache()'s — so a repeated POST /ports/N/cache is the cheapest retry,
    // and this one is reached from every caller of port_init_mode(): the boot loop in
    // port_manager_init(), a mode change and its rollback in port_set_mode_impl(), and
    // port_manager_apply_settings().
    // The CACHE sniffer reason is armed below regardless, so a later successful retry needs
    // no re-arm; sniffer_ws_dispatch() drops packets while cache_multimaster_is_enabled()
    // is false, so nothing is stored in the meantime.
    esp_err_t cache_ret = cache_sync_global();
    if (cache_ret != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: cache pool unavailable (%s) - the port comes up without the "
                      "cache, the overlay stays set and the next sync retries it",
                 index + 1, esp_err_to_name(cache_ret));
    }
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
        // Tear the transport down first: bridge_port_deinit() joins the TCP receiver
        // tasks and the UART event task (via serial_deinit), so no reader of
        // sniff_handler survives. Only then detach the sniffer — the descriptor is
        // already freed, so detaching earlier would race live readers / use-after-free.
        bridge_port_deinit(index);
        sniffer_detach(index);
        // bridge_port_deinit() clears bridge_ctx[index].serial_desc internally.
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    case PM_MODE_PASSIVE:
        // serial_deinit() joins the UART event task (the only sniff_handler reader in
        // passive mode) before freeing the descriptor; detach the sniffer afterwards.
        serial_deinit(pm_ctx[index].serial_desc);
        sniffer_detach(index);
        pm_ctx[index].serial_desc = NULL;
        memset(&pm_ctx[index].serial_cfg_at_init, 0, sizeof(pm_ctx[index].serial_cfg_at_init));
        rs485_busy_monitor_reset(index);
        rs485_stats_reset(index);
        break;

    case PM_MODE_REPEATER:
        // repeater_deinit_port() unpublishes this port from the peer's forwarding path,
        // drains the forwards already inside serial_send() into it, and joins this port's
        // own UART event task (via serial_deinit) before freeing the descriptor; detach
        // the sniffer afterwards.
        repeater_deinit_port(index);
        sniffer_detach(index);
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
    // transport re-init. sniffer_detach(index) in the branch above already cleared
    // this port's CACHE reason, and the serial port is down, so no data flows — that
    // is sufficient.
}

// ────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────

esp_err_t port_manager_init_subsystems(void)
{
    // At most one attempt per boot, whatever comes of it. Two of the subsystems below are
    // not idempotent on their own: sniffer_init() would create a second queue, a second
    // pair of timers and a second WS task and leak the first set, and
    // cache_multimaster_init() would leak its mutex. The other three tolerate a repeat —
    // repeater_init() and rs485_busy_monitor_init() both create their handle only if it is
    // still NULL, and rs485_stats_init() is a plain memset — but that is not what makes a
    // second run safe overall. Both main.c and port_manager_init() call this, so the guard
    // is what lets the two entry points coexist.
    if (s_subsystems_ready) {
        return ESP_OK;
    }
    // Set BEFORE running anything, so a partial failure cannot be retried either. There is
    // nothing to gain from a second try: the only way any of these fail is out of memory,
    // which the next call a few milliseconds later meets unchanged — and a whole lot to
    // lose, since a retry would re-run whatever part did succeed. Callers get the error and
    // log it; the degraded state that leaves behind is safe because everything reachable
    // from the HTTP handlers checks its own handles (see sniffer.c and cache_multimaster.c).
    s_subsystems_ready = true;

    // Initialise shared RS-485 infrastructure (previously done inside bridge_init()).
    rs485_busy_monitor_init();
    rs485_stats_init();

    // Create the repeater-global mutex before any port can enter repeater mode,
    // so the cross-port data path is protected from the first init onward.
    repeater_init();

    // The two below are independent of each other, so one running out of memory must not
    // cost the device the other: try both, report the first error. Losing the sniffer must
    // not also silently disable the cache.
    esp_err_t first_err = ESP_OK;

    esp_err_t ret = sniffer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sniffer_init failed: %s", esp_err_to_name(ret));
        first_err = ret;
    }

    ret = cache_multimaster_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "cache_multimaster_init failed: %s", esp_err_to_name(ret));
        if (first_err == ESP_OK) {
            first_err = ret;
        }
    }

    if (first_err != ESP_OK) {
        return first_err;
    }

    ESP_LOGI(TAG, "Port manager subsystems initialized");
    return ESP_OK;
}

esp_err_t port_manager_init(void)
{
    // Network-independent half. main.c has normally run it already, before starting the
    // HTTP server (see port_manager_init_subsystems()); the second call is then a no-op.
    // It is kept here so port_manager_init() stays self-contained for a caller that uses
    // it alone — the unit tests do.
    //
    // Logged, not propagated: a sniffer or cache mutex that would not allocate is no reason
    // to leave the RS-485 ports down and reboot. This device routes Modbus first; the
    // subsystems that failed degrade on their own (their entry points check their handles),
    // and the failure was already logged where it happened.
    //
    // Nothing in this function returns an error any more, and cache_modbus_server_init()
    // below is why the rule had to be spelled out rather than assumed. It used to be reached
    // through ESP_RETURN_ON_ERROR, so a failed start skipped the port loop below and landed
    // on main.c's ESP_ERROR_CHECK — abort, reboot, RS-485 down. Three ways it can fail, and
    // only two of them are out of memory: the reassembly mutex that would not allocate
    // (cache_modbus_server.c:427), and anything tcp_server_init() cannot get — socket,
    // descriptor, event group, connection mutex, acceptor task (tcp_server.c:598-643). The
    // third is a refused listen(), which needs the port to be held by ANY other listener on
    // this device — a bridge port, or httpd on web_port: lwIP's tcp_listen() finds an equal
    // address and returns ERR_USE, create_listen_socket() gives up after 10 attempts 100 ms
    // apart (tcp_server.c:130-142), and tcp_server_init() returns ESP_FAIL. Two ways to
    // reach it. cache_modbus_port equal to a BRIDGE port that a POST /settings brought up
    // while this task was still waiting for the network: httpd is already answering by then,
    // and main.c's comment above port_manager_init_subsystems() sizes that window. Or an
    // INHERITED cache_modbus_port == web_port, which validate_port_collisions() in
    // settings_manager.c deliberately accepts as a warning and stores in NVS so the device
    // stays reachable to be fixed over REST — httpd bound that port back in main.c, before
    // this function ran, and keeps it.
    //
    // That second one used to be the exception, and the comment here used to say so: while
    // this socket was AF_INET/INADDR_ANY and httpd's was PF_INET6/in6addr_any, lwIP compared
    // the two addresses as unequal, both listens succeeded, and the port kept TWO listeners
    // answering alternate connections instead of failing here. Fixed by binding this socket
    // the same dual-stack way httpd does; the mechanics are in create_listen_socket()
    // (tcp_server.c) and are not repeated here.
    //
    // A reboot clears none of the three: out of memory and a socket table with no free entry
    // recur on a deterministic boot path; the web_port collision comes back identically,
    // because httpd is started before this function on every boot; and the bridge-port
    // collision merely moves, since cache_modbus_server_init() runs above the port loop below
    // and after a reboot takes the port from the bridge instead. Keeping such a configuration
    // serviceable is the settings side's job; not rebooting over it is this side's.
    //
    // What a dead cache Modbus server degrades to — narrow, and reversible from settings:
    //   - Lost: the Modbus TCP interface for READING the cache (FC01-FC04 answered from
    //     memory), and, on that port only, requests addressed to the gateway's own unit id —
    //     answered elsewhere ONLY while some port satisfies all THREE of port_mode_N ==
    //     tcp_bridge, bridge_modbus_N and bridge_mode_N == BRIDGE_MODE_SERVER. That is what it
    //     takes to create modbus_tcp_server_task(), which holds mb_device_is_self()'s other
    //     call site (modbus_tcp.c:528): tcp_bridge is what reaches bridge_port_init()
    //     (port_manager.c:270-272), bridge_modbus_N is what picks modbus_tcp_init_port() over
    //     the else branch into transparent_tcp_init_port() (bridge.c:229-240) — transparent
    //     never calls mb_device_is_self(), it relays the bytes onto RS-485 where nothing serves
    //     MB_DEVICE_UNIT_ID — and server mode is the only mode modbus_tcp_init_port() accepts
    //     (modbus_tcp.c:564-567). With both RS-485 ports passive or repeater, or a tcp_bridge
    //     running transparent — all working configurations, and the cache overlay runs in
    //     passive too — this port was the only Modbus TCP way in to that unit id, and it is lost.
    //   - Kept: the cache itself keeps filling. It is fed from the sniffer path —
    //     sniffer_ws_dispatch() -> cache_multimaster_on_request()/on_response() for every
    //     port whose SNIFF_REASON_CACHE is set, which the overlay below enables — and that
    //     path never touches this server.
    //   - Kept: the register map over REST. /cache/status, /cache/json and /cache/csv are
    //     registered by cache_multimaster_register_handlers() from http_server_init().
    //   - Recoverable without a reboot: a failed start leaves the running port at 0, so
    //     cache_modbus_server_check_settings_changed() reports a change and the next
    //     POST /settings retries it through settings_update.c's release/acquire pair — which
    //     logs and carries on the same way this does.
    esp_err_t subsys_ret = port_manager_init_subsystems();
    if (subsys_ret != ESP_OK) {
        ESP_LOGE(TAG, "Subsystem init failed: %s - bringing the ports up anyway",
                 esp_err_to_name(subsys_ret));
    }

    // Everything from here on needs a network interface, which is why it stays behind
    // main.c's wait-for-network loop instead of moving up with the subsystems: the cache
    // Modbus server binds a TCP socket, and a port configured as a TCP bridge opens a
    // listening socket (server mode) or an outgoing connection (client mode).

    // Start cache Modbus TCP server only if enabled in NVS settings.
    bool cache_server_enabled = setting_items_read_bool(KEY_CACHE_MODBUS_SERVER_ENABLED);
    if (cache_server_enabled) {
        int cache_port = setting_items_read_int(KEY_CACHE_MODBUS_PORT);
        if (cache_port <= 0) cache_port = CACHE_MODBUS_SERVER_PORT;
        esp_err_t cache_ret = cache_modbus_server_init(cache_port);
        if (cache_ret != ESP_OK) {
            // The port is in the message on purpose: a collision with another listener — a
            // bridge port or httpd's web_port — is one of the three causes above, and the
            // number is what tells the reader which listener this one lost the port to.
            ESP_LOGE(TAG, "cache_modbus_server_init(port %d) failed: %s - continuing without "
                          "the cache Modbus TCP interface, the gateway keeps running",
                     cache_port, esp_err_to_name(cache_ret));
        }
    } else {
        ESP_LOGI(TAG, "Cache Modbus TCP server is disabled by settings");
    }

    // Load and normalise the per-port cache overlay before bringing ports up.
    // Enforce the single-port invariant (review #51): NVS can carry both cache_en_1 and
    // cache_en_2 set — legacy firmware wrote it, and a move whose release failed to
    // persist leaves it. Keep only the lowest-index port that asks for it and clear the
    // rest (memory + NVS), so the invariant holds from boot regardless of stored state,
    // not only through the runtime move in port_manager_set_cache().
    bool cache_claimed = false;
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
        bool want = setting_items_read_bool(cache_en_nvs_key(i));
        if (want && !cache_claimed) {
            cache_claimed = true;
            pm_ctx[i].cache_overlay = true;
        } else {
            if (want) {
                ESP_LOGW(TAG, "Port[%u]: clearing stale cache overlay — cache is single-port (review #51)", i + 1);
                (void)setting_items_save_bool(cache_en_nvs_key(i), false);
            }
            pm_ctx[i].cache_overlay = false;
        }
    }

    // Bring up each port in the mode stored in NVS.
    for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
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
    // pins the test is driving back to the UART, and the new mode would be written
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

// Apply one port's cache-overlay state: the in-memory intent, its NVS key and the
// port's live data flow. This is the ONLY place that changes a port's cache_overlay
// after boot, so an enable and the release half of a move are guaranteed to leave a
// port in exactly the same shape.
// Everything here is PER-PORT. The global pool is deliberately NOT synced: a move
// clears one port's overlay and sets another's, and syncing after each half would
// free the pool in between (no port wanting it) only to allocate it again — see
// port_manager_set_cache(), which runs the one sync once both flags are final.
// The caller MUST hold pm_lock(index): get_port_serial_desc() reads the mode and the
// descriptor that the reinit paths free and recreate.
// Returns the NVS persistence result; the live state is applied either way (persist-6).
static esp_err_t cache_overlay_apply_locked(unsigned index, bool enabled)
{
    pm_ctx[index].cache_overlay = enabled;
    esp_err_t ret = setting_items_save_bool(cache_en_nvs_key(index), enabled);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Port[%u]: Failed to save cache overlay to NVS: %s",
                 index + 1, esp_err_to_name(ret));
        // The live overlay is still applied below, but a reboot would silently
        // revert it. The failure is surfaced via the return value (persist-6).
    }

    // Wire/unwire this port's live data flow if its serial port is open.
    if (get_port_serial_desc(index) != NULL) {
        if (enabled) {
            sniffer_enable(index, SNIFF_REASON_CACHE);
        } else {
            sniffer_disable(index, SNIFF_REASON_CACHE);
        }
    }
    return ret;
}

esp_err_t port_manager_set_cache(unsigned port_index, bool enabled)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    // Single-port cache invariant (review #51): the multimaster cache is one global
    // pool keyed by slave_id, and the Cache-TCP interface answers by unit_id (it is
    // port-blind), so it can only meaningfully serve one RS-485 port. Enabling the
    // overlay therefore MOVES it: any other port holding it is released first, so
    // same-address slaves from the two buses can never collide in the pool. Enabling
    // means "the cache source is now this port" and is never refused — a client can
    // hand the cache over in one call, in either direction, without having to disable
    // the old holder first (and without the outcome depending on the order in which it
    // issues the two per-port calls).
    //
    // The move must still DROP the cached values — the entries describe the OLD bus and
    // the pool has no port dimension to keep them apart from the new one — but it must
    // not drop the pool with them. That is why the global sync is not inside
    // cache_overlay_apply_locked(): a per-half sync would see no port wanting the pool
    // between the release and the enable, free the 32 KB and immediately allocate it
    // again. The realloc is the single most likely thing here to fail (32 KB of
    // contiguous DRAM on a fragmented heap), which would turn a call that only moves a
    // working cache into one that destroys it. Instead both overlay flags are brought to
    // their final state first, ONE cache_sync_global() runs after them — with want true
    // throughout a move, so it is a no-op — and the contents are dropped by
    // cache_multimaster_clear(), which zeroes the pool and resets the stats under the
    // cache mutex without freeing anything.
    //
    // Locking. The release writes another port's pm_ctx, so it runs under THAT port's
    // pm_lock, taken and released in full before this port's pm_lock is taken. The two
    // locks are deliberately NOT nested: pm_lock is held across whole port reinits, and
    // nesting would add a pm_lock[i] -> pm_lock[j] ordering rule to keep on top of the
    // existing pm_lock -> cache_decision_mutex one.
    // The price is that the check-then-act spans TWO separate critical sections — the
    // other port is read and cleared under its lock, this port is set under a different
    // one — so it is race-free only because port_manager_set_cache() is called solely
    // from the single esp_http_server request task and its calls are therefore
    // serialised. That is the only caller today. Under that premise the overlay is held
    // by NO port between the two sections and never by two: a concurrent reader sees the
    // cache off for a moment, never two buses feeding one pool. Two concurrent callers
    // would break it: one scanning while the other sits between its release and its
    // enable finds no holder, releases nothing, and then sets its own port — so both end
    // up set. A second caller (settings_update, MQTT, Modbus control) must therefore
    // make the whole move atomic first, by holding both pm_locks across it, taken in
    // ascending index order to stay deadlock-free.
    esp_err_t release_ret = ESP_OK;
    bool moved = false;
    // Which port release_ret came from — needed only for the log below, and captured
    // alongside release_ret so the two always describe the same failed write.
    unsigned release_fail_index = 0;
    if (enabled) {
        for (unsigned i = 0; i < BRIDGES_COUNT; i++) {
            if (i == port_index) {
                continue;
            }
            pm_lock(i);
            if (pm_ctx[i].cache_overlay) {
                ESP_LOGI(TAG, "Port[%u]: taking the cache overlay over from port %u — the cache is single-port (review #51)",
                         port_index + 1, i + 1);
                esp_err_t r = cache_overlay_apply_locked(i, false);
                moved = true;
                if (r != ESP_OK && release_ret == ESP_OK) {
                    release_ret = r;
                    release_fail_index = i;
                }
            }
            pm_unlock(i);
        }
    }

    pm_lock(port_index);
    esp_err_t ret = cache_overlay_apply_locked(port_index, enabled);
    pm_unlock(port_index);

    // The one global sync for the whole operation, run only now that every overlay flag
    // is final. Across a move want is true both before and after, so have stays true and
    // neither enable() nor disable() runs: no free, no realloc, and no window with the
    // cache off. A plain enable-from-off (want false -> true) allocates here, a plain
    // disable of the last holder (true -> false) frees here — both exactly as before,
    // except that this port's data flow has already been wired/unwired above. That order
    // is the safe one for the disable (the port stops feeding the pool before it is
    // freed); for the enable it costs at most the packets seen in the microseconds before
    // the pool exists, which sniffer_ws_dispatch() drops on !cache_multimaster_is_enabled().
    esp_err_t sync_ret = cache_sync_global();

    // A move drops the accumulated values — see the block comment above. clear() wipes
    // the pool and resets the stats under the cache mutex WITHOUT freeing it, which is
    // what lets the allocation survive the move. Done last, so that it covers as much of
    // the handover as possible: a straggler packet from the old port is wiped rather than
    // left behind as a value from the wrong bus. At most ONE such packet exists —
    // sniffer_ws_dispatch() runs on the single sniffer_ws task and re-reads
    // SNIFF_REASON_CACHE per packet, so only the one already past that check when the old
    // port's overlay was cleared above can still get through — and if it is a REQUEST it
    // merely sets s_pending, because the response answering it is a second packet and
    // fails the reason check.
    // What this does NOT do is close the window, and nothing on this side can:
    // cache_multimaster_on_response() consumes the pending request in one critical
    // section, releases the cache mutex, and takes it again to write the pool, so a
    // straggler RESPONSE that matched its pending before this clear() can still store its
    // old-bus values after it. That hole is not new — with the free-and-reallocate shape
    // this replaces, the same store landed in the freshly allocated pool — and closing it
    // means a generation check across on_response()'s two critical sections, i.e. a change
    // to the cache module rather than to this move. Such a value survives until the new
    // bus reads the same register, or until the next clear/disable.
    // A fresh packet from the new source arriving in the same window is wiped too — that
    // costs one poll cycle, whereas a stale value from the old bus would be served as
    // this port's own.
    if (moved) {
        cache_multimaster_clear();
    }

    // Error precedence: the pool first, persistence second. Both are real failures, but
    // they are not equally bad — sync_ret != ESP_OK means the cache is NOT RUNNING at
    // all (nothing is being recorded, /cache/status says disabled), while a failed NVS
    // write means it is running and merely will not survive a reboot (persist-6). The
    // worse of the two is what the caller has to act on, so it wins. A failed release
    // write counts as a persistence failure too: the old holder's NVS key would still
    // say "enabled", and the boot-time normalisation in port_manager_init() keeps the
    // LOWEST-index port that asks for the overlay (see the normalisation loop there) —
    // so a move to the higher-index port would come back undone.
    //
    // What that costs when BOTH fail at once, which is why the masked error is logged:
    // the caller is told ESP_ERR_NO_MEM and answered 503 "retry", and the retry does NOT
    // rewrite the released port's key. That port's in-memory overlay is already false, so
    // the release loop above does not run a second time; only THIS port's key is written
    // again. So a retry that finally gets the pool returns ESP_OK / 200 while NVS still
    // names the old port — and the next boot hands the overlay back to it, undoing a move
    // the client was told had succeeded. The log line below is the only trace of that.
    if (sync_ret != ESP_OK) {
        if (release_ret != ESP_OK) {
            ESP_LOGE(TAG, "Port[%u]: reporting %s for the cache pool, which MASKS the failed "
                          "NVS write releasing port %u (%s) — a successful retry will not "
                          "rewrite port %u's key, so a reboot can hand the overlay back to it",
                     port_index + 1, esp_err_to_name(sync_ret),
                     release_fail_index + 1, esp_err_to_name(release_ret),
                     release_fail_index + 1);
        }
        return sync_ret;
    }
    return (ret != ESP_OK) ? ret : release_ret;
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

esp_err_t port_manager_release(unsigned port_index)
{
    if (port_index >= BRIDGES_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    pm_lock(port_index);

    // While the factory test owns the TX/DE pins, never tear a port down either. Checked INSIDE
    // the lock for the same reason as in port_manager_apply_settings() below — and it must agree
    // with it: apply_settings() is a no-op while frozen, so a release that went ahead anyway would
    // leave the port down with nothing to bring it back up until the test ends.
    // Nothing is lost by skipping: port_manager_check_settings_changed() already reports "no
    // change" for a frozen port, and wb_test's exit path calls port_manager_apply_settings() for
    // every port, which re-reads the mode and every serial/bridge parameter from NVS — including
    // whatever this settings write persisted.
    if (ports_frozen()) {
        pm_unlock(port_index);
        ESP_LOGW(TAG, "Port[%u]: release skipped, ports frozen by factory test", port_index + 1);
        return ESP_OK;
    }

    port_deinit_mode(port_index);
    pm_unlock(port_index);
    return ESP_OK;
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

    /* No length pre-checks here: hex_str_to_bytes() already rejects an odd length and anything
     * that would not fit in out_max bytes (530 hex chars = 265 bytes = MODBUS_FAST_MAX_FRAME_LEN),
     * and it is the only place that decides what a decodable hex string is. Duplicating its rules
     * here bought two more specific error messages at the price of a second copy of the limits
     * that has to be kept in step with it.
     *
     * Sized for Fast Modbus, not plain RTU: a Fast Modbus command wraps an encapsulated standard
     * command and runs to 265 bytes. At MODBUS_RTU_MAX_FRAME_LEN (256) the longest such frames
     * could not be sent through this endpoint at all — hex_str_to_bytes() rejected them. */
    uint8_t bytes[MODBUS_FAST_MAX_FRAME_LEN];
    int byte_count = hex_str_to_bytes(hex_item->valuestring, bytes, sizeof(bytes));
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

    // Reject up front a switch that would put this port's TCP bridge gateway onto a port another
    // local listener already owns (the web server, the cache Modbus server, or the other RS-485
    // bridge) — the same collision POST /settings refuses. Without this, the conflict would only
    // surface later as a bind() EADDRINUSE after a ~10x1s retry, then roll the mode back: a poor
    // and late diagnosis. Only tcp_bridge (with a saved server bridge) can collide; the other
    // modes bind nothing locally and pass through.
    esp_err_t coll = settings_manager_check_port_mode_collision(port_index, port_manager_mode_to_str(new_mode));
    if (coll != ESP_OK) {
        cJSON_Delete(req_json);
        return json_utils_send_error_status(req, "409 Conflict",
            "Selected mode would bind a TCP port already used by another listener");
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

PORT_MANAGER_STATIC esp_err_t port_set_cache_handler(httpd_req_t *req, unsigned port_index)
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
    // No conflict branch here: the cache overlay is single-port (review #51), but
    // enabling it MOVES it off the other port instead of being refused, so there is no
    // state in which a well-formed request has to be rejected.
    //
    // Everything the body could get wrong was rejected above with a 400, so nothing
    // below is the client's fault and none of it may be answered with one — an
    // integrator has to be able to tell "you asked for something invalid" (fix the
    // request) from "the device could not do it" (the request was right; the device
    // was not able). Two server-side outcomes remain, and they are split because the
    // right next move differs:
    //   ESP_ERR_NO_MEM -> 503. The 32 KB register pool would not allocate. The overlay
    //     IS recorded on this port in memory — and in NVS too when that write succeeded;
    //     the pool failure outranks a persistence failure, so a 503 does not promise the
    //     write went through — but the cache is off and /cache/status will say so.
    //     Transient by nature — it is contiguous DRAM on a
    //     fragmented heap — and retrying this very request re-attempts the allocation
    //     (cache_sync_global() sees want true, have false), so 503 "come back later"
    //     is both accurate and actionable.
    //   anything else -> 500. The NVS write failed (the change is live but will not
    //     survive a reboot, persist-6), or the cache module never initialised
    //     (ESP_ERR_INVALID_STATE — no mutex, nothing retries it before a reboot).
    //     Neither is fixed by repeating the request.
    esp_err_t ret = port_manager_set_cache(port_index, enabled);
    if (ret == ESP_ERR_NO_MEM) {
        cJSON_Delete(req_json);
        return json_utils_send_error_status(req, "503 Service Unavailable",
            "Cache pool could not be allocated: the device is out of contiguous memory");
    }
    if (ret != ESP_OK) {
        cJSON_Delete(req_json);
        return json_utils_send_error_status(req, "500 Internal Server Error",
                                            esp_err_to_name(ret));
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
    /* Clear the one-shot guard so each test starts from a device that has not
     * booted yet — otherwise the first test to run would consume the init and
     * every later one would see port_manager_init_subsystems() as a no-op. */
    s_subsystems_ready = false;
}
#endif /* __unittest_env__ */
