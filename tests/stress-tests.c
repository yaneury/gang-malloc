#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>

#include "gang-malloc.h"
#include "debug.h"

#ifndef NUM_RUNS
#define NUM_RUNS 8
#endif

#define MSG_TEMPLATE "Hello Gang Malloc World. Run: %u!"
#define SLEEP_PCT 10
#define MAX_SLEEP_TIME 2

typedef struct {
    int num_runs;
    int thread_index;
} context_t;


void* convergent_paths(void* _);
void* divergent_paths(void* _);

context_t get_thread_context(int index);

int main() {
    printf("Starting stress tests\n=====================\nGang Size: %d\nRuns: %d\n\n", MAX_GANG_SIZE, NUM_RUNS);

    srand(time(0));

    pthread_t threads[MAX_GANG_SIZE];
    context_t contexts[MAX_GANG_SIZE];


    printf("Executing runs for convergent paths\n-----------------------------------\n");

    gang_setup();
    for (unsigned int i = 0; i < MAX_GANG_SIZE; ++i) {
        contexts[i] = get_thread_context(i);
        pthread_create(&threads[i], NULL, convergent_paths, &contexts[i]);
    }

    for (unsigned int i = 0; i < MAX_GANG_SIZE; ++i) {
        pthread_join(threads[i], NULL);
    }
    gang_teardown();


    printf("\nExecuting runs for divergent paths\n----------------------------------\n");

    gang_setup();
    for (unsigned int i = 0; i < MAX_GANG_SIZE; ++i) {
        contexts[i] = get_thread_context(i);
        pthread_create(&threads[i], NULL, divergent_paths, &contexts[i]);
    }

    for (unsigned int i = 0; i < MAX_GANG_SIZE; ++i) {
        pthread_join(threads[i], NULL);
    }
    gang_teardown();

    printf("\nStress tests done!\n");
    return 0;
}

void* convergent_paths(void* _) {
    context_t* ctx = (context_t*) _;
    for (unsigned i = 0; i < ctx->num_runs; ++i) {
        char* msg = gang_malloc(sizeof(MSG_TEMPLATE) + 1);
        msg = gang_realloc(msg, 100 * (sizeof(MSG_TEMPLATE) + 1));
        sprintf(msg, MSG_TEMPLATE, i);
#if DEBUG
        debug_print(msg);
#else
        printf("%s\n", msg);
#endif
        gang_free(msg);
    }

    return NULL;
}

void* divergent_paths(void* _) {
    context_t* ctx = (context_t*) _;
    for (unsigned i = 0; i < ctx->num_runs; ++i) {
        const bool with_realloc = rand() % 2 == 0;

        if (rand() % 100 < SLEEP_PCT) {
            sleep(rand() % MAX_SLEEP_TIME);
        }
        char* msg = gang_malloc(sizeof(MSG_TEMPLATE) + 1);

        if (with_realloc) {
            if (rand() % 100 < SLEEP_PCT) {
                sleep(rand() % MAX_SLEEP_TIME);
            }
            msg = gang_realloc(msg, 100 * (sizeof(MSG_TEMPLATE) + 1));
        }


        sprintf(msg, MSG_TEMPLATE, i);
#if DEBUG
        debug_print(msg);
#else
        printf("%s\n", msg);
#endif

        if (rand() % 100 < SLEEP_PCT) {
            sleep(rand() % MAX_SLEEP_TIME);
        }
        gang_free(msg);
    }

    return NULL;
}

context_t get_thread_context(int index) {
    return (context_t) { .num_runs=NUM_RUNS, .thread_index = index };
}
