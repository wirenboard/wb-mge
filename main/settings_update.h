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

/**
 * @brief Reset every stored setting to its default, keeping the two Airzone counters.
 *
 * The one place a factory reset may be performed from, because getting it wrong is silent.
 * setting_items_set_defaults(false) rewrites EVERY stored key, the two Airzone counters
 * included — and those must not move. The Z-Wave board reads ANY difference from the value
 * it last read as exactly one event, so a counter put back to 0 here would, after the next
 * reboot, look like an inclusion request nobody made and send the node into inclusion mode
 * on its own. They are captured before the reset and written back after it, leaving NVS and
 * the RAM copies in step.
 *
 * The four Airzone SETTINGS are the opposite case: they really did return to their defaults,
 * so the board has to be told. They are reloaded first and the settings counter moves once
 * afterwards — the same values-then-counter order the POST /settings path keeps, because the
 * board reads the four registers only in the instant the counter changes.
 *
 * Both reset paths — the config button (main.c) and POST /cmd set_default_settings — go
 * through here so they cannot drift apart. Applying the new settings is left to the caller.
 *
 * @return ESP_OK, or the error setting_items_set_defaults() failed with. A counter that
 *         could not be written back is logged and does not fail the reset: the settings are
 *         already at their defaults by then, and reporting failure would say otherwise.
 */
esp_err_t settings_factory_reset(void);
