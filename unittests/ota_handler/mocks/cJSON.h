#pragma once

/* Minimal cJSON stub for the ota_handler unit tests. ota_create_success_response() only builds a
 * flat object, so the stub hands out one static node and records nothing. What it does provide is a
 * controllable cJSON_CreateObject() failure: without it the "firmware written but the response
 * could not be built" branch is unreachable, and that branch is exactly the one that must still
 * reboot the device. */

#include <stdbool.h>

typedef struct cJSON {
    int placeholder;
} cJSON;

cJSON *cJSON_CreateObject(void);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *value);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
void   cJSON_Delete(cJSON *item);

/* --- mock control --------------------------------------------------------- */

/* Make cJSON_CreateObject() return NULL, as it does when the heap is exhausted. */
void mock_cjson_set_create_object_fails(bool fails);

void mock_cjson_reset(void);
