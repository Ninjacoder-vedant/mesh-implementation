# Project 2 Plan: Mesh (PLDI 2019)

Paper: "Mesh: Compacting Memory Management for C/C++ Applications"
Bobby Powers, David Tench, Emery D. Berger, Andrew McGregor. PLDI 2019.
Paper: https://arxiv.org/abs/1902.04738
Original code: https://github.com/plasma-umass/Mesh (cloned in `reference/Mesh`)

## Goal

1. Implement Mesh ourselves (our version lives in `mymesh/`).
2. Reproduce the main results of the paper.

## Core features of Mesh

### 1. Size classes and spans
- Objects are grouped by size (16 B, 32 B, 48 B, ...).
- Each size class gets **spans**: a few pages that hold objects of one size only.
  The paper calls them MiniHeaps.
- Each span has a **bitmap**: one bit per slot, marking which slots are in use.
- Needed because meshing compares two spans by their bitmaps.

### 2. Random allocation (shuffle vector)
- malloc picks a **random free slot** in the span, not the next one in order.
- Needed because normal allocators fill slot 0, 1, 2... in every span, so no two
  spans could ever mesh. Random placement makes meshing very likely.

### 3. Heap backed by a memory file
- The whole heap is one in-memory file (`memfd_create`) mapped with `mmap`.
- Needed because only then can two virtual pages point to the same physical page.

### 4. The meshing operation
For two spans A and B whose bitmaps do not overlap:
1. Copy B's live objects into the same slots in A's physical page.
2. Remap B's virtual pages to A's physical pages (`mmap` + `MAP_FIXED`).
3. Return B's old physical pages to the OS (`fallocate` punch hole).
- This is how memory is saved. Addresses do not change, so pointers stay valid.

### 5. SplitMesher (finding pairs)
- Split the spans of a size class into two halves and compare span i of the
  left half with span i of the right half (and a few shifted offsets).
- Needed because checking every pair is too slow.

### 6. Write barrier during meshing
- While copying B into A, B is made read-only (`mprotect`).
- A thread that writes during the copy gets SIGSEGV; the handler waits until
  meshing ends, then the write is retried and goes to the new location.
- Needed so no write is lost while objects are copied.

### 7. Address-to-span lookup
- A table from each heap page to the span that owns it.
- Needed because `free(ptr)` only gives an address.

### 8. Large objects
- Objects above 16 KB get their own pages straight from the OS. Never meshed.

### 9. When to mesh
- Simple rule: after enough frees, and at most once every N milliseconds.

### 10. malloc interface
- Export malloc, free, calloc, realloc, posix_memalign, aligned_alloc,
  memalign, valloc, pvalloc, malloc_usable_size. Load with `LD_PRELOAD`.

### Dropped (performance or extras, not needed for correctness)
- Thread-local heaps (we use one global lock).
- fork() support (turn off Redis persistence: `save ""`).
- Tuning settings, statistics, macOS support.

### How the parts connect
```
malloc -> size class (1) -> span -> random free slot (2)
free   -> lookup table (7) -> clear bit in span bitmap
           -> enough frees? (9) -> SplitMesher finds pairs (5)
              -> mesh each pair (4), protected by write barrier (6)
              -> works because heap is a memory file (3)
```

## Build order

1, 7, 10, 8, 2, 3, 4, 5, 9, 6.
First a working allocator with no meshing, then meshing on top, then the
multi-threading safety last.

## Phases

| Phase | Work | Status |
|---|---|---|
| 0 | Background: malloc, virtual memory, mmap, LD_PRELOAD, RSS | |
| 1 | Read the paper (3 passes, see below) | |
| 2 | Build and run original Mesh | done (built with cmake into `reference/Mesh/build/lib/libmesh.so`) |
| 3 | Implement our version (features above) | done: all 10 features, tests pass (see `mymesh/README.md`) |
| 4 | Reproduce experiments: Redis fragmentation, Ruby, SPEC (or mimalloc-bench), ablation (no meshing / no randomization) | done, see `REPORT.md` (SPEC replaced by free programs, Firefox skipped) |
| 5 | Slides and write-up | |

## How to read the paper

- Pass 1 (30 min): abstract, intro, Figure 1 (two pages meshing), headings,
  conclusion, evaluation graphs only.
- Pass 2 (2-3 h): overview + implementation fully; algorithms section without
  the proofs. Keep a list of terms (MiniHeap, shuffle vector, meshing,
  SplitMesher, write barrier, global heap).
- Pass 3 (while coding): read source next to the implementation section,
  then the proofs and the evaluation details.
