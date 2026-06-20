/**
 * @author Jonathon Lim
 */

#ifndef __SPAN_H__
#define __SPAN_H__

#include "common.h"

#define SPAN_BLOCK_BITMAP_BITS_PER_WORD 64
#define SPAN_BLOCK_BITMAP_WORD_COUNT ((MAX_SPAN_BLOCK_COUNT + SPAN_BLOCK_BITMAP_BITS_PER_WORD - 1) / SPAN_BLOCK_BITMAP_BITS_PER_WORD)
#define SPAN_BLOCK_BITMAP_WORD_INDEX(block_index) ((block_index) >> 6)
#define SPAN_BLOCK_BITMAP_BIT_INDEX(block_index)  ((block_index) & 63)
#define SPAN_BLOCK_BITMAP_BIT(block_index) \
    (UINT64_C(1) << SPAN_BLOCK_BITMAP_BIT_INDEX(block_index))

typedef struct span_struct {
    struct span_struct *phys_next;
    struct span_struct *phys_prev;
    struct span_struct *next_in_uninitialized_bin;
    void *base;
    size_t page_count;

    char is_initialized;

    int size_class_index;
    size_t block_size;
    size_t block_count;
    size_t free_count;
    uint64_t nonfull_bitmap;
    uint64_t block_bitmap[SPAN_BLOCK_BITMAP_WORD_COUNT];

    struct span_struct *next_in_size_class_bin;
} span_t;

/**
 * @brief Initializes a span given an address space.
 * Does not allocate space, space needs to be pre allocated.
 */
span_t *cmalloc_initialize_span(int size_class_index, size_t requested_size);
/**
 * @brief Uninitializes a span. Does not sys call unmap though.
 */
void cmalloc_cache_span(span_t *span);
/**
 * @brief Gets the span that a ptr belongs to.
 */
span_t *cmalloc_get_span(void *ptr);

/**
 * @brief Returns a free block and marks it as allocated.
 */
static inline void *allocate_block(span_t *span) {
    if (span->nonfull_bitmap == 0) return NULL;
    span->free_count--;

    size_t word_index = (size_t) __builtin_ctzll(span->nonfull_bitmap);
    uint64_t word = span->block_bitmap[word_index];
    size_t bit_index = (size_t) __builtin_ctzll(word);
    size_t block_index =
        bit_index + (word_index * SPAN_BLOCK_BITMAP_BITS_PER_WORD);

    span->block_bitmap[word_index] = word & ~SPAN_BLOCK_BITMAP_BIT(block_index);
    if (span->block_bitmap[word_index] == 0)
        span->nonfull_bitmap &= ~SPAN_BLOCK_BITMAP_BIT(word_index);

    return (char *) span->base + (block_index * span->block_size);
}
/**
 * @brief Frees an allocated block and marks it as free.
 */
static inline void free_block(void *block, span_t *span) {
    span->free_count++;
    uintptr_t rel_address = (uintptr_t) block - (uintptr_t) span->base;
    size_t block_index = rel_address / span->block_size;
    size_t word_index = SPAN_BLOCK_BITMAP_WORD_INDEX(block_index);

    span->block_bitmap[word_index] |= SPAN_BLOCK_BITMAP_BIT(block_index);
    span->nonfull_bitmap |= SPAN_BLOCK_BITMAP_BIT(word_index);
}

#endif