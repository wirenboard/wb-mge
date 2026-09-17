#pragma once

/*
 * The register framework: the layer a DIY user extends.
 *
 * Every register the device exposes over Modbus is declared once, in the static
 * descriptor table in mb_registers.c. Values live in a plain RAM store guarded by a
 * mutex. There are deliberately NO per-register callbacks: application logic must never
 * run on the Modbus stack's task, because that task is what answers the bus within the
 * RTU inter-frame timeout — a callback that blocks there turns into a timed-out master,
 * not a slow function call. Instead:
 *
 *   - to PUBLISH a value (device -> master), the application calls mb_reg_set();
 *   - to REACT to a master write (master -> device), the application blocks on
 *     mb_slave_event_queue() from a task of its own. See user_app.c.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Which of the two Modbus register spaces a descriptor belongs to. They are separate
// address spaces: input register 0 and holding register 0 are different registers, which
// is why every lookup below takes a kind as well as an address.
typedef enum { MB_REG_INPUT, MB_REG_HOLDING } mb_reg_kind_t;

typedef struct {
    uint16_t      addr;      // Modbus address, 0-based
    mb_reg_kind_t kind;
    bool          writable;  // holding only
    const char   *name;      // for logs
} mb_reg_desc_t;

// Which transport a master write arrived on. All three instances share one register
// store and one unit id, so this is the only thing that tells them apart.
typedef enum { MB_SRC_RTU_PORT1, MB_SRC_RTU_PORT2, MB_SRC_TCP } mb_event_src_t;

typedef struct {
    mb_event_src_t src;
    uint16_t       addr;
    uint16_t       value;
    int64_t        timestamp_us;
} mb_write_event_t;

// Create the store, the mutex and the event queue, and start the 1 Hz task that refreshes
// the demo input registers. Idempotent: a second call is a no-op, so mb_slave_start() may
// call it on every restart.
esp_err_t mb_registers_init(void);

// Read / publish one register value. ESP_ERR_NOT_FOUND when (kind, addr) is not in the
// descriptor table.
//
// mb_reg_set() is the application's way to publish a value; it writes the store and does
// NOT post an event — events describe writes that came from a Modbus master.
esp_err_t mb_reg_get(mb_reg_kind_t kind, uint16_t addr, uint16_t *out);
esp_err_t mb_reg_set(mb_reg_kind_t kind, uint16_t addr, uint16_t value);

// The Modbus side of a write: update the store, then post exactly one event describing it.
// One event per register, so an FC16 over 4 registers produces 4 events.
esp_err_t mb_reg_write_from_master(mb_event_src_t src, mb_reg_kind_t kind,
                                   uint16_t addr, uint16_t value);

// Look a descriptor up without touching the store. The register hook in mb_slave.c uses
// this to validate a whole request range before applying any of it: Modbus requires that a
// request naming even one bad address is refused with ILLEGAL DATA ADDRESS and changes
// nothing. Returns NULL when there is no such register.
const mb_reg_desc_t *mb_reg_find(mb_reg_kind_t kind, uint16_t addr);

// The queue of master writes, depth MB_EVENT_QUEUE_DEPTH, holding mb_write_event_t by
// value. NULL before mb_registers_init(). The application owns the receiving end; nothing
// in this module reads from it.
QueueHandle_t mb_slave_event_queue(void);
