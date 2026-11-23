#!/bin/sh
# Build everything the experiments need into $BUILD (default: a folder in
# RAM, because the project disk is nearly full). Takes about 10 minutes.
#   - Redis 7.2.6 twice: with libc malloc (so LD_PRELOAD works) and with its
#     bundled jemalloc (needed for Redis's own "activedefrag")
#   - miniruby 3.3.6 (Ruby without extensions, enough for the benchmark)
#   - original Mesh in 3 variants: normal, meshing off, randomization off
#   - larson benchmark from the Mesh repo
#   - our mymesh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
P2="$(dirname "$HERE")"
MESH="$P2/reference/Mesh"
BUILD="${BUILD:-/tmp/mesh-build}"
mkdir -p "$BUILD/libs" && cd "$BUILD"

[ -f redis.tgz ] || curl -sSfL -o redis.tgz https://download.redis.io/releases/redis-7.2.6.tar.gz
[ -f ruby.tgz ] || curl -sSfL -o ruby.tgz https://cache.ruby-lang.org/pub/ruby/3.3/ruby-3.3.6.tar.gz
for v in libc jemalloc; do
  if [ ! -x "redis-$v/src/redis-server" ]; then
    mkdir -p "redis-$v" && tar xzf redis.tgz -C "redis-$v" --strip-components=1
    if [ $v = libc ]; then make -C redis-libc -j6 MALLOC=libc BUILD_TLS=no; else make -C redis-jemalloc -j6 BUILD_TLS=no; fi
  fi
done
if [ ! -x ruby/miniruby ]; then
  mkdir -p ruby && tar xzf ruby.tgz -C ruby --strip-components=1
  (cd ruby && ./configure --disable-install-doc --without-gmp && make -j6 miniruby)
fi

# Mesh's cmake always writes to $MESH/build/lib/libmesh.so, so build the
# variants one after another and copy each result.
build_mesh() {  # $1 = output name, rest = cmake options
  out=$1; shift
  rm -rf "mesh-$out" && mkdir "mesh-$out"
  (cd "mesh-$out" && cmake "$MESH" -DCMAKE_BUILD_TYPE=Release "$@" >/dev/null &&
     make heap_layers >/dev/null && make -j6 mesh >/dev/null)
  cp "$MESH/build/lib/libmesh.so" "libs/$out.so"
}
[ -f libs/libmesh-nomesh.so ] || build_mesh libmesh-nomesh -DDISABLE_MESHING=ON
[ -f libs/libmesh-norand.so ] || build_mesh libmesh-norand -DRANDOMIZATION=0
[ -f libs/libmesh.so ] || build_mesh libmesh

[ -x larson ] || g++ -O2 -pthread -o larson "$MESH/src/testing/benchmark/larson.cc"
make -C "$P2/mymesh"
echo "setup done: $BUILD"
