// alloc_stress.c
// Default: tests your cmalloc/cfree.
// Compile with -DUSE_SYSTEM_MALLOC to benchmark malloc/free instead.

#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdalign.h>
#include <stddef.h>

#include <cmalloc/cmalloc.h>

#ifndef USE_SYSTEM_MALLOC
#define ALLOC cmalloc
#define FREE  cfree
#define ALLOC_NAME "custom cmalloc/cfree"
#else
#define ALLOC malloc
#define FREE  free
#define ALLOC_NAME "system malloc/free"
#endif

typedef struct {
    void *ptr;
    size_t size;
    uint8_t pattern;
} slot_t;

static uint64_t rng_state = 0x123456789abcdefULL;

static uint64_t rng_next(void) {
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static size_t random_size(void) {
    uint64_t r = rng_next() % 100;

    if (r < 55) return 1 + (rng_next() % 64);          // tiny
    if (r < 80) return 65 + (rng_next() % 512);        // small
    if (r < 94) return 513 + (rng_next() % 4096);      // medium
    if (r < 99) return 4097 + (rng_next() % 65536);    // large
    return 65537 + (rng_next() % (1024 * 1024));       // huge
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void fill_block(slot_t *s) {
    memset(s->ptr, s->pattern, s->size);
}

static void verify_block(const slot_t *s) {
    uint8_t *p = s->ptr;
    for (size_t i = 0; i < s->size; i++) {
        if (p[i] != s->pattern) {
            fprintf(stderr,
                    "CORRUPTION: ptr=%p size=%zu index=%zu expected=%u got=%u\n",
                    s->ptr, s->size, i, s->pattern, p[i]);
            abort();
        }
    }
}

static void checked_alloc(slot_t *s, size_t size) {
    s->ptr = ALLOC(size);
    if (!s->ptr) {
        fprintf(stderr, "ALLOC failed for size %zu\n", size);
        abort();
    }

    if (((uintptr_t)s->ptr % alignof(max_align_t)) != 0) {
        fprintf(stderr, "BAD ALIGNMENT: ptr=%p size=%zu\n", s->ptr, size);
        abort();
    }

    s->size = size;
    s->pattern = (uint8_t)(1 + (rng_next() % 255));
    fill_block(s);
}

static void checked_free(slot_t *s) {
    if (!s->ptr) return;
    verify_block(s);
    FREE(s->ptr);
    s->ptr = NULL;
    s->size = 0;
}

static void random_stress(size_t slots_count, size_t iterations) {
    slot_t *slots = calloc(slots_count, sizeof(slot_t));
    assert(slots);

    size_t allocs = 0;
    size_t frees = 0;
    size_t live = 0;
    size_t peak_live = 0;

    double start = now_sec();

    for (size_t i = 0; i < iterations; i++) {
        size_t idx = rng_next() % slots_count;

        if (slots[idx].ptr) {
            checked_free(&slots[idx]);
            frees++;
            live--;
        } else {
            checked_alloc(&slots[idx], random_size());
            allocs++;
            live++;
            if (live > peak_live) peak_live = live;
        }

        // Occasionally verify a random live block without freeing it.
        if ((i % 1000) == 0) {
            size_t check = rng_next() % slots_count;
            if (slots[check].ptr) verify_block(&slots[check]);
        }
    }

    for (size_t i = 0; i < slots_count; i++) {
        if (slots[i].ptr) {
            checked_free(&slots[i]);
            frees++;
            live--;
        }
    }

    double end = now_sec();

    printf("\n=== Random stress ===\n");
    printf("allocator:   %s\n", ALLOC_NAME);
    printf("iterations:  %zu\n", iterations);
    printf("slots:       %zu\n", slots_count);
    printf("allocs:      %zu\n", allocs);
    printf("frees:       %zu\n", frees);
    printf("peak live:   %zu\n", peak_live);
    printf("time:        %.6f sec\n", end - start);
    printf("ops/sec:     %.2f\n", iterations / (end - start));

    free(slots);
}

static void fragmentation_stress(size_t n) {
    slot_t *a = calloc(n, sizeof(slot_t));
    assert(a);

    double start = now_sec();

    // Allocate many blocks.
    for (size_t i = 0; i < n; i++) {
        checked_alloc(&a[i], 16 + (i % 2048));
    }

    // Free every other block.
    for (size_t i = 0; i < n; i += 2) {
        checked_free(&a[i]);
    }

    // Fill holes with differently sized blocks.
    for (size_t i = 0; i < n; i += 2) {
        checked_alloc(&a[i], random_size());
    }

    // Free in weird order.
    for (size_t step = 0; step < n; step++) {
        size_t i = (step * 7919) % n;
        checked_free(&a[i]);
    }

    double end = now_sec();

    printf("\n=== Fragmentation stress ===\n");
    printf("allocator:   %s\n", ALLOC_NAME);
    printf("blocks:      %zu\n", n);
    printf("time:        %.6f sec\n", end - start);

    free(a);
}

static void tiny_alloc_benchmark(size_t iterations) {
    double start = now_sec();

    for (size_t i = 0; i < iterations; i++) {
        size_t size = 1 + (i % 64);
        void *p = ALLOC(size);
        if (!p) abort();
        memset(p, 0xA5, size);
        FREE(p);
    }

    double end = now_sec();

    printf("\n=== Tiny alloc/free benchmark ===\n");
    printf("allocator:   %s\n", ALLOC_NAME);
    printf("iterations:  %zu\n", iterations);
    printf("time:        %.6f sec\n", end - start);
    printf("ops/sec:     %.2f\n", iterations / (end - start));
}

int main(int argc, char **argv) {
    size_t iterations = 5 * 1000 * 1000;
    size_t slots = 100000;

    if (argc >= 2) iterations = strtoull(argv[1], NULL, 10);
    if (argc >= 3) slots = strtoull(argv[2], NULL, 10);

    printf("Running allocator benchmark: %s\n", ALLOC_NAME);

    tiny_alloc_benchmark(iterations);
    random_stress(slots, iterations);
    fragmentation_stress(slots);

    printf("\nAll tests passed.\n");
    return 0;
}