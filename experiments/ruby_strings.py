#!/usr/bin/env python3
"""Ruby experiment (paper section 6.3, Figure 8): shows that randomized
allocation is needed for meshing when the program frees memory in a regular
pattern. Runs ruby/strings.rb under each allocator and records RSS over time.

Usage: ruby_strings.py [config ...]
"""

import json
import os
import subprocess
import sys
import time

from common import HERE, MINIRUBY, RESULTS, RssSampler, env_for, save_csv, summarize

CONFIGS = ["glibc", "jemalloc", "mesh", "mesh-nomesh", "mesh-norand", "mymesh", "mymesh-nomesh", "mymesh-norand"]
SCRIPT = os.path.join(HERE, "ruby", "strings.rb")


def run(name, repeat=3):
    outdir = os.path.join(RESULTS, "ruby")
    os.makedirs(outdir, exist_ok=True)
    best = None
    for rep in range(repeat):
        t0 = time.monotonic()
        p = subprocess.Popen([MINIRUBY, SCRIPT], env=env_for(name, {"MYMESH_STATS": "1"}),
                             stderr=subprocess.PIPE, text=True)
        sampler = RssSampler(p.pid, interval=0.05).start()
        _, err = p.communicate()
        secs = time.monotonic() - t0
        samples = sampler.stop()
        r = {"config": name, "seconds": round(secs, 3), "exit_code": p.returncode, "stderr": err.strip()[-300:]}
        r.update(summarize(samples))
        if rep == 0:
            save_csv(os.path.join(outdir, f"{name}.csv"), samples)  # keep the first run's curve
            first = r
        best = secs if best is None else min(best, secs)
    first["best_seconds"] = round(best, 3)
    return first


def main():
    names = sys.argv[1:] or CONFIGS
    results = []
    for n in names:
        r = run(n)
        print(f"{n:16s} PSS mean={r['mean_pss_mib']:6.1f} peak={r['peak_pss_mib']:6.1f} MiB | RSS mean={r['mean_rss_mib']:6.1f}  "
              f"time={r['best_seconds']:.2f}s  exit={r['exit_code']}  {r['stderr'].splitlines()[-1] if r['stderr'] else ''}",
              flush=True)
        results.append(r)
    path = os.path.join(RESULTS, "ruby", "summary.json")
    old = {r["config"]: r for r in json.load(open(path))} if os.path.exists(path) else {}
    old.update({r["config"]: r for r in results})
    json.dump(list(old.values()), open(path, "w"), indent=2)


if __name__ == "__main__":
    main()
