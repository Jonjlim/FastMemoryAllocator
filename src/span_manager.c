/**
 * @author Jonathon Lim
 */

#include "span_manager.h"

#include <stdlib.h>
#include <stdio.h>

#include "page_span_map.h"

#define SPAN_PAGE_COUNT ((PAGE_SIZE == 16384) ? 4 : \
                         (PAGE_SIZE == 4096)  ? 16 : 16)

span_t *cmalloc_initialize_span(void *ptr, size_t size, int size_class_index) {
    span_t *new = (span_t *)ptr;
    new->span_size = size;
    new->size_class_index = size_class_index;

    int block_size = SIZE_CLASSES[size_class_index];
    new->block_size = block_size;
    new->block_count = (new->span_size - sizeof(span_t)) / block_size;
    new->free_count = new->block_count;
    new->free_list = NULL;

    char *block_start_address = get_span_block_start_address(new);
    for (size_t i = 0; i < new->block_count; i++) {
        free_block_t *block = (free_block_t *)(block_start_address + (i * block_size));
        block->next = new->free_list;
        new->free_list = block;
    }
    cmalloc_map_span(new);
    return new;
}

void cmalloc_uninitialize_span(span_t *span) {
    cmalloc_unmap_span(span);
}

span_t *cmalloc_get_span(void *ptr) {
    return cmalloc_get(get_page_index(ptr));
}

size_t cmalloc_calculate_span_size(size_t requested_size, int size_class_index) {
    if (size_class_index != LARGE_CLASS_SIZE_INDEX) {
        return SPAN_PAGE_COUNT * PAGE_SIZE;
    } else {
        return requested_size + sizeof(span_t);
    }
}