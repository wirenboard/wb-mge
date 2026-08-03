#include "cJSON.h"

#include <stddef.h>

static cJSON response_object;
static bool create_object_fails = false;

void mock_cjson_set_create_object_fails(bool fails)
{
    create_object_fails = fails;
}

void mock_cjson_reset(void)
{
    create_object_fails = false;
}

cJSON *cJSON_CreateObject(void)
{
    return create_object_fails ? NULL : &response_object;
}

cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *value)
{
    (void)name;
    (void)value;
    return object;
}

cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, int boolean)
{
    (void)name;
    (void)boolean;
    return object;
}

cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number)
{
    (void)name;
    (void)number;
    return object;
}

void cJSON_Delete(cJSON *item)
{
    (void)item;   // The stub hands out a static node, so there is nothing to free
}
