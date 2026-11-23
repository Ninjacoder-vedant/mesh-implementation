#include "heap.h"

#include <fcntl.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <new>

namespace mymesh
{

    // ---------------------------------------------------------------------------
    // Setup
    // ---------------------------------------------------------------------------

    static bool envFlag(const char *name)
    {
        const char *v = getenv(name);
        return v && v[0] && v[0] != '0';
    }

    bool Heap::init()
    {
        if (!arena.init())
            return false;
        meshingEnabled = !envFlag("MYMESH_NO_MESH");  // ablation: random allocation but no meshing
        randomEnabled = !envFlag("MYMESH_NO_RANDOM"); // ablation: meshing but sequential allocation
        if (const char *v = getenv("MYMESH_STATS"))
            printStats = atoi(v);
        if (const char *v = getenv("MYMESH_PERIOD_MS"))
            meshPeriodMs = strtoull(v, nullptr, 10);
        // How many meshes we allow: 1/3 of the kernel's limit on memory mappings.
        int fd = open("/proc/sys/vm/max_map_count", O_RDONLY | O_CLOEXEC);
        if (fd >= 0)
        {
            char buf[32] = {0};
            // raw syscall: plain read() would call our own read() wrapper -> getHeap() -> deadlock during init
            if (syscall(SYS_read, fd, buf, sizeof(buf) - 1) > 0)
                maxMeshedSpans = (size_t)(strtoull(buf, nullptr, 10) * kMeshesPerMapCount);
            close(fd);
        }
        rng_.seed((uint64_t)getpid() * 0x9E3779B97F4A7C15ULL ^ (uint64_t)nowMs());
        lastMeshMs_ = nowMs();
        return true;
    }

    alignas(64) static char gHeapStorage[sizeof(Heap)];
    static Heap *gHeap = nullptr;
    static pthread_mutex_t gInitLock = PTHREAD_MUTEX_INITIALIZER;

    Heap *getHeap()
    {
        Heap *h = __atomic_load_n(&gHeap, __ATOMIC_ACQUIRE);
        if (h)
            return h;
        pthread_mutex_lock(&gInitLock);
        if (!gHeap)
        {
            Heap *nh = new (gHeapStorage) Heap();
            if (!nh->init())
                abort();
            __atomic_store_n(&gHeap, nh, __ATOMIC_RELEASE);
        }
        pthread_mutex_unlock(&gInitLock);
        return gHeap;
    }

    // ---------------------------------------------------------------------------
    // Bins: partial spans of a size class, grouped by how full they are.
    // When malloc needs a new span it takes the fullest one first, which keeps
    // memory dense. Full, empty and active spans are not in any bin.
    // ---------------------------------------------------------------------------

    void Heap::binInsert(Span *s)
    {
        if (s->active || s->inUse == 0 || s->isFull())
        {
            s->bin = -1;
            return;
        }
        int b = (int)((uint64_t)s->inUse * kNumBins / s->objectCount);
        Span *&head = classes_[s->sizeClass].bins[b];
        s->bin = (int8_t)b;
        s->prev = nullptr;
        s->next = head;
        if (head)
            head->prev = s;
        head = s;
    }

    void Heap::binRemove(Span *s)
    {
        if (s->bin < 0)
            return;
        Span *&head = classes_[s->sizeClass].bins[s->bin];
        if (s->prev)
            s->prev->next = s->next;
        else
            head = s->next;
        if (s->next)
            s->next->prev = s->prev;
        s->bin = -1;
        s->prev = s->next = nullptr;
    }

    // ---------------------------------------------------------------------------
    // Spans
    // ---------------------------------------------------------------------------

    Span *Heap::newSpan(int cls)
    {
        uint32_t pages = pagesPerSpan(cls);
        uint32_t start = arena.allocPages(pages, /*allowDirty=*/true); // small spans may reuse dirty pages
        if (start == UINT32_MAX)
            return nullptr;
        Span *s = spanPool_.alloc();
        if (!s)
            return nullptr;
        s->virt[0] = start;
        s->virtCount = 1;
        s->pageCount = pages;
        s->sizeClass = cls;
        s->objectSize = kClassToSize[cls];
        s->objectCount = objectsPerSpan(cls);
        s->bin = -1;
        arena.setOwner(start, pages, s);
        stats.spansCreated++;
        return s;
    }

    // Give all of a span's memory back to the page allocator.
    // A plain small span keeps its physical memory ("dirty" pages) so the next
    // span of the same length can reuse it without system calls or page faults;
    // the arena returns dirty pages to the OS in batches (Arena::flushDirty).
    // Large spans and meshed spans give their physical memory back right away.
    void Heap::freeSpan(Span *s)
    {
        binRemove(s);
        if (s->virtCount == 1 && !s->isLarge())
        {
            arena.setOwner(s->virt[0], s->pageCount, nullptr);
            arena.freeDirty(s->virt[0], s->pageCount);
            spanPool_.free(s);
            return;
        }
        for (uint32_t i = 0; i < s->virtCount; i++)
        {
            uint32_t v = s->virt[i];
            arena.setOwner(v, s->pageCount, nullptr);
            // A meshed range points at another span's file pages. Point it back
            // at its own file pages (which are already empty) before reuse.
            if (i > 0)
                arena.remap(v, v, s->pageCount);
        }
        arena.releasePhys(s->virt[0], s->pageCount);
        for (uint32_t i = 0; i < s->virtCount; i++)
            arena.freePages(s->virt[i], s->pageCount);
        meshedSpans_ -= s->virtCount - 1;
        spanPool_.free(s);
    }

    // Pick a new span for malloc to allocate from: the fullest partial span,
    // or a new one.
    void Heap::attachSpan(SizeClass &c, int cls)
    {
        if (Span *old = c.active)
        {
            old->active = false;
            if (old->inUse == 0)
                freeSpan(old);
            else
                binInsert(old); // full spans are not put in a bin
            c.active = nullptr;
        }
        Span *s = nullptr;
        for (int b = kNumBins - 1; b >= 0 && !s; b--)
        {
            if (c.bins[b])
            {
                s = c.bins[b];
                binRemove(s);
            }
        }
        if (!s)
            s = newSpan(cls);
        if (!s)
            return;
        s->active = true;
        c.active = s;
        c.sv.refill(s->bitmap, s->objectCount, rng_, randomEnabled);
    }

    // ---------------------------------------------------------------------------
    // malloc / free
    // ---------------------------------------------------------------------------

    void *Heap::mallocSmall(int cls)
    {
        SizeClass &c = classes_[cls];
        if (!c.active || c.sv.empty())
        {
            attachSpan(c, cls);
            if (!c.active)
                return nullptr;
        }
        Span *s = c.active;
        uint32_t slot = c.sv.pop(); // feature 2: random free slot
        s->bitmap.set(slot);
        s->inUse++;
        return arena.addr(s->virt[0]) + (size_t)slot * s->objectSize;
    }

    // Feature 8: large objects get their own pages and are never meshed.
    // `align` > page size is handled by asking for extra pages.
    void *Heap::mallocLarge(size_t sz, size_t align)
    {
        size_t extra = align > kPageSize ? align - kPageSize : 0;
        size_t pages = (sz + extra + kPageSize - 1) >> kPageShift;
        if (pages >= UINT32_MAX)
            return nullptr;
        uint32_t start = arena.allocPages((uint32_t)pages, /*allowDirty=*/false); // fresh pages are zero (calloc relies on it)
        if (start == UINT32_MAX)
            return nullptr;
        Span *s = spanPool_.alloc();
        if (!s)
            return nullptr;
        s->virt[0] = start;
        s->virtCount = 1;
        s->pageCount = (uint32_t)pages;
        s->sizeClass = -1;
        s->objectCount = 1;
        s->inUse = 1;
        s->bin = -1;
        arena.setOwner(start, (uint32_t)pages, s);
        uintptr_t p = (uintptr_t)arena.addr(start);
        if (align > kPageSize)
            p = (p + align - 1) & ~(uintptr_t)(align - 1);
        return (void *)p;
    }

    void *Heap::malloc(size_t sz)
    {
        if (sz == 0)
            sz = 1;
        lock.lock();
        void *p = sz <= kMaxSmallSize ? mallocSmall(sizeToClass(sz)) : mallocLarge(sz, 0);
        lock.unlock();
        return p;
    }

    void *Heap::memalign(size_t align, size_t sz)
    {
        if (align <= 16)
            return malloc(sz);
        if (sz == 0)
            sz = 1;
        lock.lock();
        void *p;
        if (align <= kPageSize && sz <= kMaxSmallSize)
        {
            // Spans start on a page boundary, so slot k is at k * objectSize.
            // Pick a size class whose object size is a multiple of `align`.
            int cls = sizeToClass(sz < align ? align : sz);
            while (kClassToSize[cls] % align != 0)
                cls++;
            p = mallocSmall(cls);
        }
        else
        {
            p = mallocLarge(sz, align);
        }
        lock.unlock();
        return p;
    }

    void Heap::free(void *p)
    {
        if (!arena.contains(p))
            return; // NULL or not ours
        lock.lock();
        uint32_t page = arena.pageOf(p);
        Span *s = arena.spanAt(page); // feature 7: address -> span
        if (!s)
        { // double free or bad pointer: ignore
            lock.unlock();
            return;
        }
        if (s->isLarge())
        {
            freeSpan(s);
            lock.unlock();
            return;
        }

        // Find the slot. Use the start of *this* virtual range: after meshing,
        // several ranges share one span, and slot numbers are the same in all of them.
        size_t off = (size_t)((char *)p - arena.addr(arena.rangeStartAt(page)));
        uint32_t slot = (uint32_t)(off / s->objectSize);
        if (slot >= s->objectCount || !s->bitmap.isSet(slot))
        { // bad pointer or double free: ignore
            lock.unlock();
            return;
        }
        s->bitmap.clear(slot);
        s->inUse--;

        if (s->active)
        {
            classes_[s->sizeClass].sv.push(slot, rng_, randomEnabled);
        }
        else if (s->inUse == 0)
        {
            freeSpan(s);
        }
        else
        {
            binRemove(s); // fullness changed: move to the right bin
            binInsert(s);
        }

        freesSinceMesh_++;
        if ((freesSinceMesh_ & 63) == 0)
            maybeMesh(); // feature 9 (check the clock only every 64 frees)
        lock.unlock();
    }

    size_t Heap::usableSize(void *p)
    {
        if (!arena.contains(p))
            return 0;
        lock.lock();
        size_t r = 0;
        uint32_t page = arena.pageOf(p);
        if (Span *s = arena.spanAt(page))
        {
            if (s->isLarge())
                r = (size_t)(arena.addr(s->virt[0] + s->pageCount) - (char *)p);
            else
                r = s->objectSize;
        }
        lock.unlock();
        return r;
    }

    // Where does the memory go? For each size class: spans that own physical
    // memory, live bytes, and physical bytes. Walks the page table.
    void Heap::dumpClasses(int fd)
    {
        uint64_t spans[kNumSizeClasses] = {}, live[kNumSizeClasses] = {}, phys[kNumSizeClasses] = {},
                 ranges[kNumSizeClasses] = {};
        uint64_t largeSpans = 0, largeBytes = 0;
        for (uint32_t pg = 0; pg < arena.highWater; pg++)
        {
            Span *s = arena.spanAt(pg);
            if (!s || arena.rangeStartAt(pg) != pg)
                continue; // only the first page of each range
            if (s->isLarge())
            {
                largeSpans++;
                largeBytes += (uint64_t)s->pageCount * kPageSize;
                continue;
            }
            ranges[s->sizeClass]++;
            if (pg != s->virt[0])
                continue; // count physical memory once, at its owner range
            spans[s->sizeClass]++;
            live[s->sizeClass] += (uint64_t)s->inUse * s->objectSize;
            phys[s->sizeClass] += (uint64_t)s->pageCount * kPageSize;
        }
        char buf[160];
        for (int c = 0; c < kNumSizeClasses; c++)
        {
            if (!spans[c])
                continue;
            int n = snprintf(buf, sizeof(buf), "[mymesh] class %5u: spans=%7llu ranges=%7llu live=%7.1f MiB phys=%7.1f MiB used=%3d%%\n",
                             kClassToSize[c], (unsigned long long)spans[c], (unsigned long long)ranges[c],
                             live[c] / 1048576.0, phys[c] / 1048576.0, (int)(100 * live[c] / phys[c]));
            if (write(fd, buf, n) < 0)
                return;
        }
        int n = snprintf(buf, sizeof(buf), "[mymesh] large: spans=%llu bytes=%.1f MiB\n", (unsigned long long)largeSpans,
                         largeBytes / 1048576.0);
        if (write(fd, buf, n) < 0)
            return;
    }

    void Heap::afterForkChild()
    {
        lock.resetAfterFork();
        privateMode_ = true;
        meshingEnabled = false; // the child shares no physical memory with the parent any more
        arena.switchToPrivate();
    }

} // namespace mymesh
