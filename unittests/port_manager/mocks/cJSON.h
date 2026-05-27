#pragma once

/* Minimal cJSON stub for compilation.
 * port_manager.c uses cJSON for HTTP handler JSON parsing, but the
 * unit tests under test focus on port lifecycle, not HTTP handlers. */

typedef struct cJSON {
    double valuedouble;
    char *valuestring;
    struct cJSON *next;
    struct cJSON *child;
    int type;
} cJSON;

/* A cJSON item is a string if valuestring is non-NULL */
#define cJSON_IsString(x) ((x) != NULL && (x)->valuestring != NULL)
#define cJSON_IsNumber(x)  0

static inline cJSON *cJSON_Parse(const char *v)                          { (void)v; return 0; }
cJSON *cJSON_GetObjectItem(cJSON *o, const char *k);
static inline void   cJSON_Delete(cJSON *c)                              { (void)c; }
static inline cJSON *cJSON_CreateObject(void)                            { return 0; }
static inline cJSON *cJSON_AddStringToObject(cJSON *o, const char *n,
                                              const char *s)             { (void)o; (void)n; (void)s; return 0; }
static inline cJSON *cJSON_AddNumberToObject(cJSON *o, const char *n,
                                              double v)                  { (void)o; (void)n; (void)v; return 0; }
