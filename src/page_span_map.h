/**
 * @author Jonathon Lim
 * @brief maps an integer to a pointer with a radix trie implementation.
 * essentially O(1) loookup time.
 */

#ifndef __PAGE_SPAN_MAP_H__
#define __PAGE_SPAN_MAP_H__

#include "common.h"

/**
 * @brief Maps the page_index to a span.
 */
void cmalloc_map_span(span_t *span);
/**
 * @brief Removes all mapping from page_index to the span.
 */
void cmalloc_unmap_span(span_t *span);
/**
 * @brief Gets the span that page_index is mapped to.
 */
span_t *cmalloc_get(u_int64_t page_index);

#endif