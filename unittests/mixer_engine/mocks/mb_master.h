#pragma once

/* Mock of the Modbus RTU master request API, for the mixer engine tests.
 *
 * Only the two calls the VFD driver makes are here: mb_master_read_holding() and
 * mb_master_write_holding(). The rest of mb_master.h is the stack's lifecycle, which the
 * engine never touches.
 *
 * Reads are answered out of a flat register file indexed by register address, so a test sets
 * mock_mb_master_regs[8] and the driver's read of 8..10 sees it. Writes are recorded in call
 * order, which is what the step-transition tests assert on: frequency BEFORE command. */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define MOCK_MB_MASTER_MAX_WRITES   64
#define MOCK_MB_MASTER_REG_COUNT    32

typedef struct {
    uint8_t  port;
    uint8_t  slave_id;
    uint16_t addr;
    uint16_t value;
} mock_mb_master_write_t;

extern mock_mb_master_write_t mock_mb_master_writes[MOCK_MB_MASTER_MAX_WRITES];
extern int mock_mb_master_write_called;

/* What every write answers. ESP_OK by default. */
extern esp_err_t mock_mb_master_write_result;

/* The register file reads are answered from, and what every read answers.
 * ESP_OK by default; set to ESP_ERR_TIMEOUT to simulate a drive that is not there. */
extern uint16_t  mock_mb_master_regs[MOCK_MB_MASTER_REG_COUNT];
extern esp_err_t mock_mb_master_read_result;
extern int       mock_mb_master_read_called;
extern uint8_t   mock_mb_master_last_read_port;
extern uint8_t   mock_mb_master_last_read_slave_id;
extern uint16_t  mock_mb_master_last_read_addr;
extern uint16_t  mock_mb_master_last_read_count;

void mock_mb_master_reset(void);

esp_err_t mb_master_read_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                 uint16_t count, uint16_t *out);
esp_err_t mb_master_write_holding(uint8_t port, uint8_t slave_id, uint16_t addr,
                                  uint16_t value);
