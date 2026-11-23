#import "../lib.typ": *

// Figure: experiments/results/redis_pss_20s.png from experiments/plot.py, copied unchanged.
#slide(
  title: [Mesh saves about 40% on Redis, as the paper claims],
  sec: [Results],
  takeaway: [Live data is 100 MiB in every run. Mesh settles in 5 s, the built-in defragmenter in 13 s.],
)[
  #image("../figures/redis_pss_20s.png", height: 6.9cm)
]

// Numbers: experiments/results/microbench.json (stride mode, PSS after meshing).
#slide(
  title: [Without randomization, meshing saves nothing],
  sec: [Results],
  takeaway: [2 M objects, every 8th kept: in-order slots make every page identical],
)[
  #note[Memory after meshing, MiB (143 before)]
  #v(0.2cm)
  #hbars(
    max: 150,
    width: 10cm,
    gutter: 0.3cm,
    ([Original, random], 63),
    ([Original, in order], 143, bar-weak),
    ([Ours, random], 58),
    ([Ours, in order], 143, bar-weak),
  )
]

// Figure: experiments/results/ruby_pss.png from experiments/plot.py, copied unchanged.
// Numbers: experiments/results/ruby/summary.json (mean PSS).
#slide(
  title: [Ruby: big savings, but randomization did not matter],
  sec: [Results],
  takeaway: [Mean memory: jemalloc 521 MiB, Mesh 255, ours 224. In-order slots gave the same result.],
)[
  #image("../figures/ruby_pss.png", height: 6.9cm)
]
