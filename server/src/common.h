#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

void* safe_malloc(size_t size);
void* safe_realloc(void *ptr, size_t size);
void* safe_calloc(size_t nmemb, size_t size);

#endif
