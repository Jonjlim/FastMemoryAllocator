/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

#include "span_manager.h"
#include "page_span_map.h"
#include "common.h"

typedef struct bin_struct {
    struct span_struct *free_spans;
    struct span_struct *full_spans;
} bin_t;

static bin_t bins[SIZE_CLASS_COUNT];

void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (size_class_index != LARGE_CLASS_SIZE_INDEX) {
        if (bins[size_class_index].free_spans == NULL) {
            size_t request_size = cmalloc_calculate_span_size(size, size_class_index);
            span_t *ptr = mmap(NULL,
                request_size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0);
            if (ptr == MAP_FAILED) return NULL;
            cmalloc_initialize_span(ptr, request_size, size_class_index);
            bins[size_class_index].free_spans = ptr;
            ptr->next = NULL;
        }
        span_t *span = bins[size_class_index].free_spans;
        if (span->free_count == 1) {
            bins[size_class_index].free_spans = span->next;
            span->next = bins[size_class_index].full_spans;
            bins[size_class_index].full_spans = span;
        }
        return allocate_block(span);
    } else {
        printf("TOO BIG OF ALLOCATION. NOT IMPLEMENTED YET\n");
        return NULL;
    }
}

void cfree(void *ptr) {
    if (ptr == NULL) return;
    span_t *span = cmalloc_get_span(ptr);
    bin_t *bin = &(bins[span->size_class_index]);
    if (span->free_count == 0) {
        bin->full_spans = span->next;
        span->next = bin->free_spans;
        bin->free_spans = span;
    }
    free_block(ptr, span);
}