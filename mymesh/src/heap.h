// heap.h: the global heap. All malloc/free calls go through here,
// protected by one lock (we dropped the paper's thread-local heaps).
#pragma once

#include "arena.h"
#include "common.h"
#include "shuffle_vector.h"
#include "span.h"

namespace mymesh
{

    struct Stats
    {
        uint64_t meshes = 0;     // span pairs meshed
        uint64_t meshPasses = 0; // times meshAll() ran
        uint64_t meshTimeUsTotal = 0;
        uint64_t meshTimeUsMax = 0;
        uint64_t spansCreated = 0;
    };

    class Heap
    {
    public:
        bool init();

        void *malloc(size_t sz);
        void *memalign(size_t align, size_t sz);
        void free(void *p);
        size_t usableSize(void *p);
        bool isFreshZeroed() const { return !privateMode_; }

        // Meshing (meshing.cc)
        void maybeMesh(); // lock held: mesh if enough time passed since last time
        size_t meshAll(); // lock held: one meshing pass over all size classes, returns meshes done

        // Write barrier helper for the segfault handler: if `addr` is in our heap,
        // wait for any running mesh to finish and return true (caller retries the write).
        bool waitIfMeshing(void *addr);

        void afterForkChild();
        void dumpClasses(int fd); // per size class memory breakdown (MYMESH_STATS=2)

        Lock lock;
        Arena arena;
        Stats stats;
        bool meshingEnabled = true;
        bool randomEnabled = true;
        uint64_t meshPeriodMs = kDefaultMeshPeriodMs;
        int printStats = 0;            // MYMESH_STATS: 1 = summary, 2 = also per size class
        size_t maxMeshedSpans = 20000; // set from vm.max_map_count in init()

    private:
        struct SizeClass
        {
            Span *active = nullptr;    // span malloc currently takes slots from
            ShuffleVector sv;          // its free slots in random order
            Span *bins[kNumBins] = {}; // partial spans, grouped by fullness
        };

        void *mallocSmall(int cls);
        void *mallocLarge(size_t sz, size_t align);
        Span *newSpan(int cls);
        void attachSpan(SizeClass &c, int cls);
        void freeSpan(Span *s);

        void binInsert(Span *s);
        void binRemove(Span *s);

        size_t meshSizeClass(int cls, size_t budget);
        void meshPair(Span *dst, Span *src);

        SizeClass classes_[kNumSizeClasses];
        Pool<Span> spanPool_;
        Rng rng_;
        bool privateMode_ = false;

        uint64_t lastMeshMs_ = 0;
        uint64_t freesSinceMesh_ = 0;
        size_t meshedSpans_ = 0; // extra virtual ranges currently meshed (each costs memory mappings)

        Span *cand_[2 * kMaxSplit]; // scratch space for SplitMesher
    };

    Heap *getHeap(); // creates the heap on first call

} // namespace mymesh
