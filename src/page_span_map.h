/**
 * @author Jonathon Lim
 * @brief maps an integer to a pointer with a radix trie implementation.
 * essentially O(1) loookup time.
 */

#ifndef __PAGE_SPAN_MAP_H__
#define __PAGE_SPAN_MAP_H__

#include "types.h"

void insert_span(span_t *span);
void remove_span(span_t *span);
span_t *get_span(u_int64_t page_index);

#endif