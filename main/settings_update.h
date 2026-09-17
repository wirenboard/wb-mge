#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Create the lock that serialises the spawn decision in settings_update().
 *
 * Call once from app_main, before the web server and the config button task exist — those
 * are the two places settings_update() is reached from, and it must be serialised between
 * them. Idempotent, cannot fail (the mutex is statically allocated).
 */
void settings_update_init(void);

/**
 * @brief Apply the settings that have just been written to NVS to the running system.
 *
 * Reconciles every subsystem whose stored configuration differs from what it is running:
 * the web server, mDNS, Ethernet, Wi-Fi and the Modbus slave are handed to an async
 * settings_update_task, while the settings that need no socket work — V-out, the
 * terminators, the per-port tx_disabled kill-switch and the I/O bus — are applied
 * synchronously on the caller's task before it returns.
 *
 * V-out/terminators and the Modbus slave restart are skipped while the factory clock-out
 * test holds the RS-485 hardware (wb_test_clock_out_active()); that test re-applies both
 * from NVS when it exits.
 *
 * Called from more than one task: the POST /settings and POST /command handlers on the
 * httpd task, and the factory reset in main.c on config_button_task. The spawn decision is
 * serialised between them (settings_update_init()), and the Modbus stop/start pair the
 * async task runs is serialised against the clock_out test by mb_slave_lock_take().
 *
 * @return ESP_OK, or ESP_FAIL if the async settings_update_task could not be created.
 */
esp_err_t settings_update(void);

/**
 * @brief True while the async settings_update_task is still applying a previous write.
 *
 * Lets the factory clock-out test answer 503 instead of blocking its HTTP handler on the
 * mb_slave lock for as long as an apply lasts. It is NOT what makes the two mutually
 * exclusive — mb_slave_lock_take() is; this only makes the common case a clean refusal.
 *
 * Safe to call from any task.
 */
bool settings_update_in_progress(void);

/**
 * @brief True while the in-flight apply is one that restarts the web server.
 *
 * Such an apply calls httpd_stop(), which blocks until the httpd thread has left its
 * handler. An HTTP handler must therefore never WAIT for that apply to finish: it would be
 * waiting for a task that is waiting for it, until its own timeout expires. The clock-out
 * test refuses on the spot instead.
 *
 * False when no apply is in flight. Safe to call from any task.
 */
bool settings_update_restarts_web_server(void);
