# Kotlin Collection Port — 2026-08-05

> **Current-state note (2026-08-05, later the same day).** A C++ port of the same seven collections
> shipped after this one, so coverage is now six languages. This review's scope and findings are
> unchanged and still describe the Kotlin shipment; the C++ port carries its own verification,
> recorded in [the C++ port review](cpp-collection-port-2026-08-05.md).

- Created (UTC): 2026-08-05
- Repository HEAD (reviewed): `experimental` branch, post Haskell port
- Audience: Maintainers reviewing the five-language shipment of the seven research-derived collections
- Scope: The Kotlin/JVM port of all seven collections plus the level-ancestor seam they share, the
  intentional divergences it makes, the duplication it removed, and the verification evidence

## Decision Recorded

The seven research-derived collections are now ported to Kotlin, making coverage **C#, Rust, C,
Haskell, and Kotlin**. This supersedes the coverage decision in the
[Rust collection port review](rust-collection-port-review-2026-08-04.md); that review, the
[three-language parity audit](three-language-parity-audit-2026-08-05.md), and the
[Haskell port review](haskell-collection-port-2026-08-05.md) all carry current-state notes pointing
here. The remaining four workspaces (C++, OCaml, Python, TypeScript) stay unported under the same
parity-economics rule.

## What Shipped

| Collection | C# namespace | Kotlin type |
| --- | --- | --- |
| `AncestralSliceQueue<T>` | `Durable7.FingerTree` | `durable7.fingertree.AncestralSliceQueue<T>` |
| `BilateralAncestralDeque<T>` | `Durable7.FingerTree` | `durable7.fingertree.BilateralAncestralDeque<T>` |
| `ContextualRankSequence<TElement, TMachine>` | `Durable7.FingerTree` | `durable7.fingertree.ContextualRankSequence<T>` |
| `PersistentDeltaMap<TKey, TValue>` | `Durable7.FingerTree` | `durable7.fingertree.PersistentDeltaMap<K, V>` |
| `PersistentRunDeltaVector<T>` | `Durable7.FingerTree` | `durable7.fingertree.PersistentRunDeltaVector<T>` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `Durable7.FingerTree` | `durable7.fingertree.PersistentMonotoneActionHeap<E, P, A>` |
| `PersistentAncestralConnectionForest` | `Durable7.Hamt` | `durable7.hamt.PersistentAncestralConnectionForest` |

`durable7.fingertree.IncrementalAncestorArena<T>` with `MyersIncrementalAncestorArena<T>` carries the
level-ancestor seam the first two share.

## Kotlin Is the Port That Needs the Fewest Concessions

Where the Haskell port had to dissolve the arena into immutable heap structure, the JVM supplies
everything the managed reference assumes — mutable state, stable integer handles, and a monitor — so
`IncrementalAncestorArena` is a **faithful** port rather than a reinterpretation. It keeps the arena
object, `synchronized` serialization of every operation including reads, the odd-block store with
square-boundary addressing and O(sqrt(M)) slack, and the saturating hop counters. Leaf addition is
O(1) amortized and an ancestor query follows O(log M) parent/jump links, exactly as in C#. The only
renaming is `GetDepth`/`GetParent`/`GetValue` becoming `depthOf`/`parentOf`/`valueAt`, because Kotlin
reserves `get`-prefixed names for property accessors.

Similarly, the connection forest's version tokens keep **reference identity** — the class
deliberately does not override `equals` — so sibling branches at equal depth stay distinct without
the structural-identity machinery Haskell needed.

## Intentional Divergences

**Fallible boundaries return `null`, not exceptions.** This is the workspace's own documented
convention (`RrbVector`, `Rope.cursorAt`), so C#'s `ArgumentOutOfRangeException` and
`InvalidOperationException` on indexed reads, endpoint reads, slices, splits, and removals all become
`null`. Genuine contract violations still throw, and checked `Int` growth via `Math.addExact` raises
`ArithmeticException` where C# raises `OverflowException`. Where a result could legitimately be a
stored `null`, a nullable-safe wrapper carries it, following the existing `RrbPop`/`RopeCursorPeek`
idiom — which is also why the delta map keeps a presence wrapper rather than a bare `V?`: Kotlin's
nullability does not solve the presence problem for a map whose value type is itself nullable, any
more than C#'s does.

**Policies are runtime objects.** C#'s static-abstract interfaces and `IEqualityComparer`/`IComparer`
parameters become retained policy instances, matching `MeasurePolicy`/`RangeUpdateAlgebra`/`Monoid`.
Operand compatibility for concatenation and melding is gated on the retained policy being the same
object or value-equal, following `RangeUpdateSequence.concat`'s precedent rather than C#'s
reference-identity-only check.

**One equality abstraction, not two.** The two checkpoint-differential structures initially arrived
with separate equality abstractions. They were consolidated into a single retained
`ValueEqualityPolicy<T>` over a `ValueEqualityComparer<in T>` in its own file, matching the Rust
port's single-`EqualityPolicy` shape and the standing repository preference against near-duplicate
machinery that the 2026-07-29 review established for the arena. Canonical policies are shared
singletons so independently obtained instances of one kind stay compatible; a `custom` policy carries
its own identity.

**The floating-point situation is genuinely different here, and better.** Kotlin's `==` on statically
typed `Double`/`Float` is the primitive IEEE comparison and is *not* reflexive on `NaN`. But the
natural policy is generic, so it boxes and reaches `java.lang.Double.equals`, which compares raw bits
and *is* reflexive on `NaN`. Kotlin therefore needs no equivalent of Rust's `Eq` bound excluding raw
floats — the natural policy is already an equivalence relation for float payloads. The residual
difference from .NET is signed zero: the boxed comparison separates `-0.0` from `+0.0` where
`EqualityComparer<double>.Default` does not, and `reflexiveIeeeDouble`/`reflexiveIeeeFloat` are the
canonical .NET-matching relations. The suites assert each of these behaviours at runtime rather than
trusting the documentation.

## Honest Cost Statements

`ContextualRankSequence` states two bounds weaker than the managed reference, both traceable to the
strict measured-AVL engine this workspace uses instead of a lazy digit spine — the same engine
difference that already makes `ReversibleDeque`'s endpoints O(log n):

- endpoint updates are **Θ(s log n), not O(s) amortized**, because there is no digit to absorb the
  work and no lazy spine to amortize against;
- concatenation is **Θ(s·(log m + |h − h′|)), that is O(s log(n + m)), not O(s log(min(n, m)))**,
  because `concatNodes` walks the right operand's left spine unconditionally before joining by
  height difference. The `log(min(n, m))` form fails in *both* directions here, where the Rust port's
  failed only for asymmetric operands.

`PersistentDeltaMap.minEntry`/`maxEntry` are **Θ(log N), not O(1)**, because the substrate caches
subtree sizes but no extremes — the same concession the Haskell port makes.

Against those, two bounds are *stronger* than a sibling port. The connection forest's CHAMP path
factor is **worst case, not merely expected**: the port pins a private injective vertex hash and this
workspace's CHAMP builds a collision node only on full 32-bit hash equality, so no bucket can hold
two distinct vertices and a path is at most seven nodes. Rust and Haskell must both document an
expected factor. And `ContextualRankSequence` caches a per-leaf effect table, so rank, select, and
indexed descent invoke the machine zero times — where C# and Rust each spend one transition on
select.

## Adversarial Parity Audit

Each of the eight units was audited member-by-member against its C# baseline by an independent
reader, and every reported finding was passed to a second reader instructed to refute it by default.
`AncestralSliceQueue`, `BilateralAncestralDeque`, and `PersistentAncestralConnectionForest` were
found faithful with nothing to report. Eight findings survived refutation across the rest; all are
fixed. **None was a correctness defect** — every one was either a claim the documentation overstated
or a test assertion that could not fail.

| Severity | Unit | Finding | Fix |
| --- | --- | --- | --- |
| Minor | `IncrementalAncestorArena` | The O(log M) query bound and all three hop counters were pinned by no assertion. A mutation neutering the jump-link coalescing would leave every answer correct, turn queries into linear parent walks, and still pass the whole suite | New `IncrementalAncestorArenaTests.kt` porting C#'s deep-chain hop-bound guard, with the bound stated so a coalescing-free arena fails it |
| Minor | `IncrementalAncestorArena` | The arena's own primitives and error contracts were pinned by nothing — every exercise reached it through the two collections — including the guarantee that a failed addition publishes no partial node | Covered in the same new test file |
| Trivial | `IncrementalAncestorArena` | `blockCount`/`allocatedSlotCount` were read by no test, and the assertion labelled as covering the odd-block layout was an arithmetic tautology over the test's own locals. It was transliterated verbatim from an equally vacuous C# line, so this is inherited, not introduced | Replaced with real assertions against `statistics()` |
| Minor | `PersistentDeltaMap` | Two structural-sharing assertions for the no-op rule compared a value with itself, since the no-op returns the receiver | Replaced with negative controls over two genuinely distinct instances |
| Minor | `PersistentRunDeltaVector` | Two root-sharing assertions called `rollback()` on an already-clean version, which returns the receiver | Replaced with discriminating checks; the positive property remains pinned per position |
| Low | `BilateralAncestralDeque` | Two `sharesStorageWith` assertions were reflexive | One now compares two separately constructed empty handles; the other checks that clearing a *non-empty* handle does not share |
| Low | `PersistentMonotoneActionHeap` | The private `skewLink` KDoc claimed it links three trees of ranks `r, r, r`; the `zero` parameter is always rank 0, and only the other two must agree | KDoc corrected, with the reason the result is `r + 1` |
| Trivial | `ContextualRankSequence` | `getRange`'s KDoc claimed unconditionally that a whole-sequence range returns the receiver, but the empty-range branch is tested first, so an empty receiver gets a fresh instance | KDoc scoped to a non-empty receiver |

Four of the eight were vacuous test assertions of the same shape — a sharing diagnostic applied to an
operation that returns its receiver. That pattern is worth watching for in any future port: the
assertion reads as though it proves structural sharing while in fact comparing an object with itself.

## Verification

- `src/Kotlin/build.ps1` across all three workspaces: **291 test cases pass, zero errors, zero
  warnings, zero uncaught exceptions**, with the Kotlin 2.4.0 compiler and a JDK 21 toolchain. 78 of
  those cases are new — 73 from the seven collections plus 5 covering the arena seam directly.
- The suites assert **exact** backend query counts rather than ceilings — an eight-entry indexing
  profile, all 45 slice pairs, a nine-entry split profile — so a regression that stayed under a
  documented ceiling would still fail.
- Counting policies are paired with live-counter positive controls, so a zero-callback assertion is
  evidence about the code rather than about an idle counter.
- The monotone action heap's cases for composition direction, minimum tie-breaking, and
  expose-before-attach ordering were each confirmed to fail against a deliberately mutated kernel;
  the arena's new hop-bound assertion was likewise confirmed to fail against a coalescing-free arena.
- KDoc coverage remains complete, which is this workspace's standing convention rather than a
  compiler-enforced gate.
