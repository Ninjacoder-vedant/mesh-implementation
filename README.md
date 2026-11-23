# Mesh: implementation and reproduction

Requires Linux x86_64, gcc, make, cmake, curl, and Python 3 with matplotlib.

## Build and test the allocator

```sh
cd mymesh
make
make test
LD_PRELOAD=$PWD/libmymesh.so <program>
```

## Run the experiments

```sh
git clone https://github.com/plasma-umass/Mesh reference/Mesh
sh experiments/setup.sh            # builds into /tmp/mesh-build (set BUILD=... to change)
cd experiments
python3 redis_frag.py
REDIS_DURATION=20 python3 redis_frag.py
python3 ruby_strings.py
python3 microbench.py
python3 overhead.py
python3 plot.py                    # writes figures to experiments/results/
```
