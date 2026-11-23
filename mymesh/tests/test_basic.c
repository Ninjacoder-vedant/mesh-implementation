// test_basic.c: random malloc/free/realloc/memalign with content checks.
// Each object is filled with a pattern; we check it is still intact before
// freeing it and after forcing a meshing pass.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 20000
#define ROUNDS 400000

static void *ptrs[N];
static size_t sizes[N];
static unsigned char tags[N];

static uint64_t rng = 88172645463325252ULL;
static uint64_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

static size_t randomSize(void) {
  uint64_t r = rnd() % 100;
  if (r < 70) return 1 + rnd() % 256;
  if (r < 95) return 1 + rnd() % 16384;
  return 16385 + rnd() % 300000;  // large objects
}

static void fill(int i) { memset(ptrs[i], tags[i], sizes[i]); }

static int check(int i) {
  unsigned char *p = ptrs[i];
  for (size_t k = 0; k < sizes[i]; k++)
    if (p[k] != tags[i]) return 0;
  return 1;
}

int main(void) {
  size_t (*meshNow)(void) = (size_t (*)(void))dlsym(RTLD_DEFAULT, "mymesh_mesh_now");
  printf("test_basic: running %s\n", meshNow ? "with mymesh" : "with system malloc");
  int errors = 0;

  for (int round = 0; round < ROUNDS; round++) {
    int i = rnd() % N;
    if (ptrs[i]) {
      if (!check(i)) errors++;
      int op = rnd() % 4;
      if (op == 0) {  // realloc keeps the old bytes
        size_t ns = randomSize();
        size_t keep = ns < sizes[i] ? ns : sizes[i];
        ptrs[i] = realloc(ptrs[i], ns);
        unsigned char *p = ptrs[i];
        for (size_t k = 0; k < keep; k++)
          if (p[k] != tags[i]) { errors++; break; }
        sizes[i] = ns;
        fill(i);
      } else {
        free(ptrs[i]);
        ptrs[i] = NULL;
      }
    } else {
      sizes[i] = randomSize();
      tags[i] = (unsigned char)(rnd() | 1);
      int op = rnd() % 10;
      if (op == 0) {
        size_t align = (size_t)1 << (4 + rnd() % 10);  // 16 .. 8192
        if (posix_memalign(&ptrs[i], align, sizes[i]) != 0) { errors++; continue; }
        if ((uintptr_t)ptrs[i] % align) errors++;
      } else if (op == 1) {
        ptrs[i] = calloc(1, sizes[i]);
        unsigned char *p = ptrs[i];
        for (size_t k = 0; k < sizes[i]; k++)
          if (p[k]) { errors++; break; }
      } else {
        ptrs[i] = malloc(sizes[i]);
      }
      if (!ptrs[i]) { errors++; continue; }
      if ((uintptr_t)ptrs[i] % 16) errors++;
      if (malloc_usable_size(ptrs[i]) < sizes[i]) errors++;
      fill(i);
    }
    if (meshNow && round % 50000 == 0) meshNow();
  }

  if (meshNow) meshNow();
  for (int i = 0; i < N; i++) {
    if (ptrs[i]) {
      if (!check(i)) errors++;
      free(ptrs[i]);
    }
  }
  printf("test_basic: %s (%d errors)\n", errors ? "FAIL" : "PASS", errors);
  return errors != 0;
}
