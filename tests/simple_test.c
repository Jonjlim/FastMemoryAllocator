#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>

#define N 100000
#define MAX_SIZE 4096

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void correctness_test(void) {
    printf("Running correctness tests...\n");

    printf("    Checkpoint 1\n");
    void *p = cmalloc(100);
    assert(p != NULL);

    memset(p, 0xAB, 100);
    unsigned char *c = p;
    for (int i = 0; i < 100; i++) {
        assert(c[i] == 0xAB);
    }

    printf("    Checkpoint 2\n");
    cfree(p);

    // Allocate many different sizes
    void *ptrs[1000];

    printf("    Checkpoint 3\n");
    for (int i = 0; i < 1000; i++) {
        size_t size = (i % MAX_SIZE) + 1;
        // printf("        %d\n", i);
        ptrs[i] = cmalloc(size);
        // printf("        %d\n", i);
        assert(ptrs[i] != NULL);

        memset(ptrs[i], i % 256, size);

        unsigned char *data = ptrs[i];
        for (size_t j = 0; j < size; j++) {
            assert(data[j] == (unsigned char)(i % 256));
        }
    }

    printf("    Checkpoint 4\n");
    for (int i = 0; i < 1000; i++) {
        cfree(ptrs[i]);
    }

    printf("    Checkpoint 5\n");
    // Free NULL should be safe if your allocator follows standard free behavior
    cfree(NULL);

    printf("Correctness tests passed.\n");
}

static void benchmark_custom(void) {
    void *ptrs[N];

    double start = now_sec();

    for (int i = 0; i < N; i++) {
        size_t size = (i % MAX_SIZE) + 1;
        ptrs[i] = cmalloc(size);
        assert(ptrs[i] != NULL);
        memset(ptrs[i], 0xCD, size);
    }

    for (int i = 0; i < N; i++) {
        cfree(ptrs[i]);
    }

    double end = now_sec();

    printf("custom cmalloc/cfree: %.6f seconds\n", end - start);
}

static void benchmark_standard(void) {
    void *ptrs[N];

    double start = now_sec();

    for (int i = 0; i < N; i++) {
        size_t size = (i % MAX_SIZE) + 1;
        ptrs[i] = malloc(size);
        assert(ptrs[i] != NULL);
        memset(ptrs[i], 0xCD, size);
    }

    for (int i = 0; i < N; i++) {
        free(ptrs[i]);
    }

    double end = now_sec();

    printf("standard malloc/free: %.6f seconds\n", end - start);
}

static void fragmentation_test(void) {
    printf("Running fragmentation-style test...\n");

    void *ptrs[10000];

    for (int i = 0; i < 10000; i++) {
        ptrs[i] = cmalloc((i % 512) + 1);
        assert(ptrs[i] != NULL);
    }

    // Free every other block
    for (int i = 0; i < 10000; i += 2) {
        cfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    // Allocate again into freed space
    for (int i = 0; i < 10000; i += 2) {
        ptrs[i] = cmalloc((i % 256) + 1);
        assert(ptrs[i] != NULL);
    }

    for (int i = 0; i < 10000; i++) {
        cfree(ptrs[i]);
    }

    printf("Fragmentation-style test passed.\n");
}

int main(void) {
    correctness_test();
    fragmentation_test();

    printf("\nBenchmarking...\n");
    benchmark_custom();
    benchmark_standard();

    return 0;
}