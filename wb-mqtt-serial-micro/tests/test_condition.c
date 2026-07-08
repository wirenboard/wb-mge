/*
 * Unit tests for channel "condition" evaluation in template.c.
 * Self-contained: writes an inline JSON template to a temp file and parses it.
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "template.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void)    {}
void tearDown(void) {}

static const wb_channel_t *find_channel(const wb_template_t *t, const char *name)
{
    for (int i = 0; i < t->num_channels; i++) {
        if (strcmp(t->channels[i].name, name) == 0) return &t->channels[i];
    }
    return NULL;
}

/* Count how many channels carry a given name (to prove dedup of pairs). */
static int count_channels(const wb_template_t *t, const char *name)
{
    int n = 0;
    for (int i = 0; i < t->num_channels; i++) {
        if (strcmp(t->channels[i].name, name) == 0) n++;
    }
    return n;
}

/* A template exercising: array-form parameters with defaults, a same-named
 * conditional pair with DISTINCT addresses (must collapse to exactly the
 * matching variant), a parameter-gated debug channel (default off -> dropped),
 * an unconditional channel (kept), and a channel whose condition references an
 * unknown parameter (fail open -> kept).
 *
 * With Show_modes_as_range default 0, only the "!=1" Mode variant (address 2)
 * survives. Expected surviving channels: Mode(addr 2), Temperature, Ghost = 3.
 * Dropped: Mode(addr 1, ==1), DebugByte (Show_debug==1). */
static const char *TMPL_JSON =
"{\"device\":{\"name\":\"COND-TEST\",\"id\":\"cond-test\","
" \"parameters\":["
"   {\"id\":\"Show_modes_as_range\",\"default\":0},"
"   {\"id\":\"Show_debug\",\"default\":0}"
" ],"
" \"channels\":["
"   {\"name\":\"Mode\",\"reg_type\":\"holding\",\"address\":1,\"condition\":\"Show_modes_as_range==1\"},"
"   {\"name\":\"Mode\",\"reg_type\":\"holding\",\"address\":2,\"condition\":\"Show_modes_as_range!=1\"},"
"   {\"name\":\"DebugByte\",\"reg_type\":\"holding\",\"address\":9,\"condition\":\"Show_debug==1\"},"
"   {\"name\":\"Temperature\",\"reg_type\":\"input\",\"address\":4},"
"   {\"name\":\"Ghost\",\"reg_type\":\"input\",\"address\":7,\"condition\":\"No_Such_Param==1\"}"
" ]}}";

static int write_temp(const char *json, char *path_out, size_t path_sz)
{
    snprintf(path_out, path_sz, "/tmp/wb_cond_test_%d.json", (int)getpid());
    FILE *f = fopen(path_out, "w");
    if (!f) return -1;
    fputs(json, f);
    fclose(f);
    return 0;
}

void test_conditional_pair_collapses_to_matching_variant(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    /* Exactly one "Mode" survives, and it is the "!=1" variant (address 2). */
    TEST_ASSERT_EQUAL_INT(1, count_channels(&t, "Mode"));
    const wb_channel_t *mode = find_channel(&t, "Mode");
    TEST_ASSERT_NOT_NULL(mode);
    TEST_ASSERT_EQUAL_UINT32(2, mode->address);
    wb_template_free(&t);
    remove(path);
}

void test_total_channel_count_after_eval(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    /* Mode(!=1) + Temperature + Ghost = 3; Mode(==1) and DebugByte dropped. */
    TEST_ASSERT_EQUAL_INT(3, t.num_channels);
    wb_template_free(&t);
    remove(path);
}

void test_param_gated_channel_dropped(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    /* Show_debug default 0, condition "==1" => channel dropped. */
    TEST_ASSERT_NULL(find_channel(&t, "DebugByte"));
    wb_template_free(&t);
    remove(path);
}

void test_unconditional_channel_kept(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    TEST_ASSERT_NOT_NULL(find_channel(&t, "Temperature"));
    wb_template_free(&t);
    remove(path);
}

void test_unknown_param_fails_open(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    /* Unknown parameter => condition can't be evaluated => channel kept. */
    TEST_ASSERT_NOT_NULL(find_channel(&t, "Ghost"));
    wb_template_free(&t);
    remove(path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_conditional_pair_collapses_to_matching_variant);
    RUN_TEST(test_total_channel_count_after_eval);
    RUN_TEST(test_param_gated_channel_dropped);
    RUN_TEST(test_unconditional_channel_kept);
    RUN_TEST(test_unknown_param_fails_open);
    return UNITY_END();
}
