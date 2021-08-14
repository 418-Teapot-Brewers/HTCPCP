#pragma once

#include <stddef.h>

void * malloc_wrapper(size_t size);
void * calloc_wrapper(size_t nmemb, size_t size);
void * realloc_wrapper_shrink(void * old_ptr, size_t new_size);
void * realloc_wrapper_ignore(void * old_ptr, size_t new_size);
char * strndup_wrapper(const char * string, size_t len);
