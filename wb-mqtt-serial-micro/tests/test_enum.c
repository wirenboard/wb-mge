/*
 * Unit tests for enum value<->label mapping in template.c.
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

/* "Mode" has 5 aligned enum/title pairs; "Plain" has no enum; "Mismatch" has
 * 3 values but only 2 titles -> only the first 2 pairs are stored; "Neg" has
 * negative (s16) enum values; "BadItem" has a non-numeric enum element at
 * index 1 that must be skipped (with its title), keeping positional alignment. */
static const char *TMPL_JSON =
"{\"device\":{\"name\":\"ENUM-TEST\",\"id\":\"enum-test\",\"channels\":["
"  {\"name\":\"Mode\",\"reg_type\":\"holding\",\"address\":1,\"enum\":[1,2,3,4,5],"
"     \"enum_titles\":[\"Heat\",\"Cool\",\"Auto\",\"Dry\",\"Fan\"]},"
"  {\"name\":\"Plain\",\"reg_type\":\"input\",\"address\":4},"
"  {\"name\":\"Mismatch\",\"reg_type\":\"holding\",\"address\":2,\"enum\":[0,1,2],"
"     \"enum_titles\":[\"Off\",\"On\"]},"
"  {\"name\":\"Neg\",\"reg_type\":\"holding\",\"address\":3,\"format\":\"s16\","
"     \"enum\":[-1,0,1],\"enum_titles\":[\"Rev\",\"Zero\",\"Fwd\"]},"
"  {\"name\":\"BadItem\",\"reg_type\":\"holding\",\"address\":5,\"enum\":[1,\"x\",3],"
"     \"enum_titles\":[\"A\",\"B\",\"C\"]}"
"]}}";

static int write_temp(const char *json, char *path_out, size_t path_sz)
{
    snprintf(path_out, path_sz, "/tmp/wb_enum_test_%d.json", (int)getpid());
    FILE *f = fopen(path_out, "w");
    if (!f) return -1;
    fputs(json, f);
    fclose(f);
    return 0;
}

void test_enum_parsed(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *m = find_channel(&t, "Mode");
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(5, m->enum_count);
    wb_template_free(&t);
    remove(path);
}

void test_enum_title_lookup(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *m = find_channel(&t, "Mode");
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_STRING("Cool", wb_channel_enum_title(m, 2));
    TEST_ASSERT_EQUAL_STRING("Fan",  wb_channel_enum_title(m, 5));
    TEST_ASSERT_NULL(wb_channel_enum_title(m, 99));  /* value not listed */
    wb_template_free(&t);
    remove(path);
}

void test_enum_value_lookup(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *m = find_channel(&t, "Mode");
    TEST_ASSERT_NOT_NULL(m);
    long v = 0;
    TEST_ASSERT_TRUE(wb_channel_enum_value(m, "Auto", &v));
    TEST_ASSERT_EQUAL_INT(3, v);
    TEST_ASSERT_FALSE(wb_channel_enum_value(m, "Nope", &v));  /* label not listed */
    wb_template_free(&t);
    remove(path);
}

void test_channel_without_enum(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *p = find_channel(&t, "Plain");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_INT(0, p->enum_count);
    TEST_ASSERT_NULL(wb_channel_enum_title(p, 0));
    long v;
    TEST_ASSERT_FALSE(wb_channel_enum_value(p, "x", &v));
    wb_template_free(&t);
    remove(path);
}

void test_enum_mismatched_lengths(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *m = find_channel(&t, "Mismatch");
    TEST_ASSERT_NOT_NULL(m);
    /* 3 values, 2 titles -> only the first 2 pairs stored */
    TEST_ASSERT_EQUAL_INT(2, m->enum_count);
    TEST_ASSERT_EQUAL_STRING("On", wb_channel_enum_title(m, 1));
    TEST_ASSERT_NULL(wb_channel_enum_title(m, 2));  /* value 2 has no title */
    wb_template_free(&t);
    remove(path);
}

void test_enum_negative_values(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *n = find_channel(&t, "Neg");
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(3, n->enum_count);
    TEST_ASSERT_EQUAL_STRING("Rev", wb_channel_enum_title(n, -1));  /* negative key */
    long v = 0;
    TEST_ASSERT_TRUE(wb_channel_enum_value(n, "Rev", &v));
    TEST_ASSERT_EQUAL_INT(-1, v);
    wb_template_free(&t);
    remove(path);
}

void test_enum_skips_invalid_item(void)
{
    char path[128];
    TEST_ASSERT_EQUAL_INT(0, write_temp(TMPL_JSON, path, sizeof(path)));
    wb_template_t t;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse(path, &t));
    const wb_channel_t *b = find_channel(&t, "BadItem");
    TEST_ASSERT_NOT_NULL(b);
    /* enum[1] is the string "x" (non-number) -> that pair is dropped, so only
     * values 1->"A" and 3->"C" remain; value 2 has no label. */
    TEST_ASSERT_EQUAL_INT(2, b->enum_count);
    TEST_ASSERT_EQUAL_STRING("A", wb_channel_enum_title(b, 1));
    TEST_ASSERT_EQUAL_STRING("C", wb_channel_enum_title(b, 3));
    TEST_ASSERT_NULL(wb_channel_enum_title(b, 2));
    wb_template_free(&t);
    remove(path);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_enum_parsed);
    RUN_TEST(test_enum_title_lookup);
    RUN_TEST(test_enum_value_lookup);
    RUN_TEST(test_channel_without_enum);
    RUN_TEST(test_enum_mismatched_lengths);
    RUN_TEST(test_enum_negative_values);
    RUN_TEST(test_enum_skips_invalid_item);
    return UNITY_END();
}
