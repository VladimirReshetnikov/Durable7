# C Persistent Ordered Set

- Status: Active neutral collection workspace
- Created (UTC): 2026-07-15T09:00:00Z
- Repository HEAD: 2d75a79feb424f4476ec32c2d6e4f19263441bf3
- Audience: C consumers, maintainers, reviewers, and sibling-port authors
- Scope: `src/C/Ordered`

This workspace owns the C17 port of the neutral `PersistentOrderedSet` family. It combines the
public C CHAMP map with the public C FingerTree deque and has no source, link, API, test-oracle, or
semantic dependency on a Tungsten workspace.

## Surface

`tds_ordered_set` is a persistent type-erased value handle with explicit `clone`, `move`, and
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

The representation uses one ref-counted representative cell per equality class. Both the CHAMP
membership/stamp index and the sparse-stamped FingerTree entry retain that cell, so the first
representative is shared rather than copied independently between indexes. The private stamp gap,
midpoint selection, deterministic relabel, stable rebuild, and validation logic are all owned here.

## Dependency boundary

The compiled graph is exactly:

```text
tools_data_structures_ordered_c
├── tools_data_structures_hamt_c
└── tools_data_structures_finger_tree_c
```

The root C aggregator may build Ordered and Tungsten in the same invocation; that is not a runtime
or semantic dependency. Ordered does not include, link, compile, wrap, or test against Tungsten.

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
