/*
 * Poll group planner: merge contiguous HOLDING/INPUT register channels into
 * single multi-register Modbus reads to cut round-trips per poll cycle.
 * Pure logic — no I/O, no ESP dependencies; host-testable.
 */
#include "poll_planner.h"
#include <stdint.h>

/* Upper bound on collected channel indices. Channels are few (< 60), so a
 * fixed cap keeps the planner allocation-free and identical in both builds. */
#define POLL_IDX_CAP 256

/* Greedy merge: walk the sorted list, extending the open group while the
 * total span fits in max_read_regs and the gap to the next channel is within
 * max_gap; otherwise close it and open a new one. */
int mb_plan_poll_groups(const wb_channel_t *channels, int num_channels,
                        uint16_t max_read_regs, uint16_t max_gap,
                        poll_group_t *out, int max_groups)
{
    int idx[POLL_IDX_CAP];
    int n = 0;

    for (int i = 0; i < num_channels; i++) {
        const wb_channel_t *ch = &channels[i];
        if (ch->enabled && (ch->reg_type == REG_HOLDING || ch->reg_type == REG_INPUT)) {
            if (n < POLL_IDX_CAP) {
                idx[n++] = i;
            }
        }
    }

    /* Insertion sort by (reg_type, address) ascending. */
    for (int i = 1; i < n; i++) {
        int key = idx[i];
        int j = i - 1;
        while (j >= 0) {
            const wb_channel_t *a = &channels[idx[j]];
            const wb_channel_t *b = &channels[key];
            int swap = ((a->reg_type > b->reg_type) ||
                        ((a->reg_type == b->reg_type) && (a->address > b->address)));
            if (!swap) {
                break;
            }
            idx[j + 1] = idx[j];
            j--;
        }
        idx[j + 1] = key;
    }

    int n_groups = 0;
    int have_open = 0;
    reg_type_t cur_type = REG_HOLDING;
    uint32_t cur_start = 0;
    uint32_t cur_end = 0;
    int cur_members[POLL_GROUP_MAX_MEMBERS];
    int cur_n_members = 0;

    for (int i = 0; i < n; i++) {
        const wb_channel_t *ch = &channels[idx[i]];
        uint32_t span = ch->num_regs ? ch->num_regs : 1;
        uint32_t ns = ch->address;
        uint32_t ne = ch->address + span;

        int merge = 0;
        if (have_open) {
            int same_type = (ch->reg_type == cur_type);
            int span_fits = ((ne - cur_start) <= (uint32_t)max_read_regs);
            int gap_ok = (ns <= (cur_end + (uint32_t)max_gap));
            int room = (cur_n_members < POLL_GROUP_MAX_MEMBERS);
            merge = (same_type && span_fits && gap_ok && room);
        }

        if (merge) {
            if (ne > cur_end) {
                cur_end = ne;
            }
            cur_members[cur_n_members++] = idx[i];
            continue;
        }

        if (have_open) {
            if (n_groups < max_groups) {
                out[n_groups].reg_type = cur_type;
                out[n_groups].start = (uint16_t)cur_start;
                /* Hard-clamp so a lone oversized channel can never exceed the
                 * caller's read buffer (num_regs is also clamped at parse). */
                out[n_groups].count = (uint16_t)((cur_end - cur_start) > (uint32_t)max_read_regs
                                                 ? (uint32_t)max_read_regs : (cur_end - cur_start));
                for (int k = 0; k < cur_n_members; k++) {
                    out[n_groups].members[k] = cur_members[k];
                }
                out[n_groups].n_members = cur_n_members;
                n_groups++;
            }
        }

        cur_type = ch->reg_type;
        cur_start = ns;
        cur_end = ne;
        cur_members[0] = idx[i];
        cur_n_members = 1;
        have_open = 1;
    }

    if (have_open && (n_groups < max_groups)) {
        out[n_groups].reg_type = cur_type;
        out[n_groups].start = (uint16_t)cur_start;
        /* Hard-clamp so a lone oversized channel can never exceed the caller's
         * read buffer (num_regs is also clamped at parse). */
        out[n_groups].count = (uint16_t)((cur_end - cur_start) > (uint32_t)max_read_regs
                                         ? (uint32_t)max_read_regs : (cur_end - cur_start));
        for (int k = 0; k < cur_n_members; k++) {
            out[n_groups].members[k] = cur_members[k];
        }
        out[n_groups].n_members = cur_n_members;
        n_groups++;
    }

    return n_groups;
}
