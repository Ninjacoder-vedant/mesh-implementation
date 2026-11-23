#!/usr/bin/env python3
"""Run time and peak memory on allocation-heavy programs.

The paper uses SPECint2006 for this (section 6.2.3). SPEC is paid software,
so we use free programs that stress malloc in similar ways:
  perl    : perlbench-like mail filtering script (paper's most allocator-sensitive SPEC program is perlbench)
  python  : dict/list/string churn + JSON
  gcc     : compile Ruby's parse.c (about 15k lines) with -O2 (SPEC also has gcc)
  larson  : multi-threaded server-like malloc/free stress (from the Mesh repo)

Each program runs 3 times per allocator; we report the median time and the
peak RSS (from the kernel, via wait4).

Usage: overhead.py [config ...]
"""

import json
import os
import statistics
import subprocess
import sys
import time

from common import BUILD, HERE, RESULTS, env_for

CONFIGS = ["glibc", "jemalloc", "mesh", "mymesh"]
RUBY_SRC = os.path.join(BUILD, "ruby")
LARSON = os.path.join(BUILD, "larson")

WORKLOADS = {
    "perl": (["perl", os.path.join(HERE, "overhead", "perl_mail.pl"), "60000"], None),
    "python": (["python3", os.path.join(HERE, "overhead", "py_churn.py")], None),
    "gcc": (["gcc", "-O2", "-w", "-c", "parse.c", "-o", "/dev/null", "-I.", "-Iinclude",
             "-I.ext/include/x86_64-linux"], RUBY_SRC),
    "larson-1t": ([LARSON, "3", "8", "1000", "5000", "100", "4141", "1"], None),
    "larson-4t": ([LARSON, "3", "8", "1000", "5000", "100", "4141", "4"], None),
}


def run_once(cmd, cwd, env):
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    _, status, ru = os.wait4(p.pid, 0)
    secs = time.monotonic() - t0
    out = p.stdout.read()
    p.stdout.close()
    p.stderr.close()
    return secs, ru.ru_maxrss / 1024.0, os.waitstatus_to_exitcode(status), out


def main():
    names = sys.argv[1:] or CONFIGS
    results = []
    for wl, (cmd, cwd) in WORKLOADS.items():
        for name in names:
            runs = [run_once(cmd, cwd, env_for(name)) for _ in range(3)]
            r = {"workload": wl, "config": name,
                 "median_seconds": round(statistics.median(x[0] for x in runs), 3),
                 "peak_rss_mib": round(max(x[1] for x in runs), 1),
                 "exit_codes": [x[2] for x in runs]}
            if wl.startswith("larson"):  # larson prints throughput; higher is better
                line = [l for l in runs[0][3].splitlines() if "Throughput" in l]
                r["note"] = line[0] if line else ""
            print(f"{wl:10s} {name:10s} time={r['median_seconds']:7.2f}s  peak={r['peak_rss_mib']:7.1f} MiB  "
                  f"exit={r['exit_codes']} {r.get('note', '')}", flush=True)
            results.append(r)
    os.makedirs(os.path.join(RESULTS, "overhead"), exist_ok=True)
    json.dump(results, open(os.path.join(RESULTS, "overhead", "summary.json"), "w"), indent=2)


if __name__ == "__main__":
    main()
