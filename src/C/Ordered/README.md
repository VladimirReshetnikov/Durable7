# C Persistent Ordered Collections

- Status: Active neutral collection workspace
- Created (UTC): 2026-07-15T09:00:00Z
- Repository HEAD: 2d75a79feb424f4476ec32c2d6e4f19263441bf3
- Audience: C consumers, maintainers, reviewers, and sibling-port authors
- Scope: `src/C/Ordered`

This workspace owns the C17 port of the neutral persistent ordered-set and ordered-map family. It
combines the public C CHAMP map with the public C FingerTree deque and has no source, link, API, or
test-oracle dependency on anything else in the repository; see [Dependency boundary](#dependency-boundary).

## Surface

`d7_ordered_set` is a persistent type-erased value handle with explicit `clone`, `move`, and
`destroy` ownership. Its API covers:

- empty and array construction with first-equivalent-representative retention;
- hashed membership, stored-representative recovery, endpoints, indexed reads, and `index_of`;
- append, prepend, positional insertion, and explicit final-index movement;
- value, positional, endpoint, and complete removal;
- ranges, take/drop, reversal, and stable one-shot sorting;
- receiver-policy union, intersection, difference, and symmetric difference for arrays and other
  ordered sets;
- receiver-policy subset, proper-subset, superset, proper-superset, overlap, and equality relations;
- ordered visitation and two-way structural validation; and
- diagnostics for unchanged order/index root reuse.

`d7_ordered_map` adds payload-bearing entries while retaining the set's explicit key order. It
supports strict and conditional positional insertion, value-only replacement, movement, keyed and
positional removal, range/take/drop, reversal, stable entry sorting, and ordered visitation. Its
ordered-set key index and CHAMP value index publish failure-atomically; reordering shares the value
root, while replacing an existing value shares the complete order root.

`d7_ordered_multimap` composes an ordered map of ordered sets under independent key and value
policies. It preserves first-insertion order for key groups and separately for distinct values in
each group, reports key and checked pair counts, removes empty groups, and publishes every nested
edit failure-atomically with explicit clone/move/destroy ownership.

The representation uses one ref-counted representative cell per equality class. Both the CHAMP
membership/stamp index and the sparse-stamped FingerTree entry retain that cell, so the first
representative is shared rather than copied independently between indexes. The private stamp gap,
midpoint selection, deterministic relabel, stable rebuild, and validation logic are all owned here.

## Dependency boundary

The compiled graph is exactly:

```text
durable7_ordered_c
├── durable7_hamt_c
└── durable7_finger_tree_c
```

## Documentation

- [API specification](docs/api-specification.md) defines the C ownership, ordering, representative,
  normalization, failure, and complexity contracts.
- [Validation](docs/validation.md) records the serialized warning-clean gate and test coverage.
- [Test map](tests/README.md) describes the independent model and deterministic scenarios.

## Validation

From `src/C`, run one workspace at a time:

```powershell
.\build.ps1 -Workspace Ordered -RunTests
.\build.ps1 -Workspace Ordered -Configuration Release -RunTests
```

Both CMake/Ninja compilation and CTest execution are fixed to one job. No benchmark is part of the
routine validation lane.
