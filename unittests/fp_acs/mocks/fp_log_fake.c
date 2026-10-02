#include "fp_log_fake.h"

#include <string.h>

#include "fp_log.h"
#include "trace.h"

#define FP_LOG_FAKE_SEQS_MAX        16

int            fp_log_fake_init_calls = 0;
int            fp_log_fake_append_calls = 0;
bool           fp_log_fake_running = true;
fp_acs_event_t fp_log_fake_events[FP_LOG_FAKE_EVENTS_MAX];
void (*fp_log_fake_append_hook)(const fp_acs_event_t *event) = NULL;

static uint32_t fp_log_fake_seqs[FP_LOG_FAKE_SEQS_MAX];
static unsigned fp_log_fake_seqs_len;
static unsigned fp_log_fake_seqs_next;
static uint32_t fp_log_fake_last_seq;

void fp_log_fake_push_seq(uint32_t seq)
{
    if (fp_log_fake_seqs_len < FP_LOG_FAKE_SEQS_MAX) {
        fp_log_fake_seqs[fp_log_fake_seqs_len++] = seq;
    }
}

void fp_log_fake_reset(void)
{
    fp_log_fake_init_calls = 0;
    fp_log_fake_append_calls = 0;
    fp_log_fake_running = true;
    memset(fp_log_fake_events, 0, sizeof(fp_log_fake_events));
    fp_log_fake_append_hook = NULL;
    fp_log_fake_seqs_len = 0;
    fp_log_fake_seqs_next = 0;
    fp_log_fake_last_seq = 0;
}

void fp_log_init(void)
{
    fp_log_fake_init_calls++;
}

void fp_log_clock_start(void)
{
}

uint32_t fp_log_append(const fp_acs_event_t *event)
{
    if (fp_log_fake_append_calls < FP_LOG_FAKE_EVENTS_MAX) {
        fp_log_fake_events[fp_log_fake_append_calls] = *event;
    }
    fp_log_fake_append_calls++;
    trace_add("log");
    if (fp_log_fake_append_hook != NULL) {
        fp_log_fake_append_hook(event);
    }
    if (fp_log_fake_seqs_next < fp_log_fake_seqs_len) {
        fp_log_fake_last_seq = fp_log_fake_seqs[fp_log_fake_seqs_next++];
        return fp_log_fake_last_seq;
    }
    if (!fp_log_fake_running) {
        return 0;
    }
    return ++fp_log_fake_last_seq;
}

size_t fp_log_snapshot(fp_log_entry_t *out, size_t max)
{
    (void)out;
    (void)max;
    return 0;
}
