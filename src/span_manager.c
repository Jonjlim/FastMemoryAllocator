/**
 * @author Jonathon Lim
 */

#include "span_manager.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "range_map.h"

#define SPAN_PAGE_COUNT ((PAGE_SIZE == 16384) ? 4 : \
                         (PAGE_SIZE == 4096)  ? 16 : 16)

span_t *cmalloc_initialize_span(void *ptr, size_t size, int size_class_index) {
    span_t *new = (span_t *)ptr;
    new->span_size = size;
    new->size_class_index = size_class_index;

    if (size_class_index == LARGE_CLASS_SIZE_INDEX) {
        new->block_size = size - get_span_md_size(1);
        new->block_count = 1;
    } else {
        new->block_size = SIZE_CLASSES[size_class_index];
        new->block_count = SIZE_CLASS_BLOCK_COUNT[size_class_index]; 
    }
    new->free_count = new->block_count;
    new->data_address = align_up_ptr((char *) new + get_span_md_size(new->block_count));
    cmalloc_map_range(new, get_page_index(new), get_page_index(new) + ((new->span_size + PAGE_SIZE - 1) >> PAGE_SHIFT));

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
    cmalloc_unmap_range(get_page_index(span), get_page_index(span) + ((span->span_size + PAGE_SIZE - 1) >> PAGE_SHIFT));
}

span_t *cmalloc_get_span(void *ptr) {
    return cmalloc_get(get_page_index(ptr));
}

size_t cmalloc_calculate_span_size(size_t requested_size, int size_class_index) {
    if (size_class_index != LARGE_CLASS_SIZE_INDEX) {
        return SIZE_CLASS_SPAN_SIZE[size_class_index] + align_up(get_span_md_size(SIZE_CLASS_BLOCK_COUNT[size_class_index]));
    } else {
        return ((requested_size + (BYTE_ALIGNMENT - 1)) & ~(BYTE_ALIGNMENT - 1)) + align_up(get_span_md_size(1));
    }
}