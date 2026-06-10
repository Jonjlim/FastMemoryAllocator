## Brief structure overview:
**Size Classes:**
 - Array of bins for each size class.
 - 4, 8, 16, 32, 64 byte bins.
 - Allocated in global variables.


**Bins:**
 - A unique bin for each size class.
 - Holds lists of spans.
 - Allocated with size classes.


**Spans:**
 - Holds the actual blocks that will be distrubted.
 - Allocated with MMAP sys call.


**Blocks:**
 - Actual data that will be allocated to users.
 - Allocated with spans.


# Usual process:
For usual small requests, user requests cmalloc(s), s gets rounded up to the nearest class size.
The class size has a index the corresponds to a bin. The library then gets the corresponding bin
and attempts to get a span. If a span doesn't exist, create a span for that bin. Get a free block
from the span and return it.

For small free requests, the library rounds the the poitner down to a page index -- Sys calls requesting
memory returns only in chunks of page sizes, for macOS and linux systems its 16kB, for windows its 4kB.
The library when allocating the blocks already mapped each page index to a span, so when freeing, it gets
the span the block belongs to and gives it back to it.

**Small allocations:**
>cmalloc(size) -> size rounded up class size -> get bin in class size -> get a span from bin -> get a free block from span -> give block to user.


> Free(ptr) -> ptr rounded down to page index -> page index mapped to span -> give block back to span.



## Each data structure specifics:
#### Bins:    