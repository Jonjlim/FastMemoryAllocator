# FastMemoryAllocator

A custom C memory allocator (`cmalloc` / `ccalloc` / `crealloc` / `cfree`) built from scratch — size classes, span reuse, bitmap free-lists, a page-indexed radix trie for `cfree`, with cache local friendly data structures and per-thread fast paths with a global lock on slow paths.

Written as a systems-programming project to explore allocator design trade-offs and measure them against `malloc`, mimalloc, jemalloc, and tcmalloc.

- **Low-level systems design** — `mmap` arenas, page math, span splitting, metadata separated from user data
- **Performance-conscious data structures** — O(1) size-class lookup, bitmap + `ctz` free-block search, three-level page→span radix trie (no per-block headers)
- **Concurrency** — `_Thread_local` size-class bins on the hot path; a single mutex only for span creation, large allocs, and cache returns (sound while threads free their own blocks — see [Limitations](#limitations))
- **API completeness** — `realloc`-correct `crealloc`, overflow-safe `ccalloc`, static and shared library builds
- **Measurement** — benchmarked against the industry-standard [mimalloc-bench](https://github.com/daanx/mimalloc-bench) suite, plus in-repo rigor tests and a micro-benchmark



## Benchmark results

Measured with [mimalloc-bench](https://github.com/daanx/mimalloc-bench), the suite
used in the mimalloc papers, on a 10-core Apple Silicon machine (arm64, macOS,
16 KiB pages, Apple clang, `-O3`). Best of 3 runs. Every allocator is linked as a
dynamic library and reached through an identical call path, so the comparison is
like-for-like. Run it with `make suite`.

**Single-threaded** — cmalloc column is absolute seconds, others are relative to
cmalloc (**lower is better; >1.00 means slower than cmalloc**):

| benchmark      | cmalloc | system | mimalloc | jemalloc | tcmalloc |
|----------------|--------:|-------:|---------:|---------:|---------:|
| cfrac          | 2.052s  | 1.33x  | 0.84x    | 0.91x    | 0.89x    |
| espresso       | 2.293s  | 1.16x  | 0.95x    | 1.00x    | 0.96x    |
| alloc-test1    | 2.429s  | 1.31x  | 0.86x    | 0.89x    | 0.87x    |
| glibc-simple   | 1.305s  | 1.73x  | 0.89x    | 1.24x    | 0.95x    |
| malloc-large   | 0.371s  | 0.73x  | 0.70x    | 3.84x    | 0.79x    |
| mleak          | 0.034s  | 0.95x  | 1.11x    | 1.11x    | 0.94x    |

cmalloc beats the system allocator on every allocation-heavy single-threaded
benchmark (1.16x–1.73x) and lands within 5–16% of mimalloc and tcmalloc — the
size-class table, bitmap free-lists, and header-free design hold up against
production allocators. It loses to all of them on `malloc-large`, where the
one-block-span path pays for an `mmap` that the others serve from cached arenas.

**Multi-threaded** (10 threads):

| benchmark      | cmalloc | system | mimalloc | jemalloc | tcmalloc |
|----------------|--------:|-------:|---------:|---------:|---------:|
| alloc-testN    | 3.105s  | 1.72x  | 0.90x    | 0.90x    | 0.94x    |
| cache-scratchN | 0.132s  | 1.00x  | 1.02x    | 1.00x    | 0.92x    |
| cache-thrashN  | 0.126s  | 1.01x  | 1.11x    | 1.06x    | 1.07x    |
| glibc-thread   | 6.548s  | 0.60x  | 0.08x    | 0.09x    | 0.11x    |
| larsonN        | **fails** | — | — | — | — |
| mstressN       | **fails** | — | — | — | — |
| rptestN        | **fails** | — | — | — | — |

The thread-local bins do their job when threads own their own memory:
`alloc-testN` runs 1.72x faster than the system allocator and within 10% of
mimalloc. Two real problems show up under harder concurrency:

- **`glibc-thread` is ~12x slower than mimalloc.** This benchmark hammers the
  slow path, and every miss serializes on the single global mutex. It is the
  clearest measurement of what per-arena locking would buy.
- **`larsonN`, `mstressN`, and `rptestN` fail outright.** All three are built
  around *cross-thread frees* — one thread frees memory another allocated. See
  the limitation below; this is a known correctness gap, not a tuning problem.

`barnes`, `cache-scratch1`, and `cache-thrash1` are omitted: they spend almost no
time in the allocator here, so every allocator scores within 1% and the numbers
carry no signal. `xmalloc-testN` is omitted because it performs the same
cross-thread frees that corrupt `mstressN`, and it has no integrity check, so its
apparent result is not trustworthy.

### A bug the suite caught

Running the suite immediately crashed `barnes` with `SIGBUS`. Root cause: a span
could be *binnable* (`page_count < MAX_BINNED_PAGES`, a page count) yet larger
than a data chunk (`DATA_CHUNK_SIZE`, a byte count). `alloc_span` carved the span
out of a fresh 32 MiB mapping without checking it fit, so the span ran past the
end of its own mapping and the remainder page count underflowed.

The window is `DATA_CHUNK_SIZE < request ≤ MAX_BINNED_PAGES × PAGE_SIZE`. On
4 KiB-page Linux that is a 4 KiB sliver above 32 MiB and essentially unreachable;
on 16 KiB-page Apple Silicon it is everything from 32 MiB to 128 MiB. The fix
sizes the chunk to the request and only bins a remainder when one exists. The
same commit also repaired a leak where a fresh chunk's remainder overwrote the
free-span bin head instead of linking to it.



## Features

- 16-byte aligned allocations; fixed size classes from 16 B through 32 KiB
- Per-thread size-class bins of non-full spans for fast small alloc/free
- Bitmap-managed blocks inside each span (`__builtin_ctzll` for first-free)
- Page-index range map so `cfree` finds the owning span without in-object headers
- Separate metadata arena (`mmap`), bump-allocated and size-keyed free-list cached
- Fully freed small spans cached in page-count bins and split on reuse
- Multi-thread support: lock-free local bins; global `pthread_mutex_t` on slow paths (cross-thread frees are not yet safe — see [Limitations](#limitations))
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


| Path                              | Role                                        |
| --------------------------------- | ------------------------------------------- |
| `include/cmalloc/cmalloc.h`       | Public API                                  |
| `src/cmalloc.c`                   | Alloc / free / calloc / realloc + threading |
| `src/span.c` / `span.h`           | Spans and block bitmaps                     |
| `src/range_map.c` / `range_map.h` | Page-index → span radix trie                |
| `src/metadata.c` / `metadata.h`   | Allocator-internal metadata arena           |
| `src/common.h`                    | Size classes, alignment, page helpers       |
| `tests/`                          | Debug, rigor, thread-rigor, micro-benchmark |
| `bench/`                          | mimalloc-bench harness: allocator shims and run driver |




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


| Target            | What it does                                             |
| ----------------- | -------------------------------------------------------- |
| `make d`          | Debug / correctness smoke test                           |
| `make r`          | Rigor stress + timing vs system / third-party allocators |
| `make tr`         | Same workload as `r`, multi-threaded                     |
| `make mb`         | Micro-benchmark isolating alloc-path and free-path cost  |
| `make suite`      | Full mimalloc-bench suite vs system / mimalloc / jemalloc / tcmalloc |
| `make t_clean`    | Remove `tests/bin`                                       |
| `make suite_clean`| Remove suite build output                                |

```sh
make d
make r
make tr
make mb
make suite                              # clones mimalloc-bench on first run
make suite ARGS="--tests cfrac,espresso --reps 5"
make suite ARGS="--list"
```

### How the suite runs

mimalloc-bench normally swaps allocators with `LD_PRELOAD`, which does not work
for `malloc` on macOS: Apple's linker binds `malloc` to libSystem even when the
executable defines its own. `bench/run_bench.py` therefore builds every benchmark
once per allocator:

- **C benchmarks** force-include `bench/shim/alloc_shim.h`, redirecting the
  malloc family to the selected backend at the source level — the same technique
  as mimalloc's own `mimalloc-override.h`.
- **C++ benchmarks** link `bench/shim/alloc_shim_new.cpp`, which replaces global
  `operator new` / `operator delete` (these *are* replaceable at link time).

Redirection is verified by symbol inspection: the cmalloc builds import
`_cmalloc` / `_cfree` and contain no `_malloc` / `_free` imports at all.



## Limitations

- **Cross-thread frees are not safe.** `cfree` mutates the owning span's bitmap
  and its own thread-local bin without synchronization, so when one thread frees
  a block another thread allocated, two threads can race on the same span and the
  same block can be handed out twice. `bench/xthread_repro.c` demonstrates it:
  threads exchange blocks through a mutex-protected slot, so the *application*
  has no race, yet the cross-thread phase returns spurious `NULL`s from a nearly
  empty heap while the identical volume of same-thread traffic never fails. This
  is what breaks `larsonN`, `mstressN`, and `rptestN`. Fixing it properly means
  per-span ownership plus an atomic free list for remote frees, the approach
  mimalloc uses.

  ```sh
  make static
  cc -O2 -Iinclude bench/xthread_repro.c -Llib -lcmalloc -lpthread -o xthread_repro
  ./xthread_repro
  ```
- **One global lock on slow paths.** Every thread-local bin miss serializes;
  `glibc-thread` measures this at ~12x mimalloc. Per-arena or per-size-class
  locking is the natural next step.
- No full shutdown path that releases every cached arena.
- Fully free small spans are cached but not coalesced with neighbors.
- `mmap`-based; oriented at POSIX (page-size helpers exist for Windows, but the
  core path is not Windows-native).

