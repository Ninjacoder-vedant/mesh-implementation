// test_frag.c: create heavy fragmentation, then measure memory (RSS).
//
// Allocate many small objects, free a random 90% of them. A normal allocator
// keeps almost every page (each page still holds a few live objects). Mesh
// should merge sparse pages and give memory back, while all data stays intact.
//
// Usage: test_frag [objects] [object_size] [percent_to_free] [random|stride]
//   random: free a random pct% of objects
//   stride: keep only every k-th object (k = 100/(100-pct), rounded down to a
//           power of two so it divides the slots per page). With a normal
//           (sequential) allocator every page then has live objects at the
//           SAME offsets, so pages can't be meshed unless allocation is random.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long rssKiB(void) {
  FILE *f = fopen("/proc/self/status", "r");
  char line[256];
  long kb = -1;
  while (f && fgets(line, sizeof(line), f))
    if (strncmp(line, "VmRSS:", 6) == 0) kb = atol(line + 6);
  if (f) fclose(f);
  return kb;
}

// PSS counts a physical page once even if it is mapped at several virtual
// addresses (which is what meshing does). RSS counts it once per mapping.
static long pssKiB(void) {
  FILE *f = fopen("/proc/self/smaps_rollup", "r");
  char line[256];
  long kb = -1;
  while (f && fgets(line, sizeof(line), f))
    if (strncmp(line, "Pss:", 4) == 0) kb = atol(line + 4);
  if (f) fclose(f);
  return kb;
}

static uint64_t rng = 0x1234567;
static uint64_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

int main(int argc, char **argv) {
  size_t n = argc > 1 ? strtoull(argv[1], 0, 10) : 2000000;
  size_t sz = argc > 2 ? strtoull(argv[2], 0, 10) : 64;
  int pct = argc > 3 ? atoi(argv[3]) : 90;
  int stride = argc > 4 && strcmp(argv[4], "stride") == 0;
  size_t keepEvery = pct < 100 ? (size_t)(100 / (100 - pct)) : 0;
  while (keepEvery & (keepEvery - 1)) keepEvery &= keepEvery - 1;  // round down to a power of two
  size_t (*meshNow)(void) = (size_t (*)(void))dlsym(RTLD_DEFAULT, "mymesh_mesh_now");
  // the original Mesh triggers a meshing pass with mesh_mallctl("mesh.compact")
  int (*meshCtl)(const char *, void *, size_t *, void *, size_t) =
      (int (*)(const char *, void *, size_t *, void *, size_t))dlsym(RTLD_DEFAULT, "mesh_mallctl");

  char **p = malloc(n * sizeof(char *));
  long base = rssKiB();
  long pssBase = pssKiB();
  for (size_t i = 0; i < n; i++) {
    p[i] = malloc(sz);
    memset(p[i], (int)(i & 0xff), sz);
  }
  long full = rssKiB();

  size_t freed = 0;
  for (size_t i = 0; i < n; i++) {
    int drop = stride ? (keepEvery == 0 || i % keepEvery != 0) : (int)(rnd() % 100) < pct;
    if (drop) {
      free(p[i]);
      p[i] = NULL;
      freed++;
    }
  }
  long afterFree = rssKiB();

  // Give the allocator a chance to mesh (mymesh or original Mesh; no-op for others).
  // Both allocators limit the work of one pass, so run passes until nothing
  // more is found (mymesh tells us; for the original we just run 50 passes).
  for (int k = 0; k < 50; k++) {
    if (meshNow && meshNow() == 0) break;
    if (meshCtl) {
      size_t dummy = 0, len = sizeof(dummy);  // Mesh rejects the call without an output buffer
      int r = meshCtl("mesh.compact", &dummy, &len, NULL, 0);
      if (k == 0) printf("mesh_mallctl(\"mesh.compact\") returned %d\n", r);
    }
  }
  long afterMesh = rssKiB();
  long pssMesh = pssKiB();

  size_t bad = 0;
  for (size_t i = 0; i < n; i++) {
    if (!p[i]) continue;
    for (size_t k = 0; k < sz; k++)
      if ((unsigned char)p[i][k] != (unsigned char)(i & 0xff)) { bad++; break; }
  }

  double liveMiB = (double)(n - freed) * sz / 1048576.0;
  printf("objects=%zu size=%zu freed=%d%% (%s)  live data=%.1f MiB\n", n, sz, pct,
         stride ? "stride" : "random", liveMiB);
  printf("RSS MiB: full=%.1f  after_free=%.1f  after_mesh=%.1f\n", (full - base) / 1024.0,
         (afterFree - base) / 1024.0, (afterMesh - base) / 1024.0);
  printf("PSS MiB after_mesh=%.1f  (real physical memory; RSS double-counts meshed pages)\n",
         (pssMesh - pssBase) / 1024.0);
  printf("data check: %s (%zu bad objects)\n", bad ? "FAIL" : "PASS", bad);
  return bad != 0;
}
