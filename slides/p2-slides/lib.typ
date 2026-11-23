// Mesh presentation: deck style and slide helpers.
// Grayscale only: hierarchy comes from size, weight, and spacing.
// No text on a slide is smaller than 24pt (see AGENTS.md).
#import "@preview/fletcher:0.5.8" as fletcher: diagram, node, edge

#let ink = luma(15)
#let body-ink = luma(28)
#let mid = luma(95)
#let soft = luma(140)
#let faint = luma(200)
#let wash = luma(236)
#let bar-strong = luma(55)
#let bar-weak = luma(165)

#let deck(body) = {
  set document(
    title: "Mesh: Compacting Memory Management for C/C++ Applications",
    author: "Vedant Acharya, Ruddhi Shah",
  )
  set page(paper: "presentation-16-9", margin: (x: 1.7cm, top: 1.45cm, bottom: 1.55cm))
  set text(font: ("Lato", "Source Sans 3", "Noto Sans"), size: 24pt, fill: body-ink, lang: "en", hyphenate: false, costs: (runt: 4000%, widow: 400%, orphan: 400%))
  set par(leading: 0.58em, spacing: 0.9em, linebreaks: "optimized")
  set list(indent: 0pt, body-indent: 0.55em, spacing: 0.75em, marker: text(fill: soft)[•])
  set enum(indent: 0pt, body-indent: 0.55em, spacing: 0.75em)
  set table(stroke: none, inset: (x: 11pt, y: 8pt))
  show table: set text(size: 24pt, number-width: "tabular")
  // Table headings are always larger and stronger than the table body.
  show table.cell.where(y: 0): it => { set text(size: 1.08em, weight: "bold", fill: ink); set strong(delta: 0); it }
  show "→": set text(font: ("Lete Sans Math", "New Computer Modern Math"))
  // Keep hyphenated names on one line.
  show regex("\\w[\\w.]*(-\\w[\\w.]*)+"): it => box(it)
  show math.equation: set text(font: ("Lete Sans Math", "New Computer Modern Math"))
  show math.equation.where(block: true): set text(size: 26pt)
  // Code: condensed monospace at 24pt fits about 61 characters per slide width.
  show raw: set text(font: ("Inconsolata", "DejaVu Sans Mono"), stretch: 75%, size: 24pt)
  // Never break a line inside "a->b".
  show raw: it => { show regex("[\w)\]]+->\w+"): box; it }
  set strong(delta: 300)
  body
}

// Booktabs-style table rules.
#let toprule = table.hline(stroke: 1.1pt + luma(30))
#let midrule = table.hline(stroke: 0.6pt + luma(150))
#let lightrule = table.hline(stroke: 0.4pt + luma(205))
#let bottomrule = table.hline(stroke: 1.1pt + luma(30))

#let corner-text(body) = text(size: 24pt, fill: soft, body)

// Links to our code on GitHub (main branch). Update line numbers if the code changes.
#let repo-commit = "main"
// src: (file in mymesh/src, first line, last line or none)
#let src-url(src) = {
  let (file, a, b) = src
  let lines = if b == none { "#L" + str(a) } else { "#L" + str(a) + "-L" + str(b) }
  "https://github.com/Ninjacoder-vedant/mesh-implementation/blob/" + repo-commit + "/mymesh/src/" + file + lines
}
#let src-label(src) = {
  let (file, a, b) = src
  file + ":" + str(a) + if b != none { "-" + str(b) }
}

// Footer: where we are in the talk (left), link to the code (center), page number (right).
#let page-footer(sec, src: none) = context {
  let pg = counter(page).display("1")
  pad(bottom: -0.75cm, grid(
    columns: (1fr, auto, 1fr),
    align: (left + bottom, center + bottom, right + bottom),
    if sec != none { corner-text(sec) } else { [] },
    if src != none {
      link(src-url(src), text(size: 24pt, fill: mid, underline(offset: 3pt, src-label(src)) + [ ↗]))
    } else { [] },
    corner-text(pg),
  ))
}

// Every slide starts with a marker that records its page.
// A page without a marker is an overflow continuation of the previous slide.
#let marker = context [#metadata(here().page())<slide-start>]

// Lays out a slide body. Records its height, and an <overflow> marker if it does not fit.
#let fit(body) = layout(size => {
  let m = measure(body, width: size.width)
  context [#metadata((page: here().page(), h: calc.round(m.height / 1cm, digits: 2), avail: calc.round(size.height / 1cm, digits: 2)))<body-size>]
  if m.height > size.height + 0.5pt or m.width > size.width + 0.5pt {
    context [#metadata((page: here().page(), tall: repr(m.height - size.height), wide: repr(m.width - size.width)))<overflow>]
  }
  body
})

// Centered text whose lines fill evenly, so no line is left with a word or two.
#let balanced(frac: 95%, body) = layout(sz => {
  let full = sz.width * (frac / 100%)
  let height(w) = measure(block(width: w, body)).height
  let target = height(full)
  let lo = full * 0.45
  let hi = full
  for _ in range(9) {
    let mid = (lo + hi) / 2
    if height(mid) <= target + 0.1pt { hi = mid } else { lo = mid }
  }
  align(center, block(width: hi + 1pt, align(center, body)))
})

// Left-aligned text set as narrow as its line count allows, so the last line is not left with a word or two.
#let tight(body) = layout(sz => {
  let height(w) = measure(block(width: w, body)).height
  let target = height(sz.width)
  let lo = sz.width * 0.35
  let hi = sz.width
  for _ in range(10) {
    let mid = (lo + hi) / 2
    if height(mid) <= target + 0.1pt { hi = mid } else { lo = mid }
  }
  block(width: hi + 1pt, body)
})

// The finding of a slide. It spans the text width and sits directly under the exhibit.
#let band(body) = block(width: 100%, fill: wash, radius: 4pt, inset: (x: 0.7cm, y: 0.36cm), balanced(frac: 100%, text(size: 24pt, fill: ink, body)))

// Title on top. The exhibit and its finding are one group, centered in the space below.
#let slide(title: none, sec: none, takeaway: none, src: none, body) = page(
  footer: page-footer(sec, src: src),
  footer-descent: 0.35cm,
  {
    marker
    grid(
      columns: (1fr,),
      rows: (auto, 1fr),
      row-gutter: 0.45cm,
      text(size: 32pt, weight: "bold", fill: ink, title),
      align(center + horizon, pad(bottom: 0.2cm, fit({
        body
        if takeaway != none {
          v(0.05cm)
          band(takeaway)
        }
      }))),
    )
  },
)

#let section-slide(number, total, question) = page(
  footer: page-footer(none),
  footer-descent: 0.35cm,
  {
    marker
    align(center + horizon, pad(bottom: 0.5cm, block(width: 96%, {
      text(size: 24pt, weight: "bold", fill: soft)[#number / #total]
      v(0.35cm)
      set par(leading: 0.45em)
      balanced(frac: 100%, text(size: 40pt, weight: "bold", fill: ink, question))
    })))
  },
)

// The main explanatory sentence under an exhibit.
#let say(size: 24pt, body) = balanced(text(size: size, body))
// Small gray text for captions and caveats.
#let note(body) = text(size: 24pt, fill: mid, body)
#let cap(body) = balanced(frac: 98%, note(body))
#let head(body) = text(size: 26pt, weight: "bold", fill: ink, body)
#let nil = text(fill: soft)[–]

// Left-aligned block inside a centered slide body.
#let lblock(width: 90%, body) = block(width: width, align(left, body))

// A term in bold on the left, its meaning on the right.
#let deflist(size: 24pt, gutter: 0.38cm, name-width: auto, ..rows) = block(width: 100%, align(left, grid(
  columns: (name-width, 1fr),
  column-gutter: 0.8cm,
  row-gutter: gutter,
  align: (left + top, left + top),
  ..rows.pos().map(r => (text(size: size, weight: "bold", fill: ink, r.at(0)), text(size: size, r.at(1)))).flatten(),
)))

// A titled block of text with a rule above it.
#let card(title, body) = block(
  width: 100%,
  stroke: (top: 1.1pt + luma(30)),
  inset: (top: 0.3cm),
  align(left, {
    text(size: 26pt, weight: "bold", fill: ink, title)
    v(0.12cm)
    set text(size: 24pt)
    set par(leading: 0.55em, spacing: 0.6em)
    tight(body)
  }),
)

#let commas(n) = {
  let out = ""
  for (i, ch) in str(n).clusters().rev().enumerate() {
    if i > 0 and calc.rem(i, 3) == 0 { out = "," + out }
    out = ch + out
  }
  out
}

// One decimal place, always.
#let f1(v) = {
  let s = str(calc.round(v, digits: 1))
  if s.contains(".") { s } else { s + ".0" }
}

// Color key for charts.
#let keys(..items) = {
  set text(size: 24pt, fill: mid)
  items.pos().map(it => box(width: 0.42cm, height: 0.42cm, fill: it.at(1), radius: 1pt) + h(0.28em) + it.at(0)).join(h(1cm))
}

// Horizontal bars on a fixed track, so lengths are honest.
// Each row: (label, value) or (label, value, fill). A value of none shows a dash.
#let hbars(max: 100, width: 13cm, size: 24pt, fmt: v => str(v), gutter: 0.36cm, ..rows) = grid(
  columns: (auto, width, auto),
  column-gutter: (0.45cm, 0.25cm),
  row-gutter: gutter,
  align: (right + horizon, left + horizon, left + horizon),
  ..rows.pos().map(r => {
    let v = r.at(1)
    (
      text(size: size, r.at(0)),
      box(width: width, height: size * 0.95, fill: wash, radius: 1.5pt,
        if v != none { place(left, box(width: width * v / max, height: size * 0.95, fill: r.at(2, default: bar-strong), radius: 1.5pt)) }),
      text(size: size, weight: "bold", if v != none { fmt(v) } else { nil }),
    )
  }).flatten(),
)

// Bars in pairs, one pair per row. fills gives the two colors.
#let groupbars(max: 100, width: 11cm, size: 24pt, fills: (bar-strong, bar-weak), fmt: f1, gutter: 0.34cm, bold: 0, ..rows) = grid(
  columns: (auto, width, auto),
  column-gutter: (0.45cm, 0.25cm),
  row-gutter: (0.08cm, gutter) * rows.pos().len(),
  align: (right + horizon, left + horizon, left + horizon),
  ..rows.pos().map(r => (
    grid.cell(rowspan: 2, text(size: size, r.at(0))),
    box(width: width, height: size * 0.82, fill: wash, radius: 1.5pt,
      place(left, box(width: width * r.at(1) / max, height: size * 0.82, fill: fills.at(0), radius: 1.5pt))),
    text(size: size, weight: if bold == 0 { "bold" } else { "regular" }, fmt(r.at(1))),
    box(width: width, height: size * 0.82, fill: wash, radius: 1.5pt,
      place(left, box(width: width * r.at(2) / max, height: size * 0.82, fill: fills.at(1), radius: 1.5pt))),
    text(size: size, weight: if bold == 1 { "bold" } else { "regular" }, fmt(r.at(2))),
  )).flatten(),
)

// Bars around a center line. Each row: (label, difference, then one value per column).
// sides labels the two directions, and either label can be none.
#let diverge(unit: 0.5cm, neg: 5cm, pos: 5cm, size: 24pt, gutter: 0.24cm, heads: (), sides: (none, none), rowh: none, ..rows) = {
  let h = size * 0.92
  let full = if rowh == none { h } else { rowh }
  let lift = (full - h) / 2
  let side(body) = text(size: size + 1.5pt, weight: "bold", fill: ink, body)
  let side-row = box(width: neg + pos, height: size, {
    if sides.at(0) != none { place(left + horizon, box(width: neg - 0.15cm, align(right, side(sides.at(0))))) }
    if sides.at(1) != none { place(left + horizon, dx: neg + 0.15cm, box(width: pos - 0.15cm, align(left, side(sides.at(1))))) }
  })
  grid(
    columns: (auto, neg + pos) + (auto,) * heads.len(),
    column-gutter: (0.45cm, 0.55cm) + (0.55cm,) * calc.max(heads.len() - 1, 0),
    row-gutter: (gutter + 0.14cm,) + (gutter,) * rows.pos().len(),
    align: (right + horizon, left + horizon) + (right + horizon,) * heads.len(),
    [], side-row, ..heads.map(x => text(size: size + 1.5pt, weight: "bold", fill: ink, x)),
    ..rows.pos().map(r => {
      let d = r.at(1)
      (
        text(size: size, r.at(0)),
        box(width: neg + pos, height: full, {
          if d >= 0 {
            place(left, dx: neg, dy: lift, box(width: d * unit, height: h, fill: bar-strong))
          } else {
            place(left, dx: neg + d * unit, dy: lift, box(width: -d * unit, height: h, fill: bar-weak))
          }
          place(left, dx: neg - 0.4pt, line(angle: 90deg, length: full + gutter, stroke: 0.8pt + luma(80)))
        }),
        ..r.slice(2).map(v => text(size: size, v)),
      )
    }).flatten(),
  )
}

// Line chart with one y axis. Series are told apart by dash pattern and direct labels.
// series: array of dictionaries (name, ys, dash, dy).
#let linechart(
  w: 11cm,
  h: 5.6cm,
  xs: (),
  xticks: (),
  ymin: 0,
  ymax: 1,
  yticks: (),
  yfmt: str,
  series: (),
  labelw: 4.6cm,
  xtitle: none,
  size: 24pt,
) = {
  let lm = 1.25cm
  let xmin = xs.first()
  let xmax = xs.last()
  let px(x) = w * (x - xmin) / (xmax - xmin)
  let py(y) = h - h * (y - ymin) / (ymax - ymin)
  let at(x, y, body) = place(top + left, dx: x, dy: y, body)
  box(width: lm + w + labelw, height: h + (if xtitle != none { 1.6cm } else { 0.95cm }), {
    set text(size: size, number-width: "tabular", fill: mid)
    for t in yticks {
      at(lm, py(t), line(length: w, stroke: 0.4pt + faint))
      at(0pt, py(t) - 0.5em, box(width: lm - 0.25cm, align(right, yfmt(t))))
    }
    for (x, label) in xs.zip(xticks) {
      at(lm + px(x) - 1.2cm, h + 0.32cm, box(width: 2.4cm, align(center, label)))
    }
    if xtitle != none {
      at(lm, h + 0.98cm, box(width: w, align(center, xtitle)))
    }
    for s in series {
      let stroke = (paint: ink, thickness: 2pt, dash: s.at("dash", default: "solid"), cap: "round")
      for i in range(xs.len() - 1) {
        at(lm, 0pt, line(start: (px(xs.at(i)), py(s.ys.at(i))), end: (px(xs.at(i + 1)), py(s.ys.at(i + 1))), stroke: stroke))
      }
      for (x, y) in xs.zip(s.ys) {
        at(lm + px(x) - 4pt, py(y) - 4pt, circle(radius: 4pt, fill: ink, stroke: 1pt + white))
      }
      if s.name != none {
        at(lm + w + 0.3cm, py(s.ys.last()) - 0.5em + s.at("dy", default: 0pt), box(width: labelw - 0.3cm, text(fill: body-ink, s.name)))
      }
    }
  })
}

// Shared diagram style.
#let dg = diagram.with(
  node-shape: fletcher.shapes.rect,
  node-stroke: 0.8pt + luma(45),
  node-corner-radius: 4pt,
  node-inset: 12pt,
  edge-stroke: 0.8pt + luma(75),
  spacing: (1.2cm, 1cm),
)
#let t(body) = { set par(leading: 0.45em); text(size: 24pt, body) }
#let sub(body) = text(size: 24pt, fill: mid, body)
#let lbl(body) = text(size: 24pt, fill: mid, body)
// A numbered step: the number sits above the box, clear of the text.
#let step(n, width, height: auto, lift: 1.05cm, body) = box(width: width, height: height, {
  place(top + center, dy: -lift, text(size: 24pt, weight: "bold", fill: luma(120), str(n)))
  align(center + horizon, { set par(leading: 0.45em); text(size: 24pt, body) })
})
// Fixed-width node content, so a row of nodes lines up.
#let nb(width, height: auto, size: 24pt, body) = box(width: width, height: height, align(center + horizon, { set par(leading: 0.45em); text(size: size, body) }))
// A process step: the number sits above the box, and the name and detail are top-aligned so names line up.
#let stepcard(n, name, width: 5cm, height: 3.3cm, lift: 1.05cm, size: 24pt, body) = box(width: width, height: height, {
  place(top + center, dy: -lift, text(size: 24pt, weight: "bold", fill: luma(120), str(n)))
  align(center + top, {
    set par(leading: 0.5em)
    v(0.3cm)
    text(size: 26pt, weight: "bold", fill: ink, name)
    v(0.26cm)
    text(size: size, body)
  })
})
// Text rows with no heading row: a statement on the left and plain detail on the right.
#let rowlist(widths: (1fr, 1.45fr), size: 24pt, y: 12pt, ..rows) = {
  set text(size: size)
  grid(
    columns: widths,
    align: left + horizon,
    inset: (x: 12pt, y: y),
    grid.hline(stroke: 1.1pt + luma(30)),
    ..rows.pos().map(r => (text(weight: "bold", fill: ink, tight(r.at(0))), tight(r.at(1)))).intersperse((grid.hline(stroke: 0.4pt + luma(205)),)).flatten(),
    grid.hline(stroke: 1.1pt + luma(30)),
  )
}
// Text slides are tables: a heading row, light rules, and even line breaks in every cell.
#let texttable(heads: (), widths: (1fr, 1.45fr), size: 24pt, y: 12pt, ..rows) = {
  show table: set text(size: size)
  set table(inset: (x: 12pt, y: y))
  table(
    columns: widths,
    align: left + horizon,
    toprule,
    ..heads,
    midrule,
    ..rows.pos().map(r => r.map(c => tight(c))).intersperse((lightrule,)).flatten(),
    bottomrule,
  )
}

// A code excerpt from our implementation: shaded block, left-aligned, 24pt.
// Snippets are exact excerpts of our code; src points to the same lines on GitHub.
// With src, the whole block links to those lines on GitHub.
#let code(width: auto, src: none, body) = {
  let b = block(
    width: width,
    fill: wash,
    radius: 4pt,
    inset: (x: 0.6cm, y: 0.4cm),
    align(left, { set par(leading: 0.32em); body }),
  )
  if src != none { link(src-url(src), b) } else { b }
}

// A span (group of pages) drawn as a row of object slots.
// live: slots used by this span (dark). other: slots holding objects copied
// in from a meshed span (light). freed: draw an empty dashed outline instead.
#let slots(n: 8, cell: 0.62cm, live: (), other: (), mark: (), freed: false) = {
  if freed {
    return box(width: n * cell + (n - 1) * 3pt, height: cell, radius: 3pt, stroke: (paint: luma(150), thickness: 1pt, dash: "dashed"))
  }
  box(grid(
    columns: n,
    column-gutter: 3pt,
    ..range(n).map(i => box(
      width: cell,
      height: cell,
      radius: 2pt,
      fill: if i in live { bar-strong } else if i in other { bar-weak } else { wash },
      stroke: if i in mark { 2.2pt + ink } else { 0.6pt + luma(160) },
    )),
  ))
}
