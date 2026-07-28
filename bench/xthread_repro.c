/**
 * Minimal reproduction of the cross-thread free limitation.
 *
 * Both phases do the same volume of allocation and differ only in which thread
 * frees a block. Every handoff is mutex-protected, so the application itself
 * has no data race; any failure is inside the allocator.
 *
 * Expected on the current implementation: the control phase never fails, while
 * the cross-thread phase reports spurious NULL returns from a nearly empty
 * heap. This is what breaks larsonN, mstressN, and rptestN in mimalloc-bench.
 *
 * Build:
 *   make static
 *   cc -O2 -Iinclude bench/xthread_repro.c -Llib -lcmalloc -lpthread -o xthread_repro
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include <cmalloc/cmalloc.h>

#define THREADS  8
#define SLOTS    256
#define ROUNDS   200000
#define BLOCK_SZ 64
/* The race is timing dependent, so the cross-thread phase is repeated. */
#define ATTEMPTS 5

static atomic_long spurious_oom;
static void *shared_slot[SLOTS];
static pthread_mutex_t handoff = PTHREAD_MUTEX_INITIALIZER;

/* Control: each thread frees only what it allocated itself. */
static void *same_thread_worker(void *arg) {
    (void)arg;
    void *live[SLOTS] = { 0 };
    for (long i = 0; i < ROUNDS; i++) {
        int slot = i % SLOTS;
        if (live[slot] != NULL) {
            cfree(live[slot]);
        }
        live[slot] = cmalloc(BLOCK_SZ);
        if (live[slot] == NULL) {
            atomic_fetch_add(&spurious_oom, 1);
            return NULL;
        }
    }
    for (int i = 0; i < SLOTS; i++) {
        if (live[i] != NULL) {
            cfree(live[i]);
        }
    }
    return NULL;
}

/* Experiment: blocks are published to a shared slot, so the freeing thread is
   usually not the allocating thread. */
static void *cross_thread_worker(void *arg) {
    (void)arg;
    for (long i = 0; i < ROUNDS; i++) {
        int slot = i % SLOTS;

        pthread_mutex_lock(&handoff);
        void *taken = shared_slot[slot];
        shared_slot[slot] = NULL;
        pthread_mutex_unlock(&handoff);
        if (taken != NULL) {
            cfree(taken);
        }

        void *block = cmalloc(BLOCK_SZ);
        if (block == NULL) {
            atomic_fetch_add(&spurious_oom, 1);
            return NULL;
        }

        pthread_mutex_lock(&handoff);
        void *evicted = shared_slot[slot];
        shared_slot[slot] = block;
        pthread_mutex_unlock(&handoff);
        if (evicted != NULL) {
            cfree(evicted);
        }
    }
    return NULL;
}

static long run_phase(const char *label, void *(*worker)(void *)) {
    atomic_store(&spurious_oom, 0);
    for (int i = 0; i < SLOTS; i++) {
        shared_slot[i] = NULL;
    }

    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }
    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    long failures = atomic_load(&spurious_oom);
    printf("%-28s spurious NULL returns: %ld\n", label, failures);
    return failures;
}

int main(void) {
    long control = 0;
    long cross = 0;

    for (int i = 0; i < ATTEMPTS; i++) {
        control += run_phase("same-thread free (control)", same_thread_worker);
        cross += run_phase("cross-thread free", cross_thread_worker);
    }

    printf("\ncontrol total: %ld    cross-thread total: %ld\n", control, cross);
    if (cross > 0) {
        printf("Reproduced: cross-thread free corrupts allocator state.\n");
        return 1;
    }
    printf("No failure observed (the race is timing dependent; try again).\n");
    return 0;
}
