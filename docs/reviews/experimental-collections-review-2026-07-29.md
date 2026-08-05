# Experimental Collections Review — 2026-07-29

- Created (UTC): 2026-07-29
- Repository HEAD (reviewed): 55abf98 (`experimental` branch, post-consolidation)
- Audience: Maintainers evaluating the seven `*.Experimental` C# collections for promotion,
  further research, or cross-language porting
- Scope: Implementation correctness, worst-case/amortized complexity-claim verification, API
  completeness and usability, and documentation integrity for every type in
  `Durable7.FingerTree.Experimental` and `Durable7.Hamt.Experimental`; the fixes applied during
  this review

> **Current-state note: this report is fully remediated.** After this review the seven collections
> were promoted out of the `*.Experimental` namespaces into `Durable7.FingerTree` and `Durable7.Hamt`
> and ported to Rust, so namespace-qualified names below describe the state at review time.
> Finding **F1** (duplicated Myers arena machinery) is closed in BOTH languages: each now ships one
> shared incremental-ancestor arena serving both consumers, and the removed C# types were
> `IIncrementalLevelAncestorArena<T>`, `MyersLevelAncestorArena<T>`, and
> `MyersLevelAncestorStatistics`. **F4** (catalogs not indexing these collections) is closed.
> Every enhancement in "Proposed Enhancements" and every test gap named in "Test-Suite Assessment"
> has since been implemented in both languages — see the
> [2026-08-04 port review](rust-collection-port-review-2026-08-04.md) for the landed list. The
> per-collection verdicts and the reasoning below stand as written.

## Summary

All seven experimental collections were reviewed line-by-line against their research proposals,
family documentation, and test suites. **No correctness defects were found in any of the seven
implementations.** Every complexity claim checked out under manual verification of the underlying
algorithms, including the worst-case bounds that depend on nontrivial structural invariants
(skew-binary forest lengths, union-by-size height, Myers jump-chain distances, at-most-two
level-ancestor queries per bilateral slice, and the union-forest LCA argument behind
`FirstConnected`). The proposals are notably honest: shipped (Myers-backed, amortized) bounds are
consistently separated from theoretical (Alstrup–Holm) instantiations, and prior-art boundaries are
stated rather than implied away.

Two documentation gaps and one small validation bug were fixed during the review. The remaining
findings are design observations and enhancement proposals that need maintainer judgment; none
blocks continued experimental status.

Baseline before changes: full solution suite green (366 Hamt + 794 FingerTree + 80 Ordered in
Debug via `src/CSharp/test.ps1`). The FingerTree and Hamt projects were re-run green after the
fixes.

## Fixes Applied In This Review

1. **`ContextualRankSequence.CheckRange` misattributed parameter** —
   `GetRange(index, count)` with `index > Count` threw an `ArgumentOutOfRangeException` naming
   `count` instead of `index`. The check was split so each parameter is blamed correctly, matching
   the convention used by `BilateralAncestralDeque.Slice` and the non-experimental collections.
   (`src/CSharp/src/Durable7.FingerTree/ContextualRankSequence.cs`)

2. **`PersistentMonotoneActionHeap` was absent from the FingerTree family docs** — the 2026-07-29
   shipment landed with only a `validation.md` test-filter line. Added: an overview blurb and
   `Layout` entry (`src/CSharp/docs/FingerTree/overview.md`), a normative contract section
   "Persistent Monotone-Action Heap (Experimental)" (`src/CSharp/docs/FingerTree/api-specification.md`),
   and a worked usage section with a clamp/meld example (`src/CSharp/docs/FingerTree/usage.md`).

3. **`PersistentAncestralConnectionForest` was absent from the Hamt family docs** — same gap for
   the other 2026-07-29 shipment. Added: an overview blurb and `Layout` entry
   (`src/CSharp/docs/Hamt/overview.md`), a contract section "Persistent Ancestral Connection Forest
   Contract (Experimental)" (`src/CSharp/docs/Hamt/api-specification.md`), and a usage section plus
   a "Choosing A Surface" row (`src/CSharp/docs/Hamt/usage.md`). The earlier 2026-07-25 batch
   (deque, queue, sequence, delta map) and the run-delta vector had already been integrated; only
   the two newest types were missing.

## Per-Collection Verdicts

### AncestralSliceQueue (`Durable7.FingerTree.Experimental`)

**Correct; claims verified.** The Myers arena's jump construction uses
`skipped.SkipDistance <= parent.SkipDistance` where the paper uses equality; under the skew-binary
distance invariant the strict-less case is unreachable, so the code is equivalent (and safe if the
invariant were ever relaxed). The greedy descent (`take the jump iff it does not overshoot`) gives
the O(log M) query bound; `1 + d + d` cannot overflow because depth is capped at `Int32.MaxValue`
first. Odd-block addressing (`block = floor(sqrt(i))`, offset `i − block²`) is exact, with
`M + 1 + O(sqrt(M+1))` slots as documented. The anchored-empty invariant
(`tail.Depth == lowDepth − 1` when `Count == 0`) is preserved by every boundary operation,
including `Take(0)`/`Slice(k, 0)` anchoring at the ancestor of depth `lowDepth − 1`. The
U/Q-parameterized complexity table in the proposal matches the code exactly (one arena call per
scalar operation; `TryRemoveFirst` pays Q because it reads `First`).

### BilateralAncestralDeque (`Durable7.FingerTree.Experimental`)

**Correct; claims verified.** The two-oriented-segment representation (`reverse((l-a, l-t]) ++
(r-a, r-t]`) is maintained consistently across the indexer's depth arithmetic, `RemoveBase`'s
crossing-the-center ancestor query, and both slice halves. The "at most two level-ancestor
queries" claim for `Slice` holds case-by-case: a boundary-crossing slice pays at most one query
per side, and a one-sided slice pays at most two on that side; `SplitAt`'s `Take`+`Drop` pair pays
at most one query each, so the split is also ≤ 2 queries total. Minor observations:

- `RemoveBase`'s `Count == 1` branch is unreachable (callers route singletons through
  `EmptyHandle()`); harmless defensive code.
- Empty results (`Slice` with `count == 0`, `Clear`, singleton removals) re-anchor at `Bottom`,
  unlike the slice queue's provenance-preserving anchored empties. This is semantically fine for a
  deque (an empty deque has no visible history) and is the documented design, but the asymmetry
  with ASQ is worth remembering if the two are ever unified.

### ContextualRankSequence (`Durable7.FingerTree.Experimental`)

**Correct; claims verified** (one validation fix applied, above). The per-subtree summary — a
total function `state → (state, eventCount)` plus a count — is a lawful monoid (function
composition with additive nonnegative costs), so the finger tree's split/locate machinery applies.
`TrySelectEvent`'s arithmetic is exactly matched to the core's `TryLocate` contract
(`measureBefore` = prefix measure strictly before the boundary element), and both predicates are
monotone (counts and event counts are nondecreasing in the prefix). Claimed bounds — O(1) full
evaluation, O(s log n) amortized prefix/rank/select/edits, O(s) amortized endpoint updates,
O(s log(min(n,m))) concat, O(s·n) space — all follow from O(s) `Combine` on the lazily measured
tree. Notes:

- `Prepend`/`Append` at `Count == Int32.MaxValue` throw `OverflowException` from the checked
  count addition inside `Summary.Combine` rather than from `Insert`'s explicit guard. The
  exception type is the same and no partial successor is published (the constructor forces the
  root measure), so this is acceptable; adding the explicit guard to both endpoint operations
  would only improve the message.
- An invalid `TMachine.StateCount` surfaces as a `TypeInitializationException` wrapping the
  `InvalidOperationException` (static-initializer validation). Worth one doc sentence if the type
  is promoted.

### PersistentDeltaMap (`Durable7.FingerTree.Experimental`)

**Correct; claims verified.** The triple-of-persistent-maps design keeps the change index exact
under the two comparer policies: representatives are provably stable across point updates and
delete/re-add round trips (the change record retains the checkpoint representative, so when the
change index empties, current and checkpoint agree extensionally *and* representative-wise up to
value equivalence), which makes the checkpoint-root snap (`nextCurrent = _checkpoint` when
`nextChanges.IsEmpty`) sound and restores O(1) `Checkpoint`/`Rollback` identity behavior. The
`TryCeilingEntry + Compare == 0` idiom correctly recovers stored representative entries. O(log N)
point operations, O(1) checkpoint/rollback, Θ(k + 1) `GetChanges`, and O(N + k) live space all
hold. No changes needed. Enhancement candidates: bulk `SetItems`; a key-range-restricted
`GetChanges(low, high)` (the change index is itself a sorted dictionary, so this is O(log k)
seek + output).

### PersistentMonotoneActionHeap (`Durable7.FingerTree.Experimental`)

**Correct; claims verified.** This is the most algorithmically dense of the seven. Verified in
detail:

- The fused bootstrapped skew-binomial representation (rank-0 trees carry an attached embedded
  forest) is decomposed consistently by `SplitForest`/`ValidateFusedChildren`; the permitted
  rank-zero ambiguity at rank 1 affects only which route a rank-0 tree takes through delete-min
  (zeros vs. trees vs. attachment) — all routes preserve contents and produce valid skew forests.
- All traversals reach children exclusively through the tag-composing accessors
  (`Expose`/`ForestHead`/`ForestTail`), so every comparison sees fully composed logical
  priorities; melding differently transformed heaps cannot cross-apply actions, and monotonicity
  is exactly the property needed for tags to commute with the heap order.
- `SplitForest`'s `trees` accumulator provably comes out rank-nondecreasing (with at most the
  leading pair equal), which is the precondition `Uniquify`/`InsertRanked` need; `UnionUnique`'s
  cascading links stay within O(log n) total.
- Forest lengths are bounded by skew-binary digit counts of the per-forest increment counter,
  giving the O(log n) delete-min bound; insert/meld/transform/minimum are O(1) worst-case
  including allocation.
- `OrderClampPolicy.Compose` computes the exact composite: overlap → `[max(iL,oL), min(iU,oU)]`
  (verified `L ≤ U` from the two disjointness rejections), disjoint → the correct constant, and
  constants short-circuit exactly, preserving representative distinctions under coarse comparers.

Micro-observation: `SkewInsert` materializes `ForestTail(forest)` (allocating a tagged `Forest`
when a tag is pending) before the rank check that may reject linking; peeking `RawTail` ranks
first would avoid one small allocation on the hot insert path. Not applied because the current
form is clearer and the cost is O(1).

### PersistentRunDeltaVector (`Durable7.FingerTree.Experimental`)

**Correct; claims verified.** The run index arithmetic (`AddDirtyPosition` merge/extend/new,
`RemoveDirtyPosition` remove/shrink/split) maintains ordered, non-overlapping, non-adjacent
maximal runs in all cases; the impossible fourth `SetItem` case (`!wasDirty && !remainsDirty`
after a non-equal write) is genuinely unreachable because clean positions share the checkpoint
`Cell` by reference. The reference-identity cleanliness invariant, checkpoint-root
canonicalization, and O(log n) splice-based `AcceptDirtyRunAt`/`RevertDirtyRunAt` (independent of
run length, via two RRB splits + two concats) are all sound. Observations:

- **Usability**: runs are addressable only by rank. Holding a `PersistentDirtyRun` from
  `TryGetDirtyRunContaining` or `EnumerateDirtyRuns`, there is no direct way to accept/revert it —
  the caller must rediscover its rank. An `AcceptDirtyRunContaining(int index)` /
  `RevertDirtyRunContaining(int index)` pair (or run-start-keyed overloads) would close the loop
  cheaply (the index is keyed by start position already).
- `AddDirtyPosition`'s both-neighbors merge performs a redundant `.Remove(left.Key)` immediately
  followed by `.SetItem(left.Key, …)`; one persistent-map operation could be saved.
- Per-element `Cell` boxing is the price of the reference-identity design and roughly doubles
  memory for value-type payloads; this is inherent and the proposal owns it, but a promoted
  version might document the overhead explicitly in the API reference.

### PersistentAncestralConnectionForest (`Durable7.Hamt.Experimental`)

**Correct; claims verified.** The central theorem — the first-connection version of a pair equals
the maximum union-edge version on their tree path, computed as the latest `JoinedAt` strictly
below the forest LCA — holds because later unions only attach the (already common) root to other
roots, extending both vertices' parent paths by a shared rootward suffix; the LCA and everything
below it are frozen at connection time. Union by size without compression gives the O(log n) path
bound (`ValidateInvariants` enforces `Log2(VertexCount)` as the hard ceiling), and the sparse
absent-cell-is-singleton convention makes `Create` O(1) for any universe. Redundant links share
the cell map and add only a version token. Observations:

- **Version-chain retention**: every `Link` (including redundant ones) allocates a token holding a
  strong reference to its whole parent chain, so any live version retains O(history depth) token
  space even when the connectivity index is tiny. The proposal discusses this; the new API-spec
  section now states it too. A long-running service issuing many redundant links should know the
  cost is per-link, not per-successful-union.
- **API gap**: component size is tracked internally (root cells cache `Size`) but not exposed. A
  `GetComponentSize(int vertex)` returning `GetCell(FindRoot(vertex)).Size` would be O(w log n)
  and free to add. Similarly, `VertexCount`-fixed universes might warrant a
  `TryGetFirstConnected` overload accepting a version *token* to query "were these connected as of
  ancestor version V" — currently only the receiver's own version can be queried; callers must
  retain the forest value for each version of interest (which is the documented model, but worth
  an explicit note).

## Cross-Cutting Findings

### F1. Duplicated Myers arena machinery (design; needs a decision)

`MyersIncrementalAncestorArena<T>` (AncestralSliceQueue.cs) and `MyersLevelAncestorArena<T>`
(BilateralAncestralDeque.cs) — plus their interfaces (`IIncrementalAncestorArena` /
`IIncrementalLevelAncestorArena`), statistics records, and private `OddBlockStore<T>` — are
near-verbatim duplicates (~300 lines duplicated). The per-experiment isolation is defensible while
both types are research prototypes with independent lifecycles, but any promotion should
consolidate them into one shared incremental-level-ancestor seam: the interfaces are structurally
identical, and the deque's interface documentation is a strict superset (it states the publication
and O(1)-read requirements the queue's version leaves implicit). Consolidation would also let one
Alstrup–Holm backend, if ever implemented, serve both types.

### F2. Enumeration buffering (documented; acceptable)

The three arena-backed types buffer up to Θ(count) elements during enumeration (parent chains
yield back-to-front). This is documented in every affected member. A promoted version could offer
an O(1)-state enumerator when a Q(M)=O(1) backend exists, exactly as the ASQ proposal already
notes.

### F3. Pre-existing build warnings (out of scope; separate task filed)

The solution build emits ~24 `CS1587` warnings (plus one `CS0419`) from stray duplicated
`/// <summary>` lines wedged between return types and method names in **non-experimental** files
(`SumMeasure.cs`, `ProductMeasure.cs`, `FingerTreeProductExtensions.cs`,
`FingerTreeMeasureExtensions.cs`, `SortedDictionary.cs`, `SortedSet.cs`, and four test files).
These predate the experimental branch (verified present on `main`) and contradict the documented
zero-warning policy. Not fixed here to keep this branch's diff scoped; a separate task was filed
to fix them on `main`.

### F4. Top-level reference catalogs do not index the experimental namespace (needs a decision)

`docs/reference/data-structure-catalog.md` and `docs/reference/frontier-structure-catalog.md`
mention none of the seven experimental types; the frontier catalog's "Implementation Status"
section ends at the earlier implementation wave. Since both catalogs present themselves as the
cross-workspace orientation layer, an agent starting from `docs/reference` will not discover the
experimental collections at all (the proposals live under `docs/proposals`, and the family docs —
now complete after this review's fixes — are the only indexed path). If the exclusion is
deliberate ("catalogs list cross-language shipments only"), one sentence in each catalog pointing
at `docs/proposals/` for single-language experimental surfaces would close the discovery gap; if
not, a short experimental-status section listing the seven types belongs in the frontier catalog.

### F5. Complexity documentation is exceptionally honest (positive finding)

Every proposal separates shipped bounds from theoretical instantiations, names the exact
allocation model caveats (managed block zeroing breaking worst-case insertion, RRB amortization),
and refuses to count unsupported operations in dominance claims. The XML documentation on the
types matches the proposals. This discipline should be preserved verbatim if these types are
ported to the other language workspaces.

## Test-Suite Assessment

All seven test files map to their proposals' stated obligations: model-equivalence randomized
histories with retained-branch checks, failure atomicity for throwing policies/comparers,
concurrency stress over immutable versions, and — notably — *complexity guardrail* tests
(`QueueOperations_UseTheParameterizedBackendComplexityBoundary`,
`PublicOperations_RespectLevelAncestorQueryCeilings`,
`TransformAll_HasStrictConstantPolicyCallsAndAllocation`,
`GetChanges_ComparisonCountsAreIndependentOfBaselineSize`,
`CachedRankAndSelect_DoNotRescanTheInput`) that pin operation-count envelopes rather than
wall-clock, which is the right way to regression-guard asymptotic claims. The heap and forest
suites cover the two subtle cases this review independently identified as the risky ones (the
rank-zero decomposition ambiguity via `ValidateStructure_AcceptsDeeplyTaggedAdversarialShapes`,
and the LCA edge cases via `FirstConnected_HandlesOneEndpointAtLcaAfterThatLcaBecomesAChild`).

Gaps worth adding later (none urgent): an ASQ/deque test that a *custom* (non-Myers)
`IIncrementalAncestorArena` implementation drives the facades correctly (the extension seam is
public but only the shipped arena is ever plugged in); a run-delta-vector property test asserting
`AcceptDirtyRunAt` ∘ `RevertDirtyRunAt` interleavings across branches never violate
`ValidateInvariants` (currently exercised only via the randomized model test).

## Proposed Enhancements (Not Applied; For Prioritization)

1. `PersistentRunDeltaVector`: `AcceptDirtyRunContaining(int)` / `RevertDirtyRunContaining(int)`
   overloads (small, high usability value).
2. `PersistentAncestralConnectionForest`: expose `GetComponentSize(int vertex)`.
3. `PersistentDeltaMap`: range-restricted `GetChanges(TKey low, TKey high)`.
4. Consolidate the duplicated Myers arena machinery behind one interface if either arena-backed
   type is promoted (F1).
5. `ContextualRankSequence`: explicit endpoint overflow guards for uniform exception messages.
6. `PersistentMonotoneActionHeap.SkewInsert`: peek `RawTail` ranks before materializing the tagged
   tail (removes one O(1) allocation from the hot insert path).

## Validation

- Baseline (pre-review): `src/CSharp/test.ps1` — full solution green in Debug (366 Hamt,
  794 FingerTree, 80 Ordered), exit code 0.
- Post-fix: `Durable7.FingerTree.Tests` re-run green (794/794) and `Durable7.Hamt.Tests` re-run
  green (366/366). The documentation edits do not affect compilation; the only code change is the
  `ContextualRankSequence.CheckRange` split, covered by
  `BoundariesMultiEventsAndInvalidArguments_AreExact` (which does not pin the corrected parameter
  name) and re-verified by the full project run.
