// test_threads.c: tests the write barrier (feature 6).
//
// Writer threads keep incrementing counters stored in heap objects while the
// main thread frees memory and forces meshing passes. If a write ever landed
// on the old copy of a page during meshing, it would be lost and the final
// counter values would not match.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREADS 4
#define OBJS_PER_THREAD 20000
#define ITERS 300

typedef struct
{
    uint64_t counter;
    uint64_t pad[5]; // 48-byte object
} obj_t;

static obj_t *objs[THREADS][OBJS_PER_THREAD];
static volatile int stop = 0;
static uint64_t loops[THREADS];

static void *writer(void *arg)
{
    long t = (long)arg;
    uint64_t n = 0;
    while (!stop)
    {
        for (int i = 0; i < OBJS_PER_THREAD; i++)
            objs[t][i]->counter++;
        n++;
    }
    loops[t] = n;
    return NULL;
}

int main(void)
{
    size_t (*meshNow)(void) = (size_t (*)(void))dlsym(RTLD_DEFAULT, "mymesh_mesh_now");

    // Interleave objects that writers keep with "garbage" objects we free later,
    // so writer objects end up scattered over sparse, meshable pages.
    enum
    {
        GARBAGE = THREADS * OBJS_PER_THREAD * 9
    };
    void **garbage = malloc(sizeof(void *) * GARBAGE);
    size_t g = 0;
    for (int i = 0; i < OBJS_PER_THREAD; i++)
    {
        for (int t = 0; t < THREADS; t++)
        {
            objs[t][i] = calloc(1, sizeof(obj_t));
            for (int k = 0; k < 9; k++)
                garbage[g++] = malloc(sizeof(obj_t));
        }
    }

    pthread_t th[THREADS];
    for (long t = 0; t < THREADS; t++)
        pthread_create(&th[t], NULL, writer, (void *)t);

    // Free the garbage in steps and mesh while writers are running.
    for (int it = 0; it < ITERS; it++)
    {
        size_t from = (size_t)it * GARBAGE / ITERS, to = (size_t)(it + 1) * GARBAGE / ITERS;
        for (size_t k = from; k < to; k++)
            free(garbage[k]);
        if (meshNow)
            meshNow();
    }
    stop = 1;
    for (int t = 0; t < THREADS; t++)
        pthread_join(th[t], NULL);

    // A writer only checks `stop` between full loops, so every object of
    // thread t must have been incremented exactly loops[t] times.
    int errors = 0;
    for (int t = 0; t < THREADS; t++)
    {
        for (int i = 0; i < OBJS_PER_THREAD; i++)
        {
            if (objs[t][i]->counter != loops[t])
                errors++;
        }
    }
    printf("test_threads: loops per thread = %llu %llu %llu %llu\n", (unsigned long long)loops[0],
           (unsigned long long)loops[1], (unsigned long long)loops[2], (unsigned long long)loops[3]);
    printf("test_threads: %s (%d lost or wrong counters)\n", errors ? "FAIL" : "PASS", errors);
    free(garbage);
    return errors != 0;
}
