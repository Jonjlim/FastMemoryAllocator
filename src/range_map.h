/**
 * @author Jonathon Lim
 * @brief maps an integer of at most 35 bits unsigned to a pointer with a
 * radix trie implementation. Essentially O(1) loookup time.
 */

#ifndef __RANGE_MAP_H__
#define __RANGE_MAP_H__

#include <stdlib.h>

typedef struct range_map_struct { } range_map_t;

/**
 * @brief initializes a new instance of a range_map.
 */
range_map_t *cmalloc_initialize_range_map();
/**
 * @brief uninitializes a instance of a range_map.
 */
void cmalloc_destroy_range_map(range_map_t *range_map);

/**
 * @brief Maps an integer of max 35 bits to some data.
 */
void cmalloc_map(range_map_t *range_map, void *value, uint64_t key);
/**
 * @brief Removes mapping of an integer to their data.
 */
void cmalloc_unmap(range_map_t *range_map, uint64_t key);
/**
 * @brief Maps a range of integers to some data.
 * @param to exclusive
 */
void cmalloc_map_range(range_map_t *range_map, void *data, uint64_t from, uint64_t to);
/**
 * @brief Removes all mapping of an integer range to their data.
 * @param to exclusive
 */
void cmalloc_unmap_range(range_map_t *range_map, uint64_t from, uint64_t to);
/**
 * @brief Gets the data that the key's range maps to.
 */
void *cmalloc_get(range_map_t *range_map, u_int64_t key);

#endif