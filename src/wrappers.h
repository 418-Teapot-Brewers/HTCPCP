#pragma once

#include <stddef.h>

void * malloc_wrapper(size_t size);
void * realloc_wrapper_shrink(void * old_ptr, size_t new_size);
void * realloc_wrapper_ignore(void * old_ptr, size_t new_size);
