/**
 * @author Jonathon Lim
 */

#ifndef __COMMON_H__
#define __COMMON_H__

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
#define PAGE_INDEX_BIT_COUNT 64
#define MMAP_ACCESSABLE_BIT_SIZE 47 // Mmap only returns from the first 47 bits of address
#define MMAP_ACCESSABLE_BIT_SIZE_POST_BIT_SHIFT 35 // MMAP_ACCESSABLE_BIT_SIZE bit shifted minimum of 12
#endif

#include <stdlib.h>

#define MAX_FREE_SPAN_COUNT 3
#define LARGE_CLASS_SIZE_INDEX -1
#define MAX_SIZE_CLASS 32768
static const size_t SIZE_CLASSES[] = {
    8, 16, 24, 32, 40, 48, 56, 64,
    80, 96, 112, 128,
    160, 192, 224, 256,
    320, 384, 448, 512,
    640, 768, 896, 1024,
    1280, 1536, 2048,
    3072, 4096, 8192, 16384, 32768
};
#define SIZE_CLASS_COUNT \
    (sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]))

typedef struct free_block_struct {
    struct free_block_struct *next;
} free_block_t;

typedef struct span_struct {
    struct span_struct *next;
    size_t span_size;
    
    int size_class_index;
    size_t block_size;
    size_t block_count;

    size_t free_count;
    
    struct free_block_struct *free_list;
} span_t;

/**
 * @brief Returns the page index of the ptr.
 */
static inline uint64_t get_page_index(void *ptr) {
    return ((uint64_t)ptr) >> PAGE_SHIFT;
}

/**
 * @brief Returns the size class index of the given size.
 * Returns LARGE_CLASS_SIZE_INDEX if it is too big to fit in any size class.
 */
static inline int get_size_class_index(size_t size) {
    for (int i = 0; i < (int) SIZE_CLASS_COUNT; i++) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return LARGE_CLASS_SIZE_INDEX; //Too big
}

#endif