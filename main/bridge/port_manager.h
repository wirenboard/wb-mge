#pragma once

#include "esp_err.h"
#include "bridge.h"  // for BRIDGES_COUNT
#include <stdbool.h>

#ifndef __unittest_env__
#include "esp_http_server.h"
#else
typedef void *httpd_handle_t;
#endif

/**
 * @brief Transport operating mode for each RS-485 port.
 *
 * The transport mode is orthogonal to the sniffer and cache overlays: the
 * sniffer (live WS display) and the cache overlay can be enabled additively
 * on top of any non-disabled transport mode.
 *
 * PM_MODE_DISABLED   — serial port is not opened; port is fully inactive.
 * PM_MODE_TCP_BRIDGE — serial port is open; traffic is forwarded over TCP
 *                      (transparent or Modbus framing, driven by bridge settings).
 * PM_MODE_PASSIVE    — serial port is open, no TCP forwarding (passive listener).
 *                      This is the serial-only base that the cache overlay and
 *                      the live sniffer can attach to.
 * PM_MODE_REPEATER   — serial port is open and raw bytes are transparently
 *                      forwarded to the other RS-485 port (and vice-versa) to
 *                      extend the line / restore signal integrity.
 */
typedef enum {
    PM_MODE_DISABLED   = 0,
    PM_MODE_TCP_BRIDGE = 1,
    PM_MODE_PASSIVE    = 2,
    PM_MODE_REPEATER   = 3,
} pm_mode_t;

/**
 * @brief port_manager_set_mode() refused the change: the ports are frozen by the
 *        factory clock_out test (see port_manager_set_ports_frozen()).
 *
 * A dedicated code, NOT ESP_ERR_INVALID_STATE: that one is not exclusive to the
 * freeze — bridge_port_init() returns exactly ESP_ERR_INVALID_STATE for a port
 * whose bridge_mode is invalid/legacy, and it reaches the caller through
 * port_init_mode(). Mapping ESP_ERR_INVALID_STATE to 409 "clock_out test active"
 * would answer a corrupt-NVS device with a conflict about a test that is not
 * running. Only this code means "frozen".
 *
 * 0x10000 is above every base the IDF hands out in esp_err.h and its components
 * (the highest is ESP_ERR_MEMPROT_BASE, 0xd000), so it cannot alias a real code.
 */
#define PM_ERR_PORTS_FROZEN  ((esp_err_t)0x10000)

/**
 * @brief Create the port_manager global mutexes.
 *
 * Creates the freeze/bridge-start serialisation mutex and the global-cache
 * decision mutex. Deterministic and single-threaded: MUST be called once, on the
 * main task, BEFORE http_server_init() registers the URI handlers (POST
 * /ports/N/mode, /ports/N/cache, /device-template, /settings, /wb_test) that can
 * take these locks — otherwise the first concurrent HTTP request would race the
 * lazy creation and end up with a second, unshared mutex. Idempotent.
 *
 * Split out of port_manager_init() because that runs only after the network is
 * up, which is later than http_server_init().
 */
void port_manager_locks_init(void);

/**
 * @brief Initialize the port manager.
 *
 * Reads the active mode for each port from NVS and brings up the appropriate
 * subsystems.  Also initialises shared infrastructure (sniffer, cache,
 * cache_modbus_server, RS-485 monitors) that was previously done by bridge_init().
 *
 * Must be called once after NVS and settings are ready, in place of the old
 * bridge_init() + cache_modbus_server_init() calls in main.c.
 * port_manager_locks_init() must have been called earlier (before httpd start).
 *
 * @return ESP_OK on success.
 */
esp_err_t port_manager_init(void);

/**
 * @brief Switch a port to a new operating mode.
 *
 * Deinitialises the current mode, saves the new mode to NVS, then initialises
 * the new mode.
 *
 * Rejected with PM_ERR_PORTS_FROZEN while the ports are frozen by the factory
 * test (see port_manager_set_ports_frozen()): re-initialising the port would take
 * its TX and DE pins back from the test — the TX pins from the LEDC that is driving
 * the waveform, the DE pins from the plain-GPIO levels the test holds them at
 * (port 1 HIGH, port 2 LOW).
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @param mode        Target mode.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG if port_index is out of range;
 *         PM_ERR_PORTS_FROZEN if the ports are frozen by the factory test
 *         (nothing is changed, neither live nor in NVS);
 *         the init error if the new mode failed to initialise (the previous mode
 *         is rolled back) — note that this can itself be ESP_ERR_INVALID_STATE,
 *         e.g. a tcp_bridge whose bridge_mode is invalid/legacy;
 *         or the NVS save error if the mode initialised live
 *         but could not be persisted — in that case the mode is applied now but
 *         not persisted (persist-6).
 */
esp_err_t port_manager_set_mode(unsigned port_index, pm_mode_t mode);

/**
 * @brief Switch a port to a new operating mode WITHOUT persisting it to NVS.
 *
 * Behaves exactly like port_manager_set_mode() (deinit, init, rollback on init
 * failure) except that the new mode is never written to NVS: the port_mode key
 * keeps the user's configured value.
 *
 * Intended for temporary runtime overrides — the factory 100 kHz test disables
 * both ports so the LEDC can take over their TX pins, and must not clobber the
 * persisted configuration if power is lost while the test is running. Restore the
 * configured mode afterwards with port_manager_apply_settings(), which re-reads
 * the mode from NVS and re-initialises the port.
 *
 * Do NOT use this for REST/settings-driven mode changes — those must persist.
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @param mode        Target mode.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG if port_index is out of range;
 *         or the init error if the new mode failed to initialise (the previous
 *         mode is rolled back).
 */
esp_err_t port_manager_set_mode_transient(unsigned port_index, pm_mode_t mode);

/**
 * @brief Return the currently active mode for a port.
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @return Active pm_mode_t, or PM_MODE_DISABLED if port_index is out of range.
 */
pm_mode_t port_manager_get_mode(unsigned port_index);

/**
 * @brief Convert a pm_mode_t value to its NVS/JSON string representation.
 *
 * @param mode  Mode value.
 * @return Pointer to a constant string ("disabled", "tcp_bridge", "passive", "repeater"),
 *         or "unknown" for unrecognised values.
 */
const char *port_manager_mode_to_str(pm_mode_t mode);

/**
 * @brief Enable or disable the per-port cache overlay.
 *
 * The cache overlay is persisted (KEY_CACHE_EN_1/2) and is orthogonal to the
 * transport mode: it survives transport-mode changes. When enabled and the
 * port's serial is open, the sniffer is driven (via SNIFF_REASON_CACHE) to
 * feed the global multimaster cache.
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @param enabled     True to enable the cache overlay, false to disable.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG if port_index is out of range;
 *         or the NVS save error if the overlay was applied live but could not be
 *         persisted — the live state is applied but not persisted (persist-6).
 */
esp_err_t port_manager_set_cache(unsigned port_index, bool enabled);

/**
 * @brief Return the persisted cache-overlay state for a port.
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @return true if the cache overlay is enabled, false otherwise or if OOB.
 */
bool port_manager_get_cache(unsigned port_index);

/**
 * @brief Freeze / unfreeze all ports for the duration of the factory test.
 *
 * The factory 100 kHz clock-out test drives the RS-485 pins directly, after
 * transiently forcing both ports to PM_MODE_DISABLED (runtime only — NVS keeps
 * the user's configured mode): the TX pin of both ports carries the LEDC waveform,
 * and the DE pin of both ports is held as a plain GPIO — port 1 HIGH (its driver
 * transmits), port 2 LOW (its driver stays in receive). That mismatch between
 * the runtime mode and NVS would otherwise make port_manager_check_settings_changed()
 * report "changed" for every port, so any unrelated POST /settings would run
 * port_manager_apply_settings() and re-init the ports on top of the running
 * waveform.
 *
 * While frozen:
 *   - port_manager_check_settings_changed() always reports false;
 *   - port_manager_apply_settings() is a no-op returning ESP_OK;
 *   - port_manager_set_mode() is rejected with PM_ERR_PORTS_FROZEN (the REST
 *     handler turns that — and only that — into 409 Conflict);
 *   - port_manager_set_mode_transient() still works — it is how the test itself
 *     puts the ports into PM_MODE_DISABLED.
 *
 * Note that POST /settings may still write a new port_mode to NVS during the test and
 * is answered 200, while POST /ports/N/mode is answered 409. That is the intended
 * split, not an inconsistency: /ports/N/mode applies the mode immediately (a deinit +
 * re-init of a port whose TX and DE pins the test is driving), whereas /settings only
 * records it — apply_settings() is frozen, and the value takes effect when the test ends.
 * See the port_mode entry in settings_manager.c's rs485_base_mappings.
 *
 * NVS is not affected either way. Unfreeze first, then call
 * port_manager_apply_settings() for each port to bring them back up from NVS
 * (this also picks up any settings written while the test was running).
 *
 * @param frozen  True to freeze the ports, false to release them.
 */
void port_manager_set_ports_frozen(bool frozen);

/**
 * @brief Return whether the ports are currently frozen by the factory test.
 *
 * @return true if frozen (see port_manager_set_ports_frozen()).
 */
bool port_manager_ports_frozen(void);

/**
 * @brief Serialisation lock coordinating the freeze transition against parties
 *        that grab a UART OUTSIDE port_manager.
 *
 * The atomic frozen flag alone closes the race for anything that goes through
 * port_manager_set_mode() (it is rejected with PM_ERR_PORTS_FROZEN). But the
 * mqtt-serial bridge takes its UART directly (uart_set_pin() + mb_rtu_open())
 * and only becomes stoppable once its task exists — so a bridge that is mid-start
 * is invisible to the factory test's mqtt_serial_bridge_stop() and would keep
 * driving SERIAL_OUTPUT_PIN_{1,2} just as the LEDC seizes them.
 *
 * Both the bridge's start sequence and the factory test's freeze transition take
 * this lock, so they cannot interleave: the test blocks until an in-flight start
 * has fully finished (and can then stop it), and a start that begins after the
 * freeze is visible bails out via PM_ERR_PORTS_FROZEN. The lock takes no other
 * lock while held (in particular not pm_lock), so it cannot invert with it.
 */
void port_manager_serialize_lock(void);
void port_manager_serialize_unlock(void);

/**
 * @brief Deinitialise a port, releasing its serial port and its TCP listening socket.
 *
 * Release half of the two-phase settings apply: settings_update() releases EVERY subsystem
 * whose socket must change before ANY of them binds a new one, so that a TCP port can be
 * handed over between subsystems — an RS-485 gateway moving onto the port the web server or
 * the cache Modbus server is vacating, or the two gateways swapping ports — without the new
 * bind() hitting EADDRINUSE. port_manager_apply_settings() has no rollback, so a bind that
 * fails leaves the port dead until the next settings write or a reboot.
 *
 * Follow it with port_manager_apply_settings() to bring the port back up. Calling it on an
 * already-released (disabled) port is a no-op.
 *
 * No-op (returns ESP_OK without touching the port) while the ports are frozen by the factory
 * test — see port_manager_set_ports_frozen(). It has to be: apply_settings() is frozen too,
 * so a port torn down here would stay down until the test ends.
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG if port_index is out of range.
 */
esp_err_t port_manager_release(unsigned port_index);

/**
 * @brief Re-apply settings for a port, re-reading mode and parameters from NVS.
 *
 * Deinitialises the current mode and re-initialises from the current NVS
 * settings, including the port mode.  Called by settings_update when any
 * serial, bridge, or mode parameters change — as the acquire half of the
 * two-phase apply, after port_manager_release().
 *
 * No-op (returns ESP_OK without touching the port) while the ports are frozen
 * by the factory test — see port_manager_set_ports_frozen().
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @return ESP_OK on success.
 */
esp_err_t port_manager_apply_settings(unsigned port_index);

/**
 * @brief Check whether any relevant settings have changed for a port.
 *
 * For TCP_BRIDGE mode this delegates to bridge_port_check_settings_changed().
 * For other modes it compares the saved serial config and port mode against
 * the current NVS values.
 *
 * Always reports false while the ports are frozen by the factory test — see
 * port_manager_set_ports_frozen().
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @return true if settings have changed and port_manager_apply_settings()
 *         should be called.
 */
bool port_manager_check_settings_changed(unsigned port_index);

/**
 * @brief Register HTTP handlers for port mode management.
 *
 * Registers:
 *   POST /ports/1/mode        — set mode for port 1 (RS-485 Port 1)
 *   POST /ports/2/mode        — set mode for port 2 (RS-485 Port 2)
 *
 * Port mode status is exposed via the existing GET /info endpoint
 * (rs485_1.port_mode and rs485_2.port_mode fields) — no separate
 * status endpoint is needed.
 *
 * All handlers require authentication via auth_middleware_check().
 *
 * @param server  Running httpd_handle_t.
 * @return ESP_OK on success.
 */
esp_err_t port_manager_register_handlers(httpd_handle_t server);

/**
 * @brief Set or clear the TX-disabled flag for a running RS-485 port immediately.
 *
 * When disabled is true, the RS-485 line driver is physically switched off
 * (dir_pin forced LOW) and serial_send() will silently drop outgoing data.
 * When disabled is false, the dir_pin is returned to UART half-duplex control
 * and normal transmission resumes.
 *
 * Changing tx_disabled does NOT trigger a port restart.
 * If the port is not currently running, the call is a no-op (setting will be
 * applied on the next port_init_mode() invocation).
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @param disabled    True to disable TX, false to re-enable.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if port_index is out of range.
 */
esp_err_t port_manager_set_tx_disabled(unsigned port_index, bool disabled);

/**
 * @brief Send raw bytes to an RS-485 port.
 *
 * Uses the same serial_desc regardless of transport mode (PASSIVE, TCP_BRIDGE).
 * If port is disabled (no serial_desc) or serial_send fails, returns ESP_FAIL.
 * If tx_disabled is set on the descriptor, serial_send silently drops data (returns ESP_OK).
 *
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @param data        Pointer to byte buffer.
 * @param len         Number of bytes to send.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if port_index is out of range,
 *         ESP_FAIL if no serial_desc or transmission fails.
 */
esp_err_t port_manager_send_raw(unsigned port_index, const uint8_t *data, size_t len);

#ifdef __unittest_env__
void port_manager_reset_for_test(void);

/* hex_str_to_bytes: exposed for unit testing */
int hex_str_to_bytes(const char *hex, uint8_t *out, size_t out_max);
#endif /* __unittest_env__ */

