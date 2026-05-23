/**
 * @author Jonathon Lim
 */

#ifndef __SPAN_MANAGER_H__
#define __SPAN_MANAGER_H__

#include "common.h"

/**
 * @brief Initializes a span given an address space.
 * Does not allocate space, space needs to be pre allocated.
 */
span_t *cmalloc_initialize_span(void *ptr, size_t size, int size_class_index);
/**
 * @brief Uninitializes a span. Does not sys call unmap though.
 */
void cmalloc_uninitialize_span(span_t *span);
/**
 * @brief Gets the span that a ptr belongs to.
 */
span_t *cmalloc_get_span(void *ptr);
/**
 * @brief Calculates the size of the span based on user block size requested.
 */
size_t cmalloc_calculate_span_size(size_t requested_size, int size_class_index);

/**
 * @brief Returns the size of a span's metadata.
 */
static inline size_t get_span_md_size(span_t *span) {
    // return sizeof(span_t) + ((size_t) 64 * (span->block_count + (size_t)63) & ~63);
    return sizeof(*span);
}
/**
 * @brief Returns the pointer to the starting data, after the spans metadata.
 */
static inline void *get_span_block_start_address(span_t *span) {
    return ((char *) span) + get_span_md_size(span);
}
/**
 * @brief Returns a free block and marks it as allocated.
 */
static inline void *allocate_block(span_t *span) {
    free_block_t *block = span->free_list;
    span->free_list = block->next;
    span->free_count--;
    return block;
}
/**
 * @brief Frees an allocated block and marks it as free.
 */
static inline void free_block(void *block, span_t *span) {
    ((free_block_t *) block)->next = span->free_list;
    span->free_list = (free_block_t *) block;
    span->free_count++;
}

#endif