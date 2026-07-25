# C++ Persistent Ordered Collections

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Updated (UTC): 2026-07-16T22:52:15Z
- Audience: C++ consumers, maintainers, reviewers, and sibling-port authors
- Scope: Neutral C++23 ordered collections under `src/Cpp/Ordered`

This header-first workspace owns the neutral C++ `persistent_ordered_set<T, Hash, KeyEqual>`,
`persistent_ordered_map<Key, Value, Hash, KeyEqual, ValueEqual>`, and
`persistent_ordered_multimap<Key, Value, ...>`. Membership is comparer-defined
and hashed; enumeration follows insertion order or explicit positional movement. Both types
compose only the public C++ CHAMP map and FingerTree deque. They neither reference nor delegate to

The public headers live under
[`include/durable7/ordered`](include/durable7/ordered):

- [`persistent_ordered_set.hpp`](include/durable7/ordered/persistent_ordered_set.hpp)
  defines construction, lookup, addition, movement, removal, ranges, reversal, stable one-shot
  sorting, receiver-policy algebra, relations, iteration, root-sharing diagnostics, and invariant
  validation.
- [`persistent_ordered_map.hpp`](include/durable7/ordered/persistent_ordered_map.hpp)
  defines keyed and positional lookup, strict insertion, representative-preserving value updates,
  explicit movement, removal, ranges, reversal, stable sorting, policy access, and validation.
- [`persistent_ordered_multimap.hpp`](include/durable7/ordered/persistent_ordered_multimap.hpp)
  composes ordered key groups with ordered distinct values under independent policies, preserving
  first representatives and grouped enumeration while removing empty groups.
- [`ordered.hpp`](include/durable7/ordered/ordered.hpp) is the aggregate header and
  exposes library version metadata.

Both collections retain the first key representative installed for each equality class. The map
also retains the first position while letting the last distinct construction value win; replacing
a value never changes its key or position. `add`, `add_first`,
and `insert` do not move or replace a present representative. Movement is deliberately explicit,
and `move_to(index, value)` interprets `index` as the representative's final result position.
Sparse signed 64-bit labels make ordinary inserts persistent path-copy operations; exhausted gaps
trigger an unpublished deterministic relabel and dual-index rebuild.

See the [API notes](docs/api-notes.md) for the full semantic mapping and complexity contract, and
the [validation guide](docs/validation.md) plus [test map](tests/README.md) for the serialized gate.

Build and test from `src/Cpp`:

```powershell
.\build.ps1 -Workspace Ordered -RunTests
.\build.ps1 -Workspace Ordered -Configuration Release -RunTests
```

Both commands configure, compile, and run CTest with one worker. Benchmarks are intentionally not
part of this workspace or its routine validation.
