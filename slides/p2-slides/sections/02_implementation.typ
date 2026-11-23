#import "../lib.typ": *

// Overview of our allocator: the path of malloc and free.
#let box4(body) = nb(4.4cm, body)
#slide(
  title: [Our allocator follows the paper's design],
  sec: [Implementation],
  src: ("heap.cc", 199, none),
  takeaway: [Drop-in `malloc` via `LD_PRELOAD`. One global lock replaces the per-thread heaps.],
)[
  #dg(
    spacing: (0.9cm, 1.1cm),
    node((0, 0), box4[`malloc(n)`]),
    edge("-|>"),
    node((1, 0), box4[size class]),
    edge("-|>"),
    node((2, 0), box4[span: random slot]),
    edge("-|>"),
    node((3, 0), box4[pointer]),
    node((0, 1), box4[`free(p)`]),
    edge("-|>"),
    node((1, 1), box4[page table]),
    edge("-|>"),
    node((2, 1), box4[clear bit]),
    edge("-|>"),
    node((3, 1), box4[mesh every 100 ms]),
  )
]

// Story order: allocate, free, bitmap test, find pairs, remap, mesh.
// Shuffle vectors make allocation random (paper 4.2).
// A list of slot numbers drawn as boxes. mark: indexes of the boxes to highlight.
#let nums(..xs, mark: ()) = box(grid(
  columns: xs.pos().len(),
  column-gutter: 4pt,
  ..xs.pos().enumerate().map(((i, x)) => box(
    width: 0.85cm,
    height: 0.85cm,
    radius: 3pt,
    fill: if i in mark { bar-strong } else { wash },
    stroke: 0.6pt + luma(160),
    align(center + horizon, text(fill: if i in mark { white } else { ink }, str(x))),
  )),
))
#let step-row(..rows) = grid(
  columns: (auto, auto, auto),
  column-gutter: 0.6cm,
  row-gutter: 0.35cm,
  align: (right + horizon, left + horizon, left + horizon),
  ..rows.pos().flatten(),
)

// Allocation: collect free slots, shuffle once, malloc takes the last one.
#slide(
  title: [malloc takes a random free slot],
  sec: [Implementation],
  src: ("shuffle_vector.h", 35, 39),
)[
  #step-row(
    (lbl[span], slots(live: (1, 4)), lbl[slots 1 and 4 used]),
    (lbl[free slots], nums(0, 2, 3, 5, 6, 7), []),
    (lbl[shuffled], nums(6, 0, 7, 3, 2, 5, mark: (5,)), lbl[malloc takes 5]),
  )
  #v(0.35cm)
  #code(width: 100%, src: ("shuffle_vector.h", 35, 39), ```
  if (random)
      shuffle(slots, n, rng);
  }

  uint32_t pop() { return slots[--n]; }
  ```)
]

// Free: append the slot, then one Fisher-Yates swap moves it to a random place.
#slide(
  title: [free puts the slot back at a random place],
  sec: [Implementation],
  src: ("shuffle_vector.h", 43, 50),
)[
  #grid(
    columns: (auto, 1fr),
    column-gutter: 0.7cm,
    align: horizon,
    step-row(
      (lbl[list], nums(5, 2, 9), []),
      (lbl[free(7)], nums(5, 2, 9, 7, mark: (3,)), []),
      (lbl[j = 1, swap], nums(5, 7, 9, 2, mark: (1, 3)), []),
      (lbl[next malloc], nums(2, mark: (0,)), []),
    ),
    code(src: ("shuffle_vector.h", 43, 50), ```
    slots[n++] = (uint16_t)slot;
    if (random && n > 1)
    {
        // one Fisher-Yates step keeps the order random
        uint32_t j = rng.below(n);
        uint16_t t = slots[n - 1];
        slots[n - 1] = slots[j];
        slots[j] = t;
    ```),
  )
]

// Paper 4.1: MiniHeaps (we call them spans) and their bitmaps.
#slide(
  title: [Each span tracks its slots in a bitmap],
  sec: [Implementation],
  src: ("bitmap.h", 20, 25),
)[
  // Each 64-bit word w[i] holds 64 slots; 4 cells are drawn per word.
  #let wd(..args) = slots(n: 4, cell: 0.45cm, ..args)
  #let m(body) = raw(body)
  #grid(
    columns: (auto, auto, auto, auto, auto, auto),
    column-gutter: 0.5cm,
    row-gutter: 0.32cm,
    align: (right + horizon, center + horizon, center + horizon, center + horizon, center + horizon, left + horizon),
    [], m("w[0]"), m("w[1]"), m("w[2]"), m("w[3]"), [],
    m("A"), wd(live: (1,)), wd(live: (0, 2)), wd(live: (3,)), wd(live: (1,)), [],
    m("B"), wd(live: (2,)), wd(live: (3,)), wd(live: (3,)), wd(live: (0,)), [],
    m("A & B"), wd(), wd(), wd(live: (3,), mark: (3,)), wd(), [→ #m("r != 0"): clash],
  )
  #v(0.3cm)
  #code(src: ("bitmap.h", 20, 25), ```
  bool overlaps(const Bitmap &o) const {
    uint64_t r = 0;
    for (int i = 0; i < kWords; i++)
      r |= w[i] & o.w[i];
    return r != 0;
  }
  ```)
]

// Paper 4.5 (part 1): SplitMesher finds pairs within one size class.
// The example follows the real loop: left span i first tries right span i,
// then i+1, ...; spans already meshed this pass are skipped.
#let cs(name, live, mark: ()) = box(grid(
  columns: 2,
  column-gutter: 0.2cm,
  align: horizon,
  text(weight: "bold", fill: ink, name),
  slots(n: 4, cell: 0.42cm, live: live, mark: mark),
))
#slide(
  title: [SplitMesher pairs spans of the same size],
  sec: [Implementation],
  src: ("meshing.cc", 56, 106),
  takeaway: [One pass: each left span tries up to 64 right spans and meshes the first that fits],
)[
  #grid(
    columns: (auto, auto, auto, auto, auto, auto, auto),
    column-gutter: 0.45cm,
    align: horizon,
    lbl[64 B spans],
    cs("A", (1,)), cs("B", (0,)), cs("C", (0, 2)), cs("D", (0, 2)), cs("E", (1, 3)), cs("F", (3,)),
  )
  #v(0.15cm)
  #text(fill: mid)[↓ shuffle, split in half]
  #v(0.15cm)
  #grid(
    columns: (auto, 1fr),
    column-gutter: 0.8cm,
    align: horizon,
    dg(
      spacing: (3.6cm, 0.55cm),
      node-stroke: none,
      node-inset: 3pt,
      node((0, -1), lbl[left]),
      node((1, -1), lbl[right]),
      node((0, 0), cs("D", (0, 2), mark: (0,)), name: <d>),
      node((0, 1), cs("A", (1,)), name: <a>),
      node((0, 2), cs("F", (3,)), name: <f>),
      node((1, 0), cs("B", (0,), mark: (0,)), name: <b>),
      node((1, 1), cs("E", (1, 3)), name: <e>),
      node((1, 2), cs("C", (0, 2)), name: <c>),
      edge(<d>, <b>, "-|>", stroke: (dash: "dashed")),
      edge(<d>, <e>, "-|>", stroke: 2pt + ink),
      edge(<a>, <c>, "-|>", stroke: 2pt + ink),
      edge(<f>, <b>, "-|>", stroke: 2pt + ink),
    ),
    code(src: ("meshing.cc", 73, 75), ```
    shuffle(cand_, n, rng_);
    Span **left = cand_;
    Span **right = cand_ + n / 2;
    ```),
  )
]

// The try loop: each try skips taken spans and spans whose bitmaps overlap.
#slide(
  title: [Each try checks for an overlap],
  sec: [Implementation],
  src: ("meshing.cc", 82, 89),
)[
  #grid(
    columns: (auto, 1fr),
    column-gutter: 0.8cm,
    align: horizon,
    grid(
      columns: (auto, auto),
      column-gutter: 0.5cm,
      row-gutter: 0.3cm,
      align: (left + horizon, left + horizon),
      cs("D", (0, 2), mark: (0,)), [],
      cs("B", (0,), mark: (0,)), text(fill: mid)[clash: skip],
      v(0.3cm), [],
      cs("D", (0, 2)), [],
      cs("E", (1, 3)), text(fill: mid)[fit: mesh],
    ),
    code(src: ("meshing.cc", 82, 89), ```
    for (size_t k = 0; k < probes; k++)
    {
        size_t j = (i + k) % nr;
        Span *a = left[i], *b = right[j];
        if (!a || !b)
            continue;
        if (a->bitmap.overlaps(b->bitmap))
            continue; // same slot used in both: can't mesh
    ```),
  )
]

// A fit: mesh the pair and mark both spans as used for this pass.
// The code is a simplified version of meshing.cc 95-101 (the link opens the real lines).
#slide(
  title: [A fit meshes the pair],
  sec: [Implementation],
  src: ("meshing.cc", 95, 101),
)[
  #grid(
    columns: (auto, auto, auto, auto),
    column-gutter: 0.5cm,
    align: horizon,
    stack(spacing: 0.3cm, cs("D", (0, 2)), cs("E", (1, 3))),
    text(fill: mid)[→],
    box(grid(columns: 2, column-gutter: 0.2cm, align: horizon,
      text(weight: "bold", fill: ink)[D+E], slots(n: 4, cell: 0.42cm, live: (0, 2), other: (1, 3)))),
    text(fill: mid)[D and E are used up for this pass],
  )
  #v(0.4cm)
  #align(left, note[Simplified:])
  #v(0.1cm)
  #code(width: 100%, src: ("meshing.cc", 95, 101), ```
  // mesh into the span with more virtual ranges
  if (a->virtCount >= b->virtCount) meshPair(a, b);
  else meshPair(b, a);
  left[i] = right[j] = nullptr; // used up this pass
  meshed++;
  ```)
]

// Paper 4.4: the global heap is one memory file, so pages can be remapped.
#let pgnode(n) = box(width: 1.2cm, height: 0.95cm, align(center + horizon, str(n)))
#slide(
  title: [One memory file lets pages be remapped],
  sec: [Implementation],
  src: ("arena.cc", 13, 17),
)[
  #stack(
    spacing: 0.35cm,
    dg(
      spacing: (0.45cm, 1.1cm),
      node-inset: 4pt,
      node((-1.3, 0), lbl[virtual], stroke: none),
      node((-1.3, 1), lbl[file], stroke: none),
      ..range(4).map(i => node((i, 0), pgnode(i), name: label("v" + str(i)))),
      node((0, 1), pgnode(0), name: <f0>),
      node((1, 1), pgnode(1), name: <f1>),
      node((2, 1), pgnode(2), name: <f2>),
      node((3, 1), pgnode(3), name: <f3>),
      edge(<v0>, <f0>, "-|>"),
      edge(<v1>, <f1>, "-|>"),
      edge(<v2>, <f2>, "-|>"),
      edge(<v3>, <f3>, "-|>"),
    ),
    code(width: 100%, src: ("arena.cc", 13, none), ```
    fd = memfd_create("mymesh_arena", MFD_CLOEXEC);
    ```),
    code(width: 100%, src: ("arena.cc", 17, none), ```
    void *p = mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NORESERVE, fd, 0);
    ```),
  )
]

// Three layers: virtual pages -> file pages (memfd) -> physical frames in RAM.
// fv/ff/fr: which file page each virtual page shows, which frame each file page uses.
#let pg3(n, dashed: false) = box(width: 1.1cm, height: 0.9cm, align(center + horizon, str(n)))
#let layers(heading, v2f, f2r, hole: (), freed: (), bold: ()) = align(center, stack(
  spacing: 0.25cm,
  head(heading),
  dg(
    spacing: (0.5cm, 0.75cm),
    node-inset: 3pt,
    ..(1, 2).map(i => node((i - 1, 0), pg3(i), name: label("v" + str(i)))),
    ..(1, 2).map(i => node((i - 1, 1), pg3(i), name: label("f" + str(i)),
      stroke: if i in hole { (paint: luma(150), dash: "dashed") } else { 0.8pt + luma(45) })),
    ..("A", "B").enumerate().map(((i, x)) => node((i, 2), pg3(x), name: label("r" + x),
      fill: if x in freed { none } else { wash },
      stroke: if x in freed { (paint: luma(150), dash: "dashed") } else { 0.8pt + luma(45) })),
    ..v2f.map(((v, f)) => edge(label("v" + str(v)), label("f" + str(f)), "-|>",
      stroke: if v in bold { 2pt + ink } else { 0.8pt + luma(75) })),
    ..f2r.map(((f, r)) => edge(label("f" + str(f)), label("r" + r), "-|>")),
  ),
))
#let layer-labels = stack(spacing: 0.25cm, head[#h(0pt)], dg(
  spacing: (0.5cm, 0.75cm), node-stroke: none, node-inset: 3pt,
  node((0, 0), box(height: 0.9cm, align(horizon, lbl[virtual]))),
  node((0, 1), box(height: 0.9cm, align(horizon, lbl[file]))),
  node((0, 2), box(height: 0.9cm, align(horizon, lbl[RAM]))),
))

// Remap: virtual page 2 now shows file page 1, so it reaches frame A like virtual page 1.
#slide(
  title: [mmap points virtual page 2 at frame A],
  sec: [Implementation],
  src: ("arena.cc", 119, 120),
)[
  #grid(
    columns: (auto, auto, auto, auto),
    column-gutter: 1.2cm,
    align: bottom,
    layer-labels,
    layers([Before], ((1, 1), (2, 2)), ((1, "A"), (2, "B"))),
    text(size: 30pt, fill: soft)[→],
    layers([After], ((1, 1), (2, 1)), ((1, "A"), (2, "B")), bold: (2,)),
  )
  #v(0.3cm)
  #code(width: 100%, src: ("arena.cc", 119, 120), ```
  void *p = mmap(addr(virtPage), (size_t)n << kPageShift, PROT_READ | PROT_WRITE, mapFlags_ | MAP_FIXED, fd,
                 (off_t)physPage << kPageShift);
  ```)
]

// Punch hole: nothing shows file page 2 any more, so its frame goes back to the OS.
#slide(
  title: [fallocate gives frame B back to the OS],
  sec: [Implementation],
  src: ("arena.cc", 114, none),
)[
  #grid(
    columns: (auto, auto, auto, auto),
    column-gutter: 1.2cm,
    align: bottom,
    layer-labels,
    layers([Before], ((1, 1), (2, 1)), ((1, "A"), (2, "B"))),
    text(size: 30pt, fill: soft)[→],
    layers([After], ((1, 1), (2, 1)), ((1, "A"),), hole: (2,), freed: ("B",)),
  )
  #v(0.3cm)
  #code(width: 100%, src: ("arena.cc", 114, none), ```
  fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)page << kPageShift, len);
  ```)
]
