#import "lib.typ": *
#show: deck

// Title
#page(margin: (x: 0.95cm, y: 0.8cm), header: none, footer: none, {
  marker
  align(center + horizon, block(width: 84%, {
    set par(leading: 0.42em)
    text(size: 44pt, weight: "bold", fill: ink)[Mesh]
    v(0.15cm)
    text(size: 27pt, weight: "bold", fill: luma(60))[Compacting Memory Management \ for C/C++ Applications]
    v(0.9cm)
    text(size: 24pt, fill: mid)[Bobby Powers, David Tench, Emery D. Berger, Andrew McGregor]
    v(0.1cm)
    text(size: 24pt, fill: soft)[PLDI 2019]
    v(1.1cm)
    text(size: 24pt, fill: luma(80))[Vedant, Ruddhi]
  }))
})

#include "sections/01_problem.typ"
#include "sections/02_implementation.typ"
#include "sections/03_results.typ"
#include "sections/04_discussion.typ"
