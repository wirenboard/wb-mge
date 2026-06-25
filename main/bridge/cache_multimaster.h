#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#include "esp_http_server.h"

/* The /cache/enable and /cache/disable HTTP endpoints have been removed.
 * Port mode control (including cache_bus activation) is now exclusively via
 * POST /ports/N/mode through port_manager.
 *
 * The single-axis migration is complete: port_mode (set via POST /ports/N/mode
 * and the one-time legacy NVS migration in setting_items_migrate_port_mode()) is
 * the authoritative on/off axis for a port. bridge_mode is no longer an on/off
 * flag — it is only the TCP role (server/client) used when port_mode == tcp_bridge. */

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
 * @brief Invalidate the pending request for a port without storing anything.
 *
 * Must be called from sniffer_ws_task when a transaction ends WITHOUT a
 * cacheable response — a bus timeout, an exception reply (function code has the
 * 0x80 error bit) or a malformed slave frame. Those events never reach
 * cache_multimaster_on_response(), so without this call the pending request
 * would linger and a later, unrelated response of the same slave+FC (e.g. one
 * whose own request was missed on the shared bus) would be bound to this stale
 * request's start address, corrupting the cache (corr-7).
 *
 * @param port 0-based RS-485 port index.
 */
void cache_multimaster_clear_pending(uint8_t port);

/**
 * @brief Register the cache HTTP URI handlers with the given server instance.
 *
 * Registers three endpoints:
 *  - GET  /cache/status   — return enabled flag and entry count
 *  - GET  /cache/csv      — download cache as CSV (columns: slave_id,type,address,value,age_s)
 *  - GET  /cache/json     — stream cache as a JSON object (for UI polling):
 *                           {"d":[{"s":3,"t":"h","a":100,"v":1234,"age":42},...]}
 *                           age – seconds since last update, saturates at 65535 (~18 h)
 *
 * Port mode control (enable/disable cache_bus) is handled exclusively via
 * POST /ports/N/mode through port_manager — not through cache endpoints.
 *
 * The JSON endpoint uses chunked transfer with no heap allocation and is
 * optimised for frequent polling: short field names ("s","t","a","v","age"),
 * mutex released between chunks, no intermediate buffer allocation.
 *
 * @param server HTTP server handle.
 * @return ESP_OK on success.
 */
esp_err_t cache_multimaster_register_handlers(httpd_handle_t server);

/**
 * @brief Result codes returned by cache_multimaster_lookup().
 */
typedef enum {
    CACHE_LOOKUP_NOT_FOUND = 0,   /* No entry exists for this address */
    CACHE_LOOKUP_FOUND     = 1,   /* Entry found and fresh (within timeout) */
    CACHE_LOOKUP_STALE     = 2,   /* Entry found but older than value_timeout_s */
} cache_lookup_result_t;

/**
 * @brief Look up a single register or coil value in the cache.
 *
 * Searches the flat pool for an entry matching the given slave_id,
 * function_code, and address. The port field is ignored — the first
 * matching entry across all RS-485 ports is returned.
 *
 * @param slave_id        Modbus slave address.
 * @param function_code   Modbus function code: 0x01 (coil), 0x02 (discrete),
 *                        0x03 (holding register), 0x04 (input register).
 * @param address         Register or coil address (0-based).
 * @param value_out       Output parameter — set to the cached value if found.
 * @param value_timeout_s Age threshold in seconds. 0 disables the timeout
 *                        check and always returns CACHE_LOOKUP_FOUND for an
 *                        existing entry. 1..65535 enables the check: entries
 *                        older than this value return CACHE_LOOKUP_STALE.
 * @return CACHE_LOOKUP_NOT_FOUND if no entry exists,
 *         CACHE_LOOKUP_STALE if the entry is older than value_timeout_s,
 *         CACHE_LOOKUP_FOUND if the entry is fresh.
 */
cache_lookup_result_t cache_multimaster_lookup(uint8_t slave_id, uint8_t function_code,
                                               uint16_t address, uint16_t *value_out,
                                               uint16_t value_timeout_s);

/**
 * @brief Aggregate cache statistics snapshot.
 */
typedef struct {
    uint32_t packets_processed;   /* total response packets stored since last cache reset */
    uint32_t last_packet_age_s;   /* seconds since last stored packet (0 if none)          */
    uint32_t map_age_s;           /* seconds since last cache enable/clear (0 if none)      */
    uint16_t devices_on_bus;      /* count of unique slave_ids currently in the pool        */
} cache_multimaster_stats_t;

/**
 * @brief Read an aggregate snapshot of the cache statistics.
 *
 * Mirrors the logic in cache_status_handler(): counts unique slave_ids and
 * reads the packet/reset timestamps under the cache mutex, then computes ages
 * relative to the current time. Returns all-zero when the cache module is not
 * initialized (mutex or pool NULL).
 *
 * @param out Output snapshot; ignored if NULL.
 */
void cache_multimaster_get_stats(cache_multimaster_stats_t *out);
