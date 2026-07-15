# Python test map

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

Tests are grouped by shipped collection family:

- `hamt/`: CHAMP maps/sets and transients, one-descent factory updates, construction-only mutable
  builders, the persistent hash bag, transient-set relations, and the `RLock`-coordinated
  concurrent facade. Facade coverage includes caller-key factories, retained representatives,
  present `None`, exact generations, stable canonical snapshots, collision-model histories,
  failure atomicity, serialized writers, factory reentry, and hash/equality-policy reentry across
  every policy-driven mutation. Same-key and different-key hooks lock down stale-root retry, return
  values, cached `get_or_put` candidates, and generation accounting. The same group also covers
  Patricia maps/sets, exact Merkle wire vectors, verified persistence and sync, proofs, budgets,
  and typed merge.
- `finger_tree/`: measured sequences and derived collections, ropes and cursors, DABA Lite, RRB
  vectors, canonical zip-zip sets, Brodal-Okasaki heaps, priority-search queues, and the lazy-tagged
  range-update sequence. Range coverage includes algebra laws, every range boundary, retained
  branching models, callback failure atomicity, structural invariants/sharing, and iterator and
  concurrent-read semantics.
- `ordered/`: the neutral HAMT-plus-deque persistent ordered set, including representative and
  policy retention, explicit movement, sparse-stamp relabeling, ranges, stable sorting, eager
  receiver-policy algebra, relations, failure atomicity, concurrency, and generated histories.
- `tungsten/`: application-leaf persistent list and insertion-ordered association semantics.
- `numerics/`: every 256/512/1024-bit signed and unsigned wrapper, sparse integers, and byte helpers.

Hypothesis stateful/model tests supplement example and adversarial tests. Tests use public APIs
except where white-box structural assertions are necessary to establish sharing or invariants.

Parameterized and Hypothesis executions expand the test functions across all fixed widths and
generated retained-snapshot histories. Exact totals belong to completed validation records rather
than this durable coverage map.
