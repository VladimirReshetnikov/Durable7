# Python data structures

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

This workspace is the typed Python 3.11+ port of the repository-owned HAMT, FingerTree, ordered,
Tungsten-collection, and numerics families. The distribution is
`vladimir-reshetnikov-data-structures`; its import namespace is
`vladimir_reshetnikov.data_structures`.

The port follows the semantic contracts of the established sibling workspaces while using Python
idioms where they preserve those contracts: immutable value objects are iterable and sized,
fallible lookups expose both Python conveniences and explicit presence-preserving methods, and the
fixed-width integer wrappers implement normal numeric operators. The Merkle workspace retains the
exact policy identity, `MST2` block encoding, and `MSP2` proof envelope used across languages.

## Package families

- `hamt` contains the real 32-way CHAMP map/set, one-descent map factory updates, a persistent hash
  bag, a reusable construction-only bulk builder, one-way edit sessions with all six set relations,
  a lock-coordinated snapshotting concurrent facade, signed 32/64-bit Patricia collections, and the
  authenticated Merkle tree with persistence, synchronization, proofs, budgets, and typed merge.
- `finger_tree` contains the persistent measured AVL engine and deque/finger-tree facades, derived
  sorted/priority/interval collections, positional/measured/text ropes and cursors, a true 32-way
  RRB vector, canonical HMAC-ranked zip-zip set, Brodal-Okasaki heap, winner-cached priority-search
  queue, mutable six-cursor DABA Lite, and the independently implemented implicit-AVL
  `RangeUpdateSequence` with logarithmic measured range edits and lazy algebraic tags.
- `ordered` contains the general-purpose `PersistentOrderedSet`, independently composed from the
  HAMT and persistent deque. It retains first representatives, receiver `HashPolicy` identity,
  insertion order, explicit positional movement, stable one-shot sorting, and receiver-ordered set
  algebra; it never depends on the application-specific Tungsten family.
- `numerics` contains the signed and unsigned 256/512/1024-bit wrappers plus `SparseInteger`.
- `tungsten` is the application-specific leaf containing `PersistentList` and insertion-ordered
  `PersistentAssociation`; general-purpose package code never depends on it.

The root namespace re-exports every public family member:

```python
from vladimir_reshetnikov.data_structures import (
    PersistentHashBag,
    PersistentHashMap,
    PersistentOrderedSet,
    RangeUpdateSequence,
    TextRope,
    UInt256,
)

snapshot = PersistentHashMap.empty().put("answer", 42)
bag = PersistentHashBag.from_values(["alpha", "alpha", "beta"])
ordered = PersistentOrderedSet.from_values(["alpha", "beta", "alpha"])
text = TextRope.from_text("alpha\nbeta")
wrapped = UInt256(-1)


class AdditiveRangeAlgebra:
    identity = 0
    identity_tag = 0

    def combine(self, left: int, right: int) -> int:
        return left + right

    def measure(self, element: int) -> int:
        return element

    def is_identity(self, tag: int) -> bool:
        return tag == 0

    def compose(self, newer: int, older: int) -> int:
        return newer + older

    def apply_element(self, tag: int, element: int) -> int:
        return element + tag

    def apply_measure(self, tag: int, measure: int, count: int) -> int:
        return measure + tag * count


ranged = RangeUpdateSequence.from_iterable([1, 2, 3, 4], AdditiveRangeAlgebra())
ranged = ranged.apply_range(1, 2, 10)

assert snapshot["answer"] == 42
assert bag.count_of("alpha") == 2 and bag.total_count == 3
assert ordered.to_list() == ["alpha", "beta"]
assert text.lines() == ["alpha", "beta"]
assert int(wrapped) == 2**256 - 1
assert ranged.to_list() == [1, 12, 13, 4]
```

## Validate

```powershell
cd C:\DataStructures\src\Python
.\test.ps1
```

The gate installs pinned validation dependencies, checks Ruff formatting and lint, runs strict
Mypy analysis and the complete pytest/Hypothesis suite, builds both source and wheel distributions,
checks their metadata, and smoke-tests the installed wheel in a clean virtual environment.

The package has no runtime dependencies. The suite covers example, algebra-law, retained-version
model, failure-atomicity, structural-sharing, exact-wire, and concurrency behavior on both the
declared Python 3.11 floor and the local Python 3.14 toolchain.

See [API notes](docs/api-notes.md), the detailed
[range-update sequence mapping](docs/range-update-sequence.md), [validation](docs/validation.md),
and the [test map](tests/README.md).
