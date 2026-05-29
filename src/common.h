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
#include <stdint.h>

#define BYTE_ALIGNMENT 16
#define MAX_FREE_SPAN_COUNT 3
#define LARGE_CLASS_SIZE_INDEX -1
#define MAX_SIZE_CLASS 32768
static const size_t SIZE_CLASSES[] = {
    16,   32,   48,   64,   80,   96,   112,  128,
    144,  160,  176,  192,  208,  224,  240,  256,
    288,  320,  352,  384,  416,  448,  480,  512,
    576,  640,  704,  768,  832,  896,  960,  1024,
    1152, 1280, 1408, 1536, 1792, 2048,
    2560, 3072, 3584, 4096,
    5120, 6144, 7168, 8192,
    16384, 32768
};
static const size_t SIZE_CLASS_SPAN_SIZE[] = {
    65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536,
    65536, 65536, 65536, 65536, 65536, 65536, 65536, 65536,

    131072, 131072, 131072, 131072, 131072, 131072, 131072, 131072,
    131072, 131072, 131072, 131072, 131072, 131072, 131072, 131072,

    262144, 262144, 262144, 262144, 262144, 262144,

    524288, 524288, 524288, 524288,
    524288, 524288, 524288, 524288,

    1048576, 1048576
};
static const size_t SIZE_CLASS_BLOCK_COUNT[] = {
    4096, 2048, 1365, 1024, 819, 682, 585, 512,
    455, 409, 372, 341, 315, 292, 273, 256,
    455, 409, 372, 341, 315, 292, 273, 256,
    227, 204, 186, 170, 157, 146, 136, 128,
    227, 204, 186, 170, 146, 128,
    204, 170, 146, 128,
    102, 85, 73, 64,
    64, 32
};

#define SIZE_CLASS_COUNT \
    (sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]))

typedef struct span_struct {
    struct span_struct *next;
    struct span_struct *prev;
    size_t span_size;
    
    int size_class_index;
    size_t block_size;
    size_t block_count;

    size_t free_count;
    
    void *data_address;
    
    uint64_t nonfull_bitmap;
    uint64_t *block_bitmap;
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

/**
 * @brief Returns the ptr rounded up to the nearest 16.
 */
static inline void *align_up_ptr(void *ptr) {
   return (void *)(((intptr_t) ptr + (BYTE_ALIGNMENT - 1)) & ~((intptr_t) (BYTE_ALIGNMENT - 1)));
}

/**
 * @brief Returns the ptr rounded down to the nearest 16.
 */
static inline void *align_down_ptr(void *ptr) {
    return (void *)(((intptr_t) ptr) & ~((intptr_t) (BYTE_ALIGNMENT - 1)));
}

/**
 * @brief Returns the int rounded up to the nearest 16.
 */
static inline uint64_t align_up(uint64_t val) {
   return (val + (BYTE_ALIGNMENT - 1)) & ~(BYTE_ALIGNMENT - 1);
}

/**
 * @brief Returns the int rounded down to the nearest 16.
 */
static inline uint64_t align_down(uint64_t val) {
    return val & ~(BYTE_ALIGNMENT - 1);
}

#endif