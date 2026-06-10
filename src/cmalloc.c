/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "span.h"
#include "common.h"

static span_t *bins[SIZE_CLASS_COUNT];

static inline void remove_span_from_bin(span_t *target, span_t **head) {
    if (*head == target) {
        *head = target->next;
    } else {
        span_t *prev = *head;
        while (prev != NULL && prev->next != target) {
            prev = prev->next;
        }
        if (prev != NULL) {
            prev->next = target->next;
        }
    }
    target->next = NULL;
}

void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (size_class_index != LARGE_CLASS_SIZE_INDEX) {
        if (bins[size_class_index]) {
            span_t *span = bins[size_class_index];
            if (span->free_count == 1) {
                bins[size_class_index] = span->next;
                span->next = NULL;
            }
            return allocate_block(span);
        } else {
            span_t *span = cmalloc_initialize_span(size_class_index, size);
            span->next = NULL;
            bins[size_class_index] = span;
            return allocate_block(span);
        }
    } else {
        span_t *large_alloc_span = cmalloc_initialize_span(size_class_index, size);
        return allocate_block(large_alloc_span);
    }
}

void cfree(void *ptr) {
    if (ptr == NULL) return;
    span_t *span = cmalloc_get_span(ptr);
    if (span->size_class_index == LARGE_CLASS_SIZE_INDEX) {
        cmalloc_uninitialize_span(span);
    } else {
        if (span->free_count == 0) {
            span->next = bins[span->size_class_index];
            bins[span->size_class_index] = span;
        }
        free_block(ptr, span);
        if (span->free_count == span->block_count) {
            remove_span_from_bin(span, &bins[span->size_class_index]);
            cmalloc_uninitialize_span(span);
        }
    }
}