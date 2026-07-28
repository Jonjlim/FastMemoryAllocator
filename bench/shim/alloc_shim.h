/**
 * Allocator redirection shim for the mimalloc-bench suite.
 *
 * macOS does not let a program replace libSystem's malloc at link time (unlike
 * ELF, where a definition in the executable wins over libc). C benchmarks are
 * therefore redirected at the source level: this header is force-included with
 * -include so every malloc-family call in the benchmark compiles into a call to
 * the selected backend. C++ benchmarks do not need it because operator new and
 * operator delete are replaceable at link time; see alloc_shim_new.cpp.
 *
 * The same approach is used by mimalloc's own mimalloc-override.h.
 */
#ifndef ALLOC_SHIM_H
#define ALLOC_SHIM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void  *shim_malloc(size_t size);
void   shim_free(void *ptr);
void  *shim_calloc(size_t count, size_t size);
void  *shim_realloc(void *ptr, size_t size);
char  *shim_strdup(const char *s);
char  *shim_strndup(const char *s, size_t n);
int    shim_posix_memalign(void **out, size_t alignment, size_t size);
void  *shim_aligned_alloc(size_t alignment, size_t size);
void  *shim_memalign(size_t alignment, size_t size);
void  *shim_valloc(size_t size);
size_t shim_usable_size(void *ptr);

/** Name of the backend compiled into the shim, for banner output. */
const char *shim_backend_name(void);

#ifdef __cplusplus
}
#endif

/*
 * SHIM_IMPL is defined by the shim's own translation units, which must call the
 * real functions rather than recurse into themselves.
 */
#ifndef SHIM_IMPL

#define malloc             shim_malloc
#define free               shim_free
#define calloc             shim_calloc
#define realloc            shim_realloc
#define strdup             shim_strdup
#define strndup            shim_strndup
#define posix_memalign     shim_posix_memalign
#define aligned_alloc      shim_aligned_alloc
#define memalign           shim_memalign
#define valloc             shim_valloc
#define malloc_usable_size shim_usable_size
#define malloc_size        shim_usable_size

#endif /* SHIM_IMPL */

#endif /* ALLOC_SHIM_H */
