/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

#include "page_span_map.h"
#include "types.h"

#define LARGE_CLASS_SIZE_INDEX -1
#define MAX_SIZE_CLASS 32768
static const size_t SIZE_CLASSES[] = {
    8, 16, 24, 32, 40, 48, 56, 64,
    80, 96, 112, 128,
    160, 192, 224, 256,
    320, 384, 448, 512,
    640, 768, 896, 1024,
    1280, 1536, 2048,
    3072, 4096, 8192, 16384, 32768
};
#define SIZE_CLASS_COUNT \
    (sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]))

typedef struct free_block_struct {
    struct free_block_struct *next;
} free_block_t;

typedef struct bin_struct {
    struct free_block_struct *free_list;
    
    size_t block_size;
    int size_class_index;
} bin_t;

bin_t bins[SIZE_CLASS_COUNT];
span_t *span_head;

/**
 * @brief Returns the size class index of the given size.
 * Returns LARGE_CLASS_SIZE_INDEX if it is too big to fit in any size class.
 */
static inline int get_size_class_index(size_t size) {
    for (int i = 0; i < (int) SIZE_CLASS_COUNT; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return LARGE_CLASS_SIZE_INDEX; //Too big
}

/**
 * @brief Allocates space for a new span.
 * Adds new span to the span list.
 * Assigns variables already and adds free blocks to the according bin.
 * Aligns span along the address SPAN_ALIGNMENT.
 * Returns the new span.
 */
static inline span_t *allocate_new_span(int size_class_index) {
    assert((unsigned long) size_class_index < SIZE_CLASS_COUNT);

    // Get memory from system call
    size_t request_size = PAGE_SIZE * SPAN_PAGE_COUNT;
    void *true_pointer = mmap(NULL,
        request_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (true_pointer == MAP_FAILED) return NULL;

    // Initialize span variables
    span_t *new = (span_t *)true_pointer;
    new->span_size = request_size;
    new->size_class_index = size_class_index;

    int block_size = SIZE_CLASSES[size_class_index];
    new->block_size = block_size;
    new->block_count = (new->span_size - sizeof(span_t)) / block_size;
    new->free_count = new->block_count;

    new->next = span_head;
    if (span_head) span_head->prev = new;
    new->prev = NULL;
    span_head = new;

    char *block_start_address = (char *)new + sizeof(span_t);
    for (size_t i = 0; i < new->block_count; i++) {
        free_block_t *block = (free_block_t *)(block_start_address + (i * block_size));
        block->next = bins[size_class_index].free_list;
        bins[size_class_index].free_list = block;
    }
    insert_span(new);
    return new;
}

/**
 * @brief Allocates memory of size 1st parameter.
 * @return pointer to allocated memory.
 */
void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (size_class_index != -1) {
        if (bins[size_class_index].free_list == NULL) {
            allocate_new_span(size_class_index);
        }
        
        free_block_t *block = bins[size_class_index].free_list;
        get_span(get_page_index(block))->free_count--;
        bins[size_class_index].free_list = bins[size_class_index].free_list->next;
        return block;
    } else {
        printf("TOO BIG OF ALLOCATION. NOT IMPLEMENTED YET\n");
        return NULL;
    }
}

/**
 * @brief Frees the allocated memory.
 */
void cfree(void *ptr) {
    if (ptr == NULL) return;
    span_t *span = get_span(get_page_index(ptr));
    span->free_count++;
    ((free_block_t *)ptr)->next = bins[span->size_class_index].free_list;
    bins[span->size_class_index].free_list = ptr;

    if (span->free_count >= span->block_count) {
        free_block_t **block = &(bins[span->size_class_index].free_list);
        while (*block) {
            if (span == get_span(get_page_index(*block))) {
                *block = (*block)->next;
                continue;
            }
            block = &((*block)->next);
        }
        
        if (span == span_head) span_head = span->next;
        if (span->next) span->next->prev = span->prev;
        if (span->prev) span->prev->next = span->next;
        remove_span(span);
        munmap(span, span->span_size);
    }
}