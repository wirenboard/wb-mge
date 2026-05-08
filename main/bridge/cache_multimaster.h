#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifndef __unittest_env__
#include "esp_http_server.h"
#else
typedef void *httpd_handle_t;
#endif

/* TODO: The /cache/enable and /cache/disable HTTP endpoints have been removed.
 * Port mode control (including cache_bus activation) is now exclusively via
 * POST /ports/N/mode through port_manager.  The /settings bridge enable flow
 * should also be migrated to port_manager in a future refactor. */

/**
 * @file cache_multimaster.h
 * @brief Public API for the caching multimaster feature.
 *
 * This module maintains an in-memory "shadow bus" that records the last known
 * register value returned by each Modbus slave for FC03/FC04 read requests.
 * The cache can be enabled/disabled at runtime and exported as CSV via HTTP.
 */

/**
 * @brief Initialize the cache multimaster module.
 *
 * Creates the internal mutex. Must be called once before any other function.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if mutex creation fails.
 */
esp_err_t cache_multimaster_init(void);

/**
 * @brief Enable caching. Subsequent sniffer packets will update the cache.
 */
void cache_multimaster_enable(void);

/**
 * @brief Disable caching. Also clears the cache and resets pending requests.
 */
void cache_multimaster_disable(void);

/**
 * @brief Check whether caching is currently enabled.
 *
 * @return true if enabled, false otherwise.
 */
bool cache_multimaster_is_enabled(void);

/**
 * @brief Clear all cached register values and pending request state.
 */
void cache_multimaster_clear(void);

/**
 * @brief Notify the cache about an observed master request packet (FC03/FC04).
 *
 * Must be called from sniffer_ws_task when a master request is dequeued.
 * Saves the request context so that the subsequent slave response can be
 * matched and the register values stored in the cache.
 *
 * @param port      0-based RS-485 port index.
 * @param slave_id  Modbus slave address from the request.
 * @param function  Function code (0x01, 0x02, 0x03, or 0x04).
 * @param start_reg Starting register address (0-based, from the request PDU).
 * @param count     Number of registers requested.
 */
void cache_multimaster_on_request(uint8_t port, uint8_t slave_id, uint8_t function,
                                  uint16_t start_reg, uint16_t count);

/**
 * @brief Notify the cache about an observed slave response packet (FC03/FC04).
 *
 * Must be called from sniffer_ws_task when a slave response is dequeued.
 * If a matching pending request exists, the register values are stored.
 *
 * @param port         0-based RS-485 port index.
 * @param slave_id     Modbus slave address from the response.
 * @param function     Function code (0x01, 0x02, 0x03, or 0x04).
 * @param data         Raw packet bytes (data[0]=slave_id, data[1]=FC,
 *                     data[2]=byte_count, data[3..N]=register values big-endian).
 * @param data_len     Total number of bytes in @p data.
 * @param timestamp_us Capture timestamp from esp_timer_get_time().
 */
void cache_multimaster_on_response(uint8_t port, uint8_t slave_id, uint8_t function,
                                   const uint8_t *data, uint16_t data_len,
                                   uint64_t timestamp_us);

/**
 * @brief Register the cache HTTP URI handlers with the given server instance.
 *
 * Registers three endpoints:
 *  - GET  /cache/status   — return enabled flag and entry count
 *  - GET  /cache/csv      — download cache as CSV
 *  - GET  /cache/json     — stream cache as a compact JSON array (for UI polling)
 *
 * Port mode control (enable/disable cache_bus) is handled exclusively via
 * POST /ports/N/mode through port_manager — not through cache endpoints.
 *
 * The JSON endpoint uses chunked transfer with no heap allocation and is
 * optimised for frequent polling: short field names ("s","t","a","v","ts"),
 * mutex released between chunks, no intermediate buffer allocation.
 *
 * @param server HTTP server handle.
 * @return ESP_OK on success.
 */
esp_err_t cache_multimaster_register_handlers(httpd_handle_t server);

/**
 * @brief Look up a single register or coil value in the cache.
 *
 * Searches the flat pool for an entry matching the given slave_id,
 * function_code, and address. The port field is ignored — the first
 * matching entry across all RS-485 ports is returned.
 *
 * @param slave_id      Modbus slave address.
 * @param function_code Modbus function code: 0x01 (coil), 0x02 (discrete),
 *                      0x03 (holding register), 0x04 (input register).
 * @param address       Register or coil address (0-based).
 * @param value_out     Output parameter — set to the cached value if found.
 * @return true if a matching entry was found, false otherwise.
 */
bool cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                               uint16_t address, uint16_t *value_out);
