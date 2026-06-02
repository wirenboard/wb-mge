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
 * @brief Initialize the port manager.
 *
 * Reads the active mode for each port from NVS and brings up the appropriate
 * subsystems.  Also initialises shared infrastructure (sniffer, cache,
 * cache_modbus_server, RS-485 monitors) that was previously done by bridge_init().
 *
 * Must be called once after NVS and settings are ready, in place of the old
 * bridge_init() + cache_modbus_server_init() calls in main.c.
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
 * @param port_index  0-based port index (< BRIDGES_COUNT).
 * @param mode        Target mode.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG if port_index is out of range;
 *         the init error if the new mode failed to initialise (the previous mode
 *         is rolled back); or the NVS save error if the mode initialised live
 *         but could not be persisted — in that case the mode is applied now but
 *         not persisted (persist-6).
 */
esp_err_t port_manager_set_mode(unsigned port_index, pm_mode_t mode);

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
 * @return Pointer to a constant string ("disabled", "tcp_bridge", "passive"),
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
 * @brief Re-apply settings for a port, re-reading mode and parameters from NVS.
 *
 * Deinitialises the current mode and re-initialises from the current NVS
 * settings, including the port mode.  Called by settings_update when any
 * serial, bridge, or mode parameters change.
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

