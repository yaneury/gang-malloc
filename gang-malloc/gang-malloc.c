#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>
#include <memory.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>

#include "gang-malloc.h"
#include "debug.h"

#ifndef MPSP_GLIBC
#define MPSP_GLIBC 0
#endif

#ifndef BARRIER_WAIT_TIME // In milliseconds
#define BARRIER_WAIT_TIME 200
#endif

#ifndef PARALLEL_CORES
#define PARALLEL_CORES 1
#endif

typedef struct {
    bool occupied;
    pthread_mutex_t mutex;
    bool mem_allocation_barrier_created;
    pthread_barrier_t mem_allocation_barrier;
    size_t request_sizes[MAX_GANG_SIZE];
    unsigned ref_count;
    void *header;
} gang_t;

typedef struct {
    const size_t block_size;
    unsigned ref_count;
} header_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct timespec max_wait;
    gang_t gang_pool[MAX_GANG_SIZE];
    gang_t* buffer;
} core_globals_t;

static core_globals_t core_globals[PARALLEL_CORES];

#if (!MPSP_GLIBC)

static unsigned thread_indices[MAX_GANG_SIZE];
static unsigned highest_unused_index;
static pthread_key_t thr_id_key;

#endif

static core_globals_t *get_core_globals();

static unsigned int get_thread_index();

static gang_t* get_vacant_gang(gang_t* gangs, size_t len);

static gang_t* occupy_gang(core_globals_t* globals, size_t request_size, unsigned thread_index);

static void *allocate_memory(size_t max_request_size, unsigned threads_entered);

static void init_gang(gang_t* gang);

static void clear_gang(gang_t *gang, bool destroy_barrier);

static bool is_leader(const size_t *request_sizes, size_t len, size_t thread_index);

static void set_header(void *dest, header_t header);

static void set_links_to_address(void *address, size_t total_size, size_t block_size);

static header_t *get_header(void *block);

static void *get_thread_block(header_t *header, size_t index);

static size_t get_total_size(size_t max);

static size_t get_header_size();

static size_t get_block_size(size_t request_size);

static struct timespec get_milliseconds_from_now(unsigned ms);

static size_t round_up_pow2(size_t v);



void gang_setup() {
    for (size_t i = 0; i < PARALLEL_CORES; ++i) {
        pthread_mutex_init(&core_globals[i].mutex, NULL);
        pthread_cond_init(&core_globals[i].cond, NULL);
        for (unsigned j = 0; j < MAX_GANG_SIZE; ++j) {
            init_gang(&core_globals[i].gang_pool[j]);
        }
    }
#if (!MPSP_GLIBC)
    pthread_key_create(&thr_id_key, NULL);
    highest_unused_index = 0;
#endif
}

void gang_teardown() {
    for (size_t i = 0; i < PARALLEL_CORES; ++i) {
        pthread_mutex_destroy(&core_globals[i].mutex);
        pthread_cond_destroy(&core_globals[i].cond);
    }
#if (!MPSP_GLIBC)
    pthread_key_delete(thr_id_key);
    highest_unused_index = 0;
#endif
}

void *gang_malloc(const size_t size) {
    if (size == 0) {
        return NULL;
    }

    core_globals_t *globals = get_core_globals();
    const unsigned index = get_thread_index();
    gang_t* gang = occupy_gang(globals, size, index);

#if (MPSP_GLIBC)
    pthread_barrier_init(&gang->mem_allocation_barrier, NULL, gang->ref_count);
#else
    pthread_mutex_lock(&gang->mutex);
    if (!gang->mem_allocation_barrier_created) {
        pthread_barrier_init(&gang->mem_allocation_barrier, NULL, gang->ref_count);
        gang->mem_allocation_barrier_created = true;
    }
    pthread_mutex_unlock(&gang->mutex);
#endif

    if (is_leader(gang->request_sizes, MAX_GANG_SIZE, index)) {
        gang->header = allocate_memory(size, gang->ref_count);
    }

    pthread_barrier_wait(&gang->mem_allocation_barrier);
    void* header = gang->header;
    if (__atomic_sub_fetch(&gang->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
        clear_gang(gang, true);
    }
    return header == NULL ? NULL : get_thread_block(header, index);
}

void gang_free(void *ptr) {
    header_t *header = get_header(ptr);
    if (__atomic_sub_fetch(&header->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
        free(header);
    }
}

void *gang_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return gang_malloc(size);
    }

    if (size == 0) {
        gang_free(ptr);
        return NULL;
    }

    header_t *gang = get_header(ptr);
    if (get_block_size(size) < gang->block_size) {
        return ptr;
    }

    void *new_ptr = gang_malloc(size);
    if (new_ptr != NULL) {
        memcpy(new_ptr, ptr, gang->block_size - sizeof(uintptr_t));
        gang_free(ptr);
    }
    return new_ptr;
}

// Get malloc buffer for current CPU core.
// In MPSP, each core get's its own buffer from a shared global array.
// In simulation, one buffer is used.
static core_globals_t *get_core_globals() {
#if MPSP_GLIBC
    const size_t current_cpu_core = 0; // TODO: Add magic instruction here
#else
    const size_t current_cpu_core = 0;
#endif

    return &core_globals[current_cpu_core];
}

// Get index for current thread in CPU core.
// In MPSP, a magic instruction fetches the index for a particular thread.
// In simulation, a naive static counter is used.
static unsigned get_thread_index() {
#if MPSP_GLIBC
    return 0; // TODO: Add magic instruction here
#else
    unsigned *stored_index = pthread_getspecific(thr_id_key);
    if (stored_index != NULL) {
        return *stored_index;
    }

    const unsigned current_index = __atomic_fetch_add(&highest_unused_index, 1, __ATOMIC_SEQ_CST);
    thread_indices[current_index] = current_index;
    pthread_setspecific(thr_id_key, &thread_indices[current_index]);
    return thread_indices[current_index];
#endif
}

static gang_t* get_vacant_gang(gang_t* gangs, const size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (!gangs[i].occupied)  {
            gangs[i].occupied = true;
            return &gangs[i];
        }
    }

    assert(0);
    return NULL;
}

static gang_t* occupy_gang(core_globals_t* globals, const size_t request_size, const unsigned thread_index) {
    pthread_mutex_lock(&globals->mutex);
    if (globals->buffer == NULL)  {
#if (!MPSP_GLIBC)
        globals->max_wait = get_milliseconds_from_now(BARRIER_WAIT_TIME);
#endif
        globals->buffer = get_vacant_gang(globals->gang_pool, MAX_GANG_SIZE);
    }

    ++globals->buffer->ref_count;
    const unsigned threads_entered = globals->buffer->ref_count;
    gang_t* gang = globals->buffer;

    pthread_mutex_unlock(&globals->mutex);

    gang->request_sizes[thread_index] = request_size;

#if (MPSP_GLIBC)
    global->buffer = NULL;
#else
    if (threads_entered == MAX_GANG_SIZE) {
        globals->buffer = NULL;
        pthread_cond_broadcast(&globals->cond);
    } else {
        bool timedout = false;
        pthread_mutex_lock(&globals->mutex);
        while (gang->ref_count != MAX_GANG_SIZE && !timedout) {
            if (pthread_cond_timedwait(&globals->cond, &globals->mutex, &globals->max_wait) == ETIMEDOUT) {
                timedout = true;
                globals->buffer = NULL;
                pthread_cond_broadcast(&globals->cond);
            }
        }
        pthread_mutex_unlock(&globals->mutex);
    }
#endif

    return gang;
}

static void *allocate_memory(const size_t max_request_size, const unsigned threads_entered) {
    const size_t total_size = get_total_size(max_request_size);
    const size_t block_size = get_block_size(max_request_size);
    void *base = malloc(total_size);
    if (base == NULL) {
        return NULL;
    }

    header_t header = {
            .block_size = block_size,
            .ref_count = threads_entered
    };
    set_header(base, header);
    set_links_to_address(base, total_size, block_size);
    return base;
}

static void init_gang(gang_t* gang) {
    pthread_mutex_init(&gang->mutex, NULL);
    clear_gang(gang, false);
}

static void clear_gang(gang_t *gang, bool destroy_barrier) {
    for (unsigned i = 0; i < MAX_GANG_SIZE; ++i) {
        gang->request_sizes[i] = 0;
    }
    gang->mem_allocation_barrier_created = false;
    gang->header = NULL;
    gang->ref_count = 0;

    if (destroy_barrier) {
        pthread_barrier_destroy(&gang->mem_allocation_barrier);
    }

    gang->occupied = false;
}

static bool is_leader(const size_t *request_sizes, size_t len, size_t thread_index) {
    size_t max_index = 0;
    for (size_t i = 0; i < len; ++i) {
        if (request_sizes[i] > request_sizes[max_index]) {
            max_index = i;
        }
    }

    return max_index == thread_index;
}

static void set_header(void *dest, header_t header) {
    memcpy(dest, &header, sizeof(header_t));
}

static void set_links_to_address(void *address, const size_t total_size, const size_t block_size) {
    for (void *offset = address + get_header_size(); offset < address + total_size; offset += block_size) {
        *((uintptr_t *) offset) = (uintptr_t) address;
    }
}

static header_t *get_header(void *block) {
    uintptr_t addr = *((uintptr_t *) (block - sizeof(uintptr_t)));
    return (header_t *) addr;
}

static void *get_thread_block(header_t *header, size_t index) {
    const size_t block_size = header->block_size;
    return ((void *) header) + get_header_size() + (block_size * index + sizeof(uintptr_t));
}

static size_t get_total_size(const size_t max) {
    return get_header_size() + get_block_size(max) * MAX_GANG_SIZE;
}

static size_t get_header_size() {
    return round_up_pow2(sizeof(header_t));
}

static size_t get_block_size(size_t request_size) {
    return round_up_pow2(sizeof(uintptr_t) + request_size);
}

static struct timespec get_milliseconds_from_now (unsigned ms) {
  struct timeval now;
  struct timespec timeout;

  gettimeofday(&now, NULL);
  long future_micro_sec = now.tv_usec + ms * 1000;
  timeout.tv_nsec = (future_micro_sec % 1000000) * 1000;
  timeout.tv_sec = now.tv_sec + future_micro_sec / 1000000;
  return timeout;
}

static size_t round_up_pow2(size_t v) {
    v--;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    v |= v >> 32u;
    v++;
    return v;
}
