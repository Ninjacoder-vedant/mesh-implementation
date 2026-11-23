#!/usr/bin/env python3
"""Redis fragmentation experiment (paper section 6.2.2, Figure 7).

Same steps as the paper (adapted from Redis's own "active defrag" test):
  - Redis is an LRU cache with maxmemory 100 MB
  - insert 700,000 keys with 240-byte values
  - insert 170,000 keys with 492-byte values (old keys get evicted -> holes)
  - record RSS every 10 ms for 7.5 s in total
The "jemalloc+activedefrag" run turns on Redis's own defragmenter after the
inserts, like the paper.

Usage: redis_frag.py [config ...]   (default: all configs below)
"""

import json
import os
import subprocess
import sys
import time

from common import (REDIS_CLI, REDIS_JEMALLOC, REDIS_LIBC, RESULTS, RssSampler, env_for, save_csv,
                    summarize)

PORT = "6399"
DURATION = float(os.environ.get("REDIS_DURATION", "7.5"))  # paper: 7.5 s
OUTNAME = "redis" if DURATION == 7.5 else f"redis-{DURATION:g}s"  # results/<OUTNAME>/

# config name -> (redis binary, allocator, use activedefrag)
CONFIGS = {
    "jemalloc": (REDIS_JEMALLOC, "glibc", False),            # Redis default build (bundled jemalloc)
    "jemalloc+activedefrag": (REDIS_JEMALLOC, "glibc", True),
    "glibc": (REDIS_LIBC, "glibc", False),
    "mesh": (REDIS_LIBC, "mesh", False),
    "mesh-nomesh": (REDIS_LIBC, "mesh-nomesh", False),
    "mymesh": (REDIS_LIBC, "mymesh", False),
    "mymesh-nomesh": (REDIS_LIBC, "mymesh-nomesh", False),
    "mymesh-norand": (REDIS_LIBC, "mymesh-norand", False),
}


def cli(*args):
    out = subprocess.run([REDIS_CLI, "-p", PORT, *args], capture_output=True, text=True)
    return out.stdout.strip()


def info_field(section, field):
    for line in cli("info", section).splitlines():
        if line.startswith(field + ":"):
            return line.split(":", 1)[1].strip()
    return None


def run(name):
    binary, alloc, defrag = CONFIGS[name]
    logdir = os.path.join(RESULTS, OUTNAME)
    os.makedirs(logdir, exist_ok=True)
    stats_path = os.path.join(logdir, f"{name}.stderr")
    server = subprocess.Popen(
        [binary, "--port", PORT, "--save", "", "--appendonly", "no", "--maxmemory", "100mb",
         "--maxmemory-policy", "allkeys-lru", "--enable-debug-command", "yes", "--daemonize", "no",
         "--logfile", os.path.join(logdir, f"{name}.log")],
        env=env_for(alloc, {"MYMESH_STATS": "2", "MALLOCSTATS": "1"}),
        stderr=open(stats_path, "w"))
    for _ in range(200):  # wait until the server answers
        if cli("ping") == "PONG":
            break
        time.sleep(0.05)

    sampler = RssSampler(server.pid, interval=0.1).start()  # smaps_rollup is slow; sampling faster slows Redis down
    t0 = time.monotonic()
    cli("debug", "populate", "700000", "asdf1", "240")
    cli("debug", "populate", "170000", "asdf2", "492")
    insert_s = time.monotonic() - t0
    if defrag:  # same settings as Redis's own defrag test
        for k, v in [("active-defrag-cycle-min", "65"), ("active-defrag-cycle-max", "75"),
                     ("active-defrag-ignore-bytes", "2mb"), ("activedefrag", "yes")]:
            cli("config", "set", k, v)
    time.sleep(max(0.0, DURATION - (time.monotonic() - t0)))

    result = {
        "config": name,
        "insert_seconds": round(insert_s, 3),
        "keys": int(cli("dbsize") or 0),
        "used_memory_mib": int(info_field("memory", "used_memory") or 0) / 1048576,
        "redis_frag_ratio": info_field("memory", "mem_fragmentation_ratio"),
        "evicted_keys": info_field("stats", "evicted_keys"),
        "defrag_hits": info_field("stats", "active_defrag_hits"),
    }
    cli("shutdown", "nosave")
    server.wait(timeout=30)
    samples = sampler.stop()
    save_csv(os.path.join(logdir, f"{name}.csv"), samples)
    result.update(summarize(samples))
    with open(stats_path) as f:
        stats = f.read()
    # keep the one-line summaries (mymesh: "[mymesh] meshes=...", original Mesh: "MESH COUNT: ...")
    result["allocator_stats"] = [l for l in stats.splitlines() if "meshes=" in l or "MESH COUNT" in l]
    return result


def main():
    names = sys.argv[1:] or list(CONFIGS)
    results = []
    for n in names:
        r = run(n)
        print(f"{n:24s} PSS mean={r['mean_pss_mib']:6.1f} final={r['final_pss_mib']:6.1f} peak={r['peak_pss_mib']:6.1f} MiB | "
              f"RSS final={r['final_rss_mib']:6.1f} | insert={r['insert_seconds']:.2f}s keys={r['keys']}",
              flush=True)
        results.append(r)
    path = os.path.join(RESULTS, OUTNAME, "summary.json")
    old = {}
    if os.path.exists(path):
        old = {r["config"]: r for r in json.load(open(path))}
    old.update({r["config"]: r for r in results})
    json.dump(list(old.values()), open(path, "w"), indent=2)


if __name__ == "__main__":
    main()
