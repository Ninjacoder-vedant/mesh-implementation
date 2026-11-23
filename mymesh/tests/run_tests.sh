#!/bin/sh
# Run every test with mymesh loaded through LD_PRELOAD.
cd "$(dirname "$0")/.." || exit 1
LIB="$PWD/libmymesh.so"
fail=0
for t in test_basic test_threads "test_frag 2000000 64 90"; do
  echo "== $t"
  MYMESH_STATS=1 LD_PRELOAD="$LIB" ./tests/$t || fail=1
done
echo "== real programs"
LD_PRELOAD="$LIB" ls / >/dev/null && echo "ls: ok" || fail=1
LD_PRELOAD="$LIB" sh -c 'echo hello | sort | wc -l' >/dev/null && echo "sh pipeline (fork+exec): ok" || fail=1
LD_PRELOAD="$LIB" python3 -c 'import json; d=[{"k":i,"v":str(i)*10} for i in range(200000)]; json.dumps(d)' \
  && echo "python3: ok" || fail=1
[ $fail -eq 0 ] && echo "ALL PASS" || echo "SOME FAILED"
exit $fail
