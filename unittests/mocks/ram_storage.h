#pragma once

#include <stdint.h>
#include <stdbool.h>

void rams_init(void);
int rams_write_str(const char* key, const char* value);
int rams_read_str(const char* key, char* value);
int rams_write_u8(const char* key, uint8_t value);
int rams_read_u8(const char* key, uint8_t* value);
int rams_write_u16(const char* key, uint16_t value);
int rams_read_u16(const char* key, uint16_t* value);
int rams_write_u32(const char* key, uint32_t value);
int rams_read_u32(const char* key, uint32_t* value);
bool rams_has_key(const char* key);
