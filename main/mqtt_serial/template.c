#include "template.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>  /* NAN */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static const char *json_str(const cJSON *obj, const char *key, const char *def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v && cJSON_IsString(v)) return v->valuestring;
    return def;
}

static double json_num(const cJSON *obj, const char *key, double def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (v && cJSON_IsNumber(v)) return v->valuedouble;
    return def;
}

static bool json_bool(const cJSON *obj, const char *key, bool def)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!v) return def;
    if (cJSON_IsTrue(v))  return true;
    if (cJSON_IsBool(v))  return false;  /* explicit false */
    if (cJSON_IsNumber(v)) return v->valuedouble != 0.0;
    return def;
}

/* ------------------------------------------------------------------ */
/* reg_type_t from string                                              */
/* ------------------------------------------------------------------ */
static reg_type_t parse_reg_type(const char *s)
{
    if (!s) return REG_HOLDING;
    if (strcmp(s, "input")    == 0) return REG_INPUT;
    if (strcmp(s, "coil")     == 0) return REG_COIL;
    if (strcmp(s, "discrete") == 0) return REG_DISCRETE;
    return REG_HOLDING;  /* default & "holding" */
}

/* ------------------------------------------------------------------ */
/* reg_format_t from string                                            */
/* ------------------------------------------------------------------ */
static reg_format_t parse_format(const char *s)
{
    if (!s || strcmp(s, "u16") == 0) return FMT_U16;
    if (strcmp(s, "u8")     == 0) return FMT_U8;
    if (strcmp(s, "s8")     == 0) return FMT_S8;
    if (strcmp(s, "s16")    == 0) return FMT_S16;
    if (strcmp(s, "u32")    == 0) return FMT_U32;
    if (strcmp(s, "s32")    == 0) return FMT_S32;
    if (strcmp(s, "u64")    == 0) return FMT_U64;
    if (strcmp(s, "s64")    == 0) return FMT_S64;
    if (strcmp(s, "float")  == 0) return FMT_FLOAT;
    if (strcmp(s, "string") == 0) return FMT_STRING;
    if (strcmp(s, "string8")== 0) return FMT_STRING;
    if (strcmp(s, "bcd8")   == 0) return FMT_BCD8;
    if (strcmp(s, "bcd16")  == 0) return FMT_BCD16;
    if (strcmp(s, "bcd24")  == 0) return FMT_BCD24;
    if (strcmp(s, "bcd32")  == 0) return FMT_BCD32;
    return FMT_U16;
}

static ch_word_order_t parse_word_order(const char *s)
{
    if (s && strcmp(s, "little_endian") == 0) return CH_WORD_ORDER_LITTLE;
    return CH_WORD_ORDER_BIG;
}

static ch_byte_order_t parse_byte_order(const char *s)
{
    if (s && strcmp(s, "little_endian") == 0) return CH_BYTE_ORDER_LITTLE;
    return CH_BYTE_ORDER_BIG;
}

/* How many 16-bit Modbus words does this format occupy? */
static uint32_t format_num_regs(reg_type_t rtype, reg_format_t fmt,
                                 uint32_t string_data_size)
{
    /* Coil/discrete are 1-bit; we use 1 "slot" but the read is different. */
    if (rtype == REG_COIL || rtype == REG_DISCRETE) return 1;
    switch (fmt) {
        case FMT_U8:  case FMT_S8:  case FMT_U16: case FMT_S16: return 1;
        case FMT_BCD8:  case FMT_BCD16:                          return 1;
        case FMT_U32: case FMT_S32: case FMT_FLOAT:              return 2;
        case FMT_BCD24: case FMT_BCD32:                          return 2;
        case FMT_U64: case FMT_S64:                              return 4;
        case FMT_STRING:
            /* string_data_size bytes, 2 bytes per register, round up */
            return string_data_size > 0 ? (string_data_size + 1) / 2 : 1;
        default: return 1;
    }
}

/* ------------------------------------------------------------------ */
/* File reading helper                                                 */
/* ------------------------------------------------------------------ */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* Parse "channels" array from the device object                      */
/* ------------------------------------------------------------------ */
static int parse_channels(const cJSON *dev_obj, wb_template_t *out)
{
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(dev_obj, "channels");
    if (!arr || !cJSON_IsArray(arr)) {
        fprintf(stderr, "template: no 'channels' array\n");
        return -1;
    }

    int total = cJSON_GetArraySize(arr);
    if (total <= 0) {
        out->channels = NULL;
        out->num_channels = 0;
        return 0;
    }

    out->channels = calloc((size_t)total, sizeof(wb_channel_t));
    if (!out->channels) return -1;

    int count = 0;
    for (int i = 0; i < total; i++) {
        cJSON *ch = cJSON_GetArrayItem(arr, i);
        if (!ch || !cJSON_IsObject(ch)) continue;

        /* Skip channels that are disabled by default
         * ("enabled": false in the template).  They won't be polled. */
        bool enabled = json_bool(ch, "enabled", true);

        /* Skip channels that have "consists_of" (RGB composite) u2013
         * we don't support those in the PoC. */
        if (cJSON_GetObjectItemCaseSensitive(ch, "consists_of")) continue;

        const char *name = json_str(ch, "name", NULL);
        if (!name) continue;

        const char *rt_str  = json_str(ch, "reg_type", "holding");
        const char *fmt_str = json_str(ch, "format",   NULL);

        cJSON *addr_item = cJSON_GetObjectItemCaseSensitive(ch, "address");
        if (!addr_item) continue;

        uint32_t address;
        if (cJSON_IsNumber(addr_item)) {
            address = (uint32_t)(long)addr_item->valuedouble;
        } else if (cJSON_IsString(addr_item)) {
            char *end;
            address = (uint32_t)strtoul(addr_item->valuestring, &end, 0);
            if (end == addr_item->valuestring) continue; /* not a valid number */
        } else {
            continue;
        }

        wb_channel_t *c = &out->channels[count];
        c->name     = strdup(name);
        c->reg_type = parse_reg_type(rt_str);
        c->format   = parse_format(fmt_str);
        c->address  = address;
        c->scale    = json_num(ch, "scale",  1.0);
        c->offset   = json_num(ch, "offset", 0.0);
        c->enabled  = enabled;
        c->string_data_size = (uint32_t)(int)json_num(ch, "string_data_size", 0.0);
        c->num_regs = format_num_regs(c->reg_type, c->format, c->string_data_size);
        c->word_order = parse_word_order(json_str(ch, "word_order", NULL));
        c->byte_order = parse_byte_order(json_str(ch, "byte_order", NULL));

        /* error_value: parse string (hex like "0x7FFF" or decimal) or number */
        {
            cJSON *ev = cJSON_GetObjectItemCaseSensitive(ch, "error_value");
            if (!ev) {
                c->error_value = (double)NAN;
            } else if (cJSON_IsNumber(ev)) {
                c->error_value = ev->valuedouble;
            } else if (cJSON_IsString(ev)) {
                /* Parse hex ("0x7FFF") or decimal string */
                char *end;
                long long v = strtoll(ev->valuestring, &end, 0);
                c->error_value = (*end == '\0') ? (double)v : (double)NAN;
            } else {
                c->error_value = (double)NAN;
            }
        }

        /* read-only: explicit flag OR register type is inherently read-only */
        bool explicit_ro = json_bool(ch, "readonly", false);
        c->readonly = explicit_ro
                      || c->reg_type == REG_INPUT
                      || c->reg_type == REG_DISCRETE;

        count++;
    }

    out->num_channels = count;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
static int parse_json_root(cJSON *root, wb_template_t *out)
{
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (!dev) {
        fprintf(stderr, "template: no 'device' object\n");
        cJSON_Delete(root);
        return -1;
    }

    const char *dname = json_str(dev, "name", "unknown");
    const char *did   = json_str(dev, "id",   "unknown");
    strncpy(out->device_name, dname, sizeof(out->device_name) - 1);
    strncpy(out->device_id,   did,   sizeof(out->device_id) - 1);

    int rc = parse_channels(dev, out);
    cJSON_Delete(root);
    return rc;
}

int wb_template_parse(const char *path, wb_template_t *out)
{
    memset(out, 0, sizeof(*out));

    char *text = read_file(path);
    if (!text) {
        fprintf(stderr, "template: cannot read '%s'\n", path);
        return -1;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (!root) {
        fprintf(stderr, "template: JSON parse error\n");
        return -1;
    }

    return parse_json_root(root, out);
}

int wb_template_parse_str(const char *json, wb_template_t *out)
{
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "template: JSON parse error\n");
        return -1;
    }

    return parse_json_root(root, out);
}

void wb_template_free(wb_template_t *t)
{
    if (!t) return;
    for (int i = 0; i < t->num_channels; i++) {
        free(t->channels[i].name);
    }
    free(t->channels);
    memset(t, 0, sizeof(*t));
}
