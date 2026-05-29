/**
 * @author Jonathon Lim
 */

#ifndef __ARENA_MANAGER_H__
#define __ARENA_MANAGER_H__

#include <stdlib.h>

void *alloc_metadata(size_t size);
void *alloc_data(size_t size);

#endif