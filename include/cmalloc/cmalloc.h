/**
 * @author Jonathon Lim
 */

#ifndef __CMALLOC_H__
#define __CMALLOC_H__

#include <stdlib.h>

/**
 * @brief Frees the allocated memory.
 */
void cfree(void *ptr);
/**
 * @brief Allocates memory of size 1st parameter.
 * @return pointer to allocated memory.
 */
void *cmalloc(size_t size);
void *ccalloc(size_t num, size_t size);
void *crealloc(void *ptr, size_t size);

#endif