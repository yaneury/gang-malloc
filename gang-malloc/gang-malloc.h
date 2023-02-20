#ifndef GANG_MALLOC_H
#define GANG_MALLOC_H

#include <stddef.h>

#ifndef MAX_GANG_SIZE
#define MAX_GANG_SIZE 8
#endif

void gang_setup();

void gang_teardown();

void *gang_malloc(size_t size);

void gang_free(void *ptr);

void *gang_realloc(void *ptr, size_t size);

#endif
