# Architecture

FastMemoryAllocator is a small C allocator exposed through `cmalloc` and
`cfree`. It is organized around size classes, spans, page-to-span lookup, and
separate metadata storage.

The current implementation is intentionally simple: it keeps global allocator
state, uses `mmap` for backing memory, and does not provide thread-local caches
or locking.

## Public Allocation Flow

`cmalloc(size)` first classifies the request:

- Requests up to `MAX_SIZE_CLASS` (`32768` bytes) use one of the fixed size
  classes in `src/common.h`.
- Larger requests use a dedicated one-block span whose block size is the
  requested size.

For small allocations, each size class has a global bin in `src/cmalloc.c`.
A bin is a singly linked list of spans that still have at least one free block.
Allocation takes the first span in the bin, removes it from the bin if this
allocation makes it full, and returns one block from that span.

If no span exists for the requested size class, the allocator initializes a new
span with the configured span size and block count for that class.

`cfree(ptr)` maps the pointer's page index back to the owning span. It then
marks the block free. If a previously full span becomes non-full, it is put back
in its size-class bin. If a small span becomes completely free, it is removed
from the size-class bin and cached as an uninitialized span for reuse.

Large allocations are represented as one-block spans. When freed, their spans
are returned to the span cache if they are small enough to bin by page count, or
unmapped if they exceed the allocator's cached-span limit.

## Size Classes And Bins

Size classes are defined in `src/common.h`.

- Alignment is 16 bytes.
- Size classes cover `16` bytes through `32768` bytes.
- Each size class has a configured span size, from `64 KiB` for the smallest
  classes up to `1 MiB` for the largest classes.
- Each size class also has a precomputed block count.

The allocator uses the first size class that can hold the requested size. For
example, a 100-byte request maps to the 112-byte class.

Each bin stores only spans with available blocks. Fully allocated spans are not
kept in their bin, which keeps allocation fast for the common case.

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

## Metadata Allocation

Allocator metadata is allocated separately from user data in `src/metadata.c`.
Metadata chunks are `4 MiB` `mmap` regions. New metadata is served by bump
allocation and aligned to 16 bytes.

Freed metadata objects are cached by object size using another range map as a
size-keyed free-list table. This is used for reusable allocator structures such
as span and range-map nodes.

## Current Limitations

- The allocator is not thread-safe.
- `ccalloc` and `crealloc` are declared in the public header but are not
  implemented yet.
- There is no allocator shutdown path that releases all cached arenas.
- Small fully freed spans are cached for reuse; they are not currently
  coalesced with neighboring free spans.
- The implementation relies on `mmap`, so the current code is POSIX-oriented
  despite some page-size helpers for Windows.