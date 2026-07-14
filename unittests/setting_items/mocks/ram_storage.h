#pragma once

#include <stdbool.h>
#include <stdint.h>

extern bool mock_rams_write_str_called;
extern int mock_storage_read_error_code;
extern int mock_storage_write_error_code;

void rams_init(void);

bool rams_has_key(const char* key);
int rams_write_str(const char* key, const char* value);
int rams_read_str(const char* key, char* value);
int rams_erase_key(const char* key);
