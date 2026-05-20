/**
 * @author Jonathon Lim
 */

#ifndef __TYPES_H__
#define __TYPES_H__

#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H
#ifdef _WIN32
    #include <windows.h>
    #include <intrin.h>
#else
    #include <unistd.h>
#endif
static inline size_t get_system_page_size() {
    #ifdef _WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return (size_t)si.dwPageSize;
    #else
        return (size_t)sysconf(_SC_PAGESIZE); // POSIX standard
    #endif
}
static inline int get_system_page_shift() {
    size_t size = get_system_page_size();
    #ifdef _WIN32
        unsigned long index;
        _BitScanForward(&index, (unsigned long)size); // Find the first set bit
        return (int)index;
    #else
        return __builtin_ctz((unsigned int)size); // GCC/Clang built-in
    #endif
}
#define PAGE_SIZE  get_system_page_size()
#define PAGE_SHIFT get_system_page_shift()
#define SPAN_PAGE_COUNT ((PAGE_SIZE == 16384) ? 4 : \
                         (PAGE_SIZE == 4096)  ? 16 : 16)
#define PAGE_INDEX_BIT_COUNT 64
#define MMAP_ACCESSABLE_BIT_SIZE 47 // Mmap only returns from the first 47 bits of address
#define MMAP_ACCESSABLE_BIT_SIZE_POST_BIT_SHIFT 35 // MMAP_ACCESSABLE_BIT_SIZE bit shifted minimum of 12
#endif

#include <stdlib.h>

typedef struct span {
    struct span *next;
    struct span *prev;
    size_t span_size;

    int size_class_index;

    size_t block_size;
    size_t block_count;

    size_t free_count;
} span_t;

/**
 * @brief Returns the page index of the ptr.
 */
static inline uint64_t get_page_index(void *ptr) {
    return ((uint64_t)ptr) >> PAGE_SHIFT;
}

#endif