# C++ Persistent Ordered Collections

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: C++ consumers, maintainers, reviewers, and sibling-port authors
- Scope: Neutral C++23 ordered collections under `src/Cpp/Ordered`

This header-first workspace owns the neutral C++ `persistent_ordered_set<T, Hash, KeyEqual>`.
Membership is comparer-defined and hashed; enumeration follows insertion order or explicit
positional movement. The implementation composes only the public C++ CHAMP map and FingerTree
deque. It neither references nor delegates to the application-specific Tungsten workspace.

The public headers live under
[`include/tools/data_structures/ordered`](include/tools/data_structures/ordered):

- [`persistent_ordered_set.hpp`](include/tools/data_structures/ordered/persistent_ordered_set.hpp)
  defines construction, lookup, addition, movement, removal, ranges, reversal, stable one-shot
  sorting, receiver-policy algebra, relations, iteration, root-sharing diagnostics, and invariant
  validation.
- [`ordered.hpp`](include/tools/data_structures/ordered/ordered.hpp) is the aggregate header and
  exposes library version metadata.

The set retains the first representative installed for each equality class. `add`, `add_first`,
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
