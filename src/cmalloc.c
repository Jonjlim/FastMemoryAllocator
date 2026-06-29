/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include "span.h"
#include "common.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static span_t *bins[SIZE_CLASS_COUNT];

/* Runtime constants resolved once (see common.h). */
size_t cmalloc_page_size = 0;
int cmalloc_page_shift = 0;
unsigned char cmalloc_size_class_table[SIZE_CLASS_TABLE_LEN];

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
    cmalloc_runtime_init();
}

static inline void bin_push(int size_class_index, span_t *span) {
    span_t *head = bins[size_class_index];
    span->prev_in_size_class_bin = NULL;
    span->next_in_size_class_bin = head;
    if (head != NULL) {
        head->prev_in_size_class_bin = span;
    }
    bins[size_class_index] = span;
}

static inline void bin_remove(int size_class_index, span_t *span) {
    span_t *prev = span->prev_in_size_class_bin;
    span_t *next = span->next_in_size_class_bin;
    if (prev != NULL) {
        prev->next_in_size_class_bin = next;
    } else {
        bins[size_class_index] = next;
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
        span_t *span = bins[size_class_index];
        if (likely(span != NULL)) {
            // Last free block hands the span out full: drop it from the bin.
            if (unlikely(span->free_count == 1)) {
                bin_remove(size_class_index, span);
            }
            return allocate_block(span);
        }
        span = cmalloc_initialize_span(size_class_index, size);
        bin_push(size_class_index, span);
        return allocate_block(span);
    }

    span_t *large_alloc_span = cmalloc_initialize_span(size_class_index, size);
    return allocate_block(large_alloc_span);
}

void cfree(void *ptr) {
    if (unlikely(ptr == NULL)) return;
    span_t *span = cmalloc_get_span(ptr);
    int size_class_index = span->size_class_index;
    if (unlikely(size_class_index == LARGE_CLASS_SIZE_INDEX)) {
        cmalloc_cache_span(span);
        return;
    }

    // A full span (not currently binned) becomes available again on this free.
    if (unlikely(span->free_count == 0)) {
        bin_push(size_class_index, span);
    }
    free_block(ptr, span);
    if (unlikely(span->free_count == span->block_count)) {
        bin_remove(size_class_index, span);
        cmalloc_cache_span(span);
    }
}
