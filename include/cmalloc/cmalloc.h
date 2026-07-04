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
/**
 * @brief Allocates and zero-initializes num * size bytes.
 * @return pointer to allocated memory.
 */
void *ccalloc(size_t num, size_t size);
/**
 * @brief Resizes an allocation.
 * @return pointer to resized memory.
 */
void *crealloc(void *ptr, size_t size);

#endif