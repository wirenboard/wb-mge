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

/* Booleans are carried in ->type, the way real cJSON does it: the two flags are
 * distinct bits, so an item that is neither reads as "not a bool" and the handler's
 * cJSON_IsBool() rejection path stays reachable. The values themselves need not match
 * upstream cJSON — nothing but this stub and its mock ever sets or reads them. */
#define cJSON_False        (1 << 0)
#define cJSON_True         (1 << 1)

/* A cJSON item is a string if valuestring is non-NULL */
#define cJSON_IsString(x) ((x) != NULL && (x)->valuestring != NULL)
#define cJSON_IsNumber(x)  0
#define cJSON_IsBool(x)   ((x) != NULL && (((x)->type & (cJSON_True | cJSON_False)) != 0))
#define cJSON_IsTrue(x)   ((x) != NULL && (((x)->type & cJSON_True) != 0))

static inline cJSON *cJSON_Parse(const char *v)                          { (void)v; return 0; }
cJSON *cJSON_GetObjectItem(cJSON *o, const char *k);
static inline void   cJSON_Delete(cJSON *c)                              { (void)c; }
static inline cJSON *cJSON_CreateObject(void)                            { return 0; }
static inline cJSON *cJSON_AddStringToObject(cJSON *o, const char *n,
                                              const char *s)             { (void)o; (void)n; (void)s; return 0; }
static inline cJSON *cJSON_AddNumberToObject(cJSON *o, const char *n,
                                              double v)                  { (void)o; (void)n; (void)v; return 0; }
static inline cJSON *cJSON_AddBoolToObject(cJSON *o, const char *n,
                                            int b)                       { (void)o; (void)n; (void)b; return 0; }
