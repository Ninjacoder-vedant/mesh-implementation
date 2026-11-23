# Speaker Notes

Total target: 15 minutes. The notes are about 1,900 words, about 14 to 15 minutes at a normal pace (130 to 140 words per minute).
Speaking pace: about 2 words per second. Times in brackets are per slide, then the running total.

---

## 1. Title [0:20 | 0:20]

- Hello, we are presenting the implementation of Mesh from PLDI 2019 as part of our computer systems co
- It is a memory allocator for C and C++ programs.
- We implemented the concepts proposed in the paper and successfully reproduced the results reported by the authors.

## 2. C/C++ heaps waste memory they cannot compact [1:00 | 1:20]

Why do C and C++ applications waste memory?

Each row represents a virtual memory page. Dark squares are live objects; light squares are freed slots.

Even if only one object remains alive, the entire page stays in use.

Languages like Java can compact memory by moving objects and updating references. But C and C++ programs can hold raw pointers. Moving objects could break those pointers.

So partially empty pages remain in memory.

In our Redis experiment, just 100 MiB of data consumed 287 MiB with jemalloc.

## 3. Mesh merges sparse pages without moving objects [1:10 | 2:30]

Now, let's see What the paper does to solve the problem

Look at the **Before** diagram. We have two virtual pages, each backed mapped to a seperate physical page.

Notice that the live objects occupy different slots, so they don't overlap.

Mesh copies the objects from the second page into the empty slots of the first physical page, and then remaps the second virtual page to that same physical page.

Now, as you can see in **After**, both virtual pages share one physical page, and the second physical page is freed.

The important part is that no object changes its address, so all existing pointers remain valid. This is called **meshing**.

## 4. Our allocator follows the paper's design [0:50 | 3:20]

Now, let's look at how we built it. We implemented our own allocator in C++, following the paper's design. With `LD_PRELOAD`, any program uses it without changing its source code.

`malloc(n)` maps the size to a **size class**. For example, a 40-byte request goes into the 48-byte class. It then picks a **span**, a region divided into slots of that size, and returns a random free slot.

`free(p)` uses a page table to find the span, then clears the slot's bit in the span's bitmap.

Every 100 milliseconds, we try to mesh spans and reclaim physical memory.

One difference: the paper uses per-thread heaps. We use one global lock to keep it simple.

## 5. malloc takes a random free slot [1:00 | 4:20]

Now, let's look at allocation.

A conventional allocator typically fills slots sequentially: 0, 1, 2, and so on. This creates similar allocation patterns across spans, making them less likely to mesh.

To avoid this, authors randomize the allocation order.

In this example, slots 1 and 4 are already occupied, so we collect the remaining free slots: 0, 2, 3, 5, 6, and 7.

We shuffle this list once when the span is attached. Then, every `malloc` simply pops the last available slot. Here, it returns slot 5.

So we get randomized allocation without having to generate a random number or search for a free slot on every allocation.

**Code walkthrough:**

- `shuffle` function Randomizes the free-slot list once.
- `pop` takes the last available slot and removes it from the list.

This keeps allocation fast while making object placement less predictable, which improves the chances of meshing.

## 6. free puts the slot back at a random place [1:00 | 5:20]

Now, what happens when an object is freed?

We put its slot back into the same free-slot list.

For example, suppose the list is 5, 2, 9. We free slot 7, so we add it at the end.

Then, we pick a random position, in this case, position 1, and swap it with the last element. Now the list becomes 5, 7, 9, 2.

Notice that the next `malloc` gets slot 2, not the slot we just freed.

Let's look at the code.

The first line inserts the freed slot at the end of the list.

`rng.below(n)` selects a random position between 0 and n minus 1.

The remaining lines swap the last element with the element at that random position.

This is one step of the Fisher-Yates shuffle. By doing one random swap per free, we maintain a randomized allocation order without shuffling the entire list every time.

## 7. Each span tracks its slots in a bitmap [1:00 | 6:20]

This slide shows how we track occupied slots using a bitmap.

Each span maintains a bitmap with one bit per slot. A `1` means occupied, and a `0` means free. When we allocate an object, we set its bit to 1. When we free it, we clear the bit.

We store these bits in 64-bit words. So, each word tracks 64 slots, and four words can track up to 256 slots.

Why 256? A 4 KB page can hold at most 256 objects of 16 bytes each, which is our smallest size class.

Now, look at the diagram. We compare Span A and Span B word by word using bitwise AND. The result shows where both spans have an occupied slot.

As you can see, they overlap in the third word, w[2]. So these spans cannot be meshed.

Let's look at the code.

We initialize `r` to zero to collect all overlaps.

Inside the loop, we AND the corresponding words of both bitmaps and OR the result into `r`.

Finally, if `r` is non-zero, at least one slot overlaps, so the spans cannot be meshed. Otherwise, they are compatible.

## 8. SplitMesher pairs spans of the same size [1:00 | 7:20]

- Now we use this test to find pairs to mesh.
- We only mesh spans of the same size class. Here, the 64-byte spans A to F.
- These are the candidates: spans that are partly used, but not too full.
- We shuffle them and split them into a left half and a right half.
- Then one pass. D tries B first. Slot 0 clashes, so D tries the next one, E. They fit.
- A starts at its own row. E is taken, so A moves on to C. They fit.
- F starts at C, which is taken, so it wraps around to B. They fit.
- One pass meshed three pairs. Each left span tries at most 64, so the work grows with n, not n squared.

Code (point at each line):

- shuffle puts the candidates in random order.
- left points at the first half of the list, right at the second half.
- The footer link opens the whole function, including the try loop.

## 9. Each try checks for an overlap [0:50 | 8:10]

- This is the loop behind those arrows.
- The picture: D and B both use slot 0. That is a clash, so we skip B.
- D and E share no slot, so they fit and we mesh them.

Code (point at each line):

- k counts the tries, up to probes, which is 64.
- j is the right span to try. It starts at the left span's own index and moves one step each try.
- If a or b is empty, that span was already meshed in this pass. Skip it.
- overlaps is the bitmap test from before. If any slot is used in both, skip and try the next one.
- If none of these skip, the next lines mesh the pair.

## 10. A fit meshes the pair [0:40 | 8:50]

- When a try fits, we mesh the two spans, here D and E, into one physical page.
- Both are then marked as used, so neither is meshed again in this pass.

Code (simplified, point at each line):

- We mesh into the span that already has more virtual ranges. That means fewer pages to remap.
- Setting both entries to null marks them used for this pass.
- meshed counts the pairs. A pass stops at 2,500 to keep pauses short.

## 11. One memory file lets pages be remapped [0:50 | 9:40]

- We have a pair. How do we merge them without moving objects?
- Meshing needs to point a virtual page at another physical page. Normal memory cannot do that.
- So the whole heap is one in-memory file. At the start, virtual page 2 shows file page 2, and so on.

Code (point at each box):

- memfd_create makes a file that lives only in memory. fd is its handle.
- mmap maps the whole file into our address space. This is the heap.
- MAP_SHARED means every mapping of the file sees the same memory. MAP_NORESERVE means memory is only used when a page is accessed.

## 12. mmap points virtual page 2 at frame A [1:00 | 10:40]

- There are three layers: virtual pages, pages of the file, and physical frames in RAM.
- Before: virtual page 1 shows file page 1, which uses frame A. Page 2 uses frame B.
- After one mmap call, virtual page 2 shows file page 1. Now virtual pages 1 and 2 both reach frame A.
- So both pages share one physical frame, and no object moved.

Code (point at the parts):

- addr(virtPage) is the virtual page we change. n << kPageShift is the length, n pages of 4 KB.
- MAP_FIXED means use exactly this address, and replace the old mapping in one step.
- The last argument is the file page to show, physPage times 4 KB.

## 13. fallocate gives frame B back to the OS [0:50 | 11:30]

- After the remap, nothing shows file page 2 any more. But frame B still holds its old data.
- fallocate with PUNCH_HOLE deletes that part of the file. The OS frees frame B right away.
- This is where memory is actually saved.

Code (point at the parts):

- FALLOC_FL_PUNCH_HOLE deletes the data in the range. FALLOC_FL_KEEP_SIZE keeps the file length, so other pages keep their offsets.
- page << kPageShift is where the range starts, and len is how many bytes.

## 14. Mesh saves about 40% on Redis, as the paper claims [1:10 | 12:40]

Now, let's look at the results, starting with Redis, one of the paper's main benchmarks.

Redis is configured as a cache. We fill it with data, then insert new keys. Older keys get evicted, leaving holes in memory.

The plot shows memory usage over 20 seconds. All allocators initially rise as Redis fills up, and then eviction begins.

Gray represents glibc, which stays around 345 MiB. Solid blue is jemalloc, which gradually drops to 222 MiB.

Now look at the Mesh results. Red is the original implementation, and solid green is ours. Both reduce memory quickly, settling around 130 to 137 MiB.

That's 41% and 38% less memory than jemalloc, respectively. The paper reports a 39% reduction.

The dashed blue line represents Redis's own defragmenter. It reaches 123 MiB, but takes around 13 seconds. Mesh achieves comparable savings in just 5 seconds.

Finally, the dotted lines show both Mesh versions with meshing disabled. They stay much higher, confirming that the memory savings come from meshing.

## 15. Without randomization, meshing saves nothing [0:50 | 13:30]

This experiment tests whether randomization actually makes meshing work.

We allocate 2 million objects and keep every eighth object, creating a regular allocation pattern.

Before meshing, memory usage is 143 MiB.

With sequential allocation, both Mesh versions remain at 143 MiB. They fail to find opportunities to mesh.

But with random allocation, memory drops to 63 MiB for the original Mesh and 58 MiB for our implementation.

So this confirms the paper's key insight: randomization is crucial for making meshing effective, especially with regular allocation patterns.

## 16. Ruby: big savings, but randomization did not matter [0:55 | 14:25]

- Second test: a Ruby program, like the paper's. It makes many strings, keeps every 4th one, and drops the rest.
- The plot shows memory over time. Blue is jemalloc. It climbs to about 800 MiB.
- Red is the original Mesh, green is ours. Both stay between about 200 and 300. They jump up and down as strings are made and freed.
- On average: jemalloc 521 MiB, original Mesh 255, ours 224. So Mesh uses about half.
- The dash-dot lines have randomization turned off. They look the same as the solid lines.
- The paper says randomization saves 19% here. In our rebuild of the test it made no difference. Their script is not public, so ours may free in a less regular order.

## 17. Key takeaways [0:40 | 15:05]

- Three things to remember.
- Meshing saves memory without moving any object. On Redis it cut memory by about 40%, as the paper claims.
- Random allocation is what makes it work. With regular frees and slots in order, nothing meshes.
- Our own version gets the same results as the original: close on Redis, and a bit lower on Ruby.
- That is our work. Thank you. Happy to take questions.
