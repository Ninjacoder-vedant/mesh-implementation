# mymesh: our implementation of Mesh (PLDI 2019)

A drop-in `malloc` replacement that saves memory by **meshing**: two sparse
pages whose live objects sit at different offsets are merged onto one physical
page, while every object keeps its address.

This is a simpler version of the original (`../reference/Mesh`): one global
lock instead of thread-local heaps, and fork only supported for fork+exec.
All core features of the paper are here.

## Build and run

```sh
make                 # builds libmymesh.so and the tests
make test            # runs all tests
LD_PRELOAD=$PWD/libmymesh.so <any program>
```

Environment settings:

| Variable | Meaning |
|---|---|
| `MYMESH_STATS=1` | print meshing statistics at exit (`2` = also memory per size class) |
| `MYMESH_NO_MESH=1` | turn meshing off (random allocation only) |
| `MYMESH_NO_RANDOM=1` | allocate slots in order 0,1,2,... (no randomization) |
| `MYMESH_PERIOD_MS=N` | mesh at most once every N ms (default 100) |

## Where each feature lives

| # | Feature (see `../plan.md`) | File |
|---|---|---|
| 1 | Size classes, spans, bitmaps | `src/common.h`, `src/span.h`, `src/bitmap.h` |
| 2 | Random allocation (shuffle vector) | `src/shuffle_vector.h`, `Heap::mallocSmall` in `src/heap.cc` |
| 3 | Heap backed by a memory file | `Arena::init` in `src/arena.cc` |
| 4 | Meshing two spans | `Heap::meshPair` in `src/meshing.cc` |
| 5 | SplitMesher | `Heap::meshSizeClass` in `src/meshing.cc` |
| 6 | Write barrier | `meshPair` (mprotect) + `segfaultHandler` in `src/wrapper.cc` |
| 7 | Address to span lookup | `Arena::spanAt` / `setOwner` in `src/arena.h` |
| 8 | Large objects | `Heap::mallocLarge` in `src/heap.cc` |
| 9 | When to mesh | `Heap::maybeMesh` in `src/meshing.cc`, `epoll_wait` in `src/wrapper.cc` |
| 10 | malloc API | `src/wrapper.cc` |

Suggested reading order: `common.h` -> `span.h` -> `bitmap.h` ->
`shuffle_vector.h` -> `arena.cc` -> `heap.cc` -> `meshing.cc` -> `wrapper.cc`.

## Details added while running the experiments

These came up when testing on Redis and Ruby (see `../REPORT.md`):

| Detail | Why |
|---|---|
| Mesh limit read from `/proc/sys/vm/max_map_count` (1/3 of it) | Each mesh adds memory mappings. A fixed limit of 20,000 stopped meshing far too early on this machine (limit 1,048,576). The original does the same. |
| At most 2,500 meshes per pass (`kMaxMeshesPerPass`) | Bounds the pause of one pass. Same as the original. |
| Freed span pages stay "dirty" for reuse, up to 64 MB (`Arena::freeDirty` / `flushDirty`) | Returning each empty span to the OS at once cost one system call plus fresh page faults per span. Same idea and limit as the original's "scavenge". |
| Faster random numbers (multiply-shift instead of `%`) | The 64-bit division was the most expensive part of malloc in a profile. |

## Life of an object

```
malloc(50)
  -> size class 64 bytes                       (sizeToClass)
  -> active span of that class, random slot    (mallocSmall + ShuffleVector)
  -> address = span start + slot * 64

free(p)
  -> page table gives the span + range start   (Arena::spanAt)
  -> slot = (p - range start) / 64, clear bit
  -> every 64 frees: maybe mesh                (maybeMesh, at most every 100 ms)

meshing pass, per size class                   (meshSizeClass)
  -> random split of sparse spans into two halves
  -> left span i tries right spans i..i+63: bitmaps don't overlap?
  -> meshPair(dst, src):
       mprotect src read-only                  (writes now wait in segfault handler)
       copy src objects into the same slots of dst
       mmap src's virtual pages onto dst's file pages
       punch a hole in src's file pages        (memory goes back to the OS)
```

## Tests

| Test | What it checks |
|---|---|
| `tests/test_basic` | random malloc/free/realloc/calloc/posix_memalign; contents intact after meshing |
| `tests/test_threads` | 4 threads write to objects while meshing runs; no write may be lost |
| `tests/test_frag N SIZE PCT [random\|stride]` | memory (RSS) before and after meshing, plus data check |

## Known limits

- One global lock: correct but slower than the original with many threads.
- fork() without exec: the child does not get a true snapshot of the heap.
- Meshing is limited to 1/3 of `vm.max_map_count` mappings (same as the
  original). On systems with the old default (65,530) that is ~21,000 meshes.
- Measure memory with PSS, not RSS: a meshed physical page is mapped at
  several virtual addresses and RSS counts it once per mapping.
- A program that makes its own heap memory read-only with `mprotect` and
  then writes to it would hang in the segfault handler (same in the original).
