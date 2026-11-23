// meshing.cc: finding spans to mesh (SplitMesher) and meshing them.
// This is the heart of the paper (sections 3 and 4.3).
#include <sys/time.h>

#include "heap.h"

namespace mymesh
{

    static uint64_t nowUs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
    }

    // Feature 9: when to mesh. Called with the lock held.
    void Heap::maybeMesh()
    {
        if (!meshingEnabled || freesSinceMesh_ == 0)
            return; // nothing was freed, nothing new to mesh
        uint64_t now = nowMs();
        if (now - lastMeshMs_ < meshPeriodMs)
            return; // rate limit: at most once per period
        meshAll();
    }

    size_t Heap::meshAll()
    {
        uint64_t t0 = nowUs();
        arena.flushDirty(); // like the original: give cached empty memory back before meshing
        size_t found = 0;
        // Each mesh costs 3 system calls; cap the work per pass so the program
        // (and other threads waiting for the heap lock) never stall for long.
        for (int cls = 0; cls < kNumSizeClasses && found < kMaxMeshesPerPass; cls++)
            found += meshSizeClass(cls, kMaxMeshesPerPass - found);
        uint64_t dt = nowUs() - t0;

        stats.meshes += found;
        stats.meshPasses++;
        stats.meshTimeUsTotal += dt;
        if (dt > stats.meshTimeUsMax)
            stats.meshTimeUsMax = dt;
        lastMeshMs_ = nowMs();
        freesSinceMesh_ = 0;
        return found;
    }

    // Feature 5: SplitMesher.
    // 1. Collect candidate spans (not active, not too full) in random order.
    // 2. Split them into a left half and a right half.
    // 3. Each left span tries up to t right spans; the first one whose bitmap
    //    does not overlap is meshed with it.
    // Checking all pairs would be O(n^2); this is O(n * t) and the paper proves
    // it still finds a good share of the possible meshes.
    size_t Heap::meshSizeClass(int cls, size_t budget)
    {
        SizeClass &c = classes_[cls];
        const uint32_t maxInUse = (uint32_t)(objectsPerSpan(cls) * kOccupancyCutoff);

        size_t n = 0;
        for (int b = 0; b < kNumBins && n < 2 * kMaxSplit; b++)
        {
            for (Span *s = c.bins[b]; s && n < 2 * kMaxSplit; s = s->next)
            {
                if (s->inUse < maxInUse && s->virtCount < (uint32_t)kMaxMeshes)
                    cand_[n++] = s;
            }
        }
        if (n < 2)
            return 0;

        shuffle(cand_, n, rng_);
        Span **left = cand_;
        Span **right = cand_ + n / 2;
        const size_t nl = n / 2, nr = n - n / 2;
        const size_t probes = nr < (size_t)kMeshProbes ? nr : (size_t)kMeshProbes;

        size_t meshed = 0;
        for (size_t i = 0; i < nl && meshed < budget; i++)
        {
            for (size_t k = 0; k < probes; k++)
            {
                size_t j = (i + k) % nr;
                Span *a = left[i], *b = right[j];
                if (!a || !b)
                    continue;
                if (a->bitmap.overlaps(b->bitmap))
                    continue; // same slot used in both: can't mesh
                if (a->virtCount + b->virtCount > (uint32_t)kMaxMeshes)
                    continue;
                if (meshedSpans_ + b->virtCount > maxMeshedSpans)
                    return meshed; // too many memory mappings

                // keep the span that already has more virtual ranges (fewer remaps)
                if (a->virtCount >= b->virtCount)
                    meshPair(a, b);
                else
                    meshPair(b, a);
                left[i] = right[j] = nullptr; // each span is meshed at most once per pass
                meshed++;
                break;
            }
        }
        return meshed;
    }

    // Feature 4 + 6: mesh `src` into `dst`.
    // After this, every virtual range of src shows dst's physical pages, and
    // src's physical pages are returned to the OS. No object changes address.
    void Heap::meshPair(Span *dst, Span *src)
    {
        const uint32_t pages = src->pageCount;
        char *srcMem = arena.addr(src->virt[0]);
        char *dstMem = arena.addr(dst->virt[0]);

        // Write barrier (feature 6): make src read-only. Other threads can still
        // READ their objects during the copy. A WRITE traps into our segfault
        // handler, which waits for us to finish and then retries the write.
        for (uint32_t i = 0; i < src->virtCount; i++)
            arena.protectReadOnly(src->virt[i], pages);

        // Copy every live object of src to the SAME slot in dst.
        // Bitmaps don't overlap, so those slots in dst are free.
        const uint32_t osz = src->objectSize;
        src->bitmap.forEachSet([&](uint32_t slot)
                               { memcpy(dstMem + (size_t)slot * osz, srcMem + (size_t)slot * osz, osz); });
        dst->bitmap.orWith(src->bitmap);
        dst->inUse += src->inUse;

        // Point src's virtual ranges at dst's physical pages. mmap(MAP_FIXED)
        // swaps the mapping in one step and also makes the pages writable again.
        for (uint32_t i = 0; i < src->virtCount; i++)
        {
            uint32_t v = src->virt[i];
            arena.remap(v, dst->virt[0], pages);
            arena.setOwner(v, pages, dst); // free() on these addresses now finds dst
            dst->virt[dst->virtCount++] = v;
        }
        meshedSpans_ += src->virtCount;

        // Nothing maps src's old physical pages now: give them back to the OS.
        // This is where memory is actually saved.
        arena.releasePhys(src->virt[0], pages);

        binRemove(src);
        binRemove(dst);
        binInsert(dst); // dst is fuller now (maybe full)
        spanPool_.free(src);
    }

    // Segfault handler helper (feature 6).
    bool Heap::waitIfMeshing(void *addr)
    {
        if (!arena.contains(addr))
            return false;
        if (lock.heldByMe())
            return false; // fault inside our own code: a real bug
        // Meshing runs while holding the heap lock, so taking the lock waits for it to end.
        lock.lock();
        bool ours = arena.spanAt(arena.pageOf(addr)) != nullptr;
        lock.unlock();
        return ours;
    }

} // namespace mymesh
