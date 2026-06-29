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

/*
 * The page size/shift are fixed for the lifetime of the process, but the
 * POSIX query (sysconf) is a real function call. Resolving it on every
 * cfree (via PAGE_SHIFT in round_down_page_index) was a measurable per-free
 * tax. Resolve once at startup into these globals and read them thereafter.
 */
extern size_t cmalloc_page_size;
extern int cmalloc_page_shift;

#define PAGE_SIZE  cmalloc_page_size
#define PAGE_SHIFT cmalloc_page_shift
#define PAGE_INDEX_BIT_COUNT 64
#define MMAP_ACCESSABLE_BIT_SIZE 47 // Mmap only returns from the first 47 bits of address
#define MMAP_ACCESSABLE_BIT_SIZE_POST_BIT_SHIFT 35 // MMAP_ACCESSABLE_BIT_SIZE bit shifted minimum of 12
#endif

#include <stdlib.h>
#include <stdint.h>

#define BYTE_ALIGNMENT 16
#define LARGE_CLASS_SIZE_INDEX -1
#define MAX_SIZE_CLASS 32768
#define MAX_SPAN_BLOCK_COUNT 4096 // SHOULDN'T EVER BE BIGGER THAN 4096 WITHOUT SOME REFACTORING
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

/*
 * Size-class lookup table. Indexed by ceil(size / BYTE_ALIGNMENT), it maps a
 * request directly to its size-class index in O(1), replacing a linear scan
 * over all SIZE_CLASS_COUNT classes on every cmalloc. There are
 * MAX_SIZE_CLASS / BYTE_ALIGNMENT == 2048 aligned buckets, so the whole table
 * is ~2 KiB and stays warm in L1. Populated once by cmalloc_runtime_init.
 */
#define SIZE_CLASS_TABLE_LEN ((MAX_SIZE_CLASS >> 4) + 1)
extern unsigned char cmalloc_size_class_table[SIZE_CLASS_TABLE_LEN];

/**
 * @brief Resolves page size/shift and fills the size-class table. Idempotent.
 * Invoked from a library constructor so it runs before any allocation; callers
 * that cannot rely on constructor ordering may call it directly.
 */
void cmalloc_runtime_init(void);

/**
 * @brief Returns the page index of the ptr.
 */
static inline uint64_t round_down_page_index(void *ptr) {
    return ((uint64_t)ptr) >> PAGE_SHIFT;
}

/**
 * @brief Returns the page index rounded up.
 */
static inline uint64_t round_up_page_index(void *ptr) {
    return ((uint64_t)ptr + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

/**
 * @brief Rounds number up to nearest multiple of page.
 */
static inline uint64_t round_up_page(uint64_t num) {
   return (num + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

/**
 * @brief Rounds number down to nearest multiple of page.
 */
static inline uint64_t round_down_page(uint64_t num) {
   return num & ~(PAGE_SIZE - 1);
}

/**
 * @brief Returns the size class index of the given size.
 * Returns LARGE_CLASS_SIZE_INDEX if it is too big to fit in any size class.
 */
static inline int get_size_class_index(size_t size) {
    if (size > MAX_SIZE_CLASS) {
        return LARGE_CLASS_SIZE_INDEX; // Too big for any size class
    }
    return (int) cmalloc_size_class_table[(size + (BYTE_ALIGNMENT - 1)) >> 4];
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