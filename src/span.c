/**
 * @author Jonathon Lim
 */

#include "span.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

#include "metadata.h"
#include "range_map.h"

#define DATA_CHUNK_SIZE (32 * 1024 * 1024)
#define DATA_CHUNK_PAGE_COUNT (DATA_CHUNK_SIZE >> PAGE_SHIFT)
#define MAX_BINNED_PAGES 8193
#define FREE_BITMAP_WORDS ((MAX_BINNED_PAGES + 63) >> 6)
#define NO_FITTING_PAGE_SEQUENCE ((size_t)-1)
#define BITS_PER_WORD 64
#define PAGE_BITMAP_WORD_INDEX(page_count) ((page_count) >> 6)
#define PAGE_BITMAP_BIT_INDEX(page_count)  ((page_count) & 63)

typedef struct data_chunk_struct {
    char *base;
    struct data_chunk_struct *prev;
} data_chunk_t;

static range_map_t *page_map = NULL;
static data_chunk_t *data_arena;
static span_t *uninitilized_span_bin[MAX_BINNED_PAGES];
static uint64_t non_empty_bitmap[FREE_BITMAP_WORDS];

static inline void mark_binned_span_non_empty(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);
    non_empty_bitmap[word_index] |= (UINT64_C(1) << bit_index);
}

static inline void mark_binned_span_empty(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);

    non_empty_bitmap[word_index] &= ~(UINT64_C(1) << bit_index);
}

/**
 * @brief Returns the page count of the closest fitting uninitialized span.
 */
static inline size_t find_best_fitting_uninitialized_span(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);

    uint64_t word =
        non_empty_bitmap[word_index] &
        (UINT64_MAX << bit_index);

    while (1) {
        if (word != 0) {
            size_t found_bit = (size_t)__builtin_ctzll(word);
            size_t result = word_index * BITS_PER_WORD + found_bit;

            if (result < MAX_BINNED_PAGES) {
                return result;
            }

            return NO_FITTING_PAGE_SEQUENCE;
        }

        word_index++;

        if (word_index >= FREE_BITMAP_WORDS) {
            return NO_FITTING_PAGE_SEQUENCE;
        }

        word = non_empty_bitmap[word_index];
    }
}

static inline span_t *alloc_span(size_t size) {
    if (page_map == NULL) page_map = cmalloc_initialize_range_map();

    size_t page_count = round_up_page(size) >> PAGE_SHIFT;

    if (page_count >= MAX_BINNED_PAGES) {
        span_t *span = cmalloc_alloc_metadata(sizeof(span_t));
        span->base = mmap(NULL,
            page_count << PAGE_SHIFT,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        span->page_count = page_count;
        span->is_initialized = 0;
        cmalloc_map_range(page_map, span, round_down_page_index(span->base),
            round_down_page_index(span->base) + span->page_count);
        return span;
    }

    size_t best_fitting_pages = find_best_fitting_uninitialized_span(page_count);
    if (data_arena && best_fitting_pages != NO_FITTING_PAGE_SEQUENCE) {
        if (page_count == best_fitting_pages) {
            span_t *temp = uninitilized_span_bin[page_count];
            uninitilized_span_bin[page_count] = temp->next_in_uninitialized_bin;
            if (uninitilized_span_bin[page_count] == NULL)
                mark_binned_span_empty(page_count);
            return temp;
        } else {
            span_t *best_fit = uninitilized_span_bin[best_fitting_pages];
            uninitilized_span_bin[best_fitting_pages] = best_fit->next_in_uninitialized_bin;
            if (uninitilized_span_bin[best_fitting_pages] == NULL)
                mark_binned_span_empty(best_fitting_pages);
            best_fit->page_count = page_count;

            span_t *split = cmalloc_alloc_metadata(sizeof(span_t));
            split->base = best_fit->base + (page_count << PAGE_SHIFT);
            split->page_count = best_fitting_pages - page_count;
            split->is_initialized = 0;
            split->phys_next = best_fit->phys_next;
            split->phys_prev = best_fit;
            split->next_in_uninitialized_bin = uninitilized_span_bin[best_fitting_pages - page_count];
            uninitilized_span_bin[best_fitting_pages - page_count] = split;
            cmalloc_map_range(page_map, split, round_down_page_index(split->base),
                round_down_page_index(split->base) + split->page_count);
            mark_binned_span_non_empty(best_fitting_pages - page_count);

            if (best_fit->phys_next) best_fit->phys_next->phys_prev = split;
            best_fit->phys_next = split;
            return best_fit;
        }
    } else {
        data_chunk_t *prev = data_arena;
        data_arena = cmalloc_alloc_metadata(sizeof(data_chunk_t));
        data_arena->prev = prev;
        data_arena->base = mmap(NULL,
            DATA_CHUNK_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        if (data_arena->base == MAP_FAILED) {
            return NULL;
        }
        span_t *fit = cmalloc_alloc_metadata(sizeof(span_t));
        fit->base = data_arena->base;
        fit->next_in_uninitialized_bin = NULL;
        fit->page_count = page_count;
        fit->is_initialized = 0;
        cmalloc_map_range(page_map, fit, round_down_page_index(fit->base),
            round_down_page_index(fit->base) + fit->page_count);

        span_t *split = cmalloc_alloc_metadata(sizeof(span_t));
        split->base = data_arena->base + (page_count << PAGE_SHIFT);
        split->next_in_uninitialized_bin = NULL;
        split->page_count = DATA_CHUNK_PAGE_COUNT - page_count;
        split->is_initialized = 0;
        cmalloc_map_range(page_map, split, round_down_page_index(split->base),
            round_down_page_index(split->base) + split->page_count);

        fit->phys_prev = NULL;
        fit->phys_next = split;
        split->phys_prev = fit;
        split->phys_next = NULL;
        mark_binned_span_non_empty(DATA_CHUNK_PAGE_COUNT - page_count);
        uninitilized_span_bin[DATA_CHUNK_PAGE_COUNT - page_count] = split;
        return fit;
    }
}

void cmalloc_cache_span(span_t *span) {
    if (span->page_count >= MAX_BINNED_PAGES) {
        cmalloc_unmap_range(page_map, round_down_page_index(span->base),
            round_down_page_index(span->base) + span->page_count);
        munmap(span->base, span->page_count << PAGE_SHIFT);
        cmalloc_free_metadata(span, sizeof(span_t));
        return;
    }

    span->is_initialized = 0;
    span->next_in_uninitialized_bin = uninitilized_span_bin[span->page_count];
    uninitilized_span_bin[span->page_count] = span;
    mark_binned_span_non_empty(span->page_count);
}


span_t *cmalloc_initialize_span(int size_class_index, size_t requested_size) {
    span_t *new = NULL;
    if (size_class_index == LARGE_CLASS_SIZE_INDEX) {
        new = alloc_span(requested_size);
        assert(new);
        new->block_size = requested_size;
        new->block_count = 1;
    } else {
        new = alloc_span(SIZE_CLASS_SPAN_SIZE[size_class_index]);
        assert(new);
        new->block_size = SIZE_CLASSES[size_class_index];
        new->block_count = SIZE_CLASS_BLOCK_COUNT[size_class_index]; 
    }
    new->is_initialized = 1;
    new->size_class_index = size_class_index;
    new->free_count = new->block_count;

    size_t word_count =
        (new->block_count + (SPAN_BLOCK_BITMAP_BITS_PER_WORD - 1))
        / SPAN_BLOCK_BITMAP_BITS_PER_WORD;
    size_t blocks_in_last_word =
        new->block_count
        - ((word_count - 1) * SPAN_BLOCK_BITMAP_BITS_PER_WORD);

    new->nonfull_bitmap = 0;
    for (size_t i = 0; i < SPAN_BLOCK_BITMAP_WORD_COUNT; i++)
        new->block_bitmap[i] = 0;

    for (size_t word_index = 0; word_index < word_count; word_index++) {
        uint64_t free_blocks;
        if (word_index == word_count - 1
            && blocks_in_last_word < SPAN_BLOCK_BITMAP_BITS_PER_WORD) {
            free_blocks = (UINT64_C(1) << blocks_in_last_word) - UINT64_C(1);
        } else {
            free_blocks = UINT64_MAX;
        }
        new->block_bitmap[word_index] = free_blocks;
        new->nonfull_bitmap |= SPAN_BLOCK_BITMAP_BIT(word_index);
    }

    return new;
}

span_t *cmalloc_get_span(void *ptr) {
    return cmalloc_get(page_map, round_down_page_index(ptr));
}
