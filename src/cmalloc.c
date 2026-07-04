/**
 * @author Jonathon Lim
 */

#include <pthread.h>

#include <cmalloc/cmalloc.h>

#include "span.h"
#include "common.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

_Thread_local static span_t *thread_local_bin[SIZE_CLASS_COUNT];

size_t cmalloc_page_size = 0;
int cmalloc_page_shift = 0;
unsigned char cmalloc_size_class_table[SIZE_CLASS_TABLE_LEN];

pthread_mutex_t lock;

void cmalloc_runtime_init(void) {
    if (likely(cmalloc_page_size != 0)) {
        return; // Already initialized.
    }
    cmalloc_page_size = get_system_page_size();
    cmalloc_page_shift = get_system_page_shift();

    /*
     * Bucket b spans aligned sizes ((b-1)*16, b*16]; the class that serves it
     * is the smallest class whose capacity covers the bucket's largest size.
     */
    int cls = 0;
    for (size_t b = 0; b < SIZE_CLASS_TABLE_LEN; b++) {
        size_t largest_in_bucket = b << 4;
        while (cls < (int) SIZE_CLASS_COUNT
               && SIZE_CLASSES[cls] < largest_in_bucket) {
            cls++;
        }
        cmalloc_size_class_table[b] =
            (unsigned char) (cls < (int) SIZE_CLASS_COUNT ? cls : 0);
    }
}

__attribute__((constructor))
static void cmalloc_constructor(void) {
    pthread_mutex_init(&lock, NULL);
    cmalloc_runtime_init();
}

__attribute__((destructor))
static void destruct(void) {
    pthread_mutex_destroy(&lock);
}

static inline void bin_push(int size_class_index, span_t *span) {
    span_t *head = thread_local_bin[size_class_index];
    span->prev_in_size_class_bin = NULL;
    span->next_in_size_class_bin = head;
    if (head != NULL) {
        head->prev_in_size_class_bin = span;
    }
    thread_local_bin[size_class_index] = span;
}

static inline void bin_remove(int size_class_index, span_t *span) {
    span_t *prev = span->prev_in_size_class_bin;
    span_t *next = span->next_in_size_class_bin;
    if (prev != NULL) {
        prev->next_in_size_class_bin = next;
    } else {
        thread_local_bin[size_class_index] = next;
    }
    if (next != NULL) {
        next->prev_in_size_class_bin = prev;
    }
    span->prev_in_size_class_bin = NULL;
    span->next_in_size_class_bin = NULL;
}

void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (likely(size_class_index != LARGE_CLASS_SIZE_INDEX)) {
        span_t *span = thread_local_bin[size_class_index];
        if (likely(span != NULL)) {
            if (unlikely(span->free_count == 1)) {
                bin_remove(size_class_index, span);
            }
            return allocate_block(span);
        }

        pthread_mutex_lock(&lock);
        span = cmalloc_initialize_span(size_class_index, size);
        pthread_mutex_unlock(&lock);

        bin_push(size_class_index, span);
        return allocate_block(span);
    }

    pthread_mutex_lock(&lock);
    span_t *large_alloc_span = cmalloc_initialize_span(size_class_index, size);
    pthread_mutex_unlock(&lock);

    return allocate_block(large_alloc_span);
}

void cfree(void *ptr) {
    if (unlikely(ptr == NULL)) return;
    span_t *span = cmalloc_get_span(ptr);
    int size_class_index = span->size_class_index;
    if (unlikely(size_class_index == LARGE_CLASS_SIZE_INDEX)) {

        pthread_mutex_lock(&lock);
        cmalloc_cache_span(span);
        pthread_mutex_unlock(&lock);

        return;
    }

    if (unlikely(span->free_count == 0)) {
        bin_push(size_class_index, span);
    }
    free_block(ptr, span);
    if (unlikely(span->free_count == span->block_count)) {
        bin_remove(size_class_index, span);
        
        pthread_mutex_lock(&lock);
        cmalloc_cache_span(span);
        pthread_mutex_unlock(&lock);
    }
}
