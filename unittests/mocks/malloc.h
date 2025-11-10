#pragma once

#include <stdbool.h>
#include <stddef.h>

extern bool malloc_should_fail;
extern size_t last_malloc_size;
extern int allocated_count;
extern int freed_count;

void* test_malloc(size_t size);
void test_free(void* ptr);
void* get_allocated_ptr(int index);
bool was_ptr_freed(void* ptr);
void reset_malloc_tracking(void);
