#include "arena.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

namespace mymesh {

bool Arena::init() {
  // Feature 3: the heap is a memory-only file.
  fd = memfd_create("mymesh_arena", MFD_CLOEXEC);
  if (fd < 0) return false;
  if (ftruncate(fd, kArenaSize) != 0) return false;  // sparse: no real memory used yet

  void *p = mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NORESERVE, fd, 0);
  if (p == MAP_FAILED) return false;
  base = (char *)p;

  // Lookup tables, one entry per page. Untouched parts cost no memory.
  p = mmap(nullptr, kArenaPages * sizeof(Span *), PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) return false;
  pageSpan_ = (Span **)p;
  p = mmap(nullptr, kArenaPages * sizeof(uint32_t), PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (p == MAP_FAILED) return false;
  pageStart_ = (uint32_t *)p;
  return true;
}

uint32_t Arena::allocPages(uint32_t n, bool allowDirty) {
  // 0) a dirty range of exactly this length: no system call, no page faults
  if (allowDirty && n <= kExactLists && dirty_[n]) {
    FreeRun *r = dirty_[n];
    dirty_[n] = r->next;
    uint32_t s = r->start;
    dirtyBytes_ -= (size_t)n << kPageShift;
    runPool_.free(r);
    return s;
  }
  // 1) exact-size free list
  if (n <= kExactLists && exact_[n]) {
    FreeRun *r = exact_[n];
    exact_[n] = r->next;
    uint32_t s = r->start;
    runPool_.free(r);
    return s;
  }
  // 2) first fit in the list of long runs, split off the rest
  for (FreeRun **pp = &bigRuns_; *pp; pp = &(*pp)->next) {
    FreeRun *r = *pp;
    if (r->len >= n) {
      *pp = r->next;
      uint32_t s = r->start;
      if (r->len > n) freePages(s + n, r->len - n);
      runPool_.free(r);
      return s;
    }
  }
  // 3) never-used space at the end
  if ((uint64_t)highWater + n > kArenaPages) return UINT32_MAX;
  uint32_t s = highWater;
  highWater += n;
  return s;
}

void Arena::freePages(uint32_t page, uint32_t n) {
  FreeRun *r = runPool_.alloc();
  r->start = page;
  r->len = n;
  if (n <= kExactLists) {
    r->next = exact_[n];
    exact_[n] = r;
  } else {
    r->next = bigRuns_;
    bigRuns_ = r;
  }
}

void Arena::freeDirty(uint32_t page, uint32_t n) {
  FreeRun *r = runPool_.alloc();
  r->start = page;
  r->len = n;
  FreeRun **list = n <= kExactLists ? &dirty_[n] : &bigDirty_;
  r->next = *list;
  *list = r;
  dirtyBytes_ += (size_t)n << kPageShift;
  if (dirtyBytes_ > kMaxDirtyBytes) flushDirty();
}

void Arena::flushDirty() {
  auto drain = [&](FreeRun *&head) {
    while (FreeRun *r = head) {
      head = r->next;
      releasePhys(r->start, r->len);
      freePages(r->start, r->len);
      runPool_.free(r);
    }
  };
  for (uint32_t n = 1; n <= kExactLists; n++) drain(dirty_[n]);
  drain(bigDirty_);
  dirtyBytes_ = 0;
}

void Arena::releasePhys(uint32_t page, uint32_t n) {
  const size_t len = (size_t)n << kPageShift;
  if (private_) {
    madvise(addr(page), len, MADV_DONTNEED);
    return;
  }
  // "Punch a hole" in the file: the OS frees that physical memory right away.
  fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)page << kPageShift, len);
}

void Arena::remap(uint32_t virtPage, uint32_t physPage, uint32_t n) {
  // Replace the mapping in one step: virtual pages now show file pages `physPage`.
  void *p = mmap(addr(virtPage), (size_t)n << kPageShift, PROT_READ | PROT_WRITE, mapFlags_ | MAP_FIXED, fd,
                 (off_t)physPage << kPageShift);
  if (p == MAP_FAILED) abort();
}

void Arena::protectReadOnly(uint32_t page, uint32_t n) {
  if (mprotect(addr(page), (size_t)n << kPageShift, PROT_READ) != 0) abort();
}

void Arena::switchToPrivate() {
  // Called in a forked child. Map the heap file privately (copy-on-write),
  // so the child's writes and frees never touch the parent's memory.
  // This is enough for the usual fork() + exec() pattern.
  private_ = true;
  mapFlags_ = MAP_PRIVATE;
  void *p = mmap(base, kArenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED | MAP_NORESERVE, fd, 0);
  if (p == MAP_FAILED) abort();
  // The mmap above reset all meshed ranges to "page i shows file page i".
  // Point them back at the physical span they were meshed into.
  for (uint32_t pg = 0; pg < highWater; pg++) {
    Span *s = pageSpan_[pg];
    if (s && pageStart_[pg] == pg && pg != s->virt[0]) remap(pg, s->virt[0], s->pageCount);
  }
}

}  // namespace mymesh
