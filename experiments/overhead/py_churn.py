# Python workload: build and drop many dicts, lists and strings, plus JSON
# round trips. CPython uses malloc for anything above 512 bytes and for its
# memory arenas, so the allocator matters.
import json
import random

random.seed(7)
keep = []
for rnd in range(30):
    batch = [{"id": i, "name": "item-%d" % i, "tags": ["t%d" % (i % 13)] * (i % 7),
              "blob": "x" * random.choice((600, 1200, 3000))} for i in range(40000)]
    text = json.dumps(batch[:5000])
    back = json.loads(text)
    keep.extend(batch[::50])       # keep 2%, drop the rest
    if len(keep) > 60000:
        keep = keep[len(keep) // 2:]
print(len(keep), len(back))
