# Python test map

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

Tests are grouped by shipped collection family:

- `hamt/`: CHAMP maps/sets and transients, one-descent factory updates, construction-only mutable
  builders, the persistent hash bag, transient-set relations, thread-safe snapshots, Patricia
  maps/sets, exact Merkle wire vectors, verified persistence and sync, proofs, budgets, and typed
  merge.
- `finger_tree/`: measured sequences and derived collections, ropes and cursors, DABA Lite, RRB
  vectors, canonical zip-zip sets, Brodal-Okasaki heaps, and priority-search queues.
- `ordered/`: the neutral HAMT-plus-deque persistent ordered set, including representative and
  policy retention, explicit movement, sparse-stamp relabeling, ranges, stable sorting, eager
  receiver-policy algebra, relations, failure atomicity, concurrency, and generated histories.
- `tungsten/`: application-leaf persistent list and insertion-ordered association semantics.
- `numerics/`: every 256/512/1024-bit signed and unsigned wrapper, sparse integers, and byte helpers.

Hypothesis stateful/model tests supplement example and adversarial tests. Tests use public APIs
except where white-box structural assertions are necessary to establish sharing or invariants.

The current suite contains 128 tests: 47 HAMT/Patricia/Merkle tests, 51
measured/frontier-structure tests, 12 ordered-set tests, 5 Tungsten tests, and 13 numerics tests.
Parameterized and Hypothesis executions expand those test functions across all fixed widths and
generated retained-snapshot histories.
