// bitmap.h: one bit per object slot in a span (1 = in use).
// Two spans can be meshed only if their bitmaps have no common 1 bit.
#pragma once

#include <stdint.h>

#include "common.h"

namespace mymesh {

struct Bitmap {
  static constexpr int kWords = kMaxObjectsPerSpan / 64;  // 256 bits = 4 words
  uint64_t w[kWords];

  void set(uint32_t i) { w[i >> 6] |= (1ULL << (i & 63)); }
  void clear(uint32_t i) { w[i >> 6] &= ~(1ULL << (i & 63)); }
  bool isSet(uint32_t i) const { return (w[i >> 6] >> (i & 63)) & 1; }

  // The core meshing test (paper section 3): do any slots collide?
  bool overlaps(const Bitmap &o) const {
    uint64_t r = 0;
    for (int i = 0; i < kWords; i++)
      r |= w[i] & o.w[i];
    return r != 0;
  }

  void orWith(const Bitmap &o) {
    for (int i = 0; i < kWords; i++) w[i] |= o.w[i];
  }

  // Call f(slot) for every slot that is in use.
  template <typename F>
  void forEachSet(F f) const {
    for (int i = 0; i < kWords; i++) {
      uint64_t x = w[i];
      while (x) {
        int b = __builtin_ctzll(x);
        f((uint32_t)(i * 64 + b));
        x &= x - 1;  // drop lowest set bit
      }
    }
  }
};

}  // namespace mymesh
