/**
 * @author Jonathon Lim
 *
 * Micro-benchmark that isolates the two hot paths the optimization work
 * targeted, so the speedups can be attributed rather than just observed:
 *
 *   1. Allocation dispatch + fast path -- the size-class lookup table that
 *      replaced a linear scan over every size class on each cmalloc.
 *   2. The free path -- caching the page size/shift so cfree no longer pays a
 *      sysconf() call per free when mapping a pointer back to its span.
 *
 * Each phase is timed separately (bulk-allocate a batch, then bulk-free it) for
 * a spread of size classes, against both the system allocator and cmalloc, so
 * the alloc-path and free-path costs are reported independently.
 *
 * A second pass sweeps EVERY size class round-robin to stress the size-class
 * dispatch directly (the pattern that most punishes a linear scan).
 */

#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>

typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void *);

#ifndef BENCH_RUNS
#define BENCH_RUNS 5
#endif

static volatile uint64_t g_sink;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Spread of representative size classes plus a large (mmap-backed) request. */
static const size_t kSizes[] = {16, 64, 256, 1024, 4096, 65536};
#define NSIZES (sizeof(kSizes) / sizeof(kSizes[0]))

/* Batch size scaled down for big allocations to bound resident memory. */
static size_t batch_for(size_t sz) {
    if (sz <= 256) return 400000;
    if (sz <= 4096) return 120000;
    return 8000;
}

/* Time the allocate phase and the free phase of a batch independently. */
static void time_alloc_free(alloc_fn a, free_fn f, size_t sz, size_t n,
                            double *alloc_s, double *free_s) {
    void **ptrs = malloc(n * sizeof(void *));
    double best_alloc = 1e300, best_free = 1e300;

    for (int r = 0; r < BENCH_RUNS; r++) {
        double t0 = now_sec();
        for (size_t i = 0; i < n; i++) {
            ptrs[i] = a(sz);
        }
        double t1 = now_sec();
        /* Touch a header word so the allocation is observably used. */
        for (size_t i = 0; i < n; i++) {
            *(volatile uint64_t *)ptrs[i] = i;
        }
        double t2 = now_sec();
        for (size_t i = 0; i < n; i++) {
            f(ptrs[i]);
        }
        double t3 = now_sec();

        double alloc_elapsed = t1 - t0;
        double free_elapsed = t3 - t2;
        g_sink += (uint64_t)(t2 - t1); /* keep the touch loop from vanishing */
        if (alloc_elapsed < best_alloc) best_alloc = alloc_elapsed;
        if (free_elapsed < best_free) best_free = free_elapsed;
    }

    free(ptrs);
    *alloc_s = best_alloc;
    *free_s = best_free;
}

/* Round-robin sweep across every size class: pure alloc+free dispatch stress. */
static double time_class_sweep(alloc_fn a, free_fn f) {
    enum { OPS = 4000000 };
    double best = 1e300;
    for (int r = 0; r < BENCH_RUNS; r++) {
        double t0 = now_sec();
        for (int i = 0; i < OPS; i++) {
            size_t sz = kSizes[i % NSIZES];
            void *p = a(sz);
            *(volatile uint64_t *)p = (uint64_t)i;
            f(p);
        }
        double elapsed = now_sec() - t0;
        if (elapsed < best) best = elapsed;
    }
    return best;
}

int main(void) {
    printf("Micro-benchmark: isolated alloc-path and free-path cost "
           "(best of %d)\n", BENCH_RUNS);
    printf("Speedup = malloc / cmalloc  (higher is better for cmalloc)\n\n");

    printf("%-10s %14s %14s %9s   %14s %14s %9s\n",
           "size", "malloc alloc", "cmal alloc", "spd",
           "malloc free", "cmal free", "spd");
    printf("------------------------------------------------------------------"
           "-----------------------------\n");

    int alloc_wins = 0, free_wins = 0;
    for (size_t s = 0; s < NSIZES; s++) {
        size_t sz = kSizes[s];
        size_t n = batch_for(sz);
        double m_alloc, m_free, c_alloc, c_free;
        time_alloc_free(malloc, free, sz, n, &m_alloc, &m_free);
        time_alloc_free(cmalloc, cfree, sz, n, &c_alloc, &c_free);

        double m_alloc_mops = (double)n / m_alloc / 1e6;
        double c_alloc_mops = (double)n / c_alloc / 1e6;
        double m_free_mops = (double)n / m_free / 1e6;
        double c_free_mops = (double)n / c_free / 1e6;
        double alloc_spd = m_alloc / c_alloc;
        double free_spd = m_free / c_free;
        if (alloc_spd >= 1.111) alloc_wins++;
        if (free_spd >= 1.111) free_wins++;

        printf("%-10zu %11.1f Mo %11.1f Mo %8.2fx   %11.1f Mo %11.1f Mo %8.2fx\n",
               sz, m_alloc_mops, c_alloc_mops, alloc_spd,
               m_free_mops, c_free_mops, free_spd);
    }

    printf("------------------------------------------------------------------"
           "-----------------------------\n");
    double m_sweep = time_class_sweep(malloc, free);
    double c_sweep = time_class_sweep(cmalloc, cfree);
    printf("all-size-class round-robin sweep: malloc %.4fs  cmalloc %.4fs  "
           "speedup %.2fx\n", m_sweep, c_sweep, m_sweep / c_sweep);

    printf("\nalloc-path size classes >=10%% faster: %d/%zu;  "
           "free-path size classes >=10%% faster: %d/%zu\n",
           alloc_wins, NSIZES, free_wins, NSIZES);

    int pass = (m_sweep / c_sweep) >= 1.111;
    printf("TARGET (size-class dispatch >=10%% faster than malloc): %s\n",
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
