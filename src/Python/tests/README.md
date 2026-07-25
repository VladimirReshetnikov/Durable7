# Python test map

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

Tests are grouped by shipped collection family:

- `hamt/`: CHAMP maps/sets and transients, one-descent factory updates, construction-only mutable
  builders, the persistent hash bag, set-valued multimap, bidirectional relation, strict persistent
  bimap, strict map patches, directed graphs, indexed maps, transient-set relations, and the `RLock`-coordinated
  concurrent facade. Facade coverage includes caller-key factories, retained representatives,
  present `None`, exact generations, stable canonical snapshots, collision-model histories,
  failure atomicity, serialized writers, factory reentry, and hash/equality-policy reentry across
  every policy-driven mutation. Same-key and different-key hooks lock down stale-root retry, return
  values, cached `get_or_put` candidates, and generation accounting. The same group also covers
  Patricia maps/sets, exact Merkle wire vectors, verified persistence and sync, proofs, budgets,
  and typed merge. Bimap coverage locks both-domain conflicts, representatives, policy-driven
  replacement, symmetric removal, cached inverse identity, `None` presence, failure atomicity,
  retained Hypothesis models, and concurrent readers.
- `finger_tree/`: measured sequences and derived collections, the payload interval map, persistent
  chunked bit set, ropes and cursors, DABA Lite, RRB
  vectors, canonical zip-zip sets, Brodal-Okasaki heaps, priority-search queues, and the lazy-tagged
  range-update sequence. Range coverage includes algebra laws, every range boundary, retained
  branching models, callback failure atomicity, structural invariants/sharing, and iterator and
  concurrent-read semantics.
- `ordered/`: the neutral HAMT-plus-deque persistent ordered set, map, and grouped multimap, including representative
  and policy retention, explicit movement, sparse-stamp relabeling, ranges, stable sorting, dual
  indexes, eager set algebra and relations, failure atomicity, concurrency, and generated histories.

Hypothesis stateful/model tests supplement example and adversarial tests. Tests use public APIs
except where white-box structural assertions are necessary to establish sharing or invariants.

Parameterized and Hypothesis executions expand the test functions across all fixed widths and
generated retained-snapshot histories. Exact totals belong to completed validation records rather
than this durable coverage map.
