/**
 * operator new / operator delete replacements for the C++ benchmarks.
 *
 * Unlike malloc, the global operator new and operator delete are replaceable at
 * link time on macOS, so the C++ benchmarks route all of their allocation
 * through the selected backend without any source-level redirection. They call
 * the same shim_* entry points the C benchmarks use.
 */
#define SHIM_IMPL 1
#include "alloc_shim.h"

#include <cstdlib>
#include <new>

namespace {

inline void *shim_new(std::size_t size) {
    // operator new must never return null; retry through the new_handler and
    // throw bad_alloc when no handler can free memory, as [new.delete] requires.
    for (;;) {
        void *ptr = shim_malloc(size != 0 ? size : 1);
        if (ptr != nullptr) {
            return ptr;
        }
        std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

inline void *shim_new_nothrow(std::size_t size) noexcept {
    try {
        return shim_new(size);
    } catch (...) {
        return nullptr;
    }
}

inline void *shim_new_aligned(std::size_t size, std::align_val_t align) {
    const std::size_t alignment = static_cast<std::size_t>(align);
    for (;;) {
        void *ptr = shim_memalign(alignment, size != 0 ? size : 1);
        if (ptr != nullptr) {
            return ptr;
        }
        std::new_handler handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

} // namespace

void *operator new(std::size_t size) { return shim_new(size); }
void *operator new[](std::size_t size) { return shim_new(size); }

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    return shim_new_nothrow(size);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return shim_new_nothrow(size);
}

void operator delete(void *ptr) noexcept { shim_free(ptr); }
void operator delete[](void *ptr) noexcept { shim_free(ptr); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept { shim_free(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { shim_free(ptr); }

// Sized deallocation; larson-sized exercises this path specifically.
void operator delete(void *ptr, std::size_t) noexcept { shim_free(ptr); }
void operator delete[](void *ptr, std::size_t) noexcept { shim_free(ptr); }

// C++17 over-aligned forms.
void *operator new(std::size_t size, std::align_val_t align) {
    return shim_new_aligned(size, align);
}
void *operator new[](std::size_t size, std::align_val_t align) {
    return shim_new_aligned(size, align);
}
void *operator new(std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    try {
        return shim_new_aligned(size, align);
    } catch (...) {
        return nullptr;
    }
}
void *operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t &) noexcept {
    try {
        return shim_new_aligned(size, align);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void *ptr, std::align_val_t) noexcept { shim_free(ptr); }
void operator delete[](void *ptr, std::align_val_t) noexcept { shim_free(ptr); }
void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept { shim_free(ptr); }
void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept { shim_free(ptr); }
void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { shim_free(ptr); }
void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept { shim_free(ptr); }
