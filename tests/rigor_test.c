#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>
#include "bench_allocators.h"

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

static uint64_t rng_state = RNG_SEED;

static uint64_t rng_u64(void) {
  uint64_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng_state = x;
  return x;
}

static void rng_reset(void) { rng_state = RNG_SEED; }

static size_t rand_size(void) {
  uint64_t r = rng_u64() % 100;

  if (r < 70) {
    return (rng_u64() % 256) + 1;
  }

  if (r < 90) {
    return (rng_u64() % 4096) + 1;
  }

  if (r < 98) {
    return (rng_u64() % 65536) + 1;
  }

  return (rng_u64() % MAX_SIZE) + 1;
}

static double now_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
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

static void checked_alloc(alloc_fn alloc, size_t user_size, live_block_t *out) {
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
  out->pattern = rng_u64();

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

static double run_size_class_bench(alloc_fn alloc,
                                   free_fn dealloc,
                                   size_t size) {
  size_t n = MAX_LIVE;
  size_t rounds = 20;

  if (size >= 1024) {
    n = 10000;
    rounds = 12;
  }

  if (size >= 4096) {
    n = 3000;
    rounds = 8;
  }

  if (size >= 65536) {
    n = 300;
    rounds = 5;
  }

  void **ptrs = malloc(n * sizeof(void *));
  if (!ptrs) {
    perror("malloc");
    exit(1);
  }

  double start = now_sec();

  for (size_t round = 0; round < rounds; round++) {
    for (size_t i = 0; i < n; i++) {
      ptrs[i] = alloc(size);

      if (!ptrs[i]) {
        fprintf(stderr, "allocation failed size %zu\n", size);
        abort();
      }

      memset(ptrs[i], 0xAB, size);
    }

    for (size_t i = 0; i < n; i++) {
      dealloc(ptrs[i]);
    }
  }

  double elapsed = now_sec() - start;

  free(ptrs);
  return elapsed;
}

static double run_fragmentation_test(alloc_fn alloc, free_fn dealloc) {
  enum { N = 10000 };

  void **small = malloc(N * sizeof(void *));
  void **large = malloc(N * sizeof(void *));

  if (!small || !large) {
    perror("malloc");
    exit(1);
  }

  double start = now_sec();

  for (size_t i = 0; i < N; i++) {
    small[i] = alloc(32);
    large[i] = alloc(4096);

    if (!small[i] || !large[i]) {
      fprintf(stderr, "fragmentation allocation failed\n");
      abort();
    }

    memset(small[i], 0x11, 32);
    memset(large[i], 0x22, 4096);
  }

  for (size_t i = 0; i < N; i += 2) {
    dealloc(small[i]);
    dealloc(large[i]);

    small[i] = NULL;
    large[i] = NULL;
  }

  for (size_t i = 0; i < N; i += 2) {
    small[i] = alloc(48);
    large[i] = alloc(2048);

    if (!small[i] || !large[i]) {
      fprintf(stderr, "fragmentation refill failed\n");
      abort();
    }

    memset(small[i], 0x33, 48);
    memset(large[i], 0x44, 2048);
  }

  for (size_t i = 0; i < N; i++) {
    dealloc(small[i]);
    dealloc(large[i]);
  }

  double elapsed = now_sec() - start;

  free(small);
  free(large);
  return elapsed;
}

static double run_random_stress(alloc_fn alloc, free_fn dealloc) {
  live_block_t *live = calloc(MAX_LIVE, sizeof(live_block_t));

  if (!live) {
    perror("calloc");
    exit(1);
  }

  size_t live_count = 0;
  size_t op = 0;

  double start = now_sec();

  for (; op < OPS && now_sec() - start < TIME_BUDGET_SEC; op++) {
    int do_alloc = live_count == 0 ||
                   (live_count < MAX_LIVE && (rng_u64() % 100) < 60);

    if (do_alloc) {
      size_t idx = rng_u64() % MAX_LIVE;

      while (live[idx].raw != NULL) {
        idx = (idx + 1) % MAX_LIVE;
      }

      checked_alloc(alloc, rand_size(), &live[idx]);
      live_count++;
    } else {
      size_t idx = rng_u64() % MAX_LIVE;

      while (live[idx].raw == NULL) {
        idx = (idx + 1) % MAX_LIVE;
      }

      checked_free(dealloc, &live[idx]);
      live_count--;
    }

    if ((op & 0xffff) == 0) {
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
      checked_free(dealloc, &live[i]);
    }
  }

  double elapsed = now_sec() - start;

  free(live);
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

  printf("OPS=%d MAX_LIVE=%d MAX_SIZE=%d TIME_BUDGET_SEC=%.2f\n",
         OPS,
         MAX_LIVE,
         MAX_SIZE,
         TIME_BUDGET_SEC);
  printf("Comparing allocators against cmalloc");
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
    rng_reset();
    for (size_t a = 0; a < n_allocs; a++) {
      rng_reset();
      secs[a] = run_random_stress(allocs[a].alloc, allocs[a].free_fn);
    }
    print_bench_row("random stress", n_allocs, secs);
  }

  return 0;
}
