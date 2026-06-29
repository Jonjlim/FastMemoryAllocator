/**
 * Short concurrent correctness check for cmalloc/cfree.
 *
 * Several threads hammer the global allocator with mixed-size allocations,
 * write thread- and iteration-specific patterns, and verify them before free.
 * Any crash, failed allocation, or data corruption fails the test.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cmalloc/cmalloc.h>

#ifndef NUM_THREADS
#define NUM_THREADS 4
#endif

#ifndef ITERS
#define ITERS 8000
#endif

#ifndef MAX_SIZE
#define MAX_SIZE 4096
#endif

static void report_error(int thread_id, const char *msg) {
    fprintf(stderr, "thread %d: %s\n", thread_id, msg);
    abort();
}

static size_t pick_size(int thread_id, int iter) {
    uint32_t mix = (uint32_t)(thread_id * 7919u + iter * 104729u);
    return (size_t)((mix % MAX_SIZE) + 1);
}

static int fill_and_check(int thread_id, unsigned char *p, size_t n,
                          uint8_t tag) {
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)(tag + (uint8_t)i);
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char expected = (unsigned char)(tag + (uint8_t)i);
        if (p[i] != expected) {
            report_error(thread_id, "data corruption after write");
            return -1;
        }
    }
    return 0;
}

static void *worker(void *arg) {
    int thread_id = *(int *)arg;

    for (int i = 0; i < ITERS; i++) {
        size_t size = pick_size(thread_id, i);
        void *p = cmalloc(size);
        if (p == NULL) {
            report_error(thread_id, "cmalloc returned NULL");
        }

        if ((uintptr_t)p % 16 != 0) {
            report_error(thread_id, "misaligned pointer");
        }

        uint8_t tag = (uint8_t)(thread_id * 17 + i);
        if (fill_and_check(thread_id, p, size, tag) != 0) {
            cfree(p);
            return NULL;
        }
        cfree(p);
    }

    return NULL;
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    printf("thread basic test: %d threads x %d iterations (max size %d)\n",
           NUM_THREADS, ITERS, MAX_SIZE);

    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i;
        if (pthread_create(&threads[i], NULL, worker, &thread_ids[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("PASS\n");
    return 0;
}
