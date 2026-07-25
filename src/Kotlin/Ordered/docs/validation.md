# Kotlin Ordered Collections Validation

- Created (UTC): 2026-07-15T09:10:22Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers validating the Kotlin Ordered workspace
- Scope: Serialized compilation, executable tests, dependency boundary, and benchmark exclusion

## Command

From `src/Kotlin`, run the workspace gate by itself:

```powershell
.\build.ps1 -Workspace Ordered
```

The root launcher discovers Kotlin sources under `Ordered/src` and `Ordered/test`, then adds only
the public `Hamt/src` and `FingerTree/src` roots. It compiles one self-contained test jar and runs
that jar. The Kotlin backend is pinned to one thread; compiler and test JVMs see one active
processor and use the serial collector unless the caller already selected another collector.
There is no Gradle daemon or worker pool.

Run this gate sequentially with every other language workspace. Do not overlap it with .NET,
C/C++, Cargo, Cabal, npm, Python, another Kotlin invocation, or benchmark processes.

## Required Coverage

The dependency-free executable test program covers:

- default-empty canonicalization and exact custom-policy retention;
- one-pass construction, all-collision policies, duplicate collapse, and first representatives;
- nullable membership, stored-null lookup, movement, and removal;
- append, prepend, positional insertion, every small movement pair, and final-index semantics;
- exact receiver identity for duplicate additions, movement no-ops, misses, empty clear, full ranges,
  count-zero/one reverse, and unchanged stable sorts;
- positional validation before hashing and all endpoint/absence failures;
- sparse-label exhaustion, full relabel, independent sibling histories, and relabel failure atomicity;
- every valid range of a representative-rich source, take/drop boundaries, reverse, natural sort,
  stable comparator ties, and one-shot sort behavior;
- receiver-policy algebra and all six relations against a differently configured ordered set;
- eager argument normalization, late iterable failures, policy failures, comparator failures, and
  preservation of every source snapshot;
- 1,200 deterministic mixed-operation model steps plus retained branches and invariant checks; and
- version-bound iterators and concurrent readers of shared immutable snapshots.
- ordered-multimap independent policies, nested grouped order, first key/value representatives,
  duplicate identity, pair/group removal, snapshot isolation, checked counts, and validation.

`validateStructure()` runs after deterministic and generated histories, checking both directions of
the Ordered-owned map/sequence invariant without relying on Hamt or FingerTree internals.

## Dependency Boundary

The Ordered invocation's additional roots are exactly:

```text
Hamt/src
FingerTree/src
```

Do not add `Tungsten/src`, import `durable7.tungsten`, copy or link Tungsten source,
wrap a Tungsten type, or use `PersistentAssociation` as an executable oracle. A reusable mechanism
must remain independently implemented and specified here.

## Benchmark Boundary

No benchmark is part of this gate. Do not run a benchmark as shipment evidence on a contended
machine. The workspace claims its semantic and asymptotic contracts only; future isolated
measurements may guide optimization without changing those contracts.
