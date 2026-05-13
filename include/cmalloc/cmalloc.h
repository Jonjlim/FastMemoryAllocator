/**
 * @author Jonathon Lim
 */

#ifndef __CMALLOC_H__
#define __CMALLOC_H__

#include <stdlib.h>

void cfree(void *ptr);
void *cmalloc(size_t size);
void *ccalloc(size_t num, size_t size);
void *crealloc(void *ptr, size_t size);

#endif