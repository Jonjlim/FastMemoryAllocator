/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "span.h"
#include "common.h"

typedef struct bin_struct {
    struct span_struct *free_spans;
    struct span_struct *partial_spans;
    struct span_struct *full_spans;
    size_t free_span_count;
} bin_t;

static bin_t bins[SIZE_CLASS_COUNT];

static inline span_t *remove_span(span_t *target, span_t **head) {
    if (target->prev) target->prev->next = target->next;
    else *head = target->next;
    if (target->next) target->next->prev = target->prev;
    target->next = NULL;
    target->prev = NULL;
    return target;
}

static inline void insert_span(span_t *target, span_t **head) {
    target->next = *head;
    if (target->next) target->next->prev = target;
    *head = target;
    target->prev = NULL;
}

void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (size_class_index != LARGE_CLASS_SIZE_INDEX) {
        if (bins[size_class_index].partial_spans) {
            span_t *span = bins[size_class_index].partial_spans;
            if (span->free_count == 1) {
                bins[size_class_index].partial_spans = span->next;
                if (span->next) span->next->prev = NULL;
                span->next = bins[size_class_index].full_spans;
                if (span->next) span->next->prev = span;
                bins[size_class_index].full_spans = span;
                span->prev = NULL;
            }
            return allocate_block(span);
        } else if (bins[size_class_index].free_spans) {
            span_t *span = bins[size_class_index].free_spans;
            bins[size_class_index].free_spans = span->next;
            if (span->next) span->next->prev = NULL;
            bins[size_class_index].free_span_count--;
            if (span->free_count == 1) {
                span->next = bins[size_class_index].full_spans;
                if (span->next) span->next->prev = span;
                bins[size_class_index].full_spans = span;
            } else {
                span->next = bins[size_class_index].partial_spans;
                if (span->next) span->next->prev = span;
                bins[size_class_index].partial_spans = span;
            }
            span->prev = NULL;
            return allocate_block(span);
        } else {
            span_t *span = cmalloc_initialize_span(size_class_index, size);
            span->prev = NULL;
            span->next = NULL;
            if (span->block_count == 1) {
                span->next = bins[size_class_index].full_spans;
                if (span->next) span->next->prev = span;
                bins[size_class_index].full_spans = span;
            } else {
                bins[size_class_index].partial_spans = span;
            }
            return allocate_block(span);
        }
    } else {
        span_t *large_alloc_span = cmalloc_initialize_span(size_class_index, size);
        return large_alloc_span->data_address;
    }
}

void cfree(void *ptr) {
    if (ptr == NULL) return;
    span_t *span = cmalloc_get_span(ptr);
    if (span->size_class_index == LARGE_CLASS_SIZE_INDEX) {
        cmalloc_uninitialize_span(span);
    } else {
        bin_t *bin = &(bins[span->size_class_index]);
        if (span->free_count == 0 && span->block_count == 1) {
            bin->free_span_count++;
            insert_span(remove_span(span, &(bin->full_spans)), &(bin->free_spans));
        } else if (span->free_count == 0) {
            insert_span(remove_span(span, &(bin->full_spans)), &(bin->partial_spans));
        } else if (span->free_count + 1 == span->block_count) {
            bin->free_span_count++;
            insert_span(remove_span(span, &(bin->partial_spans)), &(bin->free_spans));
        }
        free_block(ptr, span);

        if (bin->free_span_count > MAX_FREE_SPAN_COUNT) {
            span = bin->free_spans;
            bin->free_spans = span->next;
            if (span->next) span->next->prev = NULL;
            bin->free_span_count--;
            cmalloc_uninitialize_span(span);
        }
    }
}