#pragma once
/*
 * Poll group planner: merge contiguous HOLDING/INPUT register channels into
 * single multi-register Modbus reads to cut round-trips per poll cycle.
 * Pure logic — no I/O, no ESP dependencies; host-testable.
 */
#include "template.h"   /* wb_channel_t, reg_type_t */
#include <stdint.h>

#define POLL_GROUP_MAX_MEMBERS 64   /* channels merged into one read */
#define POLL_MAX_READ_REGS     WB_MAX_REGS_PER_CHANNEL  /* Modbus holding/input read cap (125) */
#define POLL_MAX_GAP_REGS      16   /* bridge gaps up to this many wasted regs */

typedef struct {
    reg_type_t reg_type;                     /* REG_HOLDING or REG_INPUT */
    uint16_t   start;                        /* first register address */
    uint16_t   count;                        /* registers to read */
    int        members[POLL_GROUP_MAX_MEMBERS];
    int        n_members;
} poll_group_t;

/* Plan contiguous read groups for enabled HOLDING and INPUT channels.
 * Coil/discrete channels and disabled channels are ignored (polled elsewhere).
 * Greedy: sort by (reg_type, address); extend a group while the total span
 * stays within max_read_regs AND the gap to the next channel is <= max_gap.
 * Returns the number of groups written to out[] (<= max_groups). */
int mb_plan_poll_groups(const wb_channel_t *channels, int num_channels,
                        uint16_t max_read_regs, uint16_t max_gap,
                        poll_group_t *out, int max_groups);
