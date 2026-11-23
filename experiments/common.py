"""Shared settings for all experiments: where things are, the allocator
configurations we compare, and an RSS sampler (our version of the paper's
`mstat` tool, which records a process's memory over time)."""

import os
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
P2 = os.path.dirname(HERE)
RESULTS = os.path.join(HERE, "results")

# Big builds (Redis, Ruby, Mesh variants) live outside the project because the
# project disk is almost full. setup.sh creates them. Override with $BUILD.
BUILD = os.environ.get(
    "BUILD", "/tmp/mesh-build"
)
LIBS = os.path.join(BUILD, "libs")
MYMESH = os.path.join(P2, "mymesh", "libmymesh.so")

REDIS_LIBC = os.path.join(BUILD, "redis-libc", "src", "redis-server")       # uses malloc from libc -> LD_PRELOAD works
REDIS_JEMALLOC = os.path.join(BUILD, "redis-jemalloc", "src", "redis-server")  # Redis default, needed for activedefrag
REDIS_CLI = os.path.join(BUILD, "redis-libc", "src", "redis-cli")
MINIRUBY = os.path.join(BUILD, "ruby", "miniruby")

# name -> environment variables to add when running the program
ALLOCATORS = {
    "glibc": {},
    "jemalloc": {"LD_PRELOAD": "/usr/lib/libjemalloc.so"},
    "mesh": {"LD_PRELOAD": os.path.join(LIBS, "libmesh.so")},
    "mesh-nomesh": {"LD_PRELOAD": os.path.join(LIBS, "libmesh-nomesh.so")},
    "mesh-norand": {"LD_PRELOAD": os.path.join(LIBS, "libmesh-norand.so")},
    "mymesh": {"LD_PRELOAD": MYMESH},
    "mymesh-nomesh": {"LD_PRELOAD": MYMESH, "MYMESH_NO_MESH": "1"},
    "mymesh-norand": {"LD_PRELOAD": MYMESH, "MYMESH_NO_RANDOM": "1"},
}


def env_for(alloc, extra=None):
    env = dict(os.environ)
    env.pop("LD_PRELOAD", None)
    env.update(ALLOCATORS[alloc])
    if extra:
        env.update(extra)
    return env


def mem_kib(pid):
    """(RSS, PSS) of a process in KiB, or None if the process is gone.

    Why both: after meshing, one physical page is mapped at two (or more)
    virtual addresses. RSS counts it once per mapping, so it OVER-counts
    meshed memory. PSS counts each physical page once (it splits a page
    between all its mappings), so PSS is the real physical memory use.
    For allocators that don't mesh, RSS and PSS are almost equal."""
    rss = pss = None
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Rss:"):
                    rss = int(line.split()[1])
                elif line.startswith("Pss:"):
                    pss = int(line.split()[1])
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        return None
    if rss is None or pss is None:
        return None
    return rss, pss


class RssSampler:
    """Samples a process's RSS every `interval` seconds in a thread."""

    def __init__(self, pid, interval=0.01):
        self.pid, self.interval = pid, interval
        self.samples = []  # (seconds since start, PSS MiB, RSS MiB)
        self._stop = threading.Event()
        self._t = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.t0 = time.monotonic()
        self._t.start()
        return self

    def _run(self):
        while not self._stop.is_set():
            m = mem_kib(self.pid)
            if m is None:
                break
            self.samples.append((time.monotonic() - self.t0, m[1] / 1024.0, m[0] / 1024.0))
            time.sleep(self.interval)

    def stop(self):
        self._stop.set()
        self._t.join()
        return self.samples


def save_csv(path, samples):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("seconds,pss_mib,rss_mib\n")
        for t, p, r in samples:
            f.write(f"{t:.3f},{p:.2f},{r:.2f}\n")


def summarize(samples):
    if not samples:
        return {}
    pss = [p for _, p, _ in samples]
    rss = [r for _, _, r in samples]
    return {"mean_pss_mib": sum(pss) / len(pss), "peak_pss_mib": max(pss), "final_pss_mib": pss[-1],
            "mean_rss_mib": sum(rss) / len(rss), "peak_rss_mib": max(rss), "final_rss_mib": rss[-1]}
