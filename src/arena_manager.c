/**
 * @author Jonathon Lim
 */

#include "arena_manager.h"

#include <sys/mman.h>

#include "common.h"
#include "range_map.h"

#define META_CHUNK_SIZE (4 * 1024 * 1024)

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

    if (!meta_arena || (intptr_t) meta_arena->cur + (intptr_t) size
        > (intptr_t) meta_arena->end) {
        meta_chunk_t *prev = meta_arena;
        meta_arena = mmap(NULL,
            META_CHUNK_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
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
    if (head) {
        *cur = head;
    }
    cmalloc_map(free_list_map, ptr, size);
}

void *cmalloc_alloc_data(size_t size) {
    return mmap(NULL,
        size,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
}
void cmalloc_free_data(void *ptr, size_t size) {
    munmap(ptr, size);
}