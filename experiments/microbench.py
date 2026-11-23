#!/usr/bin/env python3
"""Controlled fragmentation test (mymesh/tests/test_frag) under every allocator.

2,000,000 objects of 64 bytes, then 90% are freed, then meshing passes run.
  random : free a random 90%
  stride : keep every 8th object -> every page keeps the SAME slots when
           allocation is sequential. This is the regular pattern that the
           paper says needs randomization (section 6.3).
Reports physical memory (PSS) after meshing, and checks all data is intact.
"""

import json
import os
import re
import subprocess

from common import P2, RESULTS, env_for

TEST = os.path.join(P2, "mymesh", "tests", "test_frag")
CONFIGS = ["glibc", "jemalloc", "mesh", "mesh-nomesh", "mesh-norand", "mymesh", "mymesh-nomesh", "mymesh-norand"]


def main():
    rows = []
    for mode in ["random", "stride"]:
        for c in CONFIGS:
            proc = subprocess.run([TEST, "2000000", "64", "90", mode], env=env_for(c), capture_output=True, text=True)
            out = proc.stdout
            if proc.returncode != 0 and "full=" not in out:
                # the original Mesh built with meshing disabled aborts in mesh_mallctl("mesh.compact")
                err = (proc.stderr.strip().splitlines() or ["?"])[-1][:160]
                rows.append({"mode": mode, "config": c, "crashed": True, "error": err})
                print(f"{mode:7s} {c:14s} CRASHED: {err}", flush=True)
                continue
            full = float(re.search(r"full=([\d.]+)", out).group(1))
            pss = float(re.search(r"PSS MiB after_mesh=([\d.]+)", out).group(1))
            live = float(re.search(r"live data=([\d.]+)", out).group(1))
            ok = "data check: PASS" in out
            rows.append({"mode": mode, "config": c, "live_mib": live, "before_free_mib": full, "after_mesh_pss_mib": pss,
                         "data_ok": ok})
            print(f"{mode:7s} {c:14s} live={live:5.1f}  before={full:6.1f}  after={pss:6.1f} MiB  data_ok={ok}",
                  flush=True)
    os.makedirs(RESULTS, exist_ok=True)
    json.dump(rows, open(os.path.join(RESULTS, "microbench.json"), "w"), indent=2)


if __name__ == "__main__":
    main()
