#pragma once

/*
 * fp_lock (main/fp_acs/fp_lock.h) as its callers see it: fp_lock_open() counts the requests, adds
 * "open" to the trace and answers fp_lock_fake_open_result.
 */

#include <stdbool.h>

extern int  fp_lock_fake_open_calls;
extern bool fp_lock_fake_open_result;

void fp_lock_fake_reset(void);
