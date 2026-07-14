#pragma once

/* Minimal cJSON stub for the auth unit tests. auth.c parses {"login", "pass"} out of the
 * request and writes {"auth"/"logout"/"error"} into the response; nothing here needs a real
 * JSON tree, so the mock hands out static nodes and records what was written. */

#define cJSON_False   (1 << 1)
#define cJSON_True    (1 << 2)
#define cJSON_String  (1 << 4)

typedef struct cJSON {
    char *valuestring;
    int   type;
} cJSON;

cJSON *cJSON_CreateObject(void);
int    cJSON_HasObjectItem(cJSON *object, const char *name);
cJSON *cJSON_GetObjectItem(cJSON *object, const char *name);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *value);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean);
void   cJSON_Delete(cJSON *item);
