/**
 * Backend implementations for the mimalloc-bench allocator shim.
 *
 * Exactly one SHIM_BACKEND_* macro is defined per build. Every backend is
 * reached through the same shim_* entry points so that all allocators are
 * measured through an identical call structure.
 */
#define SHIM_IMPL 1
#include "alloc_shim.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#if defined(SHIM_BACKEND_CM)
#include <cmalloc/cmalloc.h>
#include "span.h"
#elif defined(SHIM_BACKEND_MI)
#include <mimalloc.h>
#elif defined(SHIM_BACKEND_TC)
#include <gperftools/tcmalloc.h>
#elif defined(SHIM_BACKEND_SYS) || defined(SHIM_BACKEND_JE)
#include <malloc/malloc.h>
#else
#error "define exactly one SHIM_BACKEND_* macro"
#endif

/* ------------------------------------------------------------------ */
/* cmalloc                                                             */
/* ------------------------------------------------------------------ */
#if defined(SHIM_BACKEND_CM)

const char *shim_backend_name(void) { return "cmalloc"; }

/*
 * cmalloc blocks live at span->base + i * block_size, so an interior pointer
 * divides back to its block. That makes over-aligned allocation possible
 * without a header: allocate slack, round the result up, and let cfree map the
 * shifted pointer back to the block it came from.
 */
static size_t cm_usable(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    span_t *span = cmalloc_get_span(ptr);
    if (span == NULL || !span->is_initialized) {
        return 0;
    }
    uintptr_t offset = (uintptr_t)ptr - (uintptr_t)span->base;
    size_t block_index = offset / span->block_size;
    uintptr_t block_start =
        (uintptr_t)span->base + block_index * span->block_size;
    return span->block_size - ((uintptr_t)ptr - block_start);
}

void  *shim_malloc(size_t size)                { return cmalloc(size); }
void   shim_free(void *ptr)                    { cfree(ptr); }
void  *shim_calloc(size_t n, size_t size)      { return ccalloc(n, size); }
size_t shim_usable_size(void *ptr)             { return cm_usable(ptr); }

void *shim_realloc(void *ptr, size_t size) {
    return crealloc(ptr, size);
}

void *shim_memalign(size_t alignment, size_t size) {
    if (alignment <= 16) {
        return cmalloc(size);
    }
    /* Guard against overflow before adding alignment slack. */
    if (size > SIZE_MAX - alignment) {
        return NULL;
    }
    void *raw = cmalloc(size + alignment - 1);
    if (raw == NULL) {
        return NULL;
    }
    uintptr_t aligned = ((uintptr_t)raw + alignment - 1) & ~(uintptr_t)(alignment - 1);
    return (void *)aligned;
}

/* ------------------------------------------------------------------ */
/* mimalloc                                                            */
/* ------------------------------------------------------------------ */
#elif defined(SHIM_BACKEND_MI)

const char *shim_backend_name(void) { return "mimalloc"; }

void  *shim_malloc(size_t size)                { return mi_malloc(size); }
void   shim_free(void *ptr)                    { mi_free(ptr); }
void  *shim_calloc(size_t n, size_t size)      { return mi_calloc(n, size); }
void  *shim_realloc(void *ptr, size_t size)    { return mi_realloc(ptr, size); }
size_t shim_usable_size(void *ptr)             { return mi_usable_size(ptr); }
void  *shim_memalign(size_t a, size_t size)    { return mi_malloc_aligned(size, a); }

/* ------------------------------------------------------------------ */
/* tcmalloc                                                            */
/* ------------------------------------------------------------------ */
#elif defined(SHIM_BACKEND_TC)

const char *shim_backend_name(void) { return "tcmalloc"; }

void  *shim_malloc(size_t size)                { return tc_malloc(size); }
void   shim_free(void *ptr)                    { tc_free(ptr); }
void  *shim_calloc(size_t n, size_t size)      { return tc_calloc(n, size); }
void  *shim_realloc(void *ptr, size_t size)    { return tc_realloc(ptr, size); }
size_t shim_usable_size(void *ptr)             { return tc_malloc_size(ptr); }
void  *shim_memalign(size_t a, size_t size)    { return tc_memalign(a, size); }

/* ------------------------------------------------------------------ */
/* system malloc, and jemalloc (which replaces malloc when linked)     */
/* ------------------------------------------------------------------ */
#else

#if defined(SHIM_BACKEND_JE)
const char *shim_backend_name(void) { return "jemalloc"; }
#else
const char *shim_backend_name(void) { return "system"; }
#endif

void  *shim_malloc(size_t size)                { return malloc(size); }
void   shim_free(void *ptr)                    { free(ptr); }
void  *shim_calloc(size_t n, size_t size)      { return calloc(n, size); }
void  *shim_realloc(void *ptr, size_t size)    { return realloc(ptr, size); }
size_t shim_usable_size(void *ptr)             { return malloc_size(ptr); }

void *shim_memalign(size_t alignment, size_t size) {
    void *out = NULL;
    if (posix_memalign(&out, alignment, size) != 0) {
        return NULL;
    }
    return out;
}

#endif

/* ------------------------------------------------------------------ */
/* Shared entry points built on the backend primitives                 */
/* ------------------------------------------------------------------ */

void *shim_aligned_alloc(size_t alignment, size_t size) {
    return shim_memalign(alignment, size);
}

void *shim_valloc(size_t size) {
    return shim_memalign(4096, size);
}

int shim_posix_memalign(void **out, size_t alignment, size_t size) {
    if (out == NULL) {
        return EINVAL;
    }
    /* POSIX requires a power-of-two multiple of sizeof(void *). */
    if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
        return EINVAL;
    }
    void *ptr = shim_memalign(alignment, size);
    if (ptr == NULL) {
        return ENOMEM;
    }
    *out = ptr;
    return 0;
}

char *shim_strdup(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *copy = shim_malloc(len);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    return copy;
}

char *shim_strndup(const char *s, size_t n) {
    if (s == NULL) {
        return NULL;
    }
    size_t len = strnlen(s, n);
    char *copy = shim_malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}
