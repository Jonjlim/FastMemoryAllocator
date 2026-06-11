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

typedef struct page_sequence_struct {
    char *base;
    size_t page_count;
    struct page_sequence_struct *phys_next;
    struct page_sequence_struct *phys_prev;
    struct page_sequence_struct *free_next;
    char is_initialized;
} page_sequence_t;

static range_map_t *page_map = NULL;
static range_map_t *page_sequence_map;
static data_chunk_t *data_arena;
static page_sequence_t *binned_page_sequences[MAX_BINNED_PAGES];
static uint64_t non_empty_biinned_page_sequences_bitmap[FREE_BITMAP_WORDS];

static inline void mark_free_page_sequence_non_empty(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);
    non_empty_biinned_page_sequences_bitmap[word_index] |= (UINT64_C(1) << bit_index);
}

static inline void mark_free_page_sequence_empty(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);

    non_empty_biinned_page_sequences_bitmap[word_index] &= ~(UINT64_C(1) << bit_index);
}

static inline size_t find_closest_fitting_free_page_sequence(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);

    uint64_t word =
        non_empty_biinned_page_sequences_bitmap[word_index] &
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

        word = non_empty_biinned_page_sequences_bitmap[word_index];
    }
}

void *cmalloc_alloc_data(size_t size) {
    size_t page_count = round_up_page(size) >> PAGE_SHIFT;

    if (page_count >= MAX_BINNED_PAGES) return mmap(NULL,
        round_up_page(size),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);

    if (page_sequence_map == NULL) page_sequence_map = cmalloc_initialize_range_map();

    size_t best_fitting_pages = find_closest_fitting_free_page_sequence(page_count);
    if (data_arena && best_fitting_pages != NO_FITTING_PAGE_SEQUENCE) {
        if (page_count == best_fitting_pages) {
            page_sequence_t *temp = binned_page_sequences[page_count];
            binned_page_sequences[page_count] = temp->free_next;
            if (binned_page_sequences[page_count] == NULL)
                mark_free_page_sequence_empty(page_count);
            temp->is_initialized = 0;
            return temp->base;
        } else {
            page_sequence_t *best_fit = binned_page_sequences[best_fitting_pages];
            binned_page_sequences[best_fitting_pages] = best_fit->free_next;
            if (binned_page_sequences[best_fitting_pages] == NULL)
                mark_free_page_sequence_empty(best_fitting_pages);
            best_fit->is_initialized = 0;
            best_fit->page_count = page_count;

            page_sequence_t *split = cmalloc_alloc_metadata(sizeof(page_sequence_t));
            split->base = best_fit->base + (page_count << PAGE_SHIFT);
            cmalloc_map(page_sequence_map, split, round_down_page_index(split->base));
            split->page_count = best_fitting_pages - page_count;
            split->is_initialized = 1;
            split->phys_next = best_fit->phys_next;
            split->phys_prev = best_fit;
            split->free_next = binned_page_sequences[best_fitting_pages - page_count];
            binned_page_sequences[best_fitting_pages - page_count] = split;
            mark_free_page_sequence_non_empty(best_fitting_pages - page_count);

            if (best_fit->phys_next) best_fit->phys_next->phys_prev = split;
            best_fit->phys_next = split;
            return best_fit->base;
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
        page_sequence_t *r = cmalloc_alloc_metadata(sizeof(page_sequence_t));
        r->base = data_arena->base;
        cmalloc_map(page_sequence_map, r, round_down_page_index(r->base));
        r->free_next = NULL;
        r->page_count = page_count;
        r->is_initialized = 0;
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count] = cmalloc_alloc_metadata(sizeof(page_sequence_t));
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->base = data_arena->base + (page_count << PAGE_SHIFT);
        cmalloc_map(page_sequence_map, binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count],
            round_down_page_index(binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->base));
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->free_next = NULL;
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->page_count = DATA_CHUNK_PAGE_COUNT - page_count;
        r->phys_prev = NULL;
        r->phys_next = binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count];
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->phys_prev = r;
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->phys_next = NULL;
        binned_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->is_initialized = 1;
        mark_free_page_sequence_non_empty(DATA_CHUNK_PAGE_COUNT - page_count);
        return r->base;
    }
}
void cmalloc_free_data(void *ptr, size_t size) {
    size_t page_count = round_up_page(size) >> PAGE_SHIFT;

    if (page_count >= MAX_BINNED_PAGES) {
        munmap(ptr, round_up_page(size));
        return;
    }

    page_sequence_t *ps = cmalloc_get(page_sequence_map, round_down_page_index(ptr));
    ps->is_initialized = 1;
    ps->free_next = binned_page_sequences[page_count];
    binned_page_sequences[page_count] = ps;
    mark_free_page_sequence_non_empty(page_count);
}


span_t *cmalloc_initialize_span(int size_class_index, size_t requested_size) {
    if (page_map == NULL) page_map = cmalloc_initialize_range_map();
    assert(page_map);

    span_t *new = NULL;
    if (size_class_index == LARGE_CLASS_SIZE_INDEX) {
        new = (span_t *)cmalloc_alloc_metadata(get_span_md_size(1));
        new->span_size = round_up_page(requested_size);
        new->block_size = requested_size;
        new->block_count = 1;
    } else {
        new = (span_t *)cmalloc_alloc_metadata(get_span_md_size(SIZE_CLASS_BLOCK_COUNT[size_class_index]));
        new->span_size = round_up_page(SIZE_CLASS_SPAN_SIZE[size_class_index]);
        new->block_size = SIZE_CLASSES[size_class_index];
        new->block_count = SIZE_CLASS_BLOCK_COUNT[size_class_index]; 
    }
    new->size_class_index = size_class_index;
    new->data_address = cmalloc_alloc_data(new->span_size);
    new->free_count = new->block_count;
    cmalloc_map_range(page_map, new, round_down_page_index(new->data_address),
        round_down_page_index(((char *) new->data_address) + new->span_size));

    new->block_bitmap = (uint64_t *) ((char *) new + sizeof(span_t));
    size_t count = (new->block_count + (size_t) 63) & ~63;
    size_t chunk_count = count >> 6;
    assert(chunk_count <= 64);
    if (chunk_count == 64) new->nonfull_bitmap = UINT64_MAX;
    else  new->nonfull_bitmap = (1ULL << chunk_count) - 1ULL;
    if (count > new->block_count) new->block_bitmap[chunk_count - 1] = (1ULL << (new->block_count % 64)) - 1ULL;
    else new->block_bitmap[chunk_count - 1] = UINT64_MAX;
    for (size_t i = 0; i < chunk_count - 1; i++) new->block_bitmap[i] = UINT64_MAX;

    return new;
}

void cmalloc_uninitialize_span(span_t *span) {
    cmalloc_unmap_range(page_map, round_down_page_index(span->data_address),
        round_down_page_index(((char *) span->data_address) + span->span_size));
    cmalloc_free_data(span->data_address, span->span_size);
    cmalloc_free_metadata(span, get_span_md_size(span->block_count));
}

span_t *cmalloc_get_span(void *ptr) {
    return cmalloc_get(page_map, round_down_page_index(ptr));
}
