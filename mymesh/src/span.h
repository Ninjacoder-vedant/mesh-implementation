// span.h: metadata for one span (the paper calls it a MiniHeap).
//
// A span is a group of pages holding objects of one size class.
// Physical memory: the file pages at offset virt[0].
// Virtual memory: one or more address ranges (virt[0..virtCount-1]) that all
// point to that same physical memory. Before meshing there is only one.
// After meshing span B into span A, A.virt also contains B's old range.
#pragma once

#include "bitmap.h"
#include "common.h"

namespace mymesh
{

    struct Span
    {
        Bitmap bitmap;             // which slots are in use (physical slots, shared by all virtual ranges)
        uint32_t virt[kMaxMeshes]; // first page of each virtual range; virt[0] is also the physical (file) page
        uint32_t virtCount;
        uint32_t pageCount;   // pages per virtual range
        int32_t sizeClass;    // -1 for a large object
        uint32_t objectSize;  // bytes per slot (unused for large objects)
        uint32_t objectCount; // slots in this span (1 for large objects)
        uint32_t inUse;       // slots currently allocated
        bool active;          // currently used by malloc for its size class (never meshed while active)
        int8_t bin;           // which fullness bin it is in, -1 if none
        Span *prev, *next;    // links for the bin list

        bool isLarge() const { return sizeClass < 0; }
        bool isFull() const { return inUse == objectCount; }
    };

} // namespace mymesh
