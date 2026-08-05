#pragma once

/* Minimal cJSON stub for compilation — sniffer WS handler uses cJSON,
 * but the 4 helper functions under test do not. */
typedef struct cJSON {
    double valuedouble;
    char *valuestring;
    struct cJSON *next;
    struct cJSON *child;
    int type;
} cJSON;

#define cJSON_IsString(x)  0
#define cJSON_IsNumber(x)  0

static inline cJSON *cJSON_Parse(const char *v)                          { (void)v; return 0; }
static inline cJSON *cJSON_GetObjectItem(cJSON *o, const char *k)        { (void)o; (void)k; return 0; }
static inline void   cJSON_Delete(cJSON *c)                              { (void)c; }
