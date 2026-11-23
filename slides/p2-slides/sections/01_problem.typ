#import "../lib.typ": *

// Problem: fragmentation that C/C++ allocators cannot undo.
#slide(
  title: [C/C++ heaps waste memory they cannot compact],
  sec: [Problem],
  takeaway: [Redis holds 100 MiB of data but uses 287 MiB with jemalloc],
)[
  #grid(
    columns: (auto, 1fr),
    column-gutter: 1.4cm,
    align: horizon,
    block({
      stack(
        spacing: 0.38cm,
        slots(live: (2,)),
        slots(live: (0, 5)),
        slots(live: (6,)),
        slots(live: (3,)),
      )
      v(0.6cm)
      align(center, keys(([live object], bar-strong), ([free slot], wash)))
    }),
    lblock(width: 100%)[
      - One live object keeps its whole page in memory
      - Objects cannot be moved: pointers to them would break
    ],
  )
]

// Solution at a high level: meshing.
#let pg(..args) = slots(n: 6, cell: 0.5cm, ..args)
#slide(
  title: [Mesh merges sparse pages without moving objects],
  sec: [Solution],
  takeaway: [Every object keeps its address. One physical page goes back to the OS.],
)[
  #dg(
    spacing: (0.75cm, 1.5cm),
    node-stroke: none,
    node-inset: 5pt,
    node((0.5, -0.75), text(weight: "bold")[Before]),
    node((3.5, -0.75), text(weight: "bold")[After]),
    node((2, 0), lbl[virtual]),
    node((2, 1), lbl[physical]),
    // before: each virtual page has its own physical page
    node((0, 0), pg(live: (1, 4)), name: <va>),
    node((1, 0), pg(other: (2, 5)), name: <vb>),
    node((0, 1), pg(live: (1, 4)), name: <pa>),
    node((1, 1), pg(other: (2, 5)), name: <pb>),
    edge(<va>, <pa>, "-|>"),
    edge(<vb>, <pb>, "-|>"),
    // after: both virtual pages map to one physical page
    node((3, 0), pg(live: (1, 4)), name: <va2>),
    node((4, 0), pg(other: (2, 5)), name: <vb2>),
    node((3, 1), pg(live: (1, 4), other: (2, 5)), name: <pa2>),
    node((4, 1), pg(freed: true), name: <pb2>),
    node((4, 1.55), lbl[freed]),
    edge(<va2>, <pa2>, "-|>"),
    edge(<vb2>, <pa2>, "-|>", stroke: 2pt + ink),
  )
]
