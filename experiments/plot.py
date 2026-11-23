#!/usr/bin/env python3
"""Make the figures from results/*/ (CSV files and summary.json)."""

import csv
import json
import os
import re

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from common import RESULTS

STYLE = {  # config -> (color, line style)
    "glibc": ("#888888", "-"), "jemalloc": ("#1f77b4", "-"), "jemalloc+activedefrag": ("#1f77b4", "--"),
    "mesh": ("#d62728", "-"), "mesh-nomesh": ("#d62728", ":"), "mesh-norand": ("#d62728", "-."),
    "mymesh": ("#2ca02c", "-"), "mymesh-nomesh": ("#2ca02c", ":"), "mymesh-norand": ("#2ca02c", "-."),
}


def load(path):
    xs, ys = [], []
    with open(path) as f:
        for row in csv.DictReader(f):
            xs.append(float(row["seconds"]))
            ys.append(float(row["pss_mib"]))
    return xs, ys


def rss_plot(exp, title, out, order):
    d = os.path.join(RESULTS, exp)
    fig, ax = plt.subplots(figsize=(8, 4.5))
    for name in order:
        p = os.path.join(d, name + ".csv")
        if os.path.exists(p):
            xs, ys = load(p)
            c, ls = STYLE.get(name, ("black", "-"))
            ax.plot(xs, ys, label=name, color=c, linestyle=ls, linewidth=1.6)
    ax.set_xlabel("Time since start (seconds)")
    ax.set_ylabel("Physical memory, PSS (MiB)")
    ax.set_title(title)
    ax.set_ylim(bottom=0)
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8, ncol=2)
    fig.tight_layout()
    fig.savefig(os.path.join(RESULTS, out), dpi=130)
    plt.close(fig)


def overhead_plot():
    p = os.path.join(RESULTS, "overhead", "summary.json")
    if not os.path.exists(p):
        return
    rows = json.load(open(p))
    wls = list(dict.fromkeys(r["workload"] for r in rows))
    cfgs = list(dict.fromkeys(r["config"] for r in rows))
    get = {(r["workload"], r["config"]): r for r in rows}
    # larson runs for a fixed time and reports throughput: use time per operation instead
    for r in rows:
        m = re.search(r"Throughput = (\d+)", r.get("note", ""))
        if m:
            r["median_seconds"] = 1e9 / float(m.group(1))  # ns per operation
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    w = 0.8 / len(cfgs)
    for k, (key, label) in enumerate([("median_seconds", "Time (larson: time per op) vs glibc, log scale"),
                                      ("peak_rss_mib", "Peak RSS vs glibc")]):
        ax = axes[k]
        for j, c in enumerate(cfgs):
            vals = [get[(wl, c)][key] / get[(wl, "glibc")][key] for wl in wls]
            ax.bar([i + j * w for i in range(len(wls))], vals, w, label=c, color=STYLE.get(c, ("k", ""))[0])
        ax.axhline(1.0, color="black", linewidth=0.8)
        if k == 0:
            ax.set_yscale("log")  # mymesh's global lock makes larson-4t ~70x slower
        ax.set_xticks([i + w * (len(cfgs) - 1) / 2 for i in range(len(wls))])
        ax.set_xticklabels(wls)
        ax.set_title(label + " (lower is better)")
        ax.grid(axis="y", alpha=0.3)
    axes[0].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(RESULTS, "overhead.png"), dpi=130)
    plt.close(fig)


# The slides use these exact PNGs. Typst can only read files inside the slides
# folder, so copy them there unchanged.
SLIDE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "slides", "p2-slides", "figures")


def copy_to_slides():
    import shutil
    os.makedirs(SLIDE_DIR, exist_ok=True)
    for name in ["redis_pss_20s.png", "ruby_pss.png"]:
        shutil.copyfile(os.path.join(RESULTS, name), os.path.join(SLIDE_DIR, name))
    print("copied figures to", SLIDE_DIR)


if __name__ == "__main__":
    redis_order = ["jemalloc", "jemalloc+activedefrag", "glibc", "mesh", "mesh-nomesh", "mymesh", "mymesh-nomesh",
                   "mymesh-norand"]
    rss_plot("redis", "Redis fragmentation test, 7.5 s like the paper (Fig. 7)", "redis_pss.png", redis_order)
    rss_plot("redis-20s", "Redis fragmentation test, 20 s window", "redis_pss_20s.png", redis_order)
    rss_plot("ruby", "Ruby string benchmark (paper Fig. 8)", "ruby_pss.png",
             ["glibc", "jemalloc", "mesh", "mesh-nomesh", "mesh-norand", "mymesh", "mymesh-nomesh", "mymesh-norand"])
    overhead_plot()
    print("figures written to", RESULTS)
    copy_to_slides()
