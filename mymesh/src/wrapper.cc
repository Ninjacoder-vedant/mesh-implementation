// wrapper.cc: the public malloc API (feature 10) plus the glue the write
// barrier needs: our SIGSEGV handler, and wrappers for sigaction/signal so a
// program can't replace that handler, and for read/recv which fail with
// EFAULT (instead of faulting) when they write into a page being meshed.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "heap.h"

using namespace mymesh;

#define EXPORT extern "C" __attribute__((visibility("default")))

// ---------------------------------------------------------------------------
// malloc API
// ---------------------------------------------------------------------------

EXPORT void *malloc(size_t sz)
{
    void *p = getHeap()->malloc(sz);
    if (!p)
        errno = ENOMEM;
    return p;
}

EXPORT void free(void *p)
{
    if (p)
        getHeap()->free(p);
}

EXPORT void cfree(void *p) { free(p); }

EXPORT void *calloc(size_t n, size_t sz)
{
    size_t total;
    if (__builtin_mul_overflow(n, sz, &total))
    {
        errno = ENOMEM;
        return nullptr;
    }
    Heap *h = getHeap();
    void *p = h->malloc(total);
    if (!p)
    {
        errno = ENOMEM;
        return nullptr;
    }
    // Large objects always get fresh pages, which the OS gives us zeroed.
    if (total <= kMaxSmallSize || !h->isFreshZeroed())
        memset(p, 0, total);
    return p;
}

EXPORT void *realloc(void *p, size_t sz)
{
    if (!p)
        return malloc(sz);
    if (sz == 0)
    {
        free(p);
        return nullptr;
    }
    Heap *h = getHeap();
    size_t old = h->usableSize(p);
    if (sz <= old && sz >= old / 2)
        return p; // still fits and not wasting much: keep it
    void *n = h->malloc(sz);
    if (!n)
    {
        errno = ENOMEM;
        return nullptr;
    }
    memcpy(n, p, old < sz ? old : sz);
    h->free(p);
    return n;
}

EXPORT void *reallocarray(void *p, size_t n, size_t sz)
{
    size_t total;
    if (__builtin_mul_overflow(n, sz, &total))
    {
        errno = ENOMEM;
        return nullptr;
    }
    return realloc(p, total);
}

static bool isPow2(size_t x) { return x && !(x & (x - 1)); }

EXPORT void *memalign(size_t align, size_t sz)
{
    if (!isPow2(align))
    {
        errno = EINVAL;
        return nullptr;
    }
    void *p = getHeap()->memalign(align, sz);
    if (!p)
        errno = ENOMEM;
    return p;
}

EXPORT void *aligned_alloc(size_t align, size_t sz) { return memalign(align, sz); }

EXPORT int posix_memalign(void **out, size_t align, size_t sz)
{
    if (!isPow2(align) || align % sizeof(void *) != 0)
        return EINVAL;
    void *p = getHeap()->memalign(align, sz);
    if (!p)
        return ENOMEM;
    *out = p;
    return 0;
}

EXPORT void *valloc(size_t sz) { return memalign(kPageSize, sz); }

EXPORT void *pvalloc(size_t sz) { return memalign(kPageSize, (sz + kPageSize - 1) & ~(kPageSize - 1)); }

EXPORT size_t malloc_usable_size(void *p) { return p ? getHeap()->usableSize(p) : 0; }

// Extra API for tests and experiments: run one meshing pass right now.
// Returns how many span pairs were meshed.
EXPORT size_t mymesh_mesh_now()
{
    Heap *h = getHeap();
    h->lock.lock();
    size_t n = h->meshingEnabled ? h->meshAll() : 0;
    h->lock.unlock();
    return n;
}

// ---------------------------------------------------------------------------
// Write barrier: SIGSEGV handler
// ---------------------------------------------------------------------------

typedef int (*sigaction_fn)(int, const struct sigaction *, struct sigaction *);
static sigaction_fn realSigaction()
{
    static sigaction_fn fn = nullptr;
    if (!fn)
        fn = (sigaction_fn)dlsym(RTLD_NEXT, "sigaction");
    return fn;
}

static struct sigaction gUserSegv; // the handler the program asked for (we keep ours installed)
static pthread_mutex_t gSigLock = PTHREAD_MUTEX_INITIALIZER;

static void segfaultHandler(int sig, siginfo_t *info, void *ctx)
{
    // SEGV_ACCERR = write to a read-only page. If it is in our heap, a mesh is
    // probably copying that page: wait for it, then return. The CPU retries the
    // faulting instruction, which now writes to the new (writable) mapping.
    if (info->si_code == SEGV_ACCERR && getHeap()->waitIfMeshing(info->si_addr))
        return;

    // Not ours: pass it on to the program's handler, or crash normally.
    if (gUserSegv.sa_flags & SA_SIGINFO)
    {
        if (gUserSegv.sa_sigaction)
        {
            gUserSegv.sa_sigaction(sig, info, ctx);
            return;
        }
    }
    else if (gUserSegv.sa_handler != SIG_DFL && gUserSegv.sa_handler != SIG_IGN)
    {
        gUserSegv.sa_handler(sig);
        return;
    }
    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    realSigaction()(SIGSEGV, &dfl, nullptr); // returning re-runs the bad access -> normal crash
}

static void installSegfaultHandler()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = segfaultHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    realSigaction()(SIGSEGV, &sa, nullptr);
}

// Programs (e.g. Redis, Ruby) install their own SIGSEGV handler. We record it
// and keep ours in place; ours forwards non-meshing faults to theirs.
EXPORT int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    if (sig != SIGSEGV)
        return realSigaction()(sig, act, old);
    pthread_mutex_lock(&gSigLock);
    if (old)
        *old = gUserSegv;
    if (act)
        gUserSegv = *act;
    pthread_mutex_unlock(&gSigLock);
    return 0;
}

EXPORT sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, &old) != 0)
        return SIG_ERR;
    return old.sa_handler;
}

// The kernel does not raise SIGSEGV when a system call writes to a read-only
// page; it returns EFAULT. Retry those after waiting for the mesh.
typedef ssize_t (*read_fn)(int, void *, size_t);
typedef ssize_t (*recv_fn)(int, void *, size_t, int);

EXPORT ssize_t read(int fd, void *buf, size_t n)
{
    static read_fn real = nullptr;
    if (!real)
        real = (read_fn)dlsym(RTLD_NEXT, "read");
    ssize_t r = real(fd, buf, n);
    while (r < 0 && errno == EFAULT && getHeap()->waitIfMeshing(buf))
        r = real(fd, buf, n);
    return r;
}

EXPORT ssize_t recv(int fd, void *buf, size_t n, int flags)
{
    static recv_fn real = nullptr;
    if (!real)
        real = (recv_fn)dlsym(RTLD_NEXT, "recv");
    ssize_t r = real(fd, buf, n, flags);
    while (r < 0 && errno == EFAULT && getHeap()->waitIfMeshing(buf))
        r = real(fd, buf, n, flags);
    return r;
}

// Feature 9 (idle trigger): servers like Redis wait in epoll_wait when idle.
// That is a good moment to mesh, like the original Mesh does.
typedef int (*epoll_wait_fn)(int, struct epoll_event *, int, int);
EXPORT int epoll_wait(int epfd, struct epoll_event *ev, int maxev, int timeout)
{
    static epoll_wait_fn real = nullptr;
    if (!real)
        real = (epoll_wait_fn)dlsym(RTLD_NEXT, "epoll_wait");
    Heap *h = getHeap();
    h->lock.lock();
    h->maybeMesh();
    h->lock.unlock();
    return real(epfd, ev, maxev, timeout);
}

// ---------------------------------------------------------------------------
// fork: lock the heap so the child gets a consistent copy, then the child
// switches to a private copy of the heap file (see Arena::switchToPrivate).
// ---------------------------------------------------------------------------

static void forkPrepare() { getHeap()->lock.lock(); }
static void forkParent() { getHeap()->lock.unlock(); }
static void forkChild() { getHeap()->afterForkChild(); }

static void printStats()
{
    Heap *h = getHeap();
    if (!h->printStats)
        return;
    const Stats &s = h->stats;
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "[mymesh] meshes=%llu passes=%llu mesh_time_total_us=%llu mesh_time_max_us=%llu spans=%llu\n",
                     (unsigned long long)s.meshes, (unsigned long long)s.meshPasses,
                     (unsigned long long)s.meshTimeUsTotal, (unsigned long long)s.meshTimeUsMax,
                     (unsigned long long)s.spansCreated);
    if (n > 0)
    {
        ssize_t w = ::write(2, buf, (size_t)n);
        (void)w;
    }
    if (h->printStats > 1)
        h->dumpClasses(2);
}

__attribute__((constructor)) static void mymeshInit()
{
    getHeap();
    installSegfaultHandler();
    pthread_atfork(forkPrepare, forkParent, forkChild);
    atexit(printStats);
}
