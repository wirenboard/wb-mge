/*
 * Unit tests for poll_planner.c (Part A: pure register-read group planner).
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "poll_planner.h"

void setUp(void)    {}
void tearDown(void) {}

/* Build a single zero-initialised channel with the fields the planner reads. */
static wb_channel_t mk_chan(reg_type_t rt, uint32_t addr, uint32_t nregs, bool enabled)
{
    wb_channel_t ch = {0};
    ch.reg_type = rt;
    ch.address  = addr;
    ch.num_regs = nregs;
    ch.enabled  = enabled;
    return ch;
}

/* Four contiguous input channels; array is deliberately unsorted (addr 9 first)
 * so the sort step is exercised. Members must come out in address order. */
void test_merges_contiguous_input(void)
{
    wb_channel_t chans[4];
    chans[0] = mk_chan(REG_INPUT, 9, 2, true);   /* out-of-order */
    chans[1] = mk_chan(REG_INPUT, 4, 1, true);
    chans[2] = mk_chan(REG_INPUT, 5, 1, true);
    chans[3] = mk_chan(REG_INPUT, 8, 1, true);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 4, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 8);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL(REG_INPUT, g[0].reg_type);
    TEST_ASSERT_EQUAL_INT(4, g[0].start);
    TEST_ASSERT_EQUAL_INT(7, g[0].count);        /* 9+2 - 4 = 7 */
    TEST_ASSERT_EQUAL_INT(4, g[0].n_members);
    TEST_ASSERT_EQUAL_INT(1, g[0].members[0]);   /* addr 4 lives at index 1 */
    TEST_ASSERT_EQUAL_INT(2, g[0].members[1]);   /* addr 5 -> index 2 */
    TEST_ASSERT_EQUAL_INT(3, g[0].members[2]);   /* addr 8 -> index 3 */
    TEST_ASSERT_EQUAL_INT(0, g[0].members[3]);   /* addr 9 -> index 0 */
}

/* A channel far away (gap > max_gap) must start a new group. */
void test_big_gap_splits_group(void)
{
    wb_channel_t chans[5];
    chans[0] = mk_chan(REG_INPUT, 9, 2, true);
    chans[1] = mk_chan(REG_INPUT, 4, 1, true);
    chans[2] = mk_chan(REG_INPUT, 5, 1, true);
    chans[3] = mk_chan(REG_INPUT, 8, 1, true);
    chans[4] = mk_chan(REG_INPUT, 280, 1, true);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 5, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 8);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(4, g[0].start);
    TEST_ASSERT_EQUAL_INT(7, g[0].count);
    TEST_ASSERT_EQUAL_INT(4, g[0].n_members);
    TEST_ASSERT_EQUAL_INT(280, g[1].start);
    TEST_ASSERT_EQUAL_INT(1, g[1].count);
    TEST_ASSERT_EQUAL_INT(1, g[1].n_members);
}

/* Holding and input at adjacent addresses must NOT merge. */
void test_types_not_mixed(void)
{
    wb_channel_t chans[2];
    chans[0] = mk_chan(REG_HOLDING, 4, 1, true);
    chans[1] = mk_chan(REG_INPUT, 5, 1, true);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 2, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 8);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL(REG_HOLDING, g[0].reg_type);
    TEST_ASSERT_EQUAL_INT(1, g[0].n_members);
    TEST_ASSERT_EQUAL(REG_INPUT, g[1].reg_type);
    TEST_ASSERT_EQUAL_INT(1, g[1].n_members);
}

/* Span exceeds max_read_regs even though gaps are small -> split by span. */
void test_max_read_regs_splits_chain(void)
{
    wb_channel_t chans[3];
    chans[0] = mk_chan(REG_INPUT, 0, 1, true);
    chans[1] = mk_chan(REG_INPUT, 10, 1, true);
    chans[2] = mk_chan(REG_INPUT, 25, 1, true);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 3, 20, 16, g, 8);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(0, g[0].start);
    TEST_ASSERT_EQUAL_INT(11, g[0].count);       /* 10+1 - 0 = 11 */
    TEST_ASSERT_EQUAL_INT(2, g[0].n_members);
    TEST_ASSERT_EQUAL_INT(25, g[1].start);
    TEST_ASSERT_EQUAL_INT(1, g[1].count);
    TEST_ASSERT_EQUAL_INT(1, g[1].n_members);
}

/* Coil, discrete and disabled channels are never collected. */
void test_coil_discrete_disabled_ignored(void)
{
    wb_channel_t chans[4];
    chans[0] = mk_chan(REG_COIL, 4, 1, true);
    chans[1] = mk_chan(REG_DISCRETE, 5, 1, true);
    chans[2] = mk_chan(REG_HOLDING, 6, 1, false);   /* disabled */
    chans[3] = mk_chan(REG_HOLDING, 8, 1, true);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 4, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 8);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL(REG_HOLDING, g[0].reg_type);
    TEST_ASSERT_EQUAL_INT(8, g[0].start);
    TEST_ASSERT_EQUAL_INT(1, g[0].n_members);
    TEST_ASSERT_EQUAL_INT(3, g[0].members[0]);
}

/* Two enabled channels on the same address both become members. */
void test_duplicate_address_both_members(void)
{
    wb_channel_t chans[2];
    chans[0] = mk_chan(REG_INPUT, 4, 1, true);
    chans[1] = mk_chan(REG_INPUT, 4, 1, true);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 2, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 8);

    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(4, g[0].start);
    TEST_ASSERT_EQUAL_INT(1, g[0].count);
    TEST_ASSERT_EQUAL_INT(2, g[0].n_members);
    TEST_ASSERT_EQUAL_INT(0, g[0].members[0]);
    TEST_ASSERT_EQUAL_INT(1, g[0].members[1]);
}

/* No eligible channels -> zero groups. */
void test_no_eligible_channels(void)
{
    wb_channel_t chans[2];
    chans[0] = mk_chan(REG_COIL, 4, 1, true);
    chans[1] = mk_chan(REG_HOLDING, 5, 1, false);

    poll_group_t g[8];
    int n = mb_plan_poll_groups(chans, 2, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 8);

    TEST_ASSERT_EQUAL_INT(0, n);
}

/* max_groups cap: planner must stop writing beyond the limit. */
void test_max_groups_cap(void)
{
    wb_channel_t chans[3];
    chans[0] = mk_chan(REG_INPUT, 4, 1, true);
    chans[1] = mk_chan(REG_INPUT, 100, 1, true);   /* gap too big -> new group */
    chans[2] = mk_chan(REG_INPUT, 300, 1, true);   /* gap too big -> new group */

    poll_group_t g[2];
    int n = mb_plan_poll_groups(chans, 3, POLL_MAX_READ_REGS, POLL_MAX_GAP_REGS, g, 2);

    TEST_ASSERT_EQUAL_INT(2, n);                  /* filled the buffer, dropped the 3rd */
    TEST_ASSERT_EQUAL_INT(4, g[0].start);
    TEST_ASSERT_EQUAL_INT(100, g[1].start);
}

/* A single channel whose span exceeds max_read_regs must still yield a group
 * whose count is clamped to max_read_regs — otherwise poll_group's fixed read
 * buffer would overflow. (In production num_regs is also clamped at parse.) */
void test_oversize_single_channel_clamped(void)
{
    /* Two oversize, far-apart channels: the first is emitted by the mid-loop
     * flush (when the second opens a new group), the second by the final
     * flush — so both count-clamp sites are exercised. */
    wb_channel_t chans[2];
    chans[0] = mk_chan(REG_HOLDING, 100,  250, true);   /* span 250 > cap */
    chans[1] = mk_chan(REG_HOLDING, 5000, 250, true);   /* far -> separate group */

    poll_group_t g[4];
    int n = mb_plan_poll_groups(chans, 2, 100 /* max_read_regs */, POLL_MAX_GAP_REGS, g, 4);

    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(1, g[0].n_members);
    TEST_ASSERT_EQUAL_INT(1, g[1].n_members);
    TEST_ASSERT_TRUE(g[0].count <= 100);   /* mid-loop flush clamp */
    TEST_ASSERT_TRUE(g[1].count <= 100);   /* final flush clamp */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_merges_contiguous_input);
    RUN_TEST(test_big_gap_splits_group);
    RUN_TEST(test_types_not_mixed);
    RUN_TEST(test_max_read_regs_splits_chain);
    RUN_TEST(test_coil_discrete_disabled_ignored);
    RUN_TEST(test_duplicate_address_both_members);
    RUN_TEST(test_no_eligible_channels);
    RUN_TEST(test_max_groups_cap);
    RUN_TEST(test_oversize_single_channel_clamped);
    return UNITY_END();
}
