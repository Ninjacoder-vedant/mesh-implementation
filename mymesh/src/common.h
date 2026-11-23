// common.h: constants, size classes, lock, random numbers, and a tiny
// internal allocator for our own metadata (we can't call malloc inside malloc).
#pragma once

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include <atomic>

namespace mymesh
{

    constexpr size_t kPageSize = 4096;
    constexpr size_t kPageShift = 12;

    constexpr size_t kMaxSmallSize = 16384;      // bigger objects are "large": own pages, never meshed
    constexpr uint32_t kMaxObjectsPerSpan = 256; // 4096 / 16, also the bitmap size
    constexpr uint32_t kMinObjectsPerSpan = 8;   // same as the paper: a span holds at least 8 objects

    // The whole heap is one big virtual range backed by a memory file.
    // 64 GB of *virtual* space; only pages we touch use real memory.
    constexpr size_t kArenaSize = 64ULL << 30;
    constexpr size_t kArenaPages = kArenaSize >> kPageShift;

    constexpr int kNumSizeClasses = 24;
    constexpr int kNumBins = 4; // partial spans are grouped by how full they are (0-25%, 25-50%, ...)

    // Meshing settings (same values as the paper / original code)
    constexpr int kMeshProbes = 64;            // "t" in SplitMesher: how many right-side spans each left span tries
    constexpr double kOccupancyCutoff = 0.8;   // spans fuller than this are not worth meshing
    constexpr int kMaxMeshes = 32;             // max virtual spans sharing one physical span
    constexpr size_t kMaxSplit = 8192;         // max candidate spans per half, per size class, per pass
    constexpr size_t kMaxMeshesPerPass = 2500; // bounds the pause of one meshing pass (same as the original)
    // Each mesh adds memory mappings, and Linux limits mappings per process
    // (vm.max_map_count). Like the original, we use at most 1/3 of that limit.
    constexpr double kMeshesPerMapCount = 0.33;
    constexpr uint64_t kDefaultMeshPeriodMs = 100; // mesh at most once every 100 ms

    // Pages of freed spans are kept (with their memory, "dirty") for quick reuse,
    // up to this many bytes, then all are returned to the OS at once. Returning
    // every empty span right away cost one system call + fresh page faults per
    // span. Same limit as the original (64 MB).
    constexpr size_t kMaxDirtyBytes = 64ULL << 20;

    // Size classes: same as the original Mesh (jemalloc-like up to 1024, then powers of two).
    constexpr uint32_t kClassToSize[kNumSizeClasses] = {
        16,
        32,
        48,
        64,
        80,
        96,
        112,
        128,
        160,
        192,
        224,
        256,
        320,
        384,
        448,
        512,
        640,
        768,
        896,
        1024,
        2048,
        4096,
        8192,
        16384,
    };

    // Map a request size (1..kMaxSmallSize) to its size class.
    inline int sizeToClass(size_t sz)
    {
        if (sz <= 1024)
        {
            // small lookup table indexed by ceil(sz/16), filled on first use
            static int8_t table[65];
            static bool ready = false;
            if (!ready)
            {
                int c = 0;
                for (int i = 0; i <= 64; i++)
                {
                    while (kClassToSize[c] < (uint32_t)i * 16)
                        c++;
                    table[i] = (int8_t)c;
                }
                ready = true;
            }
            return table[(sz + 15) >> 4];
        }
        if (sz <= 2048)
            return 20;
        if (sz <= 4096)
            return 21;
        if (sz <= 8192)
            return 22;
        return 23;
    }

    // Objects per span and pages per span for a size class (same rule as the original code).
    inline uint32_t objectsPerSpan(int cls)
    {
        uint32_t n = kPageSize / kClassToSize[cls];
        if (n < kMinObjectsPerSpan)
            n = kMinObjectsPerSpan;
        if (n > kMaxObjectsPerSpan)
            n = kMaxObjectsPerSpan;
        return n;
    }
    inline uint32_t pagesPerSpan(int cls)
    {
        return (uint32_t)((objectsPerSpan(cls) * kClassToSize[cls] + kPageSize - 1) / kPageSize);
    }

    inline uint64_t nowMs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
        return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }

    // One global lock for the whole heap. We remember the owner so the
    // segfault handler can tell "this thread is inside malloc" apart.
    class Lock
    {
    public:
        void lock()
        {
            pthread_mutex_lock(&m_);
            owner_.store(pthread_self(), std::memory_order_relaxed);
            held_.store(true, std::memory_order_release);
        }
        void unlock()
        {
            held_.store(false, std::memory_order_release);
            pthread_mutex_unlock(&m_);
        }
        bool heldByMe() const
        {
            return held_.load(std::memory_order_acquire) &&
                   pthread_equal(owner_.load(std::memory_order_relaxed), pthread_self());
        }
        void resetAfterFork()
        {
            pthread_mutex_init(&m_, nullptr);
            held_.store(false);
        }

    private:
        pthread_mutex_t m_ = PTHREAD_MUTEX_INITIALIZER;
        std::atomic<pthread_t> owner_{0};
        std::atomic<bool> held_{false};
    };

    // Fast random numbers (xorshift64*). Used for random slot choice and SplitMesher.
    class Rng
    {
    public:
        explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ULL) : s_(seed ? seed : 1) {}
        uint64_t next()
        {
            s_ ^= s_ >> 12;
            s_ ^= s_ << 25;
            s_ ^= s_ >> 27;
            return s_ * 2685821657736338717ULL;
        }
        // random in [0, n). Multiply-shift instead of `%`: a 64-bit division was
        // the most expensive part of malloc in our profile.
        uint32_t below(uint32_t n) { return (uint32_t)(((next() >> 32) * (uint64_t)n) >> 32); }
        void seed(uint64_t s) { s_ = s ? s : 1; }

    private:
        uint64_t s_;
    };

    // Fisher-Yates shuffle
    template <typename T>
    inline void shuffle(T *a, size_t n, Rng &rng)
    {
        for (size_t i = n; i > 1; i--)
        {
            size_t j = rng.below((uint32_t)i);
            T t = a[i - 1];
            a[i - 1] = a[j];
            a[j] = t;
        }
    }

    // Fixed-size object pool for our own metadata, carved from mmap'd chunks.
    template <typename T>
    class Pool
    {
    public:
        T *alloc()
        {
            if (free_)
            {
                Node *n = free_;
                free_ = n->next;
                memset((void *)n, 0, sizeof(T));
                return reinterpret_cast<T *>(n);
            }
            if (cur_ + sizeof(Slot) > end_)
            {
                constexpr size_t kChunk = 1 << 20;
                void *p = mmap(nullptr, kChunk, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (p == MAP_FAILED)
                    return nullptr;
                cur_ = (char *)p;
                end_ = cur_ + kChunk;
            }
            T *t = reinterpret_cast<T *>(cur_);
            cur_ += sizeof(Slot);
            memset((void *)t, 0, sizeof(T));
            return t;
        }
        void free(T *t)
        {
            Node *n = reinterpret_cast<Node *>(t);
            n->next = free_;
            free_ = n;
        }

    private:
        struct Node
        {
            Node *next;
        };
        union Slot
        {
            alignas(16) char t[sizeof(T)];
            Node n;
        };
        Node *free_ = nullptr;
        char *cur_ = nullptr;
        char *end_ = nullptr;
    };

} // namespace mymesh
