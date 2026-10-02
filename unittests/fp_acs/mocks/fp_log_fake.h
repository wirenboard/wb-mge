#pragma once

/*
 * fp_log (main/fp_acs/fp_log.h) as fp_acs sees it. fp_log_append() keeps a copy of every event,
 * adds "log" to the trace and returns the next seq queued with fp_log_fake_push_seq(); once those
 * are used up it numbers on from the last one, as the real log does, or returns 0 (the log is not
 * running) when fp_log_fake_running is false.
 */

#include <stdbool.h>
#include <stdint.h>

#include "fp_acs.h"

#define FP_LOG_FAKE_EVENTS_MAX      16

extern int            fp_log_fake_init_calls;
extern int            fp_log_fake_append_calls;
extern bool           fp_log_fake_running;
extern fp_acs_event_t fp_log_fake_events[FP_LOG_FAKE_EVENTS_MAX];
// Called inside fp_log_append(), before it returns: what fp_acs looks like while it logs.
extern void (*fp_log_fake_append_hook)(const fp_acs_event_t *event);

void fp_log_fake_push_seq(uint32_t seq);
void fp_log_fake_reset(void);
