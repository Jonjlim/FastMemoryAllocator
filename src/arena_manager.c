/**
 * @author Jonathon Lim
 */

#include "arena_manager.h"

typedef struct meta_chunk_struct {
    struct arena_struct *next;
    char *cur;
    char *end;
} meta_chunk_t;

