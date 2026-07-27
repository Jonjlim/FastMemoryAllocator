# FastMemoryAllocator

A custom C memory allocator (`cmalloc` / `ccalloc` / `crealloc` / `cfree`) built from scratch — size classes, span reuse, bitmap free-lists, a page-indexed radix trie for `cfree`, and per-thread fast paths with a global lock on slow paths.

Written as a systems-programming project to explore allocator design trade-offs and measure them against `malloc`, mimalloc, jemalloc, and tcmalloc.

## Why this project

Resume-friendly signal this codebase is meant to show:

- **Low-level systems design** — `mmap` arenas, page math, span splitting, metadata separated from user data
- **Performance-conscious data structures** — O(1) size-class lookup, bitmap + `ctz` free-block search, three-level page→span radix trie (no per-block headers)
- **Concurrency** — `_Thread_local` size-class bins on the hot path; a single mutex only for span creation, large allocs, and cache returns
- **API completeness** — `realloc`-correct `crealloc`, overflow-safe `ccalloc`, static and shared library builds
- **Measurement** — rigor stress tests and a micro-benchmark that isolates alloc/free path cost

## Features

- 16-byte aligned allocations; fixed size classes from 16 B through 32 KiB
- Per-thread size-class bins of non-full spans for fast small alloc/free
- Bitmap-managed blocks inside each span (`__builtin_ctzll` for first-free)
- Page-index range map so `cfree` finds the owning span without in-object headers
- Separate metadata arena (`mmap`), bump-allocated and size-keyed free-list cached
- Fully freed small spans cached in page-count bins and split on reuse
- Multi-thread support: lock-free local bins; global `pthread_mutex_t` on slow paths
- Optional Homebrew detection for mimalloc / jemalloc / tcmalloc in benchmarks

## Architecture

```
cmalloc(size)
    │
    ├─ size ≤ 32 KiB ──► size-class table (O(1))
    │                        │
    │                        ▼
    │                   thread-local bin ── hit ──► take block from span bitmap
    │                        │ miss
    │                        ▼
    │                   global lock → new/cached span → push into local bin
    │
    └─ size > 32 KiB ──► global lock → one-block large span

cfree(ptr)
    │
    ▼
page index → range map (radix trie) → span_t
    │
    ├─ mark block free in bitmap
    ├─ if span was full → reinsert into thread-local bin
    └─ if span fully free → global lock → cache or munmap
```

### Size classes and bins

Defined in `src/common.h`: alignment is 16 bytes; classes run from 16 through 32768 bytes. Each class has a configured span size (64 KiB–1 MiB) and a precomputed block count. A startup lookup table maps every 16-byte-aligned bucket to a class index in O(1) (no linear scan on each alloc).

A 100-byte request lands in the 112-byte class. Bins hold only spans with free blocks; full spans are removed so the common alloc path stays short.

### Spans and bitmaps

A span is a contiguous page range divided into fixed-size blocks. Metadata tracks base pointer, page count, size class, free count, doubly linked bin links, a free-block bitmap, and a `nonfull_bitmap` of words that still have free bits.

Alloc: find first non-empty word with `__builtin_ctzll`, clear a bit, return `base + index * block_size`.  
Free: set the bit from the pointer’s block index; if the span was full, reinsert it into the thread-local bin. Doubly linked bins make remove/insert O(1).

### Page-to-span lookup

`cfree` must recover the owning span from an arbitrary pointer. `src/range_map.c` implements a three-level radix trie keyed by page index. On create/split, every page in the span is mapped to `span_t`. On free, the pointer is shifted by the cached page size to get a page index and the map returns the span — no headers in user memory.

Page size / shift are resolved once at startup (`cmalloc_runtime_init`) so the free path never calls `sysconf`.

### Backing memory and reuse

User data comes from 32 MiB `mmap` chunks, carved into spans on demand. Free uninitialized spans live in page-count bins (bitmap of non-empty bins → smallest fit). Oversized cached spans are split; remainder goes back into the pool. Spans at or above `MAX_BINNED_PAGES` use a direct `mmap` and are unmapped on free.

### Metadata

`src/metadata.c` keeps allocator structures (spans, trie nodes) in separate 4 MiB `mmap` chunks: bump allocate, 16-byte align, and recycle by object size via another range-map-backed free list. User heaps never share pages with metadata.

### Thread safety

1. **Per-thread bins** — `_Thread_local` span lists; repeated small alloc/free on a warm bin needs no lock.
2. **Global mutex** — span init when the local bin misses, all large allocations, and returns to the global free-span pool.

This is a deliberate minimal scheme, not a full multi-arena design. Range-map updates run under the mutex during create/cache; lookups on the free path do not take the lock.

### `ccalloc` and `crealloc`

Both sit on top of the same paths in `src/cmalloc.c`:

- `ccalloc` rejects `num * size` overflow, allocates, then zero-fills exactly the requested bytes (not size-class slack).
- `crealloc` matches `realloc`: `NULL` → alloc; size 0 → free; same size class → return `ptr` with no copy; otherwise allocate-copy-free. OOM leaves the original pointer intact. No in-place expansion across adjacent free blocks.

## Repository layout

| Path | Role |
|------|------|
| `include/cmalloc/cmalloc.h` | Public API |
| `src/cmalloc.c` | Alloc / free / calloc / realloc + threading |
| `src/span.c` / `span.h` | Spans and block bitmaps |
| `src/range_map.c` / `range_map.h` | Page-index → span radix trie |
| `src/metadata.c` / `metadata.h` | Allocator-internal metadata arena |
| `src/common.h` | Size classes, alignment, page helpers |
| `tests/` | Debug, rigor, thread-rigor, micro-benchmark |

## Build

```sh
make          # static + shared → lib/, .obj/
make static
make shared
make clean
```

Requires `gcc` and a POSIX-like environment (`mmap`, pthreads). Optional: Homebrew mimalloc / jemalloc / tcmalloc for benchmark comparisons.

## Usage

```c
#include <cmalloc/cmalloc.h>

int main(void) {
    char *buffer = cmalloc(128);
    if (buffer == NULL) {
        return 1;
    }
    buffer[0] = 'x';
    cfree(buffer);
    return 0;
}
```

```sh
make static
gcc -Iinclude example.c -Llib -lcmalloc -o example
```

## Tests and benchmarks

| Target | What it does |
|--------|----------------|
| `make d` | Debug / correctness smoke test |
| `make r` | Rigor stress + timing vs system / third-party allocators |
| `make tr` | Same workload as `r`, multi-threaded |
| `make mb` | Micro-benchmark isolating alloc-path and free-path cost |
| `make t_clean` | Remove `tests/bin` |

```sh
make d
make r
make tr
make mb
```

## Limitations

- One global lock on slow paths — high contention can limit scalability vs per-arena designs
- No full shutdown path that releases every cached arena
- Fully free small spans are cached but not coalesced with neighbors
- `mmap`-based; oriented at POSIX (page-size helpers exist for Windows, but the core path is not Windows-native)
