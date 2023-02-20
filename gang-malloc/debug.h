#ifndef GANG_MALLOC_DEBUG_H
#define GANG_MALLOC_DEBUG_H
#if DEBUG

#include <stdio.h>
#define debug_printf(fmt, ...) do { \
        fprintf(stderr, "[%lu] [%s:%d]: " fmt "\n", pthread_self(), __func__, __LINE__, __VA_ARGS__); \
    } while (0)

#define debug_print(str) do { \
        fprintf(stderr, "[%lu] [%s:%d]: %s\n", pthread_self(), __func__, __LINE__, str); \
    } while (0)
#else
#define debug_printf(...)
#define debug_print(...)
#endif

#endif
