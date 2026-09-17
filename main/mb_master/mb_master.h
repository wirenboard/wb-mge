#pragma once

/*
 * The Modbus RTU master: one instance per RS-485 port, and a blocking request API the
 * application calls from its own task.
 *
 * This is the mirror image of main/mb_slave/. The slave answers a bus; the master drives
 * one. The two are never compiled together - main/Kconfig.projbuild picks exactly one
 * role for the whole firmware - so they share the UART numbers, the board pins, the
 * per-port NVS keys and the ownership discipline, but no code.
 *
 * There is NO Modbus TCP here and no register store: TCP belongs to the slave role, and a
 * master has nothing to publish. What it has instead is mb_master_read_holding() and its
 * two siblings below.
 *
 * Deliberately NOT built on the CID / characteristics-descriptor mechanism esp-modbus
 * offers (mbc_master_set_descriptor + mbc_master_get_parameter). That mechanism is a
 * polling table: every value a device might read has to be declared up front, in a static
 * table, before a single request can be sent. What a DIY user wants is a function call -
 * "read 4 holding registers at 200 from slave 7 on port 1, now, from my own task" - so
 * these three functions sit straight on mbc_master_send_request(), which takes the
 * request as data rather than as a table entry.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// Bring both RS-485 ports up as Modbus RTU masters. Needs nothing but NVS (the serial
// parameters come from the same baudrate_N / parity_N / stopbits_N / databits_N keys the
// slave role reads), so app_main calls it early, before any network exists. Idempotent.
//
// Each port is brought up on its own: one that refuses to start does not take the other
// down with it. ESP_FAIL means neither came up.
esp_err_t mb_master_start_serial(void);

// Stop everything that is running, and start again what this device has asked for. This
// pair is what applies a settings change (settings_update.c) and what the factory
// clock_out test uses to borrow the UART pins (wb_test.c) - the same contract as the
// slave role's mb_slave_stop()/mb_slave_start(), because those two callers must not have
// to know which role they were built into.
//
// mb_master_stop() on an already-stopped stack is a no-op. It also waits for any request
// in flight on either port: an application task blocked inside mbc_master_send_request()
// is holding the instance this would delete.
esp_err_t mb_master_stop(void);
esp_err_t mb_master_start(void);

// True when the serial parameters of a running port no longer match NVS, or when a port
// that should be up is not - which is how a failed start gets retried by the next settings
// write. The unit id and the TCP port the slave role compares here do not exist in this
// role.
bool mb_master_check_settings_changed(void);

// ── The ownership lock ───────────────────────────────────────────────────────────────
//
// The same lock, with the same discipline, as main/mb_slave/mb_slave.h describes at
// length: esp-modbus does not survive two tasks tearing the same instances down at once,
// the stop/start pair is run from three different tasks (the httpd task through
// POST /wb_test, settings_update_task, and config_button_task on a factory reset), so
// every call that creates or deletes an instance takes this lock and holds it across the
// decision that leads into it.
//
// Not recursive: mb_master_start()/mb_master_stop() are called with it held and never take
// it themselves. The one call that does NOT take it is app_main's
// mb_master_start_serial(), which runs before any of those tasks has been created.
//
// The mutex is statically allocated, so mb_master_lock_init() cannot fail; a take on a
// lock that was never initialised returns false rather than running unguarded.
#define MB_MASTER_LOCK_WAIT_FOREVER  UINT32_MAX

void mb_master_lock_init(void);
bool mb_master_lock_take(uint32_t timeout_ms);
void mb_master_lock_give(void);

// Force the RS-485 driver of one port (index 0 or 1) off by taking its DE/RE pin away from
// the UART and driving it LOW, or hand the pin back. This is the 485_tx_dis_N kill-switch,
// and it matters at least as much here as in the slave role: a master TRANSMITS by itself,
// so this is the setting that keeps a port quiet on a bus the user does not want driven.
esp_err_t mb_master_set_tx_disabled(unsigned index, bool disabled);

// ── The request API ──────────────────────────────────────────────────────────────────
//
// Blocking calls, meant to be made from an ordinary application task (see
// main/mb_master/user_app.c). They block for as long as the exchange takes - up to the
// response timeout when nothing answers - so do not call them from a callback, a timer or
// anything else that must return promptly.
//
// port is 0 for RS-485 port 1 (UART1) and 1 for RS-485 port 2 (UART2). addr is the 0-based
// Modbus register address, exactly as it goes on the wire. out points at count native
// uint16_t values: esp-modbus has already swapped each register out of big-endian wire
// order, so no byte juggling is needed here.
//
// One mutex per port serialises these: two application tasks may call into the same port
// concurrently and will simply queue, rather than interleaving two requests on one bus.
//
// Return values worth telling apart:
//   ESP_OK                  - the slave answered and out holds its values
//   ESP_ERR_TIMEOUT         - nothing answered within the response timeout. This is the
//                             ordinary "that device is not on this bus / is switched off"
//                             answer and is deliberately distinguishable from every other
//                             failure.
//   ESP_ERR_INVALID_RESPONSE- something answered, but not with a frame we could use
//   ESP_ERR_NOT_SUPPORTED   - the slave refused the function code
//   ESP_ERR_INVALID_STATE   - this port is not running (stopped, or its start failed)
//   ESP_ERR_INVALID_ARG     - bad port, count or pointer; or, on the two reads, slave_id 0,
//                             which is the broadcast address and cannot be read from
//   ESP_FAIL                - the slave answered with a Modbus exception
esp_err_t mb_master_read_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                 uint16_t count, uint16_t *out);
esp_err_t mb_master_read_input(uint8_t port, uint8_t slave_id, uint16_t addr,
                               uint16_t count, uint16_t *out);
esp_err_t mb_master_write_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                  uint16_t value);
