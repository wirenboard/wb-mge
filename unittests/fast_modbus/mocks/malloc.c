#include <stdbool.h>
#include <stdlib.h>

bool malloc_should_fail = false;

void* test_malloc(size_t size)
{
    if (malloc_should_fail) {
        return NULL;
    }
    return malloc(size);
}
