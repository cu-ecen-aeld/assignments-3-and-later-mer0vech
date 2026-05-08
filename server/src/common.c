#include <stdlib.h>
#include <syslog.h>

#include "common.h"

void*
safe_malloc(size_t size)
{
  void *ptr = malloc(size);
  if(!ptr && size > 0) {
    syslog(LOG_ERR, "Memory allocation failed for %zu bytes.", size);
    exit(EXIT_FAILURE);
  }
  return ptr;
}

void* 
safe_realloc(void *ptr, size_t size)
{
  void *new_ptr = realloc(ptr, size);
  if(!new_ptr && size > 0) {
    syslog(LOG_ERR, "Memory reallocation failed for %zu bytes.", size);
    free(ptr);
    exit(EXIT_FAILURE);
  }
  return new_ptr;
}

void* 
safe_calloc(size_t nmemb, size_t size)
{
  void *ptr = calloc(nmemb, size);
  if(!ptr && (nmemb * size) > 0) {
    syslog(LOG_ERR, "Memory allocation failed for %zu bytes.", size);
    exit(EXIT_FAILURE);
  }
  return ptr;
}
