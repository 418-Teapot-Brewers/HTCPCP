#include "wrappers.h"
#include "htcpcpd.h" // for error()

#include <malloc.h>
#include <stddef.h>
#include <string.h>

void * malloc_wrapper(size_t size)
{
    void * tmp = malloc(size);
    if (tmp == NULL)
        error("Out of memory");
    return tmp;
}

void * realloc_wrapper_shrink(void * old_ptr, size_t new_size)
{
    void * tmp = realloc(old_ptr, new_size);
    if (tmp == NULL)
        return old_ptr;
    return tmp;
}

void * realloc_wrapper_ignore(void * old_ptr, size_t new_size)
{
    void * tmp = realloc(old_ptr, new_size);
    if (tmp == NULL)
        free(old_ptr);
    return tmp;
}

char * strndup_wrapper(const char * string, size_t len)
{
    char * newString = strndup(string, len);
    if (newString == NULL)
        error("Out of memory");
    return newString;
}
