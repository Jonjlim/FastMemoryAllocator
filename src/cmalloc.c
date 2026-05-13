/**
 * @author Jonathon Lim
 */

#define PRE_ALLOCATION_SIZE 1000000UL

#include <cmalloc/cmalloc.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>

typedef enum {
    BLOCK_FREE = 0,
    BLOCK_ALLOCATED = 1
} block_state_t;

/**
 * The metadata for a block represented in a binary search tree, ordered
 * based off size, with greater or equal blocks on the right.
 * 
 * The allocated data for each block starts after all the meta data for the
 * block. To get the data of the block, take the address of it and offset it
 * by sizeof(block_t). To get the metadata given the data, do the opposite.
 * 
 * The size variable represents the 'true' size of the struct, representing
 * how many bytes is allocated and belongs to this metadata at this structs
 * address.
 */
typedef struct block_md_struct {
    struct block_md_struct *free_parent;
    struct block_md_struct *free_left;
    struct block_md_struct *free_right;
    struct block_md_struct *next;
    struct block_md_struct *prev;
    size_t size;
    block_state_t block_state;
} block_t;

/**
 * The metadata for heap space.
 */
typedef struct heap_md_struct {
    struct block_md_struct *head;
    struct block_md_struct *free_root;
    size_t size;
} heap_t;

heap_t *heap;

/**
 * @brief Inserts the block appropriately into the tree.
 */
static inline void insert_free_block(block_t **root, block_t *block) {
    assert(root);
    assert(block);
    assert(block->block_state == BLOCK_FREE);

    block_t **curr = root;
    while (*curr != NULL) {
        assert(*curr != block);
        if (block->size < (*curr)->size) {
            curr = &((*curr)->free_left);
        } else {
            curr = &((*curr)->free_right);
        }
    }

    *curr = block;
    (*curr)->free_left = NULL;
    (*curr)->free_right = NULL;
}

/**
 * @brief Removes the block from the tree.
 */
static inline void remove_free_block(block_t **root, block_t *block) {
    assert(root && block);

    block_t **cur = root;
    while (*cur != NULL && *cur != block) {
        if (block->size < (*cur)->size) {
            cur = &((*cur)->free_left);
        } else {
            cur = &((*cur)->free_right);
        }
    }
    assert(*cur == block);
    
    block_t **successor_ptr = NULL;
    if (!(*cur)->free_left) {
        *cur = (*cur)->free_right;
    } else if (!(*cur)->free_right) {
        *cur = (*cur)->free_left;
    } else {
        successor_ptr = &((*cur)->free_right);
        while ((*successor_ptr)->free_left) successor_ptr = &((*successor_ptr)->free_left);
        block_t *successor = *successor_ptr;
        *successor_ptr = (*successor_ptr)->free_right;
        successor->free_left = (*cur)->free_left;
        if (successor != (*cur)->free_right) {
            successor->free_right = (*cur)->free_right;
        }
        *cur = successor;
    }
    block->free_left = NULL;
    block->free_left = NULL;
    block->free_right = NULL;
}

/**
 * @brief Takes a block and a target size and splits the block, resulting in a block
 * of target size, and another block of the remainder. They are both
 * inserted into the tree correctly.
 * @return Returns a pointer to the block that fits the target size;
 */
static inline block_t *fit_block(block_t *block, size_t target_size) {
    assert(block);
    assert(block->block_state == BLOCK_FREE);
    assert((block->size - sizeof(block_t) == target_size) ||
        (block->size - sizeof(block_t) > target_size + sizeof(block_t)));

    block->block_state = BLOCK_ALLOCATED;
    size_t block_virtual_size = block->size - sizeof(block_t);
    remove_free_block(&(heap->free_root), block);
    if (block_virtual_size == target_size) { 
        return block;
    }
    block_t *remainder = (block_t *)((char *)block + sizeof(block_t) + target_size);
    remainder->size = block_virtual_size - target_size;
    if (block->next) block->next->prev = remainder;
    remainder->next = block->next;
    remainder->prev = block;
    remainder->block_state = BLOCK_FREE;
    block->next = remainder;
    block->size = target_size + sizeof(block_t);
    insert_free_block(&(heap->free_root), remainder);
    return block;
}


/**
 * @brief memory of size 1st parameter.
 * @return pointer to allocated memory.
 */
void *cmalloc(size_t size) {
    if (heap == NULL) {
        heap = mmap(NULL,
            PRE_ALLOCATION_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
        heap->size = PRE_ALLOCATION_SIZE;
        heap->free_root = (block_t *)((char *)heap + sizeof(heap_t));
        heap->head = heap->free_root;
        heap->head->size = PRE_ALLOCATION_SIZE - sizeof(heap_t);
    }

    block_t *cur = heap->free_root;
    block_t *best_fit = NULL;
    while (cur != NULL) {
        if (cur->size == size + sizeof(block_t)) {
            best_fit = cur;
            break;
        }

        if (cur->size > size + (sizeof(block_t) * 2) && 
            (best_fit == NULL || cur->size - size < best_fit->size - size)) {
            best_fit = cur;
        }

        if (size < cur->size) {
            cur = cur->free_left;
        } else {
            cur = cur->free_right;
        }
    }

    block_t *biggest = heap->free_root;
    while (biggest && biggest->free_right) biggest = biggest->free_right;
    if (best_fit == NULL) {
        if (heap->free_root)
            printf("Requested size: %d   Biggest block Free: %d   Block meta data size: %d\n", (int) size, (int) biggest->size, (int) sizeof(block_t));
        printf("RAN OUT OF HEAP SPACE\n");
        return NULL;
    }
    return (void *)((char *)fit_block(best_fit, size) + sizeof(block_t));
}

/**
 * @brief Frees the allocated memory.
 */
void cfree(void *ptr) {
    if (ptr == NULL) return;
    block_t *block = (block_t *)((char *)ptr - sizeof(block_t));
    block->block_state = BLOCK_FREE;
    block->free_left = NULL;
    block->free_right = NULL;
    insert_free_block(&(heap->free_root), block);
}