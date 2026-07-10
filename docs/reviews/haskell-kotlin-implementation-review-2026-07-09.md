# Haskell and Kotlin Implementation Review — 2026-07-09

- Created (UTC): 2026-07-09T00:00:00Z
- Repository HEAD: 55457de392ad5d97d152638b17c994d5d8a02ab6
- Audience: Maintainers of the Haskell and Kotlin ports
- Scope: Correctness, semantic parity, language-idiom soundness, and API quality of the Haskell and Kotlin workspaces; fixes applied; deferred follow-ups

## Summary

The third and final installment of the 2026-07-09 cross-language review, completing coverage of
all six languages (after the [C#/Rust](csharp-rust-implementation-review-2026-07-09.md) and
[C/C++](c-cpp-implementation-review-2026-07-09.md) reviews). Four parallel review passes traced
the Haskell and Kotlin workspaces against the C# references, each primed with the checklist of
bugs that had already recurred across the other four languages; findings were re-verified before
changing anything. All six workspace test suites (three cabal, three Kotlin) were green before the
review and green after both fix commits.

The recurring-bug checklist proved its worth — of the five recurring defect classes, **four were
present in at least one of these two languages**:

| Recurring defect | Haskell | Kotlin |
| --- | --- | --- |
| HAMT set difference / symmetric-difference rebuilding (losing structural sharing) | symmetric-difference variant | **both present** |
| Tungsten rule-2 terminal no-op fast path missing | **present** | absent (already correct) |
| Interval-tree equal-low tie order (new must precede existing) | **present** | present in mirror form (sorted by high) |
| Sorted-map setItem keeping the old key | absent | **present** |
| Stamp midpoint not floor / overflow | absent (Integer, exact) | truncation-toward-zero variant |

Both languages also contributed defect classes of their own: Haskell's derived structural
`Eq`/`Ord` instances violated extensionality (verified in GHCi: `fromList [1,2,3] /= cons 1
(fromList [2,3])`), and Kotlin's Tungsten stamp tree had the same AVL split under-balancing found
in the C port (simulation drove balance factors to 15). One genuine memory-shape bug appeared in
Kotlin: `TextRope.offsetOf` overflowed `Int` for huge columns and could return a *negative*
offset.

## Fix commits

| Commit | Area | Content |
| --- | --- | --- |
| `1186e0b` | Kotlin (all three workspaces) | Set-difference sharing, null-element handling, AVL joins, list setItem spec compliance, floor midpoint, sorted-map supplied key, interval tie order, offsetOf overflow, policy value-equality, meld comparator guard |
| `55457de` | Haskell (all three packages) | Interval tie order, extensional Eq/Ord, sorted set first-wins, bag instance retention, fromChunks re-chunking, lazy middle spine, foldl' strictness, HashSet toggle/receiver-policy/no-op insert, rule-2 fast paths + setItem no-op, stamp-keyed search |

## Findings fixed

### High severity

**Kotlin `SortedMap.setItem` kept the old stored key** (recurring bug; same fix as Rust/C/C++):
with a case-insensitive comparator, `setItem("key", 1).setItem("KEY", 2)` yielded `("key", 2)`
where C# yields `("KEY", 2)`; the equal-value short-circuit discarded the supplied key too. Now
stores the supplied key unconditionally; regression-tested.

**Kotlin `TextRope.offsetOf` Int overflow**: `start + column` wrapped for huge columns, passed the
`<= end` guard as a negative number, and returned a negative offset instead of null. Now widens to
`Long`, exactly like the C# reference's `(long)start + column` (which carries a comment about
precisely this hazard).

**Kotlin HAMT `except`/`symmetricExcept` rebuilt the whole set** — the same recurring defect fixed
in Rust (and found half-present in Haskell): O(n + m) full rebuilds, no structural sharing, and an
empty argument returned a structurally new set, breaking the port's own documented root-
preservation contract. Both now fold removals / toggle distinct elements on the receiver;
sharing is regression-tested via `sharesRootWith`.

**Haskell derived `Eq`/`Ord` were not extensional** on `Deque`, `Rope`, `ReversibleDeque`,
`PriorityQueue`, and Tungsten's `PersistentList`: equality compared internal tree shape, chunk
layout, or orientation flags, so content-equal sequences compared unequal (GHCi-verified). Any
client using these types in `elem`/`nub`/test assertions or as `Map` keys got wrong answers — a
worse surface than C#'s deliberate absence of value equality. All five now carry hand-written
extensional `Eq`/`Ord` over `toList` (with a count fast path), and the unsound derived `Read`
instances (which could construct invariant-violating values) are gone. Regression-tested with
shape-divergent equal sequences.

**Haskell interval-tree equal-low tie order** (recurring bug): new equal-low intervals were
appended after existing ones; C# places them before. One-line bucket fix plus tie-order and
run-deletion regression tests. The Kotlin port had the mirror-image defect — it re-sorted by
`(low, high)`, an order the C# reference never uses — and now inserts at the low-only lower bound
with comparison-based (not `equals`-based) membership over the equal-low run.

### Medium severity

- **Kotlin Tungsten `SeqTree` AVL violation** (same class as the C port's stamp tree): split
  pre-built nodes with unbounded balance factors and repaired with a single `balance` call
  (which only fixes a difference of two); simulation reached factor 15 at n = 20k. Split, insert,
  and concat now route through a `joinNodes` primitive that descends the taller side — the same
  fix shipped for C.
- **Haskell Tungsten rule-2 fast path missing** (recurring bug): `append`/`prepend` always
  deleted and re-inserted, adopting the supplied key where the C# spec (test-locked) keeps the
  stored instance and consumes no stamp. Both now short-circuit, and `setItem` gained the
  C#-parity equal-value no-op. These require value equality, so
  `setItem`/`setItems`/`join`/`fromList`/`append`/`prepend` now carry `Eq v`.
- **Haskell `SortedSet` was last-wins** (`Data.Set.insert` replaces a stored equal element) where
  C# keeps the first stored instance; **`SortedBag` collapsed instances to counts** where C#
  retains every comparer-equal instance (new after existing). The set insert is now a
  membership-guarded no-op; the bag representation changed to insertion-ordered buckets
  (`Map a [a]`) whose `toList`/`index` return the actual stored instances and whose `deleteOne`
  removes the first stored equal. Both regression-tested with label-carrying elements.
- **Haskell strictness/space leaks**: every bulk constructor used lazy `foldl` (O(n) thunk chains,
  stack-overflow risk at scale); all switched to `foldl'`. Conversely the `Deep` middle spine was
  *too* strict — a bang forced the full overflow cascade eagerly on every cons/snoc, forfeiting
  the amortized O(1) endpoint bounds that spine laziness provides (and that the C# reference
  gets from memoized suspensions). The bang is removed; digits and cached measures stay strict.
- **Haskell `HashSet` shape issues**: `insert` of a present member rebuilt the spine (now a
  receiver-returning no-op — first instance wins, root shared, and `union` stops paying per
  common element); `symmetricDifference` materialized union + intersection + difference (now
  toggles on the receiver); the binary relations judged membership under the *argument's* policy
  where C# uses the receiver's comparer (now probe under the receiver's policy).
- **Kotlin nullable-element conflations**: set `tryRemove` reported a stored `null` element as
  absent (`get() ?: return null`), and list `updateAt` treated a stored `null` as
  index-out-of-range. Fixed via a new `tryRemoveEntry` on the map (single trie walk, surfaces the
  stored entry) and an explicit bounds check; both regression-tested.
- **Kotlin list `setItem` no-op fast path**: the ordering spec explicitly excludes the list's
  `setItem` from no-op identity ("the deque unconditionally replaces the stored element",
  test-locked in C#); the Kotlin equal-value receiver-return diverged and is removed.
- **Haskell `Rope.fromChunks`** stored caller chunks verbatim, violating the module's own
  `maxChunkSize` invariant for oversized imports; now re-chunks.

### Low severity (selected)

Kotlin: stamp midpoint truncated toward zero via BigInteger (now the C# unsigned-gap floor
formula, allocation-free); association `join` lacked the empty-receiver fast path (now adopts the
argument); `keySortWith`/`sortWith` rebuild for size ≤ 1 (now return the receiver, matching C#
`SortedBy`); stateless measure policies compared by identity so `concat` rejected equal-but-
distinct instances (now value-equal); `PriorityQueue.meld` silently mixed comparators (now
guarded); interval `contains`/`tryRemove` matched by `equals` instead of comparison (scale-
sensitive types like `BigDecimal` diverged from C#; now comparison-based). Haskell: the internal
stamp search built sentinel entries with `undefined` key/value fields riding stamp-only `Eq`/`Ord`
instances (both now replaced by a direct stamp-keyed search); READMEs document the 64-bit `Int`
stamp assumption and trie-order enumeration.

## Verified sound (traced; no defects found)

- **Haskell HAMT trie mechanics**: popcount slotting, hash truncation through a single `Word32`
  funnel, collision paths, removal canonicalization (collapses leaf/collision children only),
  bulk-construction duplicate handling with stored-key retention — all matching C#. The Tungsten
  stamp arithmetic is exact C# parity via `Integer` intermediates (floor midpoint included), and
  rules 1 and 3–8 traced correct, including insert's pre-removal index interpretation.
- **Haskell FingerTree core is a genuine Hinze–Paterson tree** (digits 1–4, Node2/Node3,
  measure-guided split, app3 concat) with correct split-accumulation order; the ReversibleDeque's
  mixed-orientation glue was traced through digit overflow, node arities, and flag algebra with
  no defect. Priority-queue peek reads the cached measure in O(1) with correct FIFO stability;
  text `offsetOf` column validation was already correct.
- **Kotlin HAMT**: bit arithmetic exactly mirrors C#'s unsigned code, canonicalization is
  correct, `put` retains the stored key, no-op identity and last-wins bulk construction are
  test-covered, sealed-interface `when`s are exhaustive without masking `else` branches.
- **Kotlin Tungsten ordering rules** 1, 3, 5, 6, 7 traced correct (including the stamp-tiebreak
  stable sorts and the relabel arithmetic), and the rule-2 fast path was already present and
  stored-key-preserving. `ReversibleDeque`'s orientation logic (index mirroring, split mirroring,
  reversed part-swapping) is correct with no leaked mutable storage.

## Parity status

- **Haskell**: HAMT and Tungsten are now at rule-level parity. The FingerTree package's core is
  finger-tree-grade, but `Rope`/`MeasuredRope` remain flat-rebuild facades over a chunk-count
  measure (every positional operation is O(n) — an *undocumented* checkpoint this review
  surfaced), the sorted facades are `containers` wrappers (documented), and the interval tree is
  an O(n)-query map-of-buckets. The deque's sorted-bound helpers are O(log² n) versus C#'s
  signposted O(log n).
- **Kotlin**: at semantic parity after these fixes. The FingerTree workspace remains the
  documented flat-`List` semantic checkpoint (every facade except `ReversibleDeque`), so
  complexity parity is out of scope by design; the Tungsten `SeqTree` now genuinely maintains
  its AVL bound.

## Deferred follow-ups

1. **Haskell rope rework** (the largest gap surfaced): measure chunks by element count and edit
   only the boundary chunk, mirroring `Rope.cs` — currently every rope edit is O(n) with zero
   structural sharing, and nothing documents it. Also rebase the text helpers on the measured
   rope (they inherit the O(n) scans). Interval-tree queries could ride a max-high annotation.
2. **Kotlin FingerTree post-checkpoint work**: when the flat-`List` checkpoint graduates to real
   trees, the recurring-bug checklist from these reviews is the porting test plan.
3. **Haskell laziness polish**: `Node` element fields and `mapValues` results are lazy (thunk
   retention under heavy mapping); consider bangs or documentation. `insertNew`-based
   `HashMap.adjust` still walks the trie twice.
4. **Coverage**: a large-`fromList` stress test (would have caught the `foldl` chains as a stack
   overflow), Haskell collision-shrink canonicalization lock-in, Kotlin `SeqTree` balance-factor
   assertions, and cross-policy set-relation tests.

## Test evidence

- `cabal test all` (src/Haskell): 3/3 suites pass with new regression coverage (interval tie
  order and run deletion, set first-wins, bag instance retention and first-instance deletion,
  list extensional equality, association terminal no-ops).
- `.\build.ps1` (src/Kotlin): all suites pass — Hamt 10 tests (+2: difference sharing,
  stored-null removal), FingerTree 15 (+2: supplied-key setItem, interval tie order),
  Tungsten 8.
- The C#, Rust, C, and C++ workspaces were not modified by this review.

## Resolution addendum — 2026-07-10

This dated addendum preserves the review's original findings and provenance above while recording
the subsequent remediation. **All four deferred follow-up groups are resolved.**

1. **Haskell rope and interval representation — resolved.** `Rope` now stores 64-element-bounded
   chunks in an element-count-measured finger tree; positional edits replace only the boundary chunk
   and retain untouched subtrees. `MeasuredRope` caches both element count and the caller's monoidal
   measure on the same substrate. Text navigation uses cached newline measures rather than full-text
   scans. `IntervalTree` is now a low-sorted finger tree annotated with last-low and maximum-high
   summaries, so lower-bound insertion and first-overlap lookup are logarithmic and overlap
   enumeration prunes unreachable prefixes. Optimized-GHC tests use `StableName` identity to prove a
   far chunk survives a boundary edit.
2. **Kotlin post-checkpoint representation — resolved.** The former flat-`List`/`String` storage was
   removed from every long-lived FingerTree-family facade. A shared immutable measured AVL sequence
   now supplies cached size/height/monoidal measure, structural joins and splits, logarithmic indexed
   edits and measure location, and path-copying JVM-node sharing. `PersistentDeque`, `FingerTree`,
   sorted collections, priority queue, max-high interval tree, positional/measured ropes, and measured
   text all use that substrate; `ReversibleDeque` retains its already-real orientation-aware tree.
   The active API notes explicitly distinguish this strict measured-AVL engine from the C#/C++ lazy
   digit spine instead of carrying a vague flat-storage checkpoint.
3. **Haskell strictness and HAMT adjustment — resolved.** Finger-tree `Node` elements are strict,
   `mapValues` forces mapped results to weak head normal form without globally making ordinary map
   values strict, and `HashMap.adjust` updates through one trie/collision traversal rather than a
   lookup followed by insertion.
4. **Deferred coverage — resolved.** Haskell includes 100,000/200,000-element bulk-construction
   stress, representation-sensitive collision-shrink canonicalization through `validStructure`,
   receiver-policy set relations, multi-chunk rope/text
   navigation, structural-sharing identity, and max-high interval model checks. Kotlin includes a
   100,000-element measured-tree build, a 5,000-command edit/split model with AVL assertions, sharing
   checks across the measured collection facades, receiver-policy HAMT relations, the recurring cross-language bug
   checklist, and 2,000 split/join/remove cycles over a 20,000-element Tungsten `SeqTree` with the AVL
   bound checked after every operation.

Resolution validation:

- `cabal test all -j1` from `src/Haskell`: all three suites pass.
- `.\build.ps1` from `src/Kotlin`: HAMT 11/11, FingerTree 18/18, and Tungsten 9/9 executable tests
  pass.
