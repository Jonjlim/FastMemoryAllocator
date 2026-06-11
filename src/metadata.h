/**
 * @author Jonathon Lim
 */

#ifndef __METADATA_H__
#define __METADATA_H__

#include <stdlib.h>

/**
 * @brief Allocates space for metadata of size from the metadata
 * memory arena using bump allocation. It is highly recommended
 * to keep size requests for metadata in some common interval as
 * bump allocation can't easily free data, meaning it just
 * recycles the same metadata sizes.
 */
void *cmalloc_alloc_metadata(size_t size);
/**
 * @brief Frees space from metadata from the metadata
 * memory arena by cacheing the metadata of that size in a free list.
 */
void cmalloc_free_metadata(void *ptr, size_t size);

#endif