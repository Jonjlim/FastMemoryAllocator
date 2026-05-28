/**
 * @author Jonathon Lim
 * @brief maps an integer to a pointer with a radix trie implementation.
 * essentially O(1) loookup time.
 */

#ifndef __RANGE_MAP_H__
#define __RANGE_MAP_H__

#include "common.h"

/**
 * @brief Maps a range of integers to some data.
 * @param to exclusive
 */
void cmalloc_map_range(void *data, uint64_t from, uint64_t to);
/**
 * @brief Removes all mapping of an integer range to their data.
 * @param to exclusive
 */
void cmalloc_unmap_range(uint64_t from, uint64_t to);
/**
 * @brief Gets the data that the key's range maps to.
 */
void *cmalloc_get(u_int64_t key);

#endif