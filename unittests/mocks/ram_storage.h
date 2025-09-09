#pragma once

#include <stdbool.h>
#include <stdint.h>

void rams_init(void);

// String storage functions (compatible with new API)
bool rams_has_key(const char* key);
int rams_write_str(const char* key, const char* value);
int rams_read_str(const char* key, char* value);
