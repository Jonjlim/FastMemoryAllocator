/**
 * Multi-threaded rigor benchmark: same workload shape as rigor_test.c, but each
 * benchmark phase runs NUM_THREADS workers in parallel. Compares cmalloc against
 * malloc, mimalloc, jemalloc, and tcmalloc when available.
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>
#include "bench_allocators.h"

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

#ifndef OPS
#define OPS 300000
#endif

#ifndef MAX_LIVE
#define MAX_LIVE 20000
#endif

#ifndef MAX_SIZE
#define MAX_SIZE 65536
#endif

#ifndef TIME_BUDGET_SEC
#define TIME_BUDGET_SEC 4.0
#endif

#define RNG_SEED 88172645463325252ULL
#define ALIGNMENT 16
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
  alloc_fn alloc;
  free_fn dealloc;
  size_t size;
  size_t n;
  size_t rounds;
} size_bench_ctx_t;

typedef struct {
  alloc_fn alloc;
  free_fn dealloc;
  int thread_id;
  size_t chunk;
  size_t chunk_index;
} frag_ctx_t;

typedef struct {
  alloc_fn alloc;
  free_fn dealloc;
  int thread_id;
  uint64_t rng;
  size_t ops_target;
  double time_budget_sec;
} stress_ctx_t;

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

  if (r < 98) {
    return (rng_u64(state) % 65536) + 1;
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
              i,
              expected,
              p[i]);
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

static void checked_alloc(alloc_fn alloc,
                        size_t user_size,
                        live_block_t *out,
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

  memset(b->user, 0xDD, b->size);
  dealloc(b->raw);

  b->raw = NULL;
  b->user = NULL;
  b->size = 0;
  b->pattern = 0;
}

static void run_alignment_test(alloc_fn alloc, free_fn dealloc) {
  for (size_t size = 1; size <= 4096; size++) {
    void *p = alloc(size);

    if (!p) {
      fprintf(stderr, "alignment allocation failed size=%zu\n", size);
      abort();
    }

    if ((uintptr_t)p % ALIGNMENT != 0) {
      fprintf(stderr,
              "misaligned pointer %p for size %zu\n",
              p,
              size);
      abort();
    }

    memset(p, 0xAA, size);
    dealloc(p);
  }
}

static void *size_bench_worker(void *arg) {
  size_bench_ctx_t *ctx = arg;
  void **ptrs = malloc(ctx->n * sizeof(void *));

  if (!ptrs) {
    perror("malloc");
    exit(1);
  }

  for (size_t round = 0; round < ctx->rounds; round++) {
    for (size_t i = 0; i < ctx->n; i++) {
      ptrs[i] = ctx->alloc(ctx->size);

      if (!ptrs[i]) {
        fprintf(stderr, "allocation failed size %zu\n", ctx->size);
        abort();
      }

      memset(ptrs[i], 0xAB, ctx->size);
    }

    for (size_t i = 0; i < ctx->n; i++) {
      ctx->dealloc(ptrs[i]);
    }
  }

  free(ptrs);
  return NULL;
}

static void size_class_params(size_t size, size_t *n, size_t *rounds) {
  *n = MAX_LIVE / (size_t)NUM_THREADS;
  *rounds = 20;

  if (size >= 1024) {
    *n = 10000 / (size_t)NUM_THREADS;
    *rounds = 12;
  }

  if (size >= 4096) {
    *n = 3000 / (size_t)NUM_THREADS;
    *rounds = 8;
  }

  if (size >= 65536) {
    *n = 300 / (size_t)NUM_THREADS;
    *rounds = 5;
  }

  if (*n == 0) {
    *n = 1;
  }
}

static double run_size_class_bench(alloc_fn alloc, free_fn dealloc, size_t size) {
  pthread_t threads[NUM_THREADS];
  size_bench_ctx_t ctx[NUM_THREADS];
  size_t n;
  size_t rounds;
  double start;
  double elapsed;

  size_class_params(size, &n, &rounds);

  for (int i = 0; i < NUM_THREADS; i++) {
    ctx[i].alloc = alloc;
    ctx[i].dealloc = dealloc;
    ctx[i].size = size;
    ctx[i].n = n;
    ctx[i].rounds = rounds;
  }

  start = now_sec();
  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&threads[i], NULL, size_bench_worker, &ctx[i]) != 0) {
      perror("pthread_create");
      exit(1);
    }
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }
  elapsed = now_sec() - start;

  return elapsed;
}

#define FRAG_TOTAL 10000U

static void run_fragmentation_chunk(alloc_fn alloc,
                                    free_fn dealloc,
                                    size_t begin,
                                    size_t end) {
  void **small = malloc((end - begin) * sizeof(void *));
  void **large = malloc((end - begin) * sizeof(void *));

  if (!small || !large) {
    perror("malloc");
    exit(1);
  }

  for (size_t i = begin; i < end; i++) {
    small[i - begin] = alloc(32);
    large[i - begin] = alloc(4096);

    if (!small[i - begin] || !large[i - begin]) {
      fprintf(stderr, "fragmentation allocation failed\n");
      abort();
    }

    memset(small[i - begin], 0x11, 32);
    memset(large[i - begin], 0x22, 4096);
  }

  for (size_t i = begin; i < end; i += 2) {
    dealloc(small[i - begin]);
    dealloc(large[i - begin]);

    small[i - begin] = NULL;
    large[i - begin] = NULL;
  }

  for (size_t i = begin; i < end; i += 2) {
    small[i - begin] = alloc(48);
    large[i - begin] = alloc(2048);

    if (!small[i - begin] || !large[i - begin]) {
      fprintf(stderr, "fragmentation refill failed\n");
      abort();
    }

    memset(small[i - begin], 0x33, 48);
    memset(large[i - begin], 0x44, 2048);
  }

  for (size_t i = begin; i < end; i++) {
    dealloc(small[i - begin]);
    dealloc(large[i - begin]);
  }

  free(small);
  free(large);
}

static void *fragmentation_worker(void *arg) {
  frag_ctx_t *ctx = arg;
  size_t begin = ctx->chunk_index * ctx->chunk;
  size_t end = begin + ctx->chunk;

  run_fragmentation_chunk(ctx->alloc, ctx->dealloc, begin, end);
  return NULL;
}

static double run_fragmentation_test(alloc_fn alloc, free_fn dealloc) {
  pthread_t threads[NUM_THREADS];
  frag_ctx_t ctx[NUM_THREADS];
  size_t chunk = FRAG_TOTAL / (size_t)NUM_THREADS;
  double start;
  double elapsed;

  if (chunk == 0) {
    chunk = 1;
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    ctx[i].alloc = alloc;
    ctx[i].dealloc = dealloc;
    ctx[i].thread_id = i;
    ctx[i].chunk = chunk;
    ctx[i].chunk_index = (size_t)i;
  }

  start = now_sec();
  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&threads[i], NULL, fragmentation_worker, &ctx[i]) != 0) {
      perror("pthread_create");
      exit(1);
    }
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }
  elapsed = now_sec() - start;

  return elapsed;
}

static void *random_stress_worker(void *arg) {
  stress_ctx_t *ctx = arg;
  size_t max_live = MAX_LIVE / (size_t)NUM_THREADS;
  live_block_t *live;
  size_t live_count = 0;
  size_t op = 0;
  double start;

  if (max_live == 0) {
    max_live = 1;
  }

  live = calloc(max_live, sizeof(live_block_t));
  if (!live) {
    perror("calloc");
    exit(1);
  }

  start = now_sec();

  for (; op < ctx->ops_target && now_sec() - start < ctx->time_budget_sec; op++) {
    int do_alloc = live_count == 0 ||
                   (live_count < max_live && (rng_u64(&ctx->rng) % 100) < 60);

    if (do_alloc) {
      size_t idx = rng_u64(&ctx->rng) % max_live;

      while (live[idx].raw != NULL) {
        idx = (idx + 1) % max_live;
      }

      checked_alloc(ctx->alloc, rand_size(&ctx->rng), &live[idx], &ctx->rng);
      live_count++;
    } else {
      size_t idx = rng_u64(&ctx->rng) % max_live;

      while (live[idx].raw == NULL) {
        idx = (idx + 1) % max_live;
      }

      checked_free(ctx->dealloc, &live[idx]);
      live_count--;
    }

    if ((op & 0xffff) == 0) {
      for (size_t i = 0; i < max_live; i++) {
        if (live[i].raw) {
          check_canaries(&live[i]);
          check_pattern(live[i].user, live[i].size, live[i].pattern);
        }
      }
    }
  }

  for (size_t i = 0; i < max_live; i++) {
    if (live[i].raw) {
      checked_free(ctx->dealloc, &live[i]);
    }
  }

  free(live);
  return NULL;
}

static double run_random_stress(alloc_fn alloc, free_fn dealloc) {
  pthread_t threads[NUM_THREADS];
  stress_ctx_t ctx[NUM_THREADS];
  size_t ops_per_thread = (size_t)OPS / (size_t)NUM_THREADS;
  double start;
  double elapsed;

  if (ops_per_thread == 0) {
    ops_per_thread = 1;
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    ctx[i].alloc = alloc;
    ctx[i].dealloc = dealloc;
    ctx[i].thread_id = i;
    ctx[i].rng = RNG_SEED ^ (uint64_t)(i + 1) * 0x9E3779B97F4A7C15ULL;
    ctx[i].ops_target = ops_per_thread;
    ctx[i].time_budget_sec = TIME_BUDGET_SEC;
  }

  start = now_sec();
  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&threads[i], NULL, random_stress_worker, &ctx[i]) != 0) {
      perror("pthread_create");
      exit(1);
    }
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(threads[i], NULL);
  }
  elapsed = now_sec() - start;

  return elapsed;
}

static void print_bench_header(const bench_allocator_t *allocs, size_t n) {
  bench_print_table_header(allocs, n, "benchmark", 28);
}

static void print_bench_row(const char *label, size_t n, const double *secs) {
  bench_print_table_row(label, 28, n, secs);
}

int main(void) {
  bench_allocator_t allocs[BENCH_ALLOCATOR_MAX];
  size_t n_allocs = bench_allocator_list(allocs, BENCH_ALLOCATOR_MAX);
  double secs[BENCH_ALLOCATOR_MAX];

  static const size_t bench_sizes[] = {
      8, 16, 32, 64, 128, 256, 1024, 4096, 65536};

  printf("NUM_THREADS=%d OPS=%d MAX_LIVE=%d MAX_SIZE=%d TIME_BUDGET_SEC=%.2f\n",
         NUM_THREADS,
         OPS,
         MAX_LIVE,
         MAX_SIZE,
         TIME_BUDGET_SEC);
  printf("Comparing allocators against cmalloc (multi-threaded)");
#ifdef HAVE_MIMALLOC
  printf(", mimalloc");
#endif
#ifdef HAVE_JEMALLOC
  printf(", jemalloc");
#endif
#ifdef HAVE_TCMALLOC
  printf(", tcmalloc");
#endif
  printf("\n");
  bench_print_format_legend();

  printf("alignment (correctness)\n");
  run_alignment_test(malloc, free);
  run_alignment_test(cmalloc, cfree);
  printf("  malloc and cmalloc: passed\n\n");

  print_bench_header(allocs, n_allocs);

  for (size_t i = 0; i < sizeof(bench_sizes) / sizeof(bench_sizes[0]); i++) {
    size_t size = bench_sizes[i];
    char label[32];
    snprintf(label, sizeof(label), "fixed size %6zu", size);

    for (size_t a = 0; a < n_allocs; a++) {
      secs[a] = run_size_class_bench(allocs[a].alloc, allocs[a].free_fn, size);
    }
    print_bench_row(label, n_allocs, secs);
  }

  {
    for (size_t a = 0; a < n_allocs; a++) {
      secs[a] = run_fragmentation_test(allocs[a].alloc, allocs[a].free_fn);
    }
    print_bench_row("fragmentation", n_allocs, secs);
  }

  {
    for (size_t a = 0; a < n_allocs; a++) {
      secs[a] = run_random_stress(allocs[a].alloc, allocs[a].free_fn);
    }
    print_bench_row("random stress", n_allocs, secs);
  }

  return 0;
}
