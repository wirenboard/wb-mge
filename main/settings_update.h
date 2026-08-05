#pragma once

#include "esp_err.h"

/**
 * @brief Apply the settings that have just been written to NVS to the running system.
 *
 * Reconciles every subsystem whose stored configuration differs from what it is running:
 * the RS-485 ports, the cache Modbus TCP server, the web server, mDNS, Ethernet and Wi-Fi
 * are handed to an async settings_update_task (two-phase release/acquire), while the
 * settings that need no socket work — V-out, tx_disabled, the I/O bus and the runtime
 * cache overlay — are applied synchronously on the caller's task before it returns.
 *
 * @param cache_apply_err  Optional out-parameter for the result of the synchronous cache
 *                         overlay apply (port_manager_apply_cache_settings()). It is the
 *                         one step here whose failure the user can act on and that nothing
 *                         retries, so a caller that can answer the user — POST /settings —
 *                         reports it as a response warning. Set to ESP_OK when the overlay
 *                         already matched NVS. May be NULL; the failure is logged either way.
 * @return ESP_OK, or ESP_FAIL if the async settings_update_task could not be created.
 *         The cache apply result is deliberately NOT folded into this: a task that could
 *         not be created and a cache that would not move are different failures with
 *         different answers, and this return value already has the first meaning.
 */
esp_err_t settings_update_with_status(esp_err_t *cache_apply_err);

/**
 * @brief settings_update_with_status() for callers with nowhere to report the cache apply.
 *
 * The factory-reset button (main.c) and POST /cmd set_default_settings answer no settings
 * request, so a failed cache apply has no response to travel back in; it is logged and that
 * is all. Everything else behaves identically.
 */
esp_err_t settings_update(void);
