// shuffle_vector.h: random allocation inside a span (paper section 4.2).
//
// Holds the free slot numbers of the span we are allocating from, in random
// order. malloc pops the last one, so every malloc gets a random free slot.
// free pushes the slot back and swaps it to a random position.
//
// Why: if every span filled slots 0,1,2,... in order, all spans would use the
// same slots and no two spans could ever be meshed.
#pragma once

#include "bitmap.h"
#include "common.h"

namespace mymesh
{

    struct ShuffleVector
    {
        uint16_t slots[kMaxObjectsPerSpan];
        uint32_t n = 0;

        bool empty() const { return n == 0; }

        // Fill with all free slots of a span.
        // random=false is the "no randomization" mode used for the paper's ablation
        // experiment: slots then come out in order 0,1,2,... like a normal allocator.
        void refill(const Bitmap &bm, uint32_t objectCount, Rng &rng, bool random)
        {
            n = 0;
            for (uint32_t i = objectCount; i-- > 0;)
            { // push high slots first so pop() returns low slots first
                if (!bm.isSet(i))
                    slots[n++] = (uint16_t)i;
            }
            if (random)
                shuffle(slots, n, rng);
        }

        uint32_t pop() { return slots[--n]; }

        void push(uint32_t slot, Rng &rng, bool random)
        {
            slots[n++] = (uint16_t)slot;
            if (random && n > 1)
            {
                // one Fisher-Yates step keeps the order random
                uint32_t j = rng.below(n);
                uint16_t t = slots[n - 1];
                slots[n - 1] = slots[j];
                slots[j] = t;
            }
        }
    };

} // namespace mymesh
