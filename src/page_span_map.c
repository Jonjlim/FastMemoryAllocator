/**
 * @author Jonathon Lim
 */

#include "page_span_map.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

#define L1_BITS 12
#define L2_BITS 12
#define L3_BITS 11

#define L1_SIZE (1 << L1_BITS)
#define L2_SIZE (1 << L2_BITS)
#define L3_SIZE (1 << L3_BITS)

typedef struct l3_trie_node_struct {
    void *l3[L3_SIZE];
    size_t count;
} l3_node;
typedef struct l2_trie_node_struct {
    l3_node *l2[L2_SIZE];
    size_t count;
} l2_node;
static l2_node *l1[L1_SIZE];

void cmalloc_map_span(span_t *span) {
    for (size_t i = 0; i < span->span_size / PAGE_SIZE; i++) {
        u_int64_t page_index = get_page_index(span) + i;
        u_int64_t l1_index = (page_index >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
        u_int64_t l2_index = (page_index >> L3_BITS) & (L2_SIZE - 1);
        u_int64_t l3_index = (page_index) & (L3_SIZE - 1);

        if (!l1[l1_index]) {
            l1[l1_index] = mmap(NULL,
            sizeof(l2_node),
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        }
        if (!(l1[l1_index]->l2[l2_index])) {
            l1[l1_index]->l2[l2_index] = mmap(NULL,
            sizeof(l3_node),
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
            l1[l1_index]->count++;
        }
        l1[l1_index]->l2[l2_index]->count++;
        l1[l1_index]->l2[l2_index]->l3[l3_index] = span;
    }
}

void cmalloc_unmap_span(span_t *span) {
    for (size_t i = 0; i < span->span_size / PAGE_SIZE; i++) {
        u_int64_t page_index = get_page_index(span) + i;
        u_int64_t l1_index = (page_index >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
        u_int64_t l2_index = (page_index >> L3_BITS) & (L2_SIZE - 1);
        u_int64_t l3_index = (page_index) & (L3_SIZE - 1);

        assert(l1[l1_index]);
        assert(l1[l1_index]->l2[l2_index]);
        assert(l1[l1_index]->l2[l2_index]->l3[l3_index]);
        l1[l1_index]->l2[l2_index]->l3[l3_index] = NULL;
        l1[l1_index]->l2[l2_index]->count--;
        if (l1[l1_index]->l2[l2_index]->count == 0) {
            munmap(l1[l1_index]->l2[l2_index], L3_SIZE);
            l1[l1_index]->l2[l2_index] = NULL;
            l1[l1_index]->count--;
        }
        if (l1[l1_index]->count == 0) {
            munmap(l1[l1_index], L2_SIZE);
            l1[l1_index] = NULL;
        }
    }
}

span_t *cmalloc_get(u_int64_t page_index) {
    u_int64_t l1_index = (page_index >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
    u_int64_t l2_index = (page_index >> L3_BITS) & (L2_SIZE - 1);
    u_int64_t l3_index = (page_index) & (L3_SIZE - 1);

    if (!l1[l1_index] || !(l1[l1_index]->l2[l2_index])) return NULL;
    return l1[l1_index]->l2[l2_index]->l3[l3_index];
}