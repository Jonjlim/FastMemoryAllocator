/**
 * Shared registry of allocators used by benchmark and rigor tests.
 *
 * System malloc and cmalloc are always included. mimalloc, jemalloc, and
 * tcmalloc are compiled in when HAVE_* is defined by the Makefile.
 *
 * Homebrew jemalloc replaces the malloc/free symbols, so the system baseline
 * uses dlsym(RTLD_NEXT, ...) when jemalloc is linked.
 */
#ifndef BENCH_ALLOCATORS_H
#define BENCH_ALLOCATORS_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <cmalloc/cmalloc.h>

#ifdef HAVE_MIMALLOC
#include <mimalloc.h>
#endif
#ifdef HAVE_JEMALLOC
#include <dlfcn.h>
#include <jemalloc/jemalloc.h>
#endif
#ifdef HAVE_TCMALLOC
#include <gperftools/tcmalloc.h>
#endif

typedef void *(*bench_alloc_fn)(size_t);
typedef void (*bench_free_fn)(void *);

typedef struct {
    const char *name;
    const char *col;
    bench_alloc_fn alloc;
    bench_free_fn free_fn;
} bench_allocator_t;

#define BENCH_ALLOCATOR_MAX 5

/* cmalloc is always the second entry (index 1) in bench_allocator_list(). */
#define BENCH_CMALLOC_IDX 1

static inline double bench_pct_of_cmalloc(double sec, double cmalloc_sec) {
    if (cmalloc_sec <= 0.0) {
        return 0.0;
    }
    return (sec / cmalloc_sec) * 100.0;
}

static inline void bench_print_time(double sec) {
    if (sec >= 1.0) {
        printf("%7.2fs", sec);
    } else if (sec >= 0.00005) {
        printf("%7.4fs", sec);
    } else {
        printf("%7.2es", sec);
    }
}

static inline void bench_print_pct(double sec, double cm_sec) {
    printf("%10.1f%%", bench_pct_of_cmalloc(sec, cm_sec));
}

static inline void bench_print_format_legend(void) {
    printf("cmalloc time is shown in seconds; every other allocator is shown as a\n");
    printf("percentage of cmalloc time (108.1%% => 1.081x cmalloc, i.e. slower).\n\n");
}

static inline void bench_print_table_header_ex(const bench_allocator_t *allocs,
                                               size_t n,
                                               const char *label_col,
                                               int label_width,
                                               int with_verdict) {
    printf("%-*s  %7s", label_width, label_col, "cmalloc");
    for (size_t i = 0; i < n; i++) {
        if (i == BENCH_CMALLOC_IDX) {
            continue;
        }
        printf("  %10s", allocs[i].col);
    }
    if (with_verdict) {
        printf("  verdict");
    }
    printf("\n");

    printf("%-*s  %7s", label_width, "", "seconds");
    for (size_t i = 0; i < n; i++) {
        if (i == BENCH_CMALLOC_IDX) {
            continue;
        }
        printf("  %10s", "% of cmal");
    }
    if (with_verdict) {
        printf("         ");
    }
    printf("\n");

    for (int i = 0; i < label_width + 9; i++) {
        printf("-");
    }
    for (size_t j = 0; j < n - 1; j++) {
        printf("  ----------");
    }
    if (with_verdict) {
        printf("  -------");
    }
    printf("\n");
}

static inline void bench_print_table_header(const bench_allocator_t *allocs,
                                            size_t n,
                                            const char *label_col,
                                            int label_width) {
    bench_print_table_header_ex(allocs, n, label_col, label_width, 0);
}

static inline void bench_print_table_row_ex(const char *label,
                                            int label_width,
                                            size_t n,
                                            const double *secs,
                                            const char *suffix) {
    double cm_sec = secs[BENCH_CMALLOC_IDX];
    printf("%-*s  ", label_width, label);
    bench_print_time(cm_sec);
    for (size_t i = 0; i < n; i++) {
        if (i == BENCH_CMALLOC_IDX) {
            continue;
        }
        bench_print_pct(secs[i], cm_sec);
    }
    if (suffix != NULL) {
        printf("  %s", suffix);
    }
    printf("\n");
}

static inline void bench_print_table_row(const char *label,
                                         int label_width,
                                         size_t n,
                                         const double *secs) {
    bench_print_table_row_ex(label, label_width, n, secs, NULL);
}

static inline void bench_print_dual_table_header(const bench_allocator_t *allocs,
                                                 size_t n,
                                                 const char *label_col,
                                                 int label_width,
                                                 const char *phase_a,
                                                 const char *phase_b) {
    const int phase_cols = 1 + (int)(n - 1);

    printf("%-*s", label_width, label_col);
    for (int phase = 0; phase < 2; phase++) {
        const char *name = phase == 0 ? phase_a : phase_b;
        printf("  |  %-7s cmalloc", name);
        for (size_t i = 0; i < n; i++) {
            if (i == BENCH_CMALLOC_IDX) {
                continue;
            }
            printf(" %11s", allocs[i].col);
        }
    }
    printf("\n");

    printf("%-*s", label_width, "");
    for (int phase = 0; phase < 2; phase++) {
        (void)phase;
        printf("  |  seconds");
        for (size_t i = 0; i < n; i++) {
            if (i == BENCH_CMALLOC_IDX) {
                continue;
            }
            printf("   %9s", "% of cmal");
        }
        if (phase_cols < 5) {
            printf(" ");
        }
    }
    printf("\n");

    for (int i = 0; i < label_width; i++) {
        printf("-");
    }
    for (int phase = 0; phase < 2; phase++) {
        printf("  +---------");
        for (size_t j = 0; j < n - 1; j++) {
            printf("  -----------");
        }
    }
    printf("\n");
}

static inline void bench_print_dual_table_row(const char *label,
                                              int label_width,
                                              size_t n,
                                              const double *secs_a,
                                              const double *secs_b) {
    printf("%-*s", label_width, label);
    for (int phase = 0; phase < 2; phase++) {
        const double *secs = phase == 0 ? secs_a : secs_b;
        double cm_sec = secs[BENCH_CMALLOC_IDX];
        printf("  |  ");
        bench_print_time(cm_sec);
        for (size_t i = 0; i < n; i++) {
            if (i == BENCH_CMALLOC_IDX) {
                continue;
            }
            bench_print_pct(secs[i], cm_sec);
        }
    }
    printf("\n");
}

#ifdef HAVE_JEMALLOC
static void *bench_libc_malloc(size_t size) {
    static bench_alloc_fn fn = NULL;
    if (!fn) {
        fn = (bench_alloc_fn)dlsym(RTLD_NEXT, "malloc");
    }
    return fn(size);
}

static void bench_libc_free(void *ptr) {
    static bench_free_fn fn = NULL;
    if (!fn) {
        fn = (bench_free_fn)dlsym(RTLD_NEXT, "free");
    }
    fn(ptr);
}

static void *bench_jemalloc_alloc(size_t size) {
    return malloc(size);
}

static void bench_jemalloc_free(void *ptr) {
    free(ptr);
}
#endif

static inline size_t bench_allocator_list(bench_allocator_t *out, size_t cap) {
    size_t n = 0;
#ifdef HAVE_JEMALLOC
    if (n < cap) {
        out[n++] = (bench_allocator_t){ "malloc", "malloc", bench_libc_malloc,
                                        bench_libc_free };
    }
#else
    if (n < cap)
        out[n++] = (bench_allocator_t){ "malloc", "malloc", malloc, free };
#endif
    if (n < cap)
        out[n++] = (bench_allocator_t){ "cmalloc", "cmal", cmalloc, cfree };
#ifdef HAVE_MIMALLOC
    if (n < cap)
        out[n++] =
            (bench_allocator_t){ "mimalloc", "mim", mi_malloc, mi_free };
#endif
#ifdef HAVE_JEMALLOC
    if (n < cap) {
        out[n++] = (bench_allocator_t){ "jemalloc", "jem", bench_jemalloc_alloc,
                                        bench_jemalloc_free };
    }
#endif
#ifdef HAVE_TCMALLOC
    if (n < cap)
        out[n++] =
            (bench_allocator_t){ "tcmalloc", "tcm", tc_malloc, tc_free };
#endif
    return n;
}

#endif /* BENCH_ALLOCATORS_H */
