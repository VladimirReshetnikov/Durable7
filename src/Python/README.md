# Python data structures

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

This workspace is the typed Python 3.11+ port of the repository-owned HAMT, FingerTree, and ordered families. The distribution is
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
  ranges, and stable one-shot sorting; the set also provides receiver-ordered algebra. The subpackage
  builds entirely on the public `hamt` and `finger_tree` surfaces and adds no runtime dependency.

The seven research-derived collections ship here too: six in `finger_tree`
(`AncestralSliceQueue`, `BilateralAncestralDeque`, `ContextualRankSequence`, `PersistentDeltaMap`,
`PersistentRunDeltaVector`, `PersistentMonotoneActionHeap`) and
`PersistentAncestralConnectionForest` in `hamt`. Two share `incremental_ancestor`, the append-only
level-ancestor seam: an `IncrementalAncestorArena` `Protocol` plus the shipped
`MyersIncrementalAncestorArena`. Because the protocol is structural, a backend satisfies the seam
without declaring that it does. The arena is the one deliberately mutable core in these families, in
the sense this README reserves for builders and cursors — nodes accumulate in place under a private
lock while every collection value built on them stays immutable. `equality` is the value-side
counterpart of `ordering`: the two delta-tracking collections retain a `ValueEquality` relation from
it, because that relation decides which writes are semantic no-ops and when a recorded change
cancels, so it must be remembered rather than passed per call.

Their bounds are stated against this workspace's substrates rather than inherited from the managed
baseline, and they move in both directions. `MeasuredSequence` is an implicit AVL join tree, not a
Hinze–Paterson finger tree, so `ContextualRankSequence` states Θ(s log n) endpoint updates rather
than O(s) amortized, and a concatenation bound keyed to the operands' *height difference* rather
than to the smaller operand — measured, not assumed: concatenating a 16384-element sequence with a
1-element one costs the same 28 measure compositions as with a 1024-element one. `SortedMap` caches
subtree sizes but no extremes, so `PersistentDeltaMap.min_entry` and `max_entry` are Θ(log N) where
the baseline claims O(1). In the other direction, neither `RrbVector` nor `SortedMap` defers
rebalancing, so `PersistentRunDeltaVector` states *worst-case* splice bounds where the C# and Rust
baselines say amortized. None of that touches the load-bearing properties: run splices stay
independent of run length and comparison-free, and range-restricted change enumeration is a real
boundary seek through `SortedMap.get_key_range`.

Four Python-specific hazards are handled rather than documented away. Python integers are arbitrary
precision, so the baseline's three overflow ceilings cannot arise and no artificial limit is
synthesized to imitate them; growth is bounded by memory. `is` degenerates on small integers, `bool`,
and interned strings, so the run-delta vector boxes elements in a private identity-bearing cell —
otherwise "a clean position reuses its exact checkpoint cell" would be vacuous for exactly the
payload types tests reach for first. Python's recursion limit is 1000 frames, so every forest, spine,
version-chain, and parent-path walk is iterative, and the tests assert depths past that limit rather
than trusting it. And `float("nan") == float("nan")` is false, which would silently leave a position
permanently dirty, so `natural_value_equality` consults identity first and `reflexive_ieee_equality`
is the .NET-compatible escape hatch. Separately, the connection forest earns an *unconditional*
CHAMP path factor — now the bound in every workspace, since Rust and Haskell pinned the same
finalizer — by pinning an injective
`fmix32` vertex hash, so no collision bucket can ever hold two distinct vertices.

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
