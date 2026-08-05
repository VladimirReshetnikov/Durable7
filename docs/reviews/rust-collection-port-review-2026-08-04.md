# Rust Collection Port Review — 2026-08-04

> **Current-state note (2026-08-05).** The language-coverage decision recorded below has since been
> superseded repeatedly: the seven collections were ported to C on 2026-08-04, and to Haskell and
> Kotlin on 2026-08-05. Coverage is now **C#, Rust, C, Haskell, and Kotlin**, with the remaining four
> workspaces still unported pending a named consumer. The Haskell port removes the level-ancestor arena entirely — a
> node is its own handle — so the "consolidated arena backend" divergence recorded here has no
> Haskell counterpart to consolidate. Everything else below still describes the Rust port as
> shipped.

- Created (UTC): 2026-08-04
- Repository HEAD (reviewed): `experimental` branch, post-promotion
- Audience: Maintainers reviewing the two-language shipment of the seven research-derived collections
- Scope: The C# namespace promotion, the Rust port of all seven collections, adversarial semantic-parity
  verification of that port against the C# baseline, and the fixes applied

## Decision Recorded

The seven collections previously segregated in `Durable7.FingerTree.Experimental` and
`Durable7.Hamt.Experimental` were promoted into the ordinary family namespaces and ported to Rust.
Namespace-qualified references in the
[2026-07-29 review](experimental-collections-review-2026-07-29.md) therefore describe a superseded
state; its finding **F1** (duplicated arena machinery) is resolved by this port.

Language coverage is deliberately **C# and Rust only**. The remaining seven workspaces are unported
pending a named consumer, per the derived catalog's parity-economics rule. Absence elsewhere is a
scheduling decision, not a gap.

## What Shipped

| Collection | C# namespace | Rust module |
| --- | --- | --- |
| `AncestralSliceQueue<T>` | `Durable7.FingerTree` | `ancestral_slice_queue` |
| `BilateralAncestralDeque<T>` | `Durable7.FingerTree` | `bilateral_ancestral_deque` |
| `ContextualRankSequence<TElement, TMachine>` | `Durable7.FingerTree` | `contextual_rank_sequence` |
| `PersistentDeltaMap<TKey, TValue>` | `Durable7.FingerTree` | `delta_map` |
| `PersistentRunDeltaVector<T>` | `Durable7.FingerTree` | `run_delta_vector` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `Durable7.FingerTree` | `monotone_action_heap` |
| `PersistentAncestralConnectionForest` | `Durable7.Hamt` | `ancestral_connection_forest` |

Two supporting Rust modules have no single C# counterpart: `equality` (a retained `EqualityPolicy<T>`,
the equality counterpart of the existing `OrderPolicy<T>`) and `incremental_ancestor` (one shared
level-ancestor arena replacing C#'s two duplicated copies).

## Intentional Divergences From The C# Baseline

Recorded here and in the local API notes, as the
[porting guide](../guides/porting-and-semantic-parity.md) requires. Semantic parity means the same
observable contract where the languages expose equivalent capabilities, not identical shapes.

1. **One consolidated ancestor arena.** C# ships `IIncrementalAncestorArena`/`MyersIncrementalAncestorArena`
   and `IIncrementalLevelAncestorArena`/`MyersLevelAncestorArena` as near-verbatim duplicates, one per
   consumer. Rust has a single `IncrementalAncestorArena<T>` trait and `MyersAncestorArena<T>`, merging
   the two interface contracts into the stricter of the two. Per-collection observable behavior is
   unchanged.
2. **`Option` replaces `DeltaMapValue<T>`.** The managed wrapper exists only because `null` is a valid
   present value in .NET; `Option` already draws that distinction.
3. **`Arc<T>` + `Arc::ptr_eq` replaces the run-delta vector's private `Cell` class.** Same
   reference identity, one fewer allocation layer.
4. **Retained `EqualityPolicy<T>` replaces `IEqualityComparer<TValue>` parameters.** The value comparer
   defines semantic no-ops and change cancellation, so it determines the observable change set and is
   retained on the value rather than taken as a `PartialEq` bound.
5. **Two complexity bounds are weaker in Rust, by substrate.** C#'s contextual rank sequence sits on a
   lazy finger tree; this crate's measured core is a height-balanced join tree with no finger. Endpoint
   updates are Θ(s log n) rather than O(s) amortized, and concatenation is Θ(s·|h_left − h_right|)
   rather than O(s log(min(n, m))). Same algorithm, weaker locality.
6. **The connection forest's CHAMP path cost is expected, not unconditional.** C# can assert a bounded
   path over the full key width; the Rust crate hashes integer keys through the retained `BuildHasher`
   truncated to 32 bits, so collision buckets can form.
7. **Change enumeration is eager.** C#'s `GetChanges()` is a lazy iterator with Θ(1) setup; the Rust
   `get_changes` materializes, so setup is Θ(k). Full consumption remains Θ(k + 1) in both. Documented
   at the method rather than silently claimed equal.

## Adversarial Verification

Each ported module was reviewed against its C# source and design proposal by an independent reviewer,
and every candidate finding was then independently refuted-or-confirmed by a second reviewer instructed
to default to refutation. Of 17 candidates, **12 were confirmed and 5 refuted**.

**No correctness defect was found in any of the seven ports.** Every confirmed finding was a
documentation-accuracy problem, one semantic-policy divergence, or a test gap. All twelve were fixed.

### Confirmed and fixed

| Severity | Kind | Module | Finding |
| --- | --- | --- | --- |
| High | parity | `run_delta_vector`, `delta_map` | The natural equality policy was bounded on `PartialEq`, so float payloads got a **non-reflexive** policy: rewriting an identical `NaN` marked the position dirty, where C#'s reflexive `EqualityComparer<T>.Default` treats it as a no-op. The phantom run then corrupted run counts, ranks, and dirty counts for every later operation, and `validate_structure` could not detect it because the index stayed internally consistent with a policy that simply was not an equivalence relation. |
| Medium | complexity | `ancestral_slice_queue` | `split_at` claimed a split at `0` makes no ancestor query; it always makes one on a non-empty queue, and cannot be specialized away because the anchored-empty rule requires the prefix to retain the node at `low_depth - 1`. |
| Medium | complexity | `contextual_rank_sequence` | Endpoint updates and concatenation inherited C# bounds the Rust substrate does not deliver (divergence 5 above). |
| Medium | complexity | `delta_map` | `min_entry`/`max_entry` documented O(1); they are Θ(log N) rank selects. |
| Medium | complexity | `ancestral_connection_forest` | Inherited C#'s unconditional bounded-CHAMP-path claim (divergence 6 above). |
| Low | complexity | `bilateral_ancestral_deque` | `validate_structure` documented as O(1); it issues up to four level-ancestor queries. |
| Low | parity | `delta_map` | Eager change enumeration documented as if lazy (divergence 7 above). |
| Low | usability | `ancestral_slice_queue` | `derive(Clone)` on result types imposed a needless `T: Clone` bound on `Arc`-backed fields. |
| Low | test gap | `incremental_ancestor` | `last_ancestor_hop_count` never asserted; hop envelope only pinned on a straight chain, never a branched tree. |
| Low | test gap | `monotone_action_heap` | No analogue of the C# throwing-policy failure-atomicity obligation; `from_entries` untested. |

The high-severity fix is the interesting one. Rather than patch the comparer, the natural-policy
constructors were tightened from `T: PartialEq` to `T: Eq`, so the type system now enforces the
equivalence-relation precondition both structures already documented — `f64: !Eq`, so float payloads
fail to compile against the natural policy instead of silently diverging. Canonical
`EqualityPolicy::reflexive_ieee()` constructors for `f32`/`f64` supply the reflexive behavior that
matches .NET's default comparer, and carry the same canonical identity flag as `natural()` so
independently constructed float policies stay mutually compatible.

### Refuted

Five candidates did not survive independent scrutiny, including a claimed unsoundness in
`AncestralSliceQueue`'s auto-derived `Send`/`Sync`. They are listed in the workflow record rather than
here, since acting on a refuted finding costs more than it saves.

## Validation

At the time of the port review: C# 1,240 tests and Rust 445 tests, both green. After the same-day
follow-up recorded above:

- C#: `src/CSharp/test.ps1` — 1,254 tests pass (367 Hamt + 807 FingerTree + 80 Ordered), zero failures.
- Rust: `src/Rust/test.ps1` — 452 tests pass across 30 binaries, zero failures. `cargo build
  --workspace` reports zero warnings and `cargo clippy --workspace --all-targets` reports no
  diagnostics attributable to the new modules.
- The port added 128 Rust tests across the nine new modules; the follow-up added 21 more across both
  languages (14 C#, 7 Rust).
- No benchmark was run; measurement remains postponed for an isolated session, per repository policy.

This evidence proves the ported semantics and the asserted structural invariants. It does not prove
the theoretical instantiations the design proposals describe — in particular, both ancestry-interval
sequences ship the Myers reference arena (O(1)-amortized addition, O(log M) queries) in every
language, and no Alstrup–Holm backend exists in any port.

## Follow-Up Landed (2026-08-04, same day)

Items 2 and 3 of the original remaining-work list were completed immediately after this review, in
both languages simultaneously so parity was never broken:

- **Run accept/revert by contained position** — C# `AcceptDirtyRunContaining`/`RevertDirtyRunContaining`,
  Rust `accept_dirty_run_containing`/`revert_dirty_run_containing`. A position outside the vector is
  an out-of-range failure; a clean position is a documented no-op returning the receiver.
- **Component size** — C# `GetComponentSize`, Rust `component_size`, both reading the count the
  union-by-size root already caches.
- **Range-restricted change enumeration** — C# `GetChanges(low, high)`, Rust `changes_in_range`,
  both seeking the change index's boundaries rather than filtering.
- **Bulk assignment** — C# `SetItems`, Rust `set_items`, both defined as the fold over single-entry
  assignment so every coalescing and cancellation rule holds verbatim, and both documented as a
  convenience rather than a better bound.
- **C# arena consolidation** — finding **F1** of the
  [2026-07-29 review](experimental-collections-review-2026-07-29.md) is now closed in C# as well.
  `IIncrementalLevelAncestorArena<T>`, `MyersLevelAncestorArena<T>`, and
  `MyersLevelAncestorStatistics` are gone; both collections share one
  `IIncrementalAncestorArena<T>`/`MyersIncrementalAncestorArena<T>` seam in
  `IncrementalAncestorArena.cs`, merging the two interface contracts into the stricter one exactly as
  the Rust port did. `BilateralAncestralDeque<T>.Create` now takes the shared interface. A new
  seam test drives BOTH collections through a custom non-Myers arena implementation over randomized
  histories, closing the review's "extension seam never exercised" gap.
- **Remaining C# code fixes** — uniform overflow reporting for the contextual sequence's endpoint
  operations, a documented note that an invalid machine state count surfaces wrapped in
  `TypeInitializationException`, one allocation removed from the action heap's insert path, and one
  redundant persistent-map operation removed from the run index's merge case.
- **Editorial** — prose describing these collections as experimental where the word denoted a
  namespace rather than research maturity has been promoted; the proposals remain the record of the
  scoped research claims.

## C Port Landed (2026-08-04)

All seven collections were subsequently ported to the **C** workspaces, making this a three-language
shipment. The C port starts from the *consolidated* arena seam rather than reproducing the
duplication C# originally shipped, so all three languages agree there from the first commit.

It follows the C workspace's established conventions: type-erased values addressed by size plus a
caller-owned type-identity tag, policies carrying copy/destroy/compare callbacks and an allocator,
`ft_status`/`d7_hamt_status` returns whose outputs are published only on success, reference-counted
handles with `_init`/`_copy`/`_move`/`_dispose`, borrow-versus-own accessor pairs where the managed
ports return a single value, and visitor traversal instead of iterators.

One substantive divergence, documented in the C API notes rather than papered over: C# and Rust
serialize the arena behind a lock or `Mutex`, but C11 has no portable mutex without `<threads.h>`,
so the C arena states the weaker contract — single-threaded unless the caller synchronizes, with
every operation including reads and statistics snapshots treated as a write. Handle reference counts
remain atomic. No port claims lock-free progress.

C adds an obligation the managed ports do not have, and the suites cover it explicitly: failure
atomicity under injected allocation failures and failing copy callbacks, verified to leave counters,
payload ownership, and outstanding allocations unchanged with the structure still usable.

**Validation.** The C FingerTree workspace reconfigured, rebuilt from clean, and passed 16/16 CTest
cases with zero warnings under `-Wall -Wextra -Wpedantic -Werror`; the Hamt forest module compiled
warning-free and passed its 16 test cases. The port added 92 C test cases across the eight new
modules. Both were verified with CMake 4.3.2 and GCC 16.1.0 because MSVC was unavailable on the
porting machine — an MSVC run remains the canonical gate for this workspace.

## Remaining Work

1. The other six language workspaces are unported by decision, not oversight. Revisit only with a
   named consumer.
2. The C evidence above is a GCC run. Re-validate under MSVC, which is the C workspaces' canonical
   toolchain and enforces `/W4 /WX` plus `/permissive-`, before treating the C port as gated.
