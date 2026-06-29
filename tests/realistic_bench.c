/**
 * @author Jonathon Lim
 *
 * Realistic allocator benchmark.
 *
 * Goal: measure cmalloc/cfree against the system malloc/free on allocation
 * patterns that mirror how real programs use the heap, and report whether
 * cmalloc clears the project target of being at least 10% faster (i.e. its
 * wall-clock time is <= 90% of the system allocator's, a >= 1.111x speedup).
 *
 * Methodology (kept honest so the numbers are falsifiable):
 *   - Every scenario runs the SAME deterministic operation sequence against
 *     both allocators (a seeded xorshift RNG, reseeded per allocator), so the
 *     two runs differ only in the allocator under test.
 *   - Allocated memory is actually USED: each block is touched on a realistic,
 *     allocator-independent schedule (first cache line + one byte per further
 *     cache line + last byte) and read back into a sink so the compiler cannot
 *     elide the work. That memory traffic is charged identically to both
 *     allocators, so it can only push the ratio toward 1.0 (it never flatters
 *     cmalloc) -- it models real use without becoming the thing we measure.
 *   - Each scenario is timed best-of-RUNS after a warmup pass to discount
 *     first-touch page faults and cache warmup.
 *
 * The headline verdict is the geometric mean of the per-scenario speedups
 * across the whole realistic suite.
 */

#define _GNU_SOURCE

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cmalloc/cmalloc.h>

typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void *);

#ifndef BENCH_RUNS
#define BENCH_RUNS 3
#endif

/* >= 10% faster means cmalloc time <= 90% of malloc time. */
#define TARGET_SPEEDUP (1.0 / 0.90)

static volatile uint64_t g_sink;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* xorshift64; reseeded per allocator so both see an identical sequence. */
static uint64_t g_rng;
static inline void rng_seed(uint64_t s) { g_rng = s ? s : 0x9E3779B97F4A7C15ULL; }
static inline uint64_t rng_next(void) {
    uint64_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng = x;
    return x;
}

/*
 * Touch a block the way a real program would: bring in the first cache line,
 * one byte per subsequent line, and the final byte; then read a byte back so
 * the stores are observably live. Identical cost for both allocators.
 */
static inline void touch(void *p, size_t n) {
    unsigned char *c = (unsigned char *)p;
    c[0] = (unsigned char)n;
    for (size_t i = 64; i < n; i += 64) {
        c[i] = (unsigned char)i;
    }
    c[n - 1] = 0x5A;
    g_sink += c[0];
}

/* Realistic size distribution: heavily small, occasionally medium/large. */
static inline size_t realistic_size(void) {
    uint64_t r = rng_next() % 1000;
    if (r < 750) return (rng_next() % 112) + 16;     /* 16..127   : tiny structs */
    if (r < 930) return (rng_next() % 384) + 128;    /* 128..511  : buffers       */
    if (r < 990) return (rng_next() % 3584) + 512;   /* 512..4095 : medium        */
    return (rng_next() % 60000) + 4096;              /* 4096..64k : occasional big */
}

/* ------------------------------------------------------------------ */
/* Scenarios. Each returns the number of allocations it performed so   */
/* the harness can report throughput. Each frees everything it makes.  */
/* ------------------------------------------------------------------ */

/*
 * Request handler: a server processing many short requests, each of which
 * allocates a small cluster of objects, uses them, and frees them all when
 * the request completes. The dominant real-world small-object churn pattern.
 */
static uint64_t scen_request_handler(alloc_fn a, free_fn f) {
    enum { REQUESTS = 250000, PER_REQ = 6 };
    static const size_t sizes[] = {16, 24, 32, 48, 64, 96, 128, 200, 256};
    void *objs[PER_REQ];
    uint64_t ops = 0;
    for (int r = 0; r < REQUESTS; r++) {
        for (int i = 0; i < PER_REQ; i++) {
            size_t sz = sizes[rng_next() % (sizeof(sizes) / sizeof(sizes[0]))];
            objs[i] = a(sz);
            touch(objs[i], sz);
            ops++;
        }
        for (int i = 0; i < PER_REQ; i++) {
            f(objs[i]);
        }
    }
    return ops;
}

/*
 * Parse tree: allocate a large population of tiny nodes (as a JSON/AST parser
 * would), use them, then tear the whole thing down at once. Batch alloc-all
 * then free-all.
 */
static uint64_t scen_parse_tree(alloc_fn a, free_fn f) {
    enum { NODES = 800000 };
    static const size_t sizes[] = {16, 24, 32, 40, 48};
    void **nodes = malloc(NODES * sizeof(void *));
    for (int i = 0; i < NODES; i++) {
        size_t sz = sizes[rng_next() % (sizeof(sizes) / sizeof(sizes[0]))];
        nodes[i] = a(sz);
        touch(nodes[i], sz);
    }
    for (int i = 0; i < NODES; i++) {
        f(nodes[i]);
    }
    free(nodes);
    return NODES;
}

/*
 * Linked-list build / traverse / teardown: pointer-chasing data structure with
 * a uniform node size, built then walked then freed in order.
 */
typedef struct list_node { struct list_node *next; uint64_t payload[3]; } list_node;
static uint64_t scen_linked_list(alloc_fn a, free_fn f) {
    enum { N = 600000 };
    list_node *head = NULL;
    for (int i = 0; i < N; i++) {
        list_node *n = a(sizeof(list_node));
        n->next = head;
        n->payload[0] = (uint64_t)i;
        head = n;
    }
    uint64_t acc = 0;
    for (list_node *n = head; n != NULL; n = n->next) {
        acc += n->payload[0];
    }
    g_sink += acc;
    while (head != NULL) {
        list_node *next = head->next;
        f(head);
        head = next;
    }
    return N;
}

/*
 * Object pool: a steady-state pool of fixed-size objects where slots are
 * recycled constantly (connection objects, particles, ECS components). Stresses
 * the hot per-size-class reuse path.
 */
static uint64_t scen_object_pool(alloc_fn a, free_fn f) {
    enum { POOL = 2048, ITERS = 2500000, OBJ = 64 };
    void **slots = malloc(POOL * sizeof(void *));
    for (int i = 0; i < POOL; i++) {
        slots[i] = a(OBJ);
        touch(slots[i], OBJ);
    }
    uint64_t ops = POOL;
    for (int it = 0; it < ITERS; it++) {
        size_t idx = rng_next() % POOL;
        f(slots[idx]);
        slots[idx] = a(OBJ);
        touch(slots[idx], OBJ);
        ops++;
    }
    for (int i = 0; i < POOL; i++) {
        f(slots[i]);
    }
    free(slots);
    return ops;
}

/*
 * Working set: the general mixed workload. Maintain a live set, then randomly
 * allocate (with the realistic size distribution) or free, with allocation
 * favored until the set is full. Mirrors a long-running app's steady heap.
 */
static uint64_t scen_working_set(alloc_fn a, free_fn f) {
    enum { LIVE = 10000, OPS = 1000000 };
    void **live = calloc(LIVE, sizeof(void *));
    size_t *szs = calloc(LIVE, sizeof(size_t));
    size_t count = 0;
    uint64_t ops = 0;
    for (int op = 0; op < OPS; op++) {
        int do_alloc = (count == 0) || (count < LIVE && (rng_next() % 100) < 58);
        if (do_alloc) {
            size_t idx = rng_next() % LIVE;
            while (live[idx] != NULL) idx = (idx + 1) % LIVE;
            size_t sz = realistic_size();
            live[idx] = a(sz);
            szs[idx] = sz;
            touch(live[idx], sz);
            count++;
            ops++;
        } else {
            size_t idx = rng_next() % LIVE;
            while (live[idx] == NULL) idx = (idx + 1) % LIVE;
            f(live[idx]);
            live[idx] = NULL;
            count--;
        }
    }
    for (size_t i = 0; i < LIVE; i++) {
        if (live[i]) f(live[i]);
    }
    free(live);
    free(szs);
    return ops;
}

/*
 * Producer / consumer: a bounded FIFO of buffers. Producers allocate and push;
 * consumers pop the oldest and free it. Models streaming / queue pipelines.
 */
static uint64_t scen_producer_consumer(alloc_fn a, free_fn f) {
    enum { QCAP = 4096, ITEMS = 1500000 };
    void **q = calloc(QCAP, sizeof(void *));
    size_t head = 0, tail = 0, depth = 0;
    uint64_t ops = 0;
    for (int i = 0; i < ITEMS; i++) {
        size_t sz = (rng_next() % 480) + 32; /* 32..511 byte messages */
        q[tail] = a(sz);
        touch(q[tail], sz);
        tail = (tail + 1) % QCAP;
        depth++;
        ops++;
        if (depth >= QCAP - 1 || (rng_next() % 100) < 50) {
            f(q[head]);
            q[head] = NULL;
            head = (head + 1) % QCAP;
            depth--;
        }
    }
    while (depth > 0) {
        f(q[head]);
        head = (head + 1) % QCAP;
        depth--;
    }
    free(q);
    return ops;
}

/*
 * String builder: many variable-length small/medium allocations with a rolling
 * window of frees (older strings released as new ones arrive). Models text
 * processing / templating.
 */
static uint64_t scen_string_builder(alloc_fn a, free_fn f) {
    enum { WINDOW = 6000, ITEMS = 1500000 };
    void **win = calloc(WINDOW, sizeof(void *));
    uint64_t ops = 0;
    for (int i = 0; i < ITEMS; i++) {
        size_t slot = (size_t)i % WINDOW;
        if (win[slot] != NULL) f(win[slot]);
        size_t sz = (rng_next() % 504) + 8; /* 8..511 byte strings */
        win[slot] = a(sz);
        touch(win[slot], sz);
        ops++;
    }
    for (int i = 0; i < WINDOW; i++) {
        if (win[i]) f(win[i]);
    }
    free(win);
    return ops;
}

/* ------------------------------------------------------------------ */

typedef uint64_t (*scenario_fn)(alloc_fn, free_fn);

typedef struct {
    const char *name;
    scenario_fn run;
    uint64_t seed;
} scenario_t;

static double time_scenario(scenario_fn run, alloc_fn a, free_fn f,
                            uint64_t seed, uint64_t *ops_out) {
    double best = 1e300;
    uint64_t ops = 0;
    rng_seed(seed);
    ops = run(a, f); /* warmup */
    for (int r = 0; r < BENCH_RUNS; r++) {
        rng_seed(seed);
        double start = now_sec();
        ops = run(a, f);
        double elapsed = now_sec() - start;
        if (elapsed < best) best = elapsed;
    }
    *ops_out = ops;
    return best;
}

int main(void) {
    static scenario_t scenarios[] = {
        {"request_handler",    scen_request_handler,    0x1111ULL},
        {"parse_tree",         scen_parse_tree,         0x2222ULL},
        {"linked_list",        scen_linked_list,        0x3333ULL},
        {"object_pool",        scen_object_pool,        0x4444ULL},
        {"working_set",        scen_working_set,        0x5555ULL},
        {"producer_consumer",  scen_producer_consumer,  0x6666ULL},
        {"string_builder",     scen_string_builder,     0x7777ULL},
    };
    const int n = (int)(sizeof(scenarios) / sizeof(scenarios[0]));

    printf("Realistic allocator benchmark  (best of %d runs, target: cmalloc >= "
           "%.1f%% faster)\n", BENCH_RUNS, (1.0 - 0.90) * 100.0);
    printf("Speedup = malloc_time / cmalloc_time   (>= %.3fx means >=10%% faster)\n\n",
           TARGET_SPEEDUP);
    printf("%-20s %12s %12s %12s %12s   %s\n",
           "scenario", "malloc(s)", "cmalloc(s)", "malloc Mops", "cmal Mops",
           "speedup");
    printf("--------------------------------------------------------------------"
           "-------------------------\n");

    double log_speedup_sum = 0.0;
    int wins_10pct = 0;

    for (int i = 0; i < n; i++) {
        uint64_t m_ops = 0, c_ops = 0;
        double m_t = time_scenario(scenarios[i].run, malloc, free,
                                   scenarios[i].seed, &m_ops);
        double c_t = time_scenario(scenarios[i].run, cmalloc, cfree,
                                   scenarios[i].seed, &c_ops);
        double speedup = m_t / c_t;
        double m_mops = (double)m_ops / m_t / 1e6;
        double c_mops = (double)c_ops / c_t / 1e6;
        log_speedup_sum += (speedup > 0 ? log(speedup) : 0.0);
        if (speedup >= TARGET_SPEEDUP) wins_10pct++;

        printf("%-20s %12.4f %12.4f %12.1f %12.1f   %6.3fx %s\n",
               scenarios[i].name, m_t, c_t, m_mops, c_mops, speedup,
               speedup >= TARGET_SPEEDUP ? "PASS" : (speedup >= 1.0 ? "ok" : "SLOW"));
    }

    double geomean = exp(log_speedup_sum / n);
    printf("--------------------------------------------------------------------"
           "-------------------------\n");
    printf("geometric-mean speedup across %d realistic scenarios: %.3fx "
           "(cmalloc time = %.1f%% of malloc)\n",
           n, geomean, 100.0 / geomean);
    printf("scenarios clearing the >=10%%-faster bar: %d/%d\n", wins_10pct, n);

    int pass = geomean >= TARGET_SPEEDUP;
    printf("\nTARGET (>=10%% faster than system malloc, realistic mix): %s\n",
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
