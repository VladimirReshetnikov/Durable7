# Python Collection Port — 2026-08-05

> **Current-state note (2026-08-06).** A TypeScript port of the same seven collections shipped
> afterwards, completing all nine languages, and the complexity-parity campaign that ran between the
> two rebuilt Python's measured core as a lazy Hinze–Paterson finger tree. Two bounds this review
> records as Python-substrate divergences — the contextual sequence's Θ(s log n) endpoints and its
> height-difference concatenation — no longer hold: both now match the reference. This review's scope
> and findings otherwise stand and still describe the Python shipment; see
> [the TypeScript port review](typescript-collection-port-2026-08-06.md) and
> [the parity retrospective](complexity-parity-retrospective-2026-08-06.md).

- Created (UTC): 2026-08-05
- Repository HEAD (reviewed): `main`, post OCaml port
- Audience: Maintainers reviewing the eight-language shipment of the seven research-derived collections
- Scope: The Python port of all seven collections plus the two seams they share, the intentional
  divergences it makes, and the verification evidence

## Decision Recorded

The seven research-derived collections are now ported to Python, making coverage **C#, Rust, C,
Haskell, Kotlin, C++, OCaml, and Python**. Only TypeScript remains unported under the same
parity-economics rule. The six earlier port reviews carry current-state notes pointing here.

## What Shipped

| Collection | Python module |
| --- | --- |
| `AncestralSliceQueue<T>` | `durable7.finger_tree.ancestral_slice_queue` |
| `BilateralAncestralDeque<T>` | `durable7.finger_tree.bilateral_ancestral_deque` |
| `ContextualRankSequence<TElement, TMachine>` | `durable7.finger_tree.contextual_rank_sequence` |
| `PersistentDeltaMap<TKey, TValue>` | `durable7.finger_tree.persistent_delta_map` |
| `PersistentRunDeltaVector<T>` | `durable7.finger_tree.persistent_run_delta_vector` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `durable7.finger_tree.persistent_monotone_action_heap` |
| `PersistentAncestralConnectionForest` | `durable7.hamt.persistent_ancestral_connection_forest` |

Two seams support them. `durable7.finger_tree.incremental_ancestor` carries the level-ancestor
service. `durable7.finger_tree.equality` carries the retained value-equality relation, and is new to
this port: two collections need it, two independent ports of them arrived with byte-identical
private copies under different names, and consolidating into one module mirrors the Rust workspace's
`ordering.rs`/`equality.rs` split. Every name is re-exported from the family packages and from the
root `durable7` namespace, which now resolves 291 unique public names.

## The Backend Seam Is a Protocol, and Statistics Deliberately Are Not On It

`IncrementalAncestorArena` is a `typing.Protocol`, matching this package's policy idiom rather than
C#'s interface. Because protocols are structural, a backend satisfies the seam without declaring
that it does and there is no registration step to forget.

`statistics()` is deliberately absent from the protocol. It describes one backend's representation —
odd-block counts, hop counters — so it belongs to `MyersIncrementalAncestorArena` rather than to the
contract. The consequence is a small API shape the consuming collections had to adopt: each takes a
caller-supplied arena, mirroring the C# overload, so a test that wants an exact query profile
constructs the Myers arena itself, holds that reference, and reads counters off it. That keeps
`mypy --strict` satisfied with no cast and keeps a hypothetical non-Myers backend from having to
invent Myers-shaped statistics.

## Bounds Move in Both Directions, and Each Was Established Rather Than Inherited

Three substrates differ from the managed baseline's, and unusually for these ports one of them is
*stronger*:

- **`ContextualRankSequence` is weaker.** `MeasuredSequence` is an implicit AVL join tree, not a
  Hinze–Paterson finger tree with digits and a lazy middle, so endpoint updates are Θ(s log n)
  rather than O(s) amortized. This was measured, not inferred: instrumenting the summary combiner
  showed `big(16384).concat(small)` costing 28 compositions whether the smaller operand held 1
  element or 1024, so the cost tracks the operands' *height difference*, not the smaller operand.
  C#'s O(s log(min(n, m))) is therefore claimed nowhere. Indexing is *better* than the reference, at
  O(log n) composing no summary at all, because the substrate descends by cached sizes.
- **`PersistentDeltaMap` extremes are weaker.** `SortedMap` caches subtree sizes but no extremes, so
  `min_entry`/`max_entry` are Θ(log N) where the baseline documents O(1). Its writes, lookups, rank,
  and neighbour queries are genuinely logarithmic, and range-restricted enumeration is a real
  boundary seek through `SortedMap.get_key_range` — this workspace was the first port whose sorted
  substrate supplied one, so the C port's filter-over-all-changes defect had no chance to recur.
- **`PersistentRunDeltaVector` is stronger.** Neither `RrbVector` nor `SortedMap` defers
  rebalancing, so splice bounds are stated *worst case* where the C# and Rust baselines say
  amortized. Because that is a stronger claim than the reference makes, it was probed directly
  rather than argued: over 512 disjoint dirty runs on a 4096-element vector, tree height stayed at
  2 across 200 successive accept-splices, so the concatenation on the splice path does not
  accumulate height. The bound the port does *not* inherit is stated too — `RrbVector.concat`
  rebalances only the seam, so each O(log n) is really O(h) in height.

`PersistentAncestralConnectionForest` earns an **unconditional** CHAMP path factor — the bound Rust
and Haskell must weaken to *expected* — by pinning an injective `fmix32` vertex hash over a universe
capped at 2³¹ − 1. Every step of that hash is a bijection of the 32-bit word, verified by
round-tripping through an explicitly constructed modular inverse, so no collision bucket can hold
two distinct vertices. Pinning is what earns the claim: the workspace default would also be
injective here, but only as a property of CPython's int-hash contract rather than of this module.

## Four Python Hazards, Handled Rather Than Documented Away

- **Arbitrary-precision integers.** The baseline raises at three ceilings — ancestry depth, node
  count, and a coalesced jump distance. None can arise, so no artificial limit is synthesized to
  imitate them and the saturating counter arithmetic is dropped for exact counters. Growth is
  bounded by memory. Both ancestry-interval collections state this identically; a cross-check
  confirmed they had not drifted apart on it.
- **Degenerate identity.** `is` is true for equal small integers, `bool`, and interned strings, so
  "a clean position reuses its exact checkpoint cell" would be vacuous for exactly the payloads a
  test reaches for first. The run-delta vector boxes elements in an identity-bearing cell. Notably
  the substrate did *not* force this — `rrb_vector._set_node` short-circuits on `is`, not `==`, so
  unlike the C++ and Kotlin workspaces there was no equality no-op rule to defeat; the boxing exists
  purely to make the invariant observable. `AncestralConnectionVersion` is a plain slotted class
  rather than a dataclass, so `==` stays reference identity: `eq=True` would have merged sibling
  branches agreeing on depth, parent, and root.
- **The 1000-frame recursion limit.** Every forest, spine, version-chain, and parent-path walk is
  iterative, including several the reference and the un-tagged sibling recurse through. The
  non-trivial one is `_union_unique`, whose recursive form conses onto the result of merging both
  tails; it is replayed from a recorded step list so comparator and policy call order is unchanged.
  Tests assert depths past the limit rather than trusting it.
- **Non-reflexive equality.** `float("nan") == float("nan")` is false, and a non-reflexive relation
  leaves a position permanently dirty in a way no internal validation can detect.
  `natural_value_equality` consults identity first; `reflexive_ieee_equality` collapses every `NaN`
  into one class, matching .NET's default comparer and Rust's `EqualityPolicy::reflexive_ieee`.

## Adversarial Parity Audit

Two waves of eight and nine agents audited all nine modules against the C# reference, each finding
passed to a separate skeptic instructed to refute it by default and to settle vacuous-test claims by
mutating a scratch copy rather than by argument. Thirteen candidates were raised and **eight
confirmed; five were refuted by execution**, including two "structural-sharing assertion cannot
fail" claims that turned out to bite. The connection forest, the deque, and the run-delta vector
produced no confirmed finding at all.

**No confirmed finding was a behavioural defect.** All eight were unpinned contracts, tests that
could not fail, or claims stronger than the code delivers — which is the failure mode this
repository cares most about. The four with real risk attached:

1. **`meld`'s documented root tie-break was observed by nothing.** The contract promises a tie
   favours the receiver; relaxing `_le` to a strict comparison left all 32 tests green, because the
   only meld tie in the suite was `heap.meld(heap)` — a root compared against itself. Now pinned in
   both directions.
2. **The test named for composition direction could not see a reversed composition.** It was built
   entirely from floors, and floor-over-floor is commutative under the clamp policy, so all three
   tag-pushdown reversals passed it. It now applies a collapsing floor/cap pair. A second gap fell
   out of fixing it: the forest-side pushdown had exactly one guard in the whole file, because
   `_cons` builds identity-tagged cells and composing against the identity is commutative. A
   dedicated case now stacks two non-identity tags on one spine cell, the only shape that reaches it.
3. **The arena's "branching shape" test built a depth-zero star.** `handles[(index * 7) %
   len(handles)]` is identically zero, since `len(handles) == index` at that point. Every leaf hung
   off bottom, so `add_leaf`'s coalescing arm never executed and no ancestor answer below depth 1
   was ever checked — five distinct corruptions of the jump-distance arithmetic passed the whole
   file, including one that returns the wrong node for 181 of 201 depths on a 200-node chain. This
   was the integrator's own test, and it is the finding this audit most justified itself by. The
   shape is now four round-robin frontiers with a fifth forked partway, and the test asserts the
   shape it needs — that the forest branches, that it reaches depth 20, and that reaching bottom
   from the deepest node costs fewer hops than its depth, which is only possible if jump links were
   built and followed.
4. **A statistic was never asserted to advance.** `add_leaf_count` appeared once, comparing two
   reads of the same counter, which also holds for a counter that is always zero; deleting the
   increment passed every test. Now pinned to a value.

The remaining four were wording: an impossibility claim that is false in the common case (below), a
`shares_storage_with` summary that promised same-root identity where the substrate delivers
node overlap, a `from_items` representative rule the port deliberately diverges from C# on but never
pinned, and one tautological assertion re-reading a value the previous line established.

## A Correction That Reached Beyond Python

The slice queue's docstring claimed a split at `0` "cannot be specialized away", because "only an
ancestor query can name that node from the retained tail". That is false whenever `low_depth == 0` —
every queue from `create`, `create_myers`, or `from_iterable` — where the required node is the
arena's bottom, an O(1) seam read. It is false for precisely the configuration the pinned
query-count test measures, and the module contradicted itself two paragraphs away.

The behaviour, the tests, and the C# reference all agree; only the prose overclaimed. But the
sentence was carried verbatim by the C, Rust, C++, and OCaml ports, and this repository prefers
"both or neither" to divergence, so all five were corrected together with the `low_depth == 0`
carve-out spelled out. Every changed line is a comment; `cargo check` and `dune build @check` exit
0, and the C and C++ headers pass `-fsyntax-only` with `-Werror` on the C side.

## Verification

- `src/Python/test.ps1` passes end to end: `ruff format --check` and `ruff check` clean over 86
  files, `mypy` strict clean over 86 files, the full pytest and Hypothesis suite green, sdist and
  wheel built, both Twine checks PASSED, and the installed-wheel smoke test succeeded in a clean
  virtual environment.
- **417 tests pass, up from 234 at baseline.** The 183 new cases are 8 for the arena seam, 32 for
  the equality relations, 121 across the six finger-tree collections, and 22 for the connection
  forest.
- Every load-bearing property was mutation-checked rather than assumed to bite. Reversing either tag
  composition direction, flipping the meld root tie, the insert tie, or the forest-minimum tie,
  dropping an expose, corrupting the coalesced jump distance in three distinct ways, deleting the
  leaf counter, adopting C#'s `from_items` representative rule, substituting a fresh-but-equal cell
  on cancellation, replacing a range seek with a filter, and removing run merging or canonicalization
  were each confirmed to fail a named case. The arena's headline bound is discriminating by
  construction: with coalescing removed a 32768-node chain needs 32768 hops against a stated ceiling
  of 64, while every ancestor *answer* stays correct, so only the hop count can catch it.

## One Thing Fixed That Was Not the Port

`src/Python/test.ps1` had been exiting 1 before running anything, because `ruff format --check`
rejected 24 files under the pinned ruff. Nothing downstream had been running. The fix was not purely
mechanical: joining those 43 docstrings is what the formatter wants, but it pushes each 1 to 3
columns past the 100-column limit, which `ruff check` then rejects, so the two gates could not both
be satisfied by reflowing and the docstring wording was shortened to fit. It is a separate commit so
it stays revertable and does not bury the port in whitespace.

The same commit's table repair is worth noting for a different reason: the research-derived
collections' language table in `docs/reference/data-structure-catalog.md` had a stray blank line
introduced by the C port, which split it in two, so the C through Haskell rows had been rendering
without a header for five commits. Repaired while adding the Python row.
