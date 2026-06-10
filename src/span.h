/**
 * @author Jonathon Lim
 */

#ifndef __SPAN_H__
#define __SPAN_H__

#include "common.h"

typedef struct span_struct {
    struct span_struct *next;
    size_t span_size;
    
    int size_class_index;
    size_t block_size;
    size_t block_count;

    size_t free_count;
    
    void *data_address;
    
    uint64_t nonfull_bitmap;
    uint64_t *block_bitmap;
} span_t;

/**
 * @brief Initializes a span given an address space.
 * Does not allocate space, space needs to be pre allocated.
 */
span_t *cmalloc_initialize_span(int size_class_index, size_t requested_size);
/**
 * @brief Uninitializes a span. Does not sys call unmap though.
 */
void cmalloc_uninitialize_span(span_t *span);
/**
 * @brief Gets the span that a ptr belongs to.
 */
span_t *cmalloc_get_span(void *ptr);

/**
 * @brief Returns the size of a span's metadata.
 */
static inline size_t get_span_md_size(size_t block_count) {
    return align_up(sizeof(span_t) + (((block_count + (size_t)63) & ~63) >> 3));
}
/**
 * @brief Returns a free block and marks it as allocated.
 */
static inline void *allocate_block(span_t *span) {
    if (span->nonfull_bitmap == 0) return NULL;
    span->free_count--;
    int block_bitmap_index = __builtin_ctzll(span->nonfull_bitmap);
    int rel_block_index = __builtin_ctzll(span->block_bitmap[block_bitmap_index]);
    int block_index = rel_block_index + (block_bitmap_index * 64);

    span->block_bitmap[block_bitmap_index] &= ~(1ULL << rel_block_index);
    if (span->block_bitmap[block_bitmap_index] == 0) span->nonfull_bitmap &= ~(1ULL << block_bitmap_index);

    return (char *) span->data_address + (block_index * span->block_size);
}
/**
 * @brief Frees an allocated block and marks it as free.
 */
static inline void free_block(void *block, span_t *span) {
    span->free_count++;
    uintptr_t rel_address = (uintptr_t) block - (uintptr_t) span->data_address;
    int block_index = rel_address / span->block_size;
    int rel_block_index = block_index % 64;
    int block_bitmap_index = block_index / 64;

    span->block_bitmap[block_bitmap_index] |= 1ULL << rel_block_index;
    span->nonfull_bitmap |= 1ULL << block_bitmap_index;
}

#endif