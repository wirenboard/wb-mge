#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#undef malloc
#undef free

#define MAX_TRACKED_ALLOCS 100

bool malloc_should_fail = false;
size_t last_malloc_size = 0;

static void* allocated_ptrs[MAX_TRACKED_ALLOCS];
static int allocated_count = 0;
static void* freed_ptrs[MAX_TRACKED_ALLOCS];
static int freed_count = 0;

void* test_malloc(size_t size)
{
    last_malloc_size = size;

    if (malloc_should_fail) {
        return NULL;
    }

    void* ptr = malloc(size);
    if (ptr && allocated_count < MAX_TRACKED_ALLOCS) {
        allocated_ptrs[allocated_count++] = ptr;
    }
    return ptr;
}

void test_free(void* ptr)
{
    if (ptr && freed_count < MAX_TRACKED_ALLOCS) {
        freed_ptrs[freed_count++] = ptr;
    }

    free(ptr);
}

void reset_malloc_tracking(void)
{
    malloc_should_fail = false;
    last_malloc_size = 0;
    allocated_count = 0;
    freed_count = 0;
    memset(allocated_ptrs, 0, sizeof(allocated_ptrs));
    memset(freed_ptrs, 0, sizeof(freed_ptrs));
}

int get_allocated_count(void)
{
    return allocated_count;
}

void* get_allocated_ptr(int index)
{
    if (index >= 0 && index < allocated_count) {
        return allocated_ptrs[index];
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
