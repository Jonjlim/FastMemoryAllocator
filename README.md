# FastMemoryAllocator

FastMemoryAllocator is a small C memory allocator experiment. It provides
`cmalloc` and `cfree` as alternatives to `malloc` and `free`, with a design
based on fixed size classes, reusable spans, bitmap-managed blocks, and
page-to-span lookup.

The project is useful as a learning-oriented allocator implementation and as a
benchmark target against the system allocator.

## Features

- 16-byte aligned allocations.
- Fixed size classes from 16 bytes through 32 KiB.
- Per-size-class span bins for fast small allocations.
- Bitmap tracking for free blocks inside each span.
- Page-index range map so `cfree` can find the owning span without storing
  headers in user allocations.
- Separate metadata arena backed by `mmap`.
- Reuse of fully freed spans through page-count bins.
- Static and shared library build targets.

## Current Status

Implemented:

- `void *cmalloc(size_t size)`
- `void cfree(void *ptr)`

Declared but not implemented yet:

- `void *ccalloc(size_t num, size_t size)`
- `void *crealloc(void *ptr, size_t size)`

The allocator is currently not thread-safe and does not install itself as the
process-wide `malloc` implementation.

## Repository Layout

- `include/cmalloc/cmalloc.h` contains the public API.
- `src/cmalloc.c` implements the top-level allocation and free paths.
- `src/span.c` and `src/span.h` manage spans and block bitmaps.
- `src/range_map.c` and `src/range_map.h` map page indices to spans.
- `src/metadata.c` and `src/metadata.h` allocate allocator-internal metadata.
- `src/common.h` defines size classes, alignment, and page helpers.
- `tests/` contains correctness, stress, and benchmark-style tests.
- `doc/ARCHITECTURE.md` describes the allocator internals in more detail.

## Build

Build both the static and shared libraries:

```sh
make
```

Build only the static library:

```sh
make static
```

Build only the shared library:

```sh
make shared
```

Generated artifacts are written to `lib/` and `.obj/`.

## Tests

Run the simple correctness and benchmark test:

```sh
make t
```

Run the broader rigor test:

```sh
make r
```

Run the longer stress test:

```sh
make lr
```

Run the debug test:

```sh
make d
```

Clean build artifacts:

```sh
make clean
```

Clean test binaries:

```sh
make t_clean
```

## Usage

Include the public header and link against `libcmalloc`.

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

Example compile command after `make static`:

```sh
gcc -Iinclude example.c -Llib -lcmalloc -o example
```

## Design Overview

Small allocations are rounded up to the nearest size class. Each size class has
a bin of spans with available blocks. A span owns a contiguous page range and
uses bitmaps to track which blocks are free.

When a span becomes full, it is removed from its size-class bin. When a block is
freed back to a full span, that span is reinserted. When every block in a small
span is free, the span is cached for later reuse instead of staying attached to
the size class.

Larger allocations use one-block spans. Spans that are small enough to cache are
returned to the reusable span pool; very large spans are unmapped on free.

See `doc/ARCHITECTURE.md` for the detailed internal model.

## Platform Notes

The implementation currently uses `mmap` for data and metadata arenas, so it is
oriented toward POSIX-like systems. The build uses `gcc` through the provided
Makefile.
