#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void* ptr;
    size_t size;
} alloc_record_t;

extern bool malloc_should_fail;
extern alloc_record_t allocated_ptrs[];

void* test_malloc(size_t size);
void test_free(void* ptr);
void* get_allocated_ptr(int index);
bool was_ptr_freed(void* ptr);
void reset_malloc_tracking(void);
void verify_malloc_tracking(int expected_allocs, int expected_frees);
