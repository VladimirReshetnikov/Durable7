# Kotlin Ordered Collections

- Created (UTC): 2026-07-15T09:10:22Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Kotlin/JVM collection users, maintainers, reviewers, and sibling-port authors
- Scope: Neutral ordered collections under `src/Kotlin/Ordered`

This workspace owns the Kotlin/JVM `PersistentOrderedSet<T>`, `PersistentOrderedMap<K, V>`, and
`PersistentOrderedMultimap<K, V>` ports. They combine equality-class
membership with durable insertion and explicit-position order while preserving immutable snapshots.
The public type lives in `durable7.ordered`.

The workspace is deliberately neutral. Its build consumes only the public Kotlin Hamt and
FingerTree source roots:

```text
Ordered
├── Hamt/src
└── FingerTree/src
```

It neither compiles nor references `Tungsten`, and it does not wrap `PersistentAssociation` or use
kernel-derived Tungsten behavior as its semantic baseline. The order-maintenance implementation,
API, diagnostics, tests, and evolution policy are independently owned here.

## Capabilities

`PersistentOrderedSet<T>` provides:

- a retained runtime `HashPolicy<T>` for hashing and equality, including nullable values;
- first-representative retention for every equality class;
- append, prepend, positional insertion, and separate explicit movement operations;
- stored-representative lookup, positional lookup, removal, and index recovery;
- contiguous ranges, take/drop, reversal, and stable one-shot sorting;
- receiver-policy union, intersection, difference, symmetric difference, and all six set relations;
- eager argument normalization, deterministic receiver/argument ordering, and policy preservation;
- sparse `Long` stamps with an unpublished full relabel when an interior or endpoint gap is
  exhausted;
- exact receiver identity for documented logical no-ops; and
- public `validateStructure()` diagnostics that recompute both directions of the dual-index
  invariant using only public substrate operations.

“Ordered” means insertion and explicit-position order, not persistent comparison-sorted order.
`sort` performs one stable reorder and does not retain the ordering comparator; later additions
append normally.

`PersistentOrderedMap<K, V>` composes the ordered set of keys with a CHAMP payload index. It adds
strict positional insertion, value replacement without movement, keyed and positional removal,
ranges, reversal, stable entry sorting, ordered iteration, component-sharing diagnostics, and
two-way validation. The first key representative and position win; bulk construction's last
payload wins.

`PersistentOrderedMultimap<K, V>` composes an ordered map of nonempty ordered value sets under
independent runtime policies. It retains first key/value representatives, nested first-insertion
order, separate key/pair counts, idempotent pair addition, and empty-group removal.

## Documentation

- [Documentation index](docs/README.md) is the entry point for durable Ordered references.
- [API and behavior notes](docs/api-notes.md) define policy, representation, ordering, movement,
  failure, and complexity contracts.
- [Validation](docs/validation.md) records the serialized build command and required coverage.
- [Executable test map](test/README.md) maps the dependency-free test program to the contract.

## Build And Test

From `src/Kotlin`, run:

```powershell
.\build.ps1 -Workspace Ordered
```

The launcher compiles Ordered plus the public Hamt and FingerTree source roots in one Kotlin/JVM
invocation, pins the Kotlin backend to one thread, exposes one active processor to compiler and test
JVMs, and selects the serial collector unless the caller already selected another collector. The
test program is dependency-free and executes after compilation. No benchmark is run or required.
