#include "template.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <math.h>  /* NAN */
#include "esp_system.h"  /* esp_get_free_heap_size */
#include "esp_log.h"

static const char *TAG_TMPL = "template";

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
/* Template parameters + channel "condition" evaluation                */
/* ------------------------------------------------------------------ */
/*
 * WB templates gate channels with a "condition" string that references
 * device "parameters" (e.g. "Show_modes_as_range==1"). We evaluate each
 * condition against the template-declared parameter values so that only the
 * matching channel variant is created. This deduplicates same-named
 * conditional pairs (which would otherwise collide on a single MQTT topic)
 * and hides parameter-gated (e.g. debug) channels.
 *
 * Parameter value source, in priority order: the parameter's "value", then
 * "default", else 0. Live per-device register reads and user overrides are
 * out of scope for this bridge.
 *
 * Supported grammar: "<param> <op> <number>", op in ==,!=,>=,<=,>,<.
 * Compound expressions (&&, ||, parentheses), a non-numeric right-hand side,
 * or an unknown parameter FAIL OPEN (channel kept) so real data is never
 * silently dropped.
 */
typedef struct {
    char   id[64];
    double value;
} tmpl_param_t;

typedef struct {
    tmpl_param_t *items;
    int           count;
} tmpl_params_t;

/* Load the device "parameters" section into a flat id->value table.
 * Accepts both template shapes:
 *   array : [ { "id":"X", "default":0, ... }, ... ]
 *   object: { "X": { "default":0, ... }, ... }   (the key is the id)
 */
static void params_load(const cJSON *dev_obj, tmpl_params_t *p)
{
    p->items = NULL;
    p->count = 0;

    cJSON *params = cJSON_GetObjectItemCaseSensitive(dev_obj, "parameters");
    if (!params) return;

    int n = cJSON_GetArraySize(params);
    if (n <= 0) return;

    p->items = calloc((size_t)n, sizeof(tmpl_param_t));
    if (!p->items) return;

    bool is_array = cJSON_IsArray(params);
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, params) {
        if (!cJSON_IsObject(it)) continue;

        /* array form: id is a field; object form: id is the child key */
        const char *id = is_array ? json_str(it, "id", NULL) : it->string;
        if (!id || !*id) continue;

        cJSON *v = cJSON_GetObjectItemCaseSensitive(it, "value");
        if (!v) v = cJSON_GetObjectItemCaseSensitive(it, "default");

        double val = 0.0;
        if (v && cJSON_IsNumber(v))      val = v->valuedouble;
        else if (v && cJSON_IsBool(v))   val = cJSON_IsTrue(v) ? 1.0 : 0.0;

        strncpy(p->items[p->count].id, id, sizeof(p->items[p->count].id) - 1);
        p->items[p->count].id[sizeof(p->items[p->count].id) - 1] = '\0';
        p->items[p->count].value = val;
        p->count++;
    }
}

static void params_free(tmpl_params_t *p)
{
    free(p->items);
    p->items = NULL;
    p->count = 0;
}

/* Look up a parameter by name span [name, name+len). Returns true if found. */
static bool params_lookup(const tmpl_params_t *p, const char *name, size_t len,
                          double *out_val)
{
    for (int i = 0; i < p->count; i++) {
        if (strlen(p->items[i].id) == len &&
            strncmp(p->items[i].id, name, len) == 0) {
            *out_val = p->items[i].value;
            return true;
        }
    }
    return false;
}

/* Trim leading/trailing ASCII spaces and tabs by moving [*s, *e). */
static void trim_span(const char **s, const char **e)
{
    while (*s < *e && (**s == ' ' || **s == '\t')) (*s)++;
    while (*e > *s && ((*e)[-1] == ' ' || (*e)[-1] == '\t')) (*e)--;
}

/* Evaluate a channel "condition". Returns true if the channel must be KEPT.
 * Empty/absent condition -> keep. Unsupported/unknown -> fail open (keep). */
static bool eval_condition(const char *cond, const tmpl_params_t *params)
{
    if (!cond || !*cond) return true;

    /* Compound / unsupported expressions -> fail open. */
    if (strstr(cond, "&&") || strstr(cond, "||") ||
        strchr(cond, '(')  || strchr(cond, ')')) {
        return true;
    }

    /* Find the operator: two-char ops first, then single-char. */
    static const char *ops2[] = { "==", "!=", ">=", "<=" };
    const char *op_pos = NULL;
    int  op_len = 0;
    char op0 = 0, op1 = 0;
    for (size_t i = 0; i < sizeof(ops2) / sizeof(ops2[0]); i++) {
        const char *q = strstr(cond, ops2[i]);
        if (q) { op_pos = q; op_len = 2; op0 = ops2[i][0]; op1 = ops2[i][1]; break; }
    }
    if (!op_pos) {
        const char *q;
        if ((q = strchr(cond, '>')) || (q = strchr(cond, '<')) || (q = strchr(cond, '='))) {
            op_pos = q; op_len = 1; op0 = *q; op1 = 0;
        }
    }
    if (!op_pos) return true;  /* not a comparison we understand -> keep */

    /* Left operand = parameter id (trimmed span). */
    const char *ls = cond, *le = op_pos;
    trim_span(&ls, &le);
    if (le <= ls) return true;

    /* Right operand = number (trimmed, copied to a small buffer). */
    const char *rs = op_pos + op_len, *re = cond + strlen(cond);
    trim_span(&rs, &re);
    if (re <= rs) return true;
    size_t nlen = (size_t)(re - rs);
    char numbuf[32];
    if (nlen >= sizeof(numbuf)) return true;
    memcpy(numbuf, rs, nlen);
    numbuf[nlen] = '\0';
    char *endp;
    double rhs = strtod(numbuf, &endp);
    /* Require the whole (already-trimmed) right side to be numeric; a partial
     * parse like "1.5.6" or "1abc" -> fail open per the grammar contract. */
    if (endp == numbuf || *endp != '\0') return true;

    double lhs;
    if (!params_lookup(params, ls, (size_t)(le - ls), &lhs)) {
        return true;  /* unknown parameter -> fail open */
    }

    if (op_len == 2) {
        if (op0 == '=' && op1 == '=') return lhs == rhs;
        if (op0 == '!' && op1 == '=') return lhs != rhs;
        if (op0 == '>' && op1 == '=') return lhs >= rhs;
        if (op0 == '<' && op1 == '=') return lhs <= rhs;
    } else {
        if (op0 == '>') return lhs >  rhs;
        if (op0 == '<') return lhs <  rhs;
        if (op0 == '=') return lhs == rhs;  /* tolerate a lone '=' as equality */
    }
    return true;
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

    /* Load parameters once so channel conditions can be evaluated. */
    tmpl_params_t params;
    params_load(dev_obj, &params);

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

        /* Skip channels whose "condition" is not satisfied by the template's
         * parameter values. Deduplicates conditional same-named pairs and
         * hides parameter-gated channels. */
        const char *cond = json_str(ch, "condition", NULL);
        if (!eval_condition(cond, &params)) continue;

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
        /* Copy channel name into fixed-size array; truncate if needed */
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
        /* Parse WB type for Home Assistant device class mapping */
        const char *type_str = json_str(ch, "type", NULL);
        if (type_str) {
            strncpy(c->type, type_str, sizeof(c->type) - 1);
            c->type[sizeof(c->type) - 1] = '\0';
        } else {
            c->type[0] = '\0';
        }
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

    params_free(&params);
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
        /* Distinguish JSON syntax error from OOM */
        const char *err = cJSON_GetErrorPtr();
        uint32_t free_heap = esp_get_free_heap_size();
        if (err) {
            ESP_LOGE(TAG_TMPL, "JSON parse error near: '%.32s' (free heap: %"PRIu32" bytes)", err, free_heap);
        } else {
            ESP_LOGE(TAG_TMPL, "JSON parse failed — likely out of memory (free heap: %"PRIu32" bytes)", free_heap);
        }
        return -1;
    }

    return parse_json_root(root, out);
}

int wb_template_parse_str(const char *json, wb_template_t *out)
{
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        /* Distinguish JSON syntax error from OOM */
        const char *err = cJSON_GetErrorPtr();
        uint32_t free_heap = esp_get_free_heap_size();
        if (err) {
            ESP_LOGE(TAG_TMPL, "JSON parse error near: '%.32s' (free heap: %"PRIu32" bytes)", err, free_heap);
        } else {
            ESP_LOGE(TAG_TMPL, "JSON parse failed — likely out of memory (free heap: %"PRIu32" bytes)", free_heap);
        }
        return -1;
    }

    return parse_json_root(root, out);
}

void wb_template_free(wb_template_t *t)
{
    if (!t) return;
    /* name and type are fixed-size arrays — no individual free needed */
    free(t->channels);
    memset(t, 0, sizeof(*t));
}
