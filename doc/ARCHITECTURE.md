# Architecture

FastMemoryAllocator is a small C allocator exposed through `cmalloc`, `ccalloc`,
`crealloc`, and `cfree`. It is organized around size classes, spans,
page-to-span lookup, and separate metadata storage.

The current implementation is intentionally simple: it keeps global allocator
state, uses `mmap` for backing memory, and uses a coarse thread-safety model
described below.

## Public Allocation Flow

`cmalloc(size)` first classifies the request:

- Requests up to `MAX_SIZE_CLASS` (`32768` bytes) use one of the fixed size
classes in `src/common.h`.
- Larger requests use a dedicated one-block span whose block size is the
requested size.

For small allocations, each size class has a per-thread bin in `src/cmalloc.c`.
A bin is a doubly linked list of spans that still have at least one free block.
Allocation takes the first span in the thread-local bin, removes it from the
bin if this allocation makes it full, and returns one block from that span.

If no span exists in the thread-local bin for the requested size class, the
allocator acquires a global `pthread_mutex_t`, initializes a new span with the
configured span size and block count for that class, releases the lock, and
pushes the span into the calling thread's bin.

`cfree(ptr)` maps the pointer's page index back to the owning span. It then
marks the block free. If a previously full span becomes non-full, it is put back
in the calling thread's size-class bin. If a small span becomes completely free,
it is removed from the thread-local bin and cached as an uninitialized span for
reuse under the global lock.

Large allocations are represented as one-block spans. Their spans are always
created under the global lock. When freed, they are returned to the span cache
(if small enough to bin by page count) or unmapped under the same lock.

## ccalloc And crealloc

Both functions are implemented in `src/cmalloc.c` on top of the existing
allocation paths.

`ccalloc(num, size)` rejects requests where `num * size` would overflow
`size_t`, then calls `cmalloc` with the product. On success it zero-fills exactly
`num * size` bytes (not the size-class slack beyond the request). A zero-byte
product still goes through `cmalloc(0)` and skips `memset`.

`crealloc(ptr, size)` mirrors the standard `realloc` interface:

- `ptr == NULL` delegates to `cmalloc(size)`.
- `size == 0` frees `ptr` and returns `NULL`.
- Otherwise the owning span's `block_size` is compared to the requested size.
  If the current size class already covers the new size, the original pointer is
  returned with no copy. This is the fast path for growth that stays within the
  same bucket (for example, growing a 100-byte logical allocation within a
  112-byte block).
- If a larger size class is required, a new block is allocated, the full old
  block (`span->block_size` bytes) is copied, the old block is freed, and the
  new pointer is returned. If the new allocation fails, `NULL` is returned and
  `ptr` is left unchanged.

There is no in-place expansion into adjacent free blocks; cross-class growth
always uses allocate-copy-free, similar to mimalloc's general `realloc` path.

## Size Classes And Bins

Size classes are defined in `src/common.h`.

- Alignment is 16 bytes.
- Size classes cover `16` bytes through `32768` bytes.
- Each size class has a configured span size, from `64 KiB` for the smallest
classes up to `1 MiB` for the largest classes.
- Each size class also has a precomputed block count.

The allocator uses the first size class that can hold the requested size. For
example, a 100-byte request maps to the 112-byte class. Resolution is O(1): a
lookup table (`cmalloc_size_class_table`, built once at startup) maps each
16-byte-aligned bucket directly to its class index, replacing a linear scan
over every class on each allocation.

Each bin stores only spans with available blocks. Fully allocated spans are not
kept in their bin, which keeps allocation fast for the common case.

## Thread Safety

Thread safety is implemented in `src/cmalloc.c` with two mechanisms:

1. **Per-thread size-class bins.** Each thread keeps its own
  `_Thread_local` array of span lists (`thread_local_bin`). The common
   small-allocation and small-free paths operate on these lists without
   locking.
2. **A global mutex on slow paths.** A single `pthread_mutex_t` protects
  operations that touch shared allocator state:
  - initializing a new span when the thread-local bin is empty;
  - all large allocations;
  - caching or releasing spans back to the global free-span pool.

The hot path for repeated small allocations from spans already in a thread's
local bin is lock-free. Contention shows up when threads miss their local cache
and need fresh spans, or when spans are returned to the global cache.

This is a minimal locking scheme, not a fully concurrent design. The page-to-span
range map in `src/range_map.c` does not currently take the global lock on
lookups; span map updates happen while the mutex is held during span creation
and caching.

## Spans

A span describes a contiguous page range that is divided into fixed-size blocks.
The span metadata tracks:

- The backing memory base pointer and page count.
- The size-class index, block size, block count, and free count.
- Links used for physical span order and free-span bins.
- A bitmap of free blocks.
- A `nonfull_bitmap` that identifies bitmap words containing at least one free
block.

Block allocation is bitmap based:

1. Find the first non-empty bitmap word with `__builtin_ctzll`.
2. Find the first free bit in that word.
3. Clear the bit and return `base + block_index * block_size`.

Freeing a block computes its block index relative to the span base, sets the
corresponding bitmap bit, and marks the bitmap word non-empty again.

## Backing Memory And Span Reuse

Allocator data memory is requested from the OS in `32 MiB` chunks. A chunk is
split into spans as allocation requests arrive.

Free, uninitialized spans are stored in page-count bins. A bitmap tracks which
page-count bins are non-empty, allowing the allocator to find the smallest
cached span with at least the requested page count.

When a cached span is larger than needed, it is split:

- The first part is returned to the caller.
- The remainder becomes a new uninitialized span and is inserted into the
appropriate page-count bin.
- Both pieces are mapped in the page-to-span range map.

Spans at or above `MAX_BINNED_PAGES` are not cached. They are allocated with a
direct `mmap` and unmapped when released.

## Page-To-Span Lookup

`cfree` needs to find the owning span for an arbitrary pointer. The allocator
does this with the range map in `src/range_map.c`.

The range map is a three-level radix trie keyed by page index. When a span is
created or split, every page index in that span is mapped to the span metadata.
On free, the pointer is shifted by the system page size to get its page index,
and the map returns the owning `span_t`.

This makes free independent of size-class bins and avoids storing per-block
headers in user memory.

The system page size and its log2 shift are resolved once at startup (via a
library constructor calling `cmalloc_runtime_init`) and cached in
`cmalloc_page_size` / `cmalloc_page_shift`. The page-index math on the free
path (`round_down_page_index`) therefore reads a cached value rather than
calling `sysconf` on every `cfree`.

Size-class bins are doubly linked (`next_in_size_class_bin` /
`prev_in_size_class_bin`), so removing a span that has become full or fully
free is O(1) instead of a linear walk of the bin.

## Metadata Allocation

Allocator metadata is allocated separately from user data in `src/metadata.c`.
Metadata chunks are `4 MiB` `mmap` regions. New metadata is served by bump
allocation and aligned to 16 bytes.

Freed metadata objects are cached by object size using another range map as a
size-keyed free-list table. This is used for reusable allocator structures such
as span and range-map nodes.

## Current Limitations

- Thread safety uses a single global lock on slow paths rather than per-arena or  
per-size-class locking; high contention can limit scalability.
- There is no allocator shutdown path that releases all cached arenas.
- Small fully freed spans are cached for reuse; they are not currently
coalesced with neighboring free spans.
- The implementation relies on `mmap`, so the current code is POSIX-oriented
despite some page-size helpers for Windows.

