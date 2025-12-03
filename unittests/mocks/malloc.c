#include "unity.h"
#include "malloc.h"

#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free

#define MAX_TRACKED_ALLOCS          100

alloc_record_t allocated_ptrs[MAX_TRACKED_ALLOCS] = {0};
bool malloc_should_fail = false;

static void* freed_ptrs[MAX_TRACKED_ALLOCS] = {0};
static int allocated_count = 0;
static int freed_count = 0;

static bool was_ptr_allocated(void* ptr)
{
    for (int i = 0; i < allocated_count; i++) {
        if (allocated_ptrs[i].ptr == ptr) {
            return true;
        }
    }
    return false;
}

void* test_malloc(size_t size)
{
    TEST_ASSERT_LESS_THAN_MESSAGE(
        MAX_TRACKED_ALLOCS,
        allocated_count,
        "Exceeded maximum number of tracked allocations in mock"
    );

    if (malloc_should_fail) {
        return NULL;
    }

    void* ptr = malloc(size);
    if (!ptr) {
        return NULL;
    }

    allocated_ptrs[allocated_count].ptr = ptr;
    allocated_ptrs[allocated_count].size = size;
    allocated_count++;

    return ptr;
}

void test_free(void* ptr)
{
    if (ptr == NULL) {
        return;
    }

    if (!was_ptr_allocated(ptr)) {
        TEST_FAIL_MESSAGE("Attempting to free untracked pointer");
    }

    if (was_ptr_freed(ptr)) {
        TEST_FAIL_MESSAGE("Double free detected");
    }

    TEST_ASSERT_LESS_THAN_MESSAGE(
        MAX_TRACKED_ALLOCS,
        freed_count,
        "Exceeded maximum number of tracked deallocations in mock"
    );

    freed_ptrs[freed_count++] = ptr;

    free(ptr);
}

void* get_allocated_ptr(int index)
{
    if (index >= 0 && index < allocated_count) {
        return allocated_ptrs[index].ptr;
    }
    return NULL;
}

bool was_ptr_freed(void* ptr)
{
    for (int i = 0; i < freed_count; i++) {
        if (freed_ptrs[i] == ptr) {
            return true;
        }
    }
    return false;
}

void reset_malloc_tracking(void)
{
    for (int i = 0; i < allocated_count; i++) {
        if (!was_ptr_freed(allocated_ptrs[i].ptr)) {
            free(allocated_ptrs[i].ptr);
        }
    }

    malloc_should_fail = false;
    allocated_count = 0;
    freed_count = 0;
    memset(allocated_ptrs, 0, sizeof(allocated_ptrs));
    memset(freed_ptrs, 0, sizeof(freed_ptrs));
}

void verify_malloc_tracking(int expected_allocs, int expected_frees)
{
    TEST_ASSERT_EQUAL_MESSAGE(
        expected_allocs,
        allocated_count,
        "Unexpected number of allocations"
    );

    TEST_ASSERT_EQUAL_MESSAGE(
        expected_frees,
        freed_count,
        "Unexpected number of deallocations"
    );

    if (expected_allocs == expected_frees) {
        for (int i = 0; i < allocated_count; i++) {
            TEST_ASSERT_TRUE_MESSAGE(
                was_ptr_freed(allocated_ptrs[i].ptr),
                "Not all allocated pointers were freed"
            );
        }
    }
}
