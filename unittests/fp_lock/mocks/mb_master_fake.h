#pragma once

/*
 * The Modbus master as fp_lock sees it, over fake relay modules told apart by their unit id: their
 * coils (mb_master_write_coil(), mb_master_read_coil()) and their holding registers
 * (mb_master_read_holding(), mb_master_write_holding()).
 *
 * Coils: a write that goes through sets the coil, a read returns it (OFF until written). The result
 * of a coil exchange is the next one queued with mb_fake_push_results(), in the order the coil
 * exchanges happen, or mb_fake_default_result once the queue is used up.
 *
 * Holding registers: every register reads 0 until it is set (mb_fake_set_holding()) or written. An
 * exchange fails with the error set for its register (mb_fake_set_read_error(),
 * mb_fake_set_write_error()) - a read when any register it covers has one - else with
 * mb_fake_default_result; one that fails changes nothing. Every write tried is logged in
 * mb_fake_writes, whatever its result.
 *
 * Every exchange is appended to the trace (mocks/fp_acs/trace.h):
 *     "W<unit>.<coil>=<0|1>"     a coil write
 *     "R<unit>.<coil>"           a coil read
 *     "H<unit>.<reg>=<value>"    a holding register write
 *     "H<unit>.<reg>?"           a holding register read, and "H<unit>.<reg>*<count>?" for more
 *                                than one register - only while mb_fake_trace_reads is set
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define MB_FAKE_WRITES_MAX          64

typedef struct {
    uint8_t   unit;
    uint16_t  reg;
    uint16_t  value;
    esp_err_t result;
} mb_fake_write_t;

extern esp_err_t       mb_fake_default_result;
// Exchanges sent to a port other than 0 (RS-485 port 1, where the relay module is).
extern int             mb_fake_wrong_port_count;
extern int             mb_fake_exchange_count;
// Whether holding register reads are traced: off, so that a trace shows what is written.
extern bool            mb_fake_trace_reads;
// The holding register writes tried, in order; mb_fake_write_count goes on past MB_FAKE_WRITES_MAX.
extern mb_fake_write_t mb_fake_writes[MB_FAKE_WRITES_MAX];
extern unsigned        mb_fake_write_count;
// Holding register reads (FC03 requests, whatever their register count).
extern unsigned        mb_fake_read_count;

// Queues result for the next times coil exchanges.
void     mb_fake_push_results(esp_err_t result, unsigned times);
void     mb_fake_reset(void);

void     mb_fake_set_holding(uint8_t unit, uint16_t reg, uint16_t value);
uint16_t mb_fake_holding(uint8_t unit, uint16_t reg);
// Makes every read (or write) of the register fail with err from now on; ESP_OK lifts it.
void     mb_fake_set_read_error(uint8_t unit, uint16_t reg, esp_err_t err);
void     mb_fake_set_write_error(uint8_t unit, uint16_t reg, esp_err_t err);
bool     mb_fake_coil(uint8_t unit, uint16_t coil);
