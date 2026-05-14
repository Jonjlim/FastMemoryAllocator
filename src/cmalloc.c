/**
 * @author Jonathon Lim
 */

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

#define SPAN_SIZE (64 * 1024)
#define SPAN_ALIGNMENT (64 * 1024)
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

typedef struct span {
    void *mmap_true_pointer;
    struct span *next;

    size_t span_size;
    size_t span_alignment;

    int size_class_index;

    size_t block_size;
    size_t block_count;

    size_t free_count;
} span_t;

bin_t bins[SIZE_CLASS_COUNT];
span_t *span_head;

/**
 * @brief Returns the span that ptr falls under.
 */
static inline span_t *get_span(void *ptr) {
    assert(ptr);
    uintptr_t ptr_num = (uintptr_t) ptr;
    ptr_num = (uintptr_t)SPAN_ALIGNMENT * (uintptr_t)floor((double)ptr_num / (double)SPAN_ALIGNMENT);
    return (span_t *)ptr_num;
}

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
 * @brief Returns the given pointer to a multiple of alignment, rounded up.
 */
static inline void *align_pointer_ceil(void *ptr, unsigned long alignment) {
    assert(alignment != 0);
    uintptr_t ptr_num = (uintptr_t) ptr;
    return (void *)((uintptr_t)alignment * (uintptr_t)ceil((double)ptr_num / (double)alignment));
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
    size_t request_size = SPAN_ALIGNMENT + SPAN_SIZE;
    void *true_pointer = mmap(NULL,
        request_size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    if (true_pointer == MAP_FAILED) return NULL;

    // Initialize span variables
    span_t *new = (span_t *)align_pointer_ceil(true_pointer, SPAN_ALIGNMENT);
    new->span_size = SPAN_SIZE;
    new->span_alignment = SPAN_ALIGNMENT;
    new->size_class_index = size_class_index;
    new->mmap_true_pointer = true_pointer;

    int block_size = SIZE_CLASSES[size_class_index];
    new->block_size = block_size;
    new->block_count = (new->span_size - sizeof(span_t)) / block_size;
    new->free_count = new->block_count;

    new->next = span_head;
    span_head = new;

    char *block_start_address = (char *)new + sizeof(span_t);
    for (size_t i = 0; i < new->block_count; i++) {
        free_block_t *block = (free_block_t *)(block_start_address + (i * block_size));
        block->next = bins[size_class_index].free_list;
        bins[size_class_index].free_list = block;
    }
    return new;
}

/**
 * @brief Allocates memory of size 1st parameter.
 * @return pointer to allocated memory.
 */
void *cmalloc(size_t size) {
    int size_class_index = get_size_class_index(size);
    if (size_class_index == -1) {
        printf("TOO BIG OF ALLOCATION. NOT IMPLEMENTED YET\n");
        return NULL;
    }
    if (bins[size_class_index].free_list == NULL) {
        allocate_new_span(size_class_index);
    }
    
    free_block_t *block = bins[size_class_index].free_list;
    get_span(block)->free_count--;
    bins[size_class_index].free_list = bins[size_class_index].free_list->next;
    return block;
}

/**
 * @brief Frees the allocated memory.
 */
void cfree(void *ptr) {
    if (ptr == NULL) return;
    span_t *span = get_span(ptr);
    span->free_count++;
    ((free_block_t *)ptr)->next = bins[span->size_class_index].free_list;
    bins[span->size_class_index].free_list = ptr;
}