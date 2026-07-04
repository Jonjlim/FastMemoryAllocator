/**
 * Concurrent stress + throughput benchmark for cmalloc/cfree.
 *
 * Phase 1 runs several worker threads with randomized alloc/free traffic and
 * canary/pattern checks (similar spirit to rigor_test, but concurrent).
 * Phase 2 times single-thread vs multi-thread alloc/free throughput for a
 * spread of size classes and reports aggregate Mops/s.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

#ifndef OPS_PER_THREAD
#define OPS_PER_THREAD 40000
#endif

#ifndef MAX_LIVE
#define MAX_LIVE 512
#endif

#ifndef MAX_SIZE
#define MAX_SIZE 8192
#endif

#ifndef BENCH_OPS
#define BENCH_OPS 200000
#endif

#define FRONT_CANARY 0xCAFEBABEDEADBEEFULL
#define BACK_CANARY  0xBADC0FFEE0DDF00DULL

typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void *);

typedef struct {
    void *raw;
    unsigned char *user;
    size_t size;
    uint64_t pattern;
} live_block_t;

typedef struct {
    int thread_id;
    uint64_t rng;
    int errors;
} worker_ctx_t;

typedef struct {
    alloc_fn alloc;
    free_fn dealloc;
    int nthreads;
    size_t size;
    size_t ops_per_thread;
    double elapsed_sec;
} bench_ctx_t;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static uint64_t rng_u64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static size_t rand_size(uint64_t *state) {
    uint64_t r = rng_u64(state) % 100;

    if (r < 70) {
        return (rng_u64(state) % 256) + 1;
    }
    if (r < 90) {
        return (rng_u64(state) % 4096) + 1;
    }
    return (rng_u64(state) % MAX_SIZE) + 1;
}

static void fill_pattern(unsigned char *p, size_t n, uint64_t pattern) {
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)((pattern + i * 131) & 0xff);
    }
}

static void check_pattern(unsigned char *p, size_t n, uint64_t pattern) {
    for (size_t i = 0; i < n; i++) {
        unsigned char expected = (unsigned char)((pattern + i * 131) & 0xff);
        if (p[i] != expected) {
            fprintf(stderr,
                    "DATA CORRUPTION: byte %zu expected 0x%02x got 0x%02x\n",
                    i, expected, p[i]);
            abort();
        }
    }
}

static void check_canaries(live_block_t *b) {
    uint64_t *front = (uint64_t *)b->raw;
    uint64_t *back = (uint64_t *)(b->user + b->size);

    if (*front != FRONT_CANARY) {
        fprintf(stderr, "FRONT CANARY CORRUPTED size=%zu\n", b->size);
        abort();
    }
    if (*back != BACK_CANARY) {
        fprintf(stderr, "BACK CANARY CORRUPTED size=%zu\n", b->size);
        abort();
    }
}

static void checked_alloc(alloc_fn alloc, size_t user_size, live_block_t *out,
                          uint64_t *rng) {
    size_t total = user_size + 2 * sizeof(uint64_t);
    void *raw = alloc(total);
    if (!raw) {
        fprintf(stderr, "allocation failed for %zu bytes\n", user_size);
        abort();
    }

    uint64_t *front = (uint64_t *)raw;
    unsigned char *user = (unsigned char *)(front + 1);
    uint64_t *back = (uint64_t *)(user + user_size);

    *front = FRONT_CANARY;
    *back = BACK_CANARY;

    out->raw = raw;
    out->user = user;
    out->size = user_size;
    out->pattern = rng_u64(rng);
    fill_pattern(user, user_size, out->pattern);
}

static void checked_free(free_fn dealloc, live_block_t *b) {
    check_canaries(b);
    check_pattern(b->user, b->size, b->pattern);
    dealloc(b->raw);
    b->raw = NULL;
    b->user = NULL;
    b->size = 0;
    b->pattern = 0;
}

static void *stress_worker(void *arg) {
    worker_ctx_t *ctx = arg;
    live_block_t live[MAX_LIVE];
    size_t live_count = 0;

    memset(live, 0, sizeof(live));

    for (size_t op = 0; op < (size_t)OPS_PER_THREAD; op++) {
        int do_alloc = live_count == 0 ||
                       (live_count < MAX_LIVE && (rng_u64(&ctx->rng) % 100) < 62);

        if (do_alloc) {
            size_t idx = rng_u64(&ctx->rng) % MAX_LIVE;
            while (live[idx].raw != NULL) {
                idx = (idx + 1) % MAX_LIVE;
            }
            checked_alloc(cmalloc, rand_size(&ctx->rng), &live[idx], &ctx->rng);
            live_count++;
        } else {
            size_t idx = rng_u64(&ctx->rng) % MAX_LIVE;
            while (live[idx].raw == NULL) {
                idx = (idx + 1) % MAX_LIVE;
            }
            checked_free(cfree, &live[idx]);
            live_count--;
        }

        if ((op & 0x3ff) == 0) {
            for (size_t i = 0; i < MAX_LIVE; i++) {
                if (live[i].raw) {
                    check_canaries(&live[i]);
                    check_pattern(live[i].user, live[i].size, live[i].pattern);
                }
            }
        }
    }

    for (size_t i = 0; i < MAX_LIVE; i++) {
        if (live[i].raw) {
            checked_free(cfree, &live[i]);
        }
    }

    return NULL;
}

static void run_concurrent_stress(void) {
    pthread_t threads[NUM_THREADS];
    worker_ctx_t ctx[NUM_THREADS];

    printf("concurrent stress: %d threads x %d ops (max live %d)\n",
           NUM_THREADS, OPS_PER_THREAD, MAX_LIVE);

    for (int i = 0; i < NUM_THREADS; i++) {
        ctx[i].thread_id = i;
        ctx[i].rng = 88172645463325252ULL ^ (uint64_t)(i + 1) * 0x9E3779B97F4A7C15ULL;
        ctx[i].errors = 0;
        if (pthread_create(&threads[i], NULL, stress_worker, &ctx[i]) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("  stress phase: passed\n");
}

static void *bench_worker(void *arg) {
    bench_ctx_t *ctx = arg;

    for (size_t i = 0; i < ctx->ops_per_thread; i++) {
        void *p = ctx->alloc(ctx->size);
        if (!p) {
            fprintf(stderr, "bench allocation failed size=%zu\n", ctx->size);
            abort();
        }
        *(volatile uint64_t *)p = (uint64_t)i;
        ctx->dealloc(p);
    }

    return NULL;
}

static double run_throughput_bench(alloc_fn alloc, free_fn dealloc, int nthreads,
                                   size_t size, size_t ops_per_thread) {
    pthread_t threads[NUM_THREADS];
    bench_ctx_t ctx[NUM_THREADS];
    double start;
    double elapsed;

    if (nthreads > NUM_THREADS) {
        fprintf(stderr, "nthreads exceeds NUM_THREADS compile limit\n");
        exit(1);
    }

    for (int i = 0; i < nthreads; i++) {
        ctx[i].alloc = alloc;
        ctx[i].dealloc = dealloc;
        ctx[i].nthreads = nthreads;
        ctx[i].size = size;
        ctx[i].ops_per_thread = ops_per_thread;
    }

    start = now_sec();
    for (int i = 0; i < nthreads; i++) {
        if (pthread_create(&threads[i], NULL, bench_worker, &ctx[i]) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }
    elapsed = now_sec() - start;

    return elapsed;
}

static void print_bench_row(const char *label, double malloc_sec, double cmalloc_sec,
                            int nthreads, size_t total_ops) {
    double malloc_mops = (double)total_ops / malloc_sec / 1e6;
    double cmalloc_mops = (double)total_ops / cmalloc_sec / 1e6;
    double ratio = cmalloc_sec / malloc_sec;

    printf("%-22s  %2d thr  malloc %7.2f Mops/s  cmalloc %7.2f Mops/s  "
           "cmalloc %.1f%% of malloc\n",
           label, nthreads, malloc_mops, cmalloc_mops, ratio * 100.0);
}

int main(void) {
    static const size_t bench_sizes[] = {16, 64, 256, 1024, 4096};
    size_t total_ops = (size_t)BENCH_OPS;

    printf("NUM_THREADS=%d OPS_PER_THREAD=%d MAX_LIVE=%d BENCH_OPS=%d\n\n",
           NUM_THREADS, OPS_PER_THREAD, MAX_LIVE, BENCH_OPS);

    run_concurrent_stress();

    printf("\nthroughput benchmark (best-effort, %zu ops total per row)\n",
           total_ops);
    printf("Percent = cmalloc time as %% of malloc (lower is faster for cmalloc)\n");
    printf("--------------------------------------------------------------------------\n");

    for (size_t i = 0; i < sizeof(bench_sizes) / sizeof(bench_sizes[0]); i++) {
        size_t size = bench_sizes[i];
        char label[24];
        size_t ops_per_thread = total_ops;
        double malloc_1t, cmalloc_1t;
        double malloc_mt, cmalloc_mt;

        snprintf(label, sizeof(label), "size %4zu", size);

        malloc_1t = run_throughput_bench(malloc, free, 1, size, ops_per_thread);
        cmalloc_1t = run_throughput_bench(cmalloc, cfree, 1, size, ops_per_thread);
        print_bench_row(label, malloc_1t, cmalloc_1t, 1, ops_per_thread);

        ops_per_thread = total_ops / (size_t)NUM_THREADS;
        malloc_mt = run_throughput_bench(malloc, free, NUM_THREADS, size,
                                         ops_per_thread);
        cmalloc_mt = run_throughput_bench(cmalloc, cfree, NUM_THREADS, size,
                                          ops_per_thread);
        print_bench_row(label, malloc_mt, cmalloc_mt, NUM_THREADS, total_ops);
    }

    printf("--------------------------------------------------------------------------\n");
    printf("PASS\n");
    return 0;
}
