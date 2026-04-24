/*
 * Unit tests for template.c - JSON template parsing.
 * Tests work with real .json files from ../templates/.
 */
#define _POSIX_C_SOURCE 200809L

#include "unity.h"
#include "template.h"

#include <string.h>

/* Template directory: injected by Makefile via -DTMPL_DIR="..." */
#ifndef TMPL_DIR
#define TMPL_DIR "../../templates"
#endif

#define TMPL(name) TMPL_DIR "/" name

void setUp(void)   {}
void tearDown(void) {}

/*
 * Helper: find a channel by name in parsed template.
 * Returns NULL if not found.
 */
static const wb_channel_t *find_channel(const wb_template_t *t, const char *name)
{
    for (int i = 0; i < t->num_channels; i++) {
        if (strcmp(t->channels[i].name, name) == 0)
            return &t->channels[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* WB-MRPS6 template: simple relay module                              */
/* ------------------------------------------------------------------ */

void test_mrps6_device_name(void)
{
    wb_template_t t;
    int rc = wb_template_parse(TMPL("config-wb-mrps6.json"), &t);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("WB-MRPS6", t.device_name);
    TEST_ASSERT_EQUAL_STRING("wb-mrps6", t.device_id);
    wb_template_free(&t);
}

void test_mrps6_coil_K1(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-mrps6.json"), &t);
    const wb_channel_t *ch = find_channel(&t, "K1");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(REG_COIL, ch->reg_type);
    TEST_ASSERT_EQUAL_UINT32(0, ch->address);
    TEST_ASSERT_EQUAL_UINT32(1, ch->num_regs);
    TEST_ASSERT_FALSE(ch->readonly);
    TEST_ASSERT_TRUE(ch->enabled);
    wb_template_free(&t);
}

void test_mrps6_supply_voltage(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-mrps6.json"), &t);
    const wb_channel_t *ch = find_channel(&t, "Supply voltage");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(REG_INPUT, ch->reg_type);
    TEST_ASSERT_EQUAL_UINT32(121, ch->address);
    TEST_ASSERT_EQUAL_UINT32(1, ch->num_regs);
    /* scale = 0.001 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.001f, (float)ch->scale);
    TEST_ASSERT_TRUE(ch->readonly);
    wb_template_free(&t);
}

void test_mrps6_serial_no_u32(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-mrps6.json"), &t);
    const wb_channel_t *ch = find_channel(&t, "Serial NO");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(REG_INPUT, ch->reg_type);
    TEST_ASSERT_EQUAL_UINT32(270, ch->address);
    TEST_ASSERT_EQUAL_INT(FMT_U32, ch->format);
    TEST_ASSERT_EQUAL_UINT32(2, ch->num_regs);  /* u32 = 2 words */
    wb_template_free(&t);
}

void test_mrps6_uptime_disabled(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-mrps6.json"), &t);
    const wb_channel_t *ch = find_channel(&t, "Uptime");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_FALSE(ch->enabled);  /* enabled: false in template */
    wb_template_free(&t);
}

/* ------------------------------------------------------------------ */
/* WB-MS-THLS: sensor template                                         */
/* ------------------------------------------------------------------ */

void test_thls_temperature_s16(void)
{
    wb_template_t t;
    int rc = wb_template_parse(TMPL("config-wb-ms-thls.json"), &t);
    TEST_ASSERT_EQUAL_INT(0, rc);
    const wb_channel_t *ch = find_channel(&t, "Temperature");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(REG_INPUT, ch->reg_type);
    TEST_ASSERT_EQUAL_INT(FMT_S16, ch->format);
    TEST_ASSERT_EQUAL_UINT32(0, ch->address);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, (float)ch->scale);
    TEST_ASSERT_TRUE(ch->readonly);
    wb_template_free(&t);
}

void test_thls_humidity(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-ms-thls.json"), &t);
    const wb_channel_t *ch = find_channel(&t, "Humidity");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(REG_INPUT, ch->reg_type);
    TEST_ASSERT_EQUAL_INT(FMT_U16, ch->format);
    TEST_ASSERT_EQUAL_UINT32(1, ch->address);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, (float)ch->scale);
    wb_template_free(&t);
}

/* ------------------------------------------------------------------ */
/* WB-MRGBW-D: string format channel                                   */
/* ------------------------------------------------------------------ */

void test_mrgbw_fw_version_string(void)
{
    wb_template_t t;
    int rc = wb_template_parse(TMPL("config-wb-mrgbw-d.json"), &t);
    TEST_ASSERT_EQUAL_INT(0, rc);
    const wb_channel_t *ch = find_channel(&t, "FW Version");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(FMT_STRING, ch->format);
    TEST_ASSERT_EQUAL_UINT32(250, ch->address);
    /* string_data_size=16 bytes -> 8 registers */
    TEST_ASSERT_EQUAL_UINT32(8, ch->num_regs);
    TEST_ASSERT_FALSE(ch->enabled);  /* disabled in template */
    TEST_ASSERT_TRUE(ch->readonly);
    wb_template_free(&t);
}

void test_mrgbw_rgb_channel_skipped(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-mrgbw-d.json"), &t);
    /* "RGB" channel has consists_of -> must be skipped */
    const wb_channel_t *ch = find_channel(&t, "RGB");
    TEST_ASSERT_NULL(ch);
    wb_template_free(&t);
}

void test_mrgbw_white_holding(void)
{
    wb_template_t t;
    wb_template_parse(TMPL("config-wb-mrgbw-d.json"), &t);
    const wb_channel_t *ch = find_channel(&t, "White");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_INT(REG_HOLDING, ch->reg_type);
    TEST_ASSERT_EQUAL_UINT32(3, ch->address);
    TEST_ASSERT_FALSE(ch->readonly);
    wb_template_free(&t);
}

/* ------------------------------------------------------------------ */
/* Error handling                                                       */
/* ------------------------------------------------------------------ */

void test_parse_nonexistent_file(void)
{
    wb_template_t t;
    int rc = wb_template_parse("/nonexistent/path/template.json", &t);
    TEST_ASSERT_NOT_EQUAL(0, rc);
}

void test_parse_invalid_json(void)
{
    /* Use a file that exists but is not valid JSON (this binary itself) */
    wb_template_t t;
    /* /dev/null is valid but empty JSON parse will fail */
    int rc = wb_template_parse("/dev/null", &t);
    TEST_ASSERT_NOT_EQUAL(0, rc);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_mrps6_device_name);
    RUN_TEST(test_mrps6_coil_K1);
    RUN_TEST(test_mrps6_supply_voltage);
    RUN_TEST(test_mrps6_serial_no_u32);
    RUN_TEST(test_mrps6_uptime_disabled);

    RUN_TEST(test_thls_temperature_s16);
    RUN_TEST(test_thls_humidity);

    RUN_TEST(test_mrgbw_fw_version_string);
    RUN_TEST(test_mrgbw_rgb_channel_skipped);
    RUN_TEST(test_mrgbw_white_holding);

    RUN_TEST(test_parse_nonexistent_file);
    RUN_TEST(test_parse_invalid_json);

    return UNITY_END();
}
