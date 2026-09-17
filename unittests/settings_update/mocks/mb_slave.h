#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

extern int  mock_mb_slave_start_called;
extern int  mock_mb_slave_stop_called;
extern int  mock_mb_slave_check_settings_changed_called;
extern bool mock_mb_slave_check_settings_changed_result;

/* Call ids (call_sequence.c), so a test can assert the ORDER of the ownership lock
 * against the stop/start pair it is supposed to enclose. 0 = never called in this test;
 * mock_mb_slave_reset() clears them, so a value left over from an earlier test cannot
 * make an ordering assertion pass by accident. */
extern unsigned mock_mb_slave_start_call_seq;
extern unsigned mock_mb_slave_stop_call_seq;

/* The ownership lock. take() answers mock_mb_slave_lock_take_result (true by default)
 * and records the timeout it was asked for, so a test can exercise both the normal path
 * and the "the lock was not free" one. */
extern int      mock_mb_slave_lock_init_called;
extern int      mock_mb_slave_lock_take_called;
extern int      mock_mb_slave_lock_give_called;
extern unsigned mock_mb_slave_lock_take_call_seq;
extern unsigned mock_mb_slave_lock_give_call_seq;
extern uint32_t mock_mb_slave_lock_take_timeout_ms;
extern bool     mock_mb_slave_lock_take_result;
/* Takes that have not been given back yet: a single-threaded test reads it to tell
 * whether the code under test still holds the lock. */
extern int      mock_mb_slave_lock_held_count;

void mock_mb_slave_reset(void);

esp_err_t mb_slave_start(void);
esp_err_t mb_slave_stop(void);
bool mb_slave_check_settings_changed(void);
esp_err_t mb_slave_set_tx_disabled(unsigned index, bool disabled);

#define MB_SLAVE_LOCK_WAIT_FOREVER  UINT32_MAX

void mb_slave_lock_init(void);
bool mb_slave_lock_take(uint32_t timeout_ms);
void mb_slave_lock_give(void);
