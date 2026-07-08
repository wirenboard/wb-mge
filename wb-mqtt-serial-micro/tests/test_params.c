/*
 * Unit tests for wb_template_extract_params and wb_template_parse_str_ex
 * (condition override by live device parameter values).
 * Self-contained: feeds inline JSON strings to the parser API.
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "template.h"

#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

static const wb_channel_t *find_channel(const wb_template_t *t, const char *name)
{
    for (int i = 0; i < t->num_channels; i++) {
        if (strcmp(t->channels[i].name, name) == 0) return &t->channels[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* wb_template_extract_params                                          */
/* ------------------------------------------------------------------ */

static const char *TMPL_ARRAY_PARAMS =
"{\"device\":{\"name\":\"X\",\"id\":\"x\","
" \"parameters\":["
"   {\"id\":\"P1\",\"reg_type\":\"coil\",\"address\":100,\"default\":1},"
"   {\"id\":\"P2\",\"reg_type\":\"holding\",\"address\":5,\"default\":7}"
" ]}}";

void test_extract_array_form(void)
{
    wb_param_t out[4];
    int n = wb_template_extract_params(TMPL_ARRAY_PARAMS, out, 4);
    TEST_ASSERT_EQUAL_INT(2, n);

    TEST_ASSERT_EQUAL_STRING("P1", out[0].id);
    TEST_ASSERT_EQUAL_INT(REG_COIL, out[0].reg_type);
    TEST_ASSERT_EQUAL_UINT16(100, out[0].address);
    TEST_ASSERT_EQUAL_FLOAT(1.0, out[0].value);

    TEST_ASSERT_EQUAL_STRING("P2", out[1].id);
    TEST_ASSERT_EQUAL_INT(REG_HOLDING, out[1].reg_type);
    TEST_ASSERT_EQUAL_UINT16(5, out[1].address);
    TEST_ASSERT_EQUAL_FLOAT(7.0, out[1].value);
}

void test_extract_object_form(void)
{
    const char *json =
    "{\"device\":{\"name\":\"X\",\"id\":\"x\","
    " \"parameters\":{"
    "   \"P1\":{\"reg_type\":\"coil\",\"address\":100,\"default\":0}"
    " }}}";

    wb_param_t out[4];
    int n = wb_template_extract_params(json, out, 4);
    TEST_ASSERT_EQUAL_INT(1, n);

    TEST_ASSERT_EQUAL_STRING("P1", out[0].id);
    TEST_ASSERT_EQUAL_INT(REG_COIL, out[0].reg_type);
    TEST_ASSERT_EQUAL_UINT16(100, out[0].address);
    TEST_ASSERT_EQUAL_FLOAT(0.0, out[0].value);
}

void test_extract_default_none_is_zero(void)
{
    const char *json =
    "{\"device\":{\"name\":\"X\",\"id\":\"x\","
    " \"parameters\":["
    "   {\"id\":\"NoDefault\",\"reg_type\":\"holding\",\"address\":3}"
    " ]}}";

    wb_param_t out[4];
    int n = wb_template_extract_params(json, out, 4);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_FLOAT(0.0, out[0].value);
}

void test_extract_truncation(void)
{
    const char *json =
    "{\"device\":{\"name\":\"X\",\"id\":\"x\","
    " \"parameters\":["
    "   {\"id\":\"A\",\"address\":1},"
    "   {\"id\":\"B\",\"address\":2},"
    "   {\"id\":\"C\",\"address\":3},"
    "   {\"id\":\"D\",\"address\":4}"
    " ]}}";

    wb_param_t out[2];
    int n = wb_template_extract_params(json, out, 2);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("A", out[0].id);
    TEST_ASSERT_EQUAL_STRING("B", out[1].id);
}

/* ------------------------------------------------------------------ */
/* wb_template_parse_str_ex: override wins                             */
/* ------------------------------------------------------------------ */

/* A channel gated by Show_x==1; the JSON parameter Show_x has default 0,
 * so the default parse drops the channel. Overriding Show_x to 1 must keep
 * it; overriding to 0 must drop it. */
static const char *TMPL_OVERRIDE =
"{\"device\":{\"name\":\"OVR\",\"id\":\"ovr\","
" \"parameters\":["
"   {\"id\":\"Show_x\",\"default\":0}"
" ],"
" \"channels\":["
"   {\"name\":\"Gated\",\"reg_type\":\"holding\",\"address\":42,\"condition\":\"Show_x==1\"}"
" ]}}";

void test_parse_ex_override_wins(void)
{
    /* Override Show_x=1 -> channel present. */
    {
        wb_param_t p;
        strcpy(p.id, "Show_x");
        p.reg_type = REG_HOLDING;
        p.address = 0;
        p.value = 1.0;

        wb_template_t t;
        TEST_ASSERT_EQUAL_INT(0, wb_template_parse_str_ex(TMPL_OVERRIDE, &p, 1, &t));
        TEST_ASSERT_NOT_NULL(find_channel(&t, "Gated"));
        wb_template_free(&t);
    }

    /* Override Show_x=0 -> channel absent (matches the JSON default). */
    {
        wb_param_t p;
        strcpy(p.id, "Show_x");
        p.reg_type = REG_HOLDING;
        p.address = 0;
        p.value = 0.0;

        wb_template_t t;
        TEST_ASSERT_EQUAL_INT(0, wb_template_parse_str_ex(TMPL_OVERRIDE, &p, 1, &t));
        TEST_ASSERT_NULL(find_channel(&t, "Gated"));
        wb_template_free(&t);
    }
}

/* ------------------------------------------------------------------ */
/* wb_template_parse_str_ex: NULL fallback matches wb_template_parse_str */
/* ------------------------------------------------------------------ */

static const char *TMPL_FALLBACK =
"{\"device\":{\"name\":\"FB\",\"id\":\"fb\","
" \"parameters\":["
"   {\"id\":\"Show_modes_as_range\",\"default\":0}"
" ],"
" \"channels\":["
"   {\"name\":\"Mode\",\"reg_type\":\"holding\",\"address\":1,\"condition\":\"Show_modes_as_range==1\"},"
"   {\"name\":\"Mode\",\"reg_type\":\"holding\",\"address\":2,\"condition\":\"Show_modes_as_range!=1\"},"
"   {\"name\":\"Temperature\",\"reg_type\":\"input\",\"address\":4}"
" ]}}";

void test_parse_ex_null_fallback(void)
{
    wb_template_t t_str;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse_str(TMPL_FALLBACK, &t_str));

    wb_template_t t_ex;
    TEST_ASSERT_EQUAL_INT(0, wb_template_parse_str_ex(TMPL_FALLBACK, NULL, 0, &t_ex));

    /* Same channel count. */
    TEST_ASSERT_EQUAL_INT(t_str.num_channels, t_ex.num_channels);

    /* Same surviving channel addresses (order preserved by the parser). */
    for (int i = 0; i < t_str.num_channels; i++) {
        TEST_ASSERT_EQUAL_UINT32(t_str.channels[i].address, t_ex.channels[i].address);
    }

    wb_template_free(&t_str);
    wb_template_free(&t_ex);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_extract_array_form);
    RUN_TEST(test_extract_object_form);
    RUN_TEST(test_extract_default_none_is_zero);
    RUN_TEST(test_extract_truncation);
    RUN_TEST(test_parse_ex_override_wins);
    RUN_TEST(test_parse_ex_null_fallback);
    return UNITY_END();
}
