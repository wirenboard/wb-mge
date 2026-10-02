#pragma once

/*
 * The Modbus master as fp_lock sees it: mb_master_write_coil() and mb_master_read_coil().
 *
 * Every exchange is appended to the trace (mocks/fp_acs/trace.h): "W<unit>.<coil>=<0|1>" for a
 * write, "R<unit>.<coil>" for a read. Its result is the next one queued with
 * mb_fake_push_results(), in the order the exchanges happen, or mb_fake_default_result once the
 * queue is used up.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

extern esp_err_t mb_fake_default_result;
// Exchanges sent to a port other than 0 (RS-485 port 1, where the relay module is).
extern int       mb_fake_wrong_port_count;
extern int       mb_fake_exchange_count;

// Queues result for the next times exchanges.
void mb_fake_push_results(esp_err_t result, unsigned times);
void mb_fake_reset(void);
