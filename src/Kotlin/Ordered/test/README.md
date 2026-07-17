# Kotlin Ordered Executable Tests

- Created (UTC): 2026-07-15T09:10:22Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Maintainers and reviewers of the Kotlin Ordered port
- Scope: `OrderedTests.kt`

`OrderedTests.kt` is a dependency-free executable suite launched by:

```powershell
cd src/Kotlin
.\build.ps1 -Workspace Ordered
```

The twelve named scenarios form four layers:

1. construction, nullable values, representative retention, point edits, endpoints, and eager
   positional validation;
2. exhaustive small movement pairs, sparse-label/relabel histories, every small valid range,
   reversal, and stable/natural sorting;
3. receiver-policy algebra, all set relations, eager normalization, injected policy/comparator/
   iterable/relabel failures, and failure atomicity; and
4. a 1,200-step deterministic list/set model with retained branches, followed by independent
   iterator and concurrent-reader checks.

Every layer calls the public Ordered-owned `validateStructure()` diagnostic. The tests intentionally
use constant-hash collision policies, object-distinct equivalent representatives, nullable values,
semantically different policy objects, and unpublished rebuild failures. They do not import or
compile Tungsten and do not execute benchmarks.

The ordered-map scenario additionally covers payload replacement without movement, retained source
values, order/value root sharing, explicit movement, stable entry sorting, range extraction, and
dual-index validation.

The ordered-multimap scenario covers independent policies, nested grouped order, stored key/value
representatives, duplicate no-ops, pair and whole-group removal, retained snapshots, counts, and
nested invariant validation.
