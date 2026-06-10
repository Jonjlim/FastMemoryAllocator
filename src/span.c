/**
 * @author Jonathon Lim
 */

#include "span.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "arena_manager.h"
#include "range_map.h"

static range_map_t *page_map = NULL;

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