# Python data structures

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

This workspace is the typed Python 3.11+ port of the repository-owned HAMT, FingerTree, ordered,
and Tungsten-collection families. The distribution is
`durable7`; its import namespace is
`durable7`.

The port follows the semantic contracts of the established sibling workspaces while using Python
idioms where they preserve those contracts: immutable value objects are iterable and sized,
and fallible lookups expose both Python conveniences and explicit presence-preserving methods. The Merkle workspace retains the
exact policy identity, `MST2` block encoding, and `MSP2` proof envelope used across languages.

## Package families

- `hamt` contains the real 32-way CHAMP map/set, one-descent map factory updates, a persistent hash
  bag, a set-valued persistent hash multimap, a bidirectional persistent relation, a strict
  persistent bimap, `PersistentMapPatch`, `PersistentDirectedGraph`, `PersistentIndexedMap`, a
  reusable construction-only bulk builder, one-way edit sessions with all six set relations,
  an `RLock`-coordinated consumer-semantic snapshotting facade, signed 32/64-bit Patricia
  collections, and the authenticated Merkle tree with persistence, synchronization, proofs,
  budgets, and typed merge. The facade preserves Ctrie-facing mutation and snapshot behavior but
  makes no lock-free GCAS/RDCSS progress claim; root-identity retries prevent a reentrant factory
  or hash/equality-policy callback from publishing over a newer nested update.
- `finger_tree` contains the persistent measured AVL engine and deque/finger-tree facades, derived
  sorted/priority/interval collections, the payload-bearing `PersistentIntervalMap`,
  `PersistentChunkedBitSet`,
  positional/measured/text ropes and cursors, a true 32-way
  RRB vector, canonical HMAC-ranked zip-zip set, Brodal-Okasaki heap, winner-cached priority-search
  queue, mutable six-cursor DABA Lite, and the independently implemented implicit-AVL
  `RangeUpdateSequence` with logarithmic measured range edits and lazy algebraic tags.
- `ordered` contains the general-purpose `PersistentOrderedSet`, `PersistentOrderedMap`, and
  grouped `PersistentOrderedMultimap`,
  independently composed from the HAMT and persistent deque. They retain first representatives,
  receiver `HashPolicy` identity, insertion order, explicit positional movement, sparse labels,
  ranges, and stable one-shot sorting; the set also provides receiver-ordered algebra. Neither
  depends on the application-specific Tungsten family.
- `tungsten` is the application-specific leaf containing `PersistentList` and insertion-ordered
  `PersistentAssociation`; general-purpose package code never depends on it.

The root namespace re-exports every public family member:

```python
from durable7 import (
    PersistentHashBag,
    PersistentBiMap,
    PersistentHashMap,
    PersistentOrderedSet,
    RangeUpdateSequence,
    TextRope,
)

snapshot = PersistentHashMap.empty().put("answer", 42)
bag = PersistentHashBag.from_values(["alpha", "alpha", "beta"])
bimap = PersistentBiMap.empty().add("answer", 42)
ordered = PersistentOrderedSet.from_values(["alpha", "beta", "alpha"])
text = TextRope.from_text("alpha\nbeta")


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
assert bimap.inverse[42] == "answer"
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
