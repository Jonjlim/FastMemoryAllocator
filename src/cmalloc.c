/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

#include "span_manager.h"
#include "common.h"

typedef struct bin_struct {
    struct span_struct *free_spans;
    struct span_struct *partial_spans;
    struct span_struct *full_spans;
    size_t free_span_count;
} bin_t;

static bin_t bins[SIZE_CLASS_COUNT];

void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (size_class_index != LARGE_CLASS_SIZE_INDEX) {
        if (bins[size_class_index].partial_spans) {
            span_t *span = bins[size_class_index].partial_spans;
            if (span->free_count == 1) {
                bins[size_class_index].partial_spans = span->next;
                span->next = bins[size_class_index].full_spans;
                bins[size_class_index].full_spans = span;
            }
            return allocate_block(span);
        } else if (bins[size_class_index].free_spans) {
            bins[size_class_index].free_span_count--;
            span_t *span = bins[size_class_index].free_spans;
            if (span->free_count == 1) {
                bins[size_class_index].free_spans = span->next;
                span->next = bins[size_class_index].full_spans;
                bins[size_class_index].full_spans = span;
            }
            return allocate_block(span);
        } else {
            size_t request_size = cmalloc_calculate_span_size(size, size_class_index);
            span_t *ptr = mmap(NULL,
                request_size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1,
                0);
            if (ptr == MAP_FAILED) return NULL;
            cmalloc_initialize_span(ptr, request_size, size_class_index);
            bins[size_class_index].partial_spans = ptr;
            ptr->next = NULL;
            return allocate_block(ptr);
        }
    } else {
        printf("TOO BIG OF ALLOCATION. NOT IMPLEMENTED YET\n");
        return NULL;
    }
}

void cfree(void *ptr) {
    if (ptr == NULL) return;
    span_t *span = cmalloc_get_span(ptr);
    bin_t *bin = &(bins[span->size_class_index]);
    if (span->free_count == 0 && span->block_count == 1) {
        bin->free_span_count++;
        bin->full_spans = span->next;
        span->next = bin->free_spans;
        bin->free_spans = span;
    } else if (span->free_count == 0) {
        bin->full_spans = span->next;
        span->next = bin->partial_spans;
        bin->partial_spans = span;
    } else if (span->free_count + 1 == span->free_count) {
        bin->free_span_count++;
        bin->partial_spans = span->next;
        span->next = bin->free_spans;
        bin->free_spans = span;
    }
    free_block(ptr, span);

    if (bin->free_span_count > MAX_FREE_SPAN_COUNT) {
        span = bin->free_spans;
        bin->free_spans = span->next;
        size_t size = span->span_size;
        cmalloc_uninitialize_span(span);
        munmap(span, size);
    }
}