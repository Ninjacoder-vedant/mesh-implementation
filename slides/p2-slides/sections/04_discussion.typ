#import "../lib.typ": *

// Numbers: slides 15 to 17 (redis-20s, microbench, ruby summaries).
#slide(
  title: [Key takeaways],
  sec: [Discussion],
)[
  #rowlist(
    widths: (1fr, 1.1fr),
    ([Meshing saves memory without moving objects], [Redis: 41% (original) and 38% (ours) less than jemalloc]),
    ([Randomization makes meshing work], [Regular frees: 0% saved in order, 56 to 59% with random slots]),
    ([Our version matches the original], [Redis 137 vs 130 MiB, Ruby 224 vs 255 MiB]),
  )
]
