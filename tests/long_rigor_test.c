#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>

#ifndef OPS
#define OPS 2000000
#endif

#ifndef MAX_LIVE
#define MAX_LIVE 100000
#endif

#ifndef MAX_SIZE
#define MAX_SIZE 65536
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

static uint64_t rng_state = 88172645463325252ULL;

static uint64_t rng_u64(void) {
  uint64_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  rng_state = x;
  return x;
}

static size_t rand_size(void) {
  uint64_t r = rng_u64() % 100;

  if (r < 70) return (rng_u64() % 256) + 1;
  if (r < 90) return (rng_u64() % 4096) + 1;
  if (r < 98) return (rng_u64() % 65536) + 1;
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

static void *checked_alloc(alloc_fn alloc, size_t user_size, live_block_t *out) {
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

  return user;
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

static void run_random_stress(const char *name, alloc_fn alloc, free_fn dealloc) {
  live_block_t *live = calloc(MAX_LIVE, sizeof(live_block_t));
  if (!live) {
    perror("calloc");
    exit(1);
  }

  size_t live_count = 0;
  size_t allocs = 0;
  size_t frees = 0;
  size_t peak_live = 0;

  double start = now_sec();

  for (size_t op = 0; op < OPS; op++) {
    int do_alloc = live_count == 0 || 
                   (live_count < MAX_LIVE && (rng_u64() % 100) < 60);

    if (do_alloc) {
      size_t idx = rng_u64() % MAX_LIVE;

      while (live[idx].raw != NULL) {
        idx = (idx + 1) % MAX_LIVE;
      }

      checked_alloc(alloc, rand_size(), &live[idx]);
      live_count++;
      allocs++;

      if (live_count > peak_live) {
        peak_live = live_count;
      }
    } else {
      size_t idx = rng_u64() % MAX_LIVE;

      while (live[idx].raw == NULL) {
        idx = (idx + 1) % MAX_LIVE;
      }

      checked_free(dealloc, &live[idx]);
      live_count--;
      frees++;
    }

    if ((op & 0x3fff) == 0) {
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
      frees++;
    }
  }

  double end = now_sec();

  printf("%-16s random stress: %.3f sec | allocs=%zu frees=%zu peak_live=%zu\n",
         name, end - start, allocs, frees, peak_live);

  free(live);
}

static void run_size_class_bench(const char *name,
                                 alloc_fn alloc,
                                 free_fn dealloc,
                                 size_t size) {
  void **ptrs = malloc(MAX_LIVE * sizeof(void *));
  if (!ptrs) {
    perror("malloc");
    exit(1);
  }

  double start = now_sec();

  for (size_t round = 0; round < 50; round++) {
    for (size_t i = 0; i < MAX_LIVE; i++) {
      ptrs[i] = alloc(size);
      if (!ptrs[i]) {
        fprintf(stderr, "%s failed size %zu\n", name, size);
        abort();
      }
      memset(ptrs[i], 0xAB, size);
    }

    for (size_t i = 0; i < MAX_LIVE; i++) {
      dealloc(ptrs[i]);
    }
  }

  double end = now_sec();

  printf("%-16s fixed size %6zu: %.3f sec\n", name, size, end - start);

  free(ptrs);
}

static void run_fragmentation_test(const char *name,
                                   alloc_fn alloc,
                                   free_fn dealloc) {
  enum { N = 50000 };
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
  }

  for (size_t i = 0; i < N; i++) {
    dealloc(small[i]);
    dealloc(large[i]);
  }

  double end = now_sec();

  printf("%-16s fragmentation: %.3f sec\n", name, end - start);

  free(small);
  free(large);
}

static void run_alignment_test(const char *name, alloc_fn alloc, free_fn dealloc) {
  for (size_t size = 1; size <= 4096; size++) {
    void *p = alloc(size);

    if (!p) {
      fprintf(stderr, "%s failed alignment allocation size=%zu\n", name, size);
      abort();
    }

    if ((uintptr_t)p % _Alignof(max_align_t) != 0) {
      fprintf(stderr,
              "%s returned misaligned pointer %p for size %zu\n",
              name, p, size);
      abort();
    }

    memset(p, 0xAA, size);
    dealloc(p);
  }

  printf("%-16s alignment: passed\n", name);
}

static void run_all(const char *name, alloc_fn alloc, free_fn dealloc) {
  printf("\n=== %s ===\n", name);

  run_alignment_test(name, alloc, dealloc);

  run_size_class_bench(name, alloc, dealloc, 8);
  run_size_class_bench(name, alloc, dealloc, 16);
  run_size_class_bench(name, alloc, dealloc, 32);
  run_size_class_bench(name, alloc, dealloc, 64);
  run_size_class_bench(name, alloc, dealloc, 128);
  run_size_class_bench(name, alloc, dealloc, 256);
  run_size_class_bench(name, alloc, dealloc, 1024);
  run_size_class_bench(name, alloc, dealloc, 4096);
  run_size_class_bench(name, alloc, dealloc, 65536);

  run_fragmentation_test(name, alloc, dealloc);
  run_random_stress(name, alloc, dealloc);
}

int main(void) {
  printf("OPS=%d MAX_LIVE=%d MAX_SIZE=%d\n", OPS, MAX_LIVE, MAX_SIZE);

  run_all("stdlib", malloc, free);
  run_all("cmalloc", cmalloc, cfree);

  return 0;
}