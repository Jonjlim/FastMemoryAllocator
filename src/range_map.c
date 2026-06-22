/**
 * @author Jonathon Lim
 */

#include "range_map.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "metadata.h"

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
typedef struct l1_trie_node_struct {
    l2_node *l1[L1_SIZE];
} l1_node;

range_map_t *cmalloc_initialize_range_map() {
    return cmalloc_alloc_metadata(sizeof(l1_node));
}

void cmalloc_map(range_map_t *range_map, void *value, uint64_t key) {
    l1_node *l1 = (l1_node *)range_map;
    uint64_t i = key;
    uint64_t l1_index = (i >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
    uint64_t l2_index = (i >> L3_BITS) & (L2_SIZE - 1);
    uint64_t l3_index = (i) & (L3_SIZE - 1);

    if (!(l1->l1)[l1_index]) {
        (l1->l1)[l1_index] = cmalloc_alloc_metadata(sizeof(l2_node));
        memset((l1->l1)[l1_index], 0, sizeof(l2_node));
    }
    if (!((l1->l1)[l1_index]->l2[l2_index])) {
        (l1->l1)[l1_index]->l2[l2_index] = cmalloc_alloc_metadata(sizeof(l3_node));
        memset((l1->l1)[l1_index]->l2[l2_index], 0, sizeof(l3_node));
        (l1->l1)[l1_index]->count++;
    }
    if ((l1->l1)[l1_index]->l2[l2_index]->l3[l3_index] == NULL)
        (l1->l1)[l1_index]->l2[l2_index]->count++;
    (l1->l1)[l1_index]->l2[l2_index]->l3[l3_index] = value;
}

void cmalloc_unmap(range_map_t *range_map, uint64_t key) {
    l1_node *l1 = (l1_node *)range_map;
    uint64_t i = key;
    uint64_t l1_index = (i >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
    uint64_t l2_index = (i >> L3_BITS) & (L2_SIZE - 1);
    uint64_t l3_index = (i) & (L3_SIZE - 1);

    assert((l1->l1)[l1_index]);
    assert((l1->l1)[l1_index]->l2[l2_index]);
    assert((l1->l1)[l1_index]->l2[l2_index]->l3[l3_index]);
    (l1->l1)[l1_index]->l2[l2_index]->l3[l3_index] = NULL;
    (l1->l1)[l1_index]->l2[l2_index]->count--;
    if ((l1->l1)[l1_index]->l2[l2_index]->count == 0) {
        cmalloc_free_metadata((l1->l1)[l1_index]->l2[l2_index], sizeof(l3_node));
        (l1->l1)[l1_index]->l2[l2_index] = NULL;
        (l1->l1)[l1_index]->count--;
    }
    if ((l1->l1)[l1_index]->count == 0) {
        cmalloc_free_metadata((l1->l1)[l1_index], sizeof(l2_node));
        (l1->l1)[l1_index] = NULL;
    }
}

void cmalloc_map_range(range_map_t *range_map, void *data, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        cmalloc_map(range_map, data, i);
    }
}

void cmalloc_unmap_range(range_map_t *range_map, uint64_t from, uint64_t to) {
    for (uint64_t i = from; i < to; i++) {
        cmalloc_unmap(range_map, i);
    }
}

void *cmalloc_get(range_map_t *range_map, u_int64_t key) {
    l1_node *l1 = (l1_node *)range_map;
    u_int64_t l1_index = (key >> (L2_BITS + L3_BITS)) & (L1_SIZE - 1);
    u_int64_t l2_index = (key >> L3_BITS) & (L2_SIZE - 1);
    u_int64_t l3_index = (key) & (L3_SIZE - 1);

    if (!(l1->l1)[l1_index] || !((l1->l1)[l1_index]->l2[l2_index])) return NULL;
    return (l1->l1)[l1_index]->l2[l2_index]->l3[l3_index];
}