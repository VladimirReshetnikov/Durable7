# Haskell Collection Port — 2026-08-05

> **Current-state note (2026-08-05, later the same day).** A Kotlin port of the same seven
> collections shipped after this one, so coverage is now five languages. This review's scope and
> findings are unchanged and still describe the Haskell shipment; the Kotlin port carries its own
> verification, recorded in [the Kotlin port review](kotlin-collection-port-2026-08-05.md).

- Created (UTC): 2026-08-05
- Repository HEAD (reviewed): `experimental` branch, post C-port (`a001dea`)
- Audience: Maintainers reviewing the four-language shipment of the seven research-derived collections
- Scope: The Haskell port of all seven collections plus the level-ancestor seam they share, the
  intentional divergences it makes, and the verification evidence

## Decision Recorded

The seven research-derived collections are now ported to Haskell, making coverage **C#, Rust, C, and
Haskell**. This supersedes the language-coverage decision in the
[Rust collection port review](rust-collection-port-review-2026-08-04.md), which recorded C# and Rust
only and deferred the rest pending a named consumer; that review and the
[three-language parity audit](three-language-parity-audit-2026-08-05.md) carry current-state notes
pointing here. The remaining five workspaces (C++, Kotlin, OCaml, Python, TypeScript) stay unported
under the same parity-economics rule, so absence there is still a scheduling decision rather than a
gap.

## What Shipped

| Collection | C# namespace | Haskell module |
| --- | --- | --- |
| `AncestralSliceQueue<T>` | `Durable7.FingerTree` | `Durable7.FingerTree.AncestralSliceQueue` |
| `BilateralAncestralDeque<T>` | `Durable7.FingerTree` | `Durable7.FingerTree.BilateralAncestralDeque` |
| `ContextualRankSequence<TElement, TMachine>` | `Durable7.FingerTree` | `Durable7.FingerTree.ContextualRankSequence` |
| `PersistentDeltaMap<TKey, TValue>` | `Durable7.FingerTree` | `Durable7.FingerTree.PersistentDeltaMap` |
| `PersistentRunDeltaVector<T>` | `Durable7.FingerTree` | `Durable7.FingerTree.PersistentRunDeltaVector` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `Durable7.FingerTree` | `Durable7.FingerTree.PersistentMonotoneActionHeap` |
| `PersistentAncestralConnectionForest` | `Durable7.Hamt` | `Durable7.Hamt.PersistentAncestralConnectionForest` |

`Durable7.FingerTree.IncrementalAncestor` carries the level-ancestor seam the first two share. Each
collection is reachable only through its own module, not the family umbrella: several of them export
a `ValidationStatistics` type, so umbrella re-export would be ambiguous. That matches how
`RangeUpdateSequence`, the previous addition, is already exposed.

## The Headline Divergence: The Arena Disappears

The managed and native ports own a mutable, lock-serialized, append-only arena of integer handles,
injected through a `Create(arena)` factory and observed through an arena statistics snapshot. In
Haskell a `Node a` **is** its own handle and the forest is ordinary immutable heap structure. The
consequences are all simplifications:

- no arena object, no lock, and no lock-free-progress question;
- no handle-recycling or cross-arena-handle hazard — presenting a node to the wrong forest is
  unrepresentable rather than merely undefined;
- the `Create(arena)` backend-injection seam and the `Create`/`CreateMyers` factory split collapse
  into a single `empty`;
- leaf addition improves from **O(1) amortized to O(1) worst case**, because there is no odd-block
  store to grow. Ancestor queries still follow the Myers two-link scheme in O(log M) hops.

The one capability genuinely lost is the arena's retained query counters, which a pure forest has
nowhere to keep. `ancestorAtDepthHops` returns a query's hop count to the caller that caused it, and
`BilateralAncestralDeque` layers `QueryCost`-returning `indexCost`/`sliceCost`/`splitAtCost`/
`removeFirstCost`/`removeLastCost` siblings on that, defined as projections of the same cores so no
logic is duplicated. Without them the at-most-two-query ceiling would be undocumentable and
untestable rather than merely uninstrumented.

## Other Intentional Divergences

**Policies are retained records of functions**, never typeclass constraints — the same choice the
Rust port makes, for the same reason: the value relation decides which writes are semantic no-ops
and when a recorded change cancels, so it must be remembered. The cost is that a record of functions
has no identity, so operand compatibility for `append`/`meld` is a documented caller obligation
rather than a detected error. One exception is kept because it is observable and destructive: the
contextual rank sequence rejects a declared state-count mismatch, since a wrong width would index an
effect table out of range and silently corrupt summaries.

**`Maybe` replaces both exceptions and the presence-safe wrapper.** `ArgumentOutOfRangeException`
and `InvalidOperationException` become `Nothing`; `DeltaMapValue<T>`, which C# needs only because
`null` is a valid present value, becomes `Maybe` as it did in Rust. `error` with a module-qualified
message is reserved for unreachable invariant violations and overflow, matching `BrodalOkasakiHeap`.

**Reference identity is reconstructed where an invariant depends on it.** The run-delta vector's
"a clean position reuses its exact checkpoint cell" rule needs object identity, which pure Haskell
does not expose. An internal cell carries a globally unique token drawn from an atomic counter
through the `unsafePerformIO`/`NOINLINE` device `Data.Unique` uses. That makes the identity test
available to the *pure* `validateStructure` and turns `RrbVector.setAt`'s `Eq` no-op check into
exactly the identity short circuit `Arc::ptr_eq` provides in Rust; `StableName` diagnostics confirm
the same fact independently in the tests.

**Connection-forest versions have structural rather than referential identity**, recording their
link endpoints so sibling branches at equal depth stay distinct. The visible consequence — two
versions built by genuinely identical operations from one parent are a single value — is asserted
explicitly by the test suite rather than left implicit.

**`Integer` where the baseline checks a fixed width.** Contextual event counts and forest history
depths are unbounded, so those overflow contracts are vacuous; the baseline's *negative* event-count
rejection is kept, and element counts remain checked `Int`.

## Honest Cost Statements

Three bounds are weaker than the baseline because the Haskell substrate is, and each is stated at the
function rather than buried:

- `ContextualRankSequence.toList` is **Θ(s·n), not O(n)** — the measured core's left-view fold
  rebuilds a spine node per step, and each rebuild forces a fresh O(s) effect-table composition,
  where the reference's enumerator walks a traversal stack and combines no measures. This one was
  found by the parity audit below rather than by the port, which is why the audit exists.
- `PersistentDeltaMap.minEntry`/`maxEntry` are **Θ(log N), not O(1)** — `SortedMap` caches no
  extremes, so reaching the leftmost entry is a full descent.
- The connection forest's CHAMP path factor is **expected, not worst-case** — `Hashable` mixes to an
  `Int` that `HashMap` truncates to 32 bits, so the trie is unconditionally at most seven levels but
  two vertices colliding in those bits share a list-scanned bucket. The Rust port reaches the same
  conclusion; the difference worth recording is that this mixer is deterministic, so a colliding
  vertex set is a property of the universe rather than of the run. The union-by-size parent-path
  factor stays worst-case O(log n).

`PersistentDeltaMap.changesInRange` deserves a specific note because the C port had a real defect
here. It shipped as a `ceilingEntry` seek plus a bounded `higherEntry` walk — genuinely independent
of how many changes fall outside the window, not a filter over all changes — but with an
O(log(k + 1)) per-element term rather than the baseline's Θ(1), because the shared `SortedMap`
exposed no key-range restriction. That gap has since been closed: `SortedMap.keyRange` now provides
an ordered inclusive restriction from two antitone boundary descents, and the delta map's range
walk is a restriction plus a lazy ascending traversal, hitting the baseline's O(log(k + 1)) plus
Θ(1)-per-element bound. A measured 4-element window over a 2,048-record change index fell from 61
key comparisons to 22, and the suite's budget assertion was tightened from 512 to 40 so it
discriminates between the two implementations rather than merely ruling out a full scan. The same
change replaced `SortedMap.slice`, which had been a Θ(n) `toList`/`fromList` round trip, with
O(log n) structural take/drop.

Against those, one bound is **better** than a sibling port: the contextual rank sequence keeps the
reference's O(s) amortized endpoint updates and O(s log(min(n, m))) concatenation, which the Rust
port must weaken, because the Haskell substrate is a genuine Hinze–Paterson finger tree with digits
rather than a height-balanced join tree.

## Adversarial Parity Audit

Each of the seven ports was then audited member-by-member against its C# baseline by an independent
reader, and every reported finding was passed to a second reader instructed to refute it by default.
Seven candidate findings were raised; six survived refutation and are fixed here, and one was
refuted. `BilateralAncestralDeque` and `PersistentDeltaMap` were found faithful with nothing to
report.

| Severity | Collection | Finding | Fix |
| --- | --- | --- | --- |
| Serious | `ContextualRankSequence` | The haddock claimed enumeration is O(n), but the substrate delivers Θ(s·n): `Measured.toList` is a left-view fold, and every step rebuilds a spine node whose strict measure field forces a fresh O(s) effect-table composition. The C# reference genuinely delivers O(n) because its enumerator walks a traversal stack and combines no measures | Both claim sites corrected to Θ(s·n), with the reason and the contrast to the reference stated |
| Low | `PersistentAncestralConnectionForest` | `collectLineage`'s accumulator was unforced, so the "accumulator-tail-recursive" property the haddock claims was not delivered — the walk deferred H nested `Map.insert` applications whose forcing is H-deep on the stack, at exactly the scale the haddock names | Accumulators forced in `collectLineage` and `walkAllPaths` |
| Low | `PersistentAncestralConnectionForest` | The `validateStructure` bound counted one lineage-membership lookup per stored cell, but it runs once per parent-path *step*, costing an extra log n factor | Bound restated as O(H log H + m log n (w + log H)) with the reason |
| Minor | `PersistentRunDeltaVector` | Three assertions labelled as root-canonicalization proofs were vacuous self-comparisons: each compared a clean version against `checkpoint`/`rollback` of itself, and both return the receiver when clean | Two now compare against an external clean witness; the third, where accepting folds the current root into the checkpoint and leaves no such witness, asserts the discriminating per-position consequence instead |
| Trivial | `AncestralSliceQueue` | The haddock stated flatly that a split at 0 is not query-free, contradicting its own adjacent sentence when the queue is empty | Both sentences scoped to a non-empty receiver, matching the Rust wording |
| Trivial | `PersistentMonotoneActionHeap` | The `meld` note claimed the result adopts the left operand's policy, but the empty-operand short-circuits return the other operand verbatim | Reworded to scope the claim and note that the surviving policy depends on emptiness |

The refuted finding claimed the run-delta vector's `StableName` witnesses do not independently
confirm the identity-token discipline; on inspection they do.

Only one finding was a behavioural defect (the unforced accumulator); the rest were claims the code
did not deliver or tests that could not fail. That the serious one was a complexity claim is
consistent with the repository's standing rule that a port must never inherit a bound its substrate
cannot deliver — the rule exists because this is the failure mode that survives testing.

## Verification

- `cabal test all --jobs=1` from `src/Haskell`: all three suites pass — `ft-test`, `hamt-test`, and
  `ordered-test` — with **zero compiler warnings** across the workspace under `-Wall -Wcompat`.
- Every new module and test module additionally type-checks under `-XHaskell2010`, which is what the
  cabal files actually select. This caught two defects that a bare `ghc` check missed, because bare
  `ghc` defaults to GHC2021: a missing `FlexibleContexts` in the contextual rank sequence and a
  missing `NumericUnderscores` in the heap tests.
- The suites pin the load-bearing rules rather than asserting shape: exact ancestor-query profiles
  rather than ceilings alone, comparison-free run splices under a counting policy with a positive
  control, transition budgets proving the contextual sequence neither rescans nor defers, callback
  budgets proving change enumeration costs no policy calls, and randomized retained-branch models
  against reference models for every collection.
- The monotone action heap's suite was additionally mutation-tested: flipping either composition
  direction, skipping any of the three expose sites, reversing minimum tie-breaking, or making the
  insert root tie strict are each caught by a named case. One surviving mutant is behaviour-
  preserving by construction — `splitForest`'s rank-1 arms differ only in internal shape, which the
  API's unspecified enumeration order does not expose — and the C# and Rust suites share that
  property.
- A `-O2` hazard was found and fixed during the port and is worth carrying forward: a
  `sharesRootWith` that `StableName`-compares a small all-strict facade record fails under `-O2`,
  because GHC's CPR analysis unboxes and rebuilds the record around unchanged children. Compare an
  inner node instead, as `RangeUpdateSequence` does.
