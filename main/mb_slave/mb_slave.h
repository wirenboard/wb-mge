#pragma once

/*
 * The Modbus slave: one device, reachable three ways at once — RTU on RS-485 port 1, RTU
 * on RS-485 port 2, and Modbus TCP over Ethernet/Wi-Fi. All three answer the same unit id
 * (mb_slave_id) and serve the same register store (mb_registers.h).
 *
 * Framing, CRC, RTU inter-frame timing and the TCP listener all come from the official
 * espressif/esp-modbus component. What this module adds is the wiring: the three
 * controller instances, the UART pins and RS-485 direction control esp-modbus does not
 * touch, and the register hook that routes every request into the register framework.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// The two halves start independently, because they need different things to be ready.
//
// The RS-485 pair needs nothing but NVS, and this device's primary use case is a DIY board
// on a controller's RS-485 bus with no Ethernet cable and no Wi-Fi at all — so app_main
// brings it up early, long before any network exists. The TCP instance waits for a network
// to be up. Both read the serial parameters, the unit id and the TCP port from NVS on
// every call, and both are idempotent.
esp_err_t mb_slave_start_serial(void);
esp_err_t mb_slave_start_tcp(void);

// Stop everything that is running, and start again every half this device has asked for —
// including one that was asked for and is not up, i.e. a start that failed earlier. This
// pair is what applies a settings change (settings_update.c) and what the factory clock_out
// test uses to borrow the UART pins (wb_test.c); neither of them has to know which halves
// exist on this device. mb_slave_start() therefore does NOT bring up a TCP instance that
// was never asked for — on a device whose network never came up, a restart restarts the
// RS-485 ports and nothing else.
//
// mb_slave_stop() on an already-stopped stack is a no-op.
esp_err_t mb_slave_stop(void);
esp_err_t mb_slave_start(void);

// True when the serial parameters of a running port, the unit id or the TCP port in NVS no
// longer match what the running instances were started with — or when a half that should
// be up is not, which is how a failed start gets retried by the next settings write.
bool mb_slave_check_settings_changed(void);

// ── The ownership lock ───────────────────────────────────────────────────────────────
//
// esp-modbus does not survive two tasks tearing the same instances down at once:
// mbc_slave_stop() answers "mb stack start event set error" and the second caller blocks
// inside the delete. A stop/start pair is run from three different tasks — the httpd task
// (POST /wb_test), settings_update_task, and, through settings_update(), the config button's
// task on a factory reset — so the exclusion cannot rest on "they all run on one task".
//
// Every call that creates or deletes an instance takes this lock, and holds it across the
// decision that leads into it as well: settings_update_task reads wb_test_clock_out_active()
// under it, and the clock_out test raises that same guard under it, so the check and the
// act cannot be split by the other side. app_main's one-off mb_slave_start_tcp() takes it
// too — by then the httpd task and config_button_task exist, and the moment that call sets
// mb_tcp_wanted an apply would see "wanted and not running" and start deleting the instance
// being created.
//
// The one call that does NOT take it is app_main's mb_slave_start_serial(), which runs
// before any of those tasks has been created and therefore has nobody to race.
//
// The lock is NOT recursive, so a caller that already holds it must not take it again:
// mb_slave_start()/mb_slave_stop() are called with it held and never take it themselves.
//
// mb_slave_lock_init() is called once from app_main, before the web server and the config
// button task exist. The mutex is statically allocated, so creating it cannot fail; a take
// on a lock that was never initialised returns false rather than running unguarded.
#define MB_SLAVE_LOCK_WAIT_FOREVER  UINT32_MAX

void mb_slave_lock_init(void);
bool mb_slave_lock_take(uint32_t timeout_ms);
void mb_slave_lock_give(void);

// Force the RS-485 driver of one port (index 0 or 1) off by taking its DE/RE pin away
// from the UART and driving it LOW, or hand the pin back. This is the 485_tx_dis_N
// kill-switch; update_serial_tx_disabled() is what applies the NVS setting to it.
esp_err_t mb_slave_set_tx_disabled(unsigned index, bool disabled);
