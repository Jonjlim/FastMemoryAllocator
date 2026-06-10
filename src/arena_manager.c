/**
 * @author Jonathon Lim
 */

#include "arena_manager.h"

#include <sys/mman.h>

#include "common.h"
#include "range_map.h"

#define META_CHUNK_SIZE (4 * 1024 * 1024)
#define DATA_CHUNK_SIZE (32 * 1024 * 1024)
#define DATA_CHUNK_PAGE_COUNT (DATA_CHUNK_SIZE >> PAGE_SHIFT)
#define MAX_BINNED_PAGES 8193
#define FREE_BITMAP_WORDS ((MAX_BINNED_PAGES + 63) >> 6)
#define NO_FITTING_PAGE_SEQUENCE ((size_t)-1)
#define BITS_PER_WORD 64
#define PAGE_BITMAP_WORD_INDEX(page_count) ((page_count) >> 6)
#define PAGE_BITMAP_BIT_INDEX(page_count)  ((page_count) & 63)

typedef struct meta_chunk_struct {
    struct meta_chunk_struct *prev;
    char *cur;
    char *end;
} meta_chunk_t;

static meta_chunk_t *meta_arena;
static range_map_t *free_list_map;

void *cmalloc_alloc_metadata(size_t size) {
    if (free_list_map) {
        void **free_list = cmalloc_get(free_list_map, size);
        if (free_list) {
            void *temp = free_list;
            cmalloc_map(free_list_map, *free_list, size);
            return temp;
        }
    }

    if (!meta_arena || (intptr_t) meta_arena->end - (intptr_t) meta_arena->cur
        < (intptr_t) size) {
        meta_chunk_t *prev = meta_arena;
        meta_arena = mmap(NULL,
            META_CHUNK_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        if (meta_arena == MAP_FAILED) {
            return NULL;
        }
        meta_arena->prev = prev;
        meta_arena->cur = align_up_ptr((char *) meta_arena + sizeof(meta_chunk_t));
        meta_arena->end = (char *) meta_arena + META_CHUNK_SIZE;
    }

    void *r = meta_arena->cur;
    meta_arena->cur =  align_up_ptr(meta_arena->cur + size);
    return r;
}

void cmalloc_free_metadata(void *ptr, size_t size) {
    if (!free_list_map) {
        free_list_map = cmalloc_initialize_range_map();
    }
    void **cur = ptr;
    void **head = cmalloc_get(free_list_map, size);
    *cur = head;
    cmalloc_map(free_list_map, ptr, size);
}

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
    char is_free;
} page_sequence_t;

static range_map_t *page_sequence_map;
static data_chunk_t *data_arena;
static page_sequence_t *free_page_sequences[MAX_BINNED_PAGES];
static uint64_t non_empty_free_pages_bitmap[FREE_BITMAP_WORDS];

static inline void mark_free_page_sequence_non_empty(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);
    non_empty_free_pages_bitmap[word_index] |= (UINT64_C(1) << bit_index);
}

static inline void mark_free_page_sequence_empty(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);

    non_empty_free_pages_bitmap[word_index] &= ~(UINT64_C(1) << bit_index);
}

static inline size_t find_closest_fitting_free_page_sequence(size_t page_count) {
    size_t word_index = PAGE_BITMAP_WORD_INDEX(page_count);
    size_t bit_index  = PAGE_BITMAP_BIT_INDEX(page_count);

    uint64_t word =
        non_empty_free_pages_bitmap[word_index] &
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

        word = non_empty_free_pages_bitmap[word_index];
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
    if (!data_arena && best_fitting_pages != NO_FITTING_PAGE_SEQUENCE) {
        if (page_count == best_fitting_pages) {
            page_sequence_t *temp = free_page_sequences[page_count];
            free_page_sequences[page_count] = temp->free_next;
            temp->is_free = 0;
            return temp->base;
        } else {
            page_sequence_t *best_fit = free_page_sequences[best_fitting_pages];
            free_page_sequences[best_fitting_pages] = best_fit->free_next;
            best_fit->is_free = 0;

            page_sequence_t *split = cmalloc_alloc_metadata(sizeof(page_sequence_t));
            split->base = best_fit->base + (page_count << PAGE_SHIFT);
            cmalloc_map(page_sequence_map, split, round_down_page_index(split->base));
            split->page_count = best_fitting_pages - page_count;
            split->is_free = 1;
            split->phys_next = best_fit->phys_next;
            split->phys_prev = best_fit;
            split->free_next = free_page_sequences[best_fitting_pages - page_count];
            free_page_sequences[best_fitting_pages - page_count] = split;

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
        r->is_free = 0;
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count] = cmalloc_alloc_metadata(sizeof(page_sequence_t));
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->base = data_arena->base + (page_count << PAGE_SHIFT);
        cmalloc_map(page_sequence_map, free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count],
            round_down_page_index(free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->base));
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->free_next = NULL;
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->page_count = DATA_CHUNK_PAGE_COUNT - page_count;
        r->phys_prev = NULL;
        r->phys_next = free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count];
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->phys_prev = free_page_sequences[page_count];
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->phys_next = NULL;
        free_page_sequences[DATA_CHUNK_PAGE_COUNT - page_count]->is_free = 1;
        mark_free_page_sequence_non_empty(DATA_CHUNK_PAGE_COUNT - page_count);
        return r->base;
    }
}
void cmalloc_free_data(void *ptr, size_t size) {
    size_t page_count = round_up_page(size) >> PAGE_SHIFT;

    if (page_count >= MAX_BINNED_PAGES) munmap(ptr, round_up_page(size));
    
    page_sequence_t *ps = cmalloc_get(page_sequence_map, round_down_page_index(ptr));
    ps->is_free = 1;
    ps->free_next = free_page_sequences[page_count];
    free_page_sequences[page_count] = ps;
}