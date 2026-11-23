// arena.h: the virtual memory layer (paper section 4.1 "Meshable arena").
//
// The heap is ONE memory file (memfd_create) mapped at `base`. Virtual page i
// normally shows file page i. Meshing changes this with mmap(MAP_FIXED) so a
// virtual page can show a different file page. This is only possible because
// the memory is a file; normal anonymous memory can't be remapped this way.
#pragma once

#include "common.h"
#include "span.h"

namespace mymesh
{

    class Arena
    {
    public:
        bool init();

        bool contains(const void *p) const
        {
            return (const char *)p >= base && (const char *)p < base + kArenaSize;
        }
        uint32_t pageOf(const void *p) const { return (uint32_t)(((const char *)p - base) >> kPageShift); }
        char *addr(uint32_t page) const { return base + ((size_t)page << kPageShift); }

        // --- page allocator (virtual address ranges) ---
        // allowDirty: may return pages that still hold old data (not zeroed)
        uint32_t allocPages(uint32_t n, bool allowDirty); // returns first page, or UINT32_MAX if out of space
        void freePages(uint32_t page, uint32_t n);        // give back a range whose physical memory is already released
        void freeDirty(uint32_t page, uint32_t n);        // give back a range that keeps its physical memory for reuse
        void flushDirty();                                // release the physical memory of all dirty ranges

        // --- physical memory and mappings ---
        void releasePhys(uint32_t page, uint32_t n);                  // give file pages back to the OS
        void remap(uint32_t virtPage, uint32_t physPage, uint32_t n); // make virtual pages show other file pages
        void protectReadOnly(uint32_t page, uint32_t n);              // write barrier for meshing

        // --- address -> span lookup (feature 7) ---
        // For every page: which span owns it, and where its virtual range starts.
        Span *spanAt(uint32_t page) const { return pageSpan_[page]; }
        uint32_t rangeStartAt(uint32_t page) const { return pageStart_[page]; }
        void setOwner(uint32_t start, uint32_t n, Span *s)
        {
            for (uint32_t i = 0; i < n; i++)
            {
                pageSpan_[start + i] = s;
                pageStart_[start + i] = start;
            }
        }

        // After fork the child must not share the heap file with the parent.
        void switchToPrivate();

        char *base = nullptr;
        int fd = -1;
        uint32_t highWater = 0; // pages [0, highWater) have been handed out at least once

    private:
        struct FreeRun
        {
            uint32_t start, len;
            FreeRun *next;
        };
        static constexpr uint32_t kExactLists = 64; // exact-size free lists for runs of 1..64 pages
        FreeRun *exact_[kExactLists + 1] = {};
        FreeRun *bigRuns_ = nullptr;           // longer runs, first-fit
        FreeRun *dirty_[kExactLists + 1] = {}; // ranges that still have physical memory, by length
        FreeRun *bigDirty_ = nullptr;
        size_t dirtyBytes_ = 0;
        Pool<FreeRun> runPool_;

        Span **pageSpan_ = nullptr;
        uint32_t *pageStart_ = nullptr;
        int mapFlags_ = MAP_SHARED;
        bool private_ = false;
    };

} // namespace mymesh
