#include <stdbool.h>
#include <stdlib.h>

extern bool malloc_should_fail;

void* test_malloc(size_t size)
{
    if (malloc_should_fail) {
        return NULL;
    }
    return malloc(size);
}
