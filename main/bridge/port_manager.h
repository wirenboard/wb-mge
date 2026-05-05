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
 * @brief Mutually exclusive operating modes for each RS-485 port.
 *
 * PM_MODE_DISABLED   — serial port is not opened; port is fully inactive.
 * PM_MODE_TCP_BRIDGE — serial port is open; traffic is forwarded over TCP
 *                      (transparent or Modbus framing, driven by bridge settings).
 * PM_MODE_SNIFFER    — serial port is open; packets are parsed and streamed
 *                      to WebSocket clients via the sniffer module.
 * PM_MODE_CACHE_BUS  — serial port is open; packets are parsed and the latest
 *                      register values are kept in the in-memory cache
 *                      (cache_multimaster).
 */
typedef enum {
    PM_MODE_DISABLED   = 0,
    PM_MODE_TCP_BRIDGE = 1,
    PM_MODE_SNIFFER    = 2,
    PM_MODE_CACHE_BUS  = 3,
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
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if port_index is out of range.
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
 * @return Pointer to a constant string ("disabled", "tcp_bridge", "sniffer",
 *         "cache_bus"), or "unknown" for unrecognised values.
 */
const char *port_manager_mode_to_str(pm_mode_t mode);

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

