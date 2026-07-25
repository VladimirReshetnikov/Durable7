# C# and Rust Implementation Review — 2026-07-09

- Created (UTC): 2026-07-09T00:00:00Z
- Repository HEAD: 2c7b1f21c260d9bd0962e7a017f05b9a8aa918c3
- Audience: Maintainers of the C# reference libraries and the Rust ports
- Scope: Correctness, semantic parity, API quality, and test coverage of the C# and Rust workspaces; fixes applied; deferred follow-ups

> **Current-state note (verified 2026-07-12): all seven deferred groups are resolved.** The
> “remaining” and “deferred” passages below describe reviewed HEAD `2c7b1f2`. The
> [resolution addendum](#resolution-addendum--2026-07-10) records the shipped Rust FingerTree
> completion (`ac16eed`), C# Numerics contract work (`44a1b6a`, `20eb680`, `b8c580c`), and coverage
> closure (`3303967`, `129522c`). Current implementations are indexed by the
> [Rust FingerTree README](../../src/Rust/FingerTree/README.md) and the
> [C# Numerics overview](../../src/CSharp/docs/Numerics/overview.md); none of the historical
> follow-up list is still pending.

## Summary

A full-depth review of the C# workspaces (Numerics, HAMT, FingerTree, Tungsten — the semantic
references) and all three Rust crates (Hamt, FingerTree, Tungsten), performed with six parallel
review passes (one per library area) followed by manual verification of every finding against the
actual code before any change was made. Baseline test suites were green before the review
(Rust workspace: 61 tests; C# solution: 720 tests) and green after every commit.

Two **critical correctness bugs** were found and fixed:

1. **Rust `MeasuredRope::split_by_measure` corrupted the right half** — the boundary chunk's
   elements appeared twice (`[2,4,8]` split at `sum > 5` produced right `[4,8,2,4,8]`).
2. **C# `SparseInteger.operator +` silently dropped accumulated bits** during carry cascades —
   `(SparseInteger)7 + (2^64 + 1)` returned `2^64` instead of `2^64 + 8`; directed fuzzing
   produced ~170 wrong sums per 200,000 additions, and multiplication inherited the corruption.

Both had test-suite gaps that let them survive: the rope test asserted only the left half's
length plus the right half's *internal* consistency (which the duplicated tree satisfied), and
the SparseInteger suite was five example-based facts with no randomized model tests. Both gaps
are now closed.

Beyond the two critical bugs, the review found and fixed a set of medium-severity parity
divergences between the Rust ports and their C# references, several C# contract bugs
(misvalidated arguments, wrong exception metadata, doc claims contradicting behavior), and a
collection of API-quality gaps in the Rust crates. No high-severity bugs were found in the
C# HAMT, C# FingerTree engine cores (including the lazy-middle concurrency protocol), C#
Tungsten, Rust HAMT trie core, Rust FingerTree balanced-tree core, or Rust Tungsten ordering
logic — each of those was traced in detail and is listed under "Verified sound" below.

## Fix commits

| Commit | Area | Content |
| --- | --- | --- |
| `6a9970f` | Rust Hamt | Set-difference structural sharing, per-operation trait bounds, missing trait impls |
| `11c6f9c` | Rust FingerTree | `split_by_measure` corruption, sorted-map key replacement, interval-tree insert order and lookup complexity, O(1) `peek_priority`, exact iterator `size_hint` |
| `ffe26c0` | Rust Tungsten | Stamp-midpoint parity, invariant `expect`, unbounded split `Clone` |
| `5caa6ab` | C# Numerics | `SparseInteger` addition carry bug, hex leading-zero parsing (all six wide types) |
| `2c7b1f2` | C# collections | `RopeText.OffsetOf` line validation, `SortedSet.Merge` canonicalization, exception `ParamName` fixes, HAMT dead branch, Tungsten spec/doc contract fixes |

## Methodology

- Six parallel review passes, each tracing one library area line-by-line against its
  counterpart: Rust Hamt vs C# HAMT; Rust FingerTree core (deque/measured) vs the C# engine
  cores; Rust FingerTree facades (sorted, priority queue, interval tree, ropes) vs the C#
  facades; Rust Tungsten vs C# Tungsten plus the kernel-verified ordering spec; C# Numerics
  standalone; C# FingerTree/HAMT/Tungsten standalone (as reference implementations).
- The Numerics pass executed the actual library against `BigInteger` oracles: 3,000-iteration
  fuzz across all six wide types for `+ - * / % << >>`, comparisons, and decimal/hex
  round-trips (zero arithmetic errors found in the wide types); 200,000-iteration directed
  fuzz for `SparseInteger` (which exposed the addition bug); 20,000-iteration checked-operator
  fuzz.
- The Rust FingerTree core pass ran ~10k randomized model-checked operations in a scratch
  harness with the modules' own private `validate()` invariant checkers executed after every
  step, plus adversarial concat-height, reverse-heavy, and sorted-bound fuzz.
- Every reported finding was re-verified against the source before fixing; agent findings that
  did not reproduce were discarded.
- BCL parity questions (signed hex parsing with redundant leading zeros) were settled by
  probing the actual .NET 10 runtime, not from memory.

## Findings fixed

### Critical

**F-1. Rust `MeasuredRope::split_by_measure` duplicated the boundary chunk in the right half**
(`src/Rust/FingerTree/src/rope.rs`). The method split the chunk tree with `split_at_index`
(whose right side still contains the boundary chunk) and then *also* prepended the boundary
chunk's suffix, so the right half contained `chunk[offset..] ++ chunk ++ rest`. Every
non-trivial call returned a right half with corrupted length, measure, and content
(`left.len() + right.len() ≠ rope.len()`). The crate's own 8192-element test passed because it
asserted only `left.len()` and the right half's internal consistency. Fixed by routing through
`try_split_find` (which excludes the boundary chunk from both sides) exactly like
`split_at_count`; new tests assert the two halves partition the rope across chunk shapes and
boundary positions.

**F-2. C# `SparseInteger.operator +` dropped the accumulated partial sum on carry**
(`src/CSharp/src/Durable7.Numerics/SparseInteger.cs`). The carry branch read the private
`positions` **field** of the intermediate sum `new SparseInteger(xPositionsNew) + x1.Exp2()`;
whenever that sum fits into `ulong` its `positions` field is `null`, so every bit accumulated
so far was silently discarded. Runtime-verified failures: `7 + (2^64+1) = 2^64` (expected
`2^64+8`); ~170 failures per 200,000 fuzzed additions over operands mixing dense low bits with
scattered high bits; `operator *` (built on `+`) corrupted accordingly. Fixed by materializing
through the `Positions` **property**, which computes the position array for small values. The
swapped branch comments in `PlusOne` were also corrected. New tests: directed carry-boundary
examples plus a 2,000-iteration randomized `BigInteger`-model fuzz for `+`, `*`, and
`CompareTo` biased toward exactly the carry-heavy operand shape that exposed the bug.

### Medium — Rust/C# parity divergences (Rust aligned to C#)

**F-3. Rust `PersistentHashSet::except` / `symmetric_except` rebuilt the whole set.** C#
`Except` folds `Remove` over the probe (O(m) removals; untouched subtrees stay shared; an
empty probe returns the receiver's root) and `SymmetricExcept` toggles membership on the
receiver. The Rust versions materialized a probe and rebuilt the result from scratch —
O(n + m), no structural sharing, contradicting the documented root-preservation contract.
Both now mirror C#, with sharing asserted by tests (`shares_root_with`).

**F-4. Rust `SortedMap::set_item` kept the previously stored key instance;**
C# `SortedDictionary.SetItem` stores the supplied key. Observable whenever the ordering
compares only part of the key. Now matches C#, regression-tested with a
comparer-equal-but-distinct key type. (Note the deliberate asymmetry, same in both languages:
the *association's* `SetItem` keeps the stored key, the *sorted map's* stores the supplied
one.)

**F-5. Rust `IntervalTree::insert` placed new equal-low intervals after existing ones**
(lexicographic `(low, high)` upper bound); C# splits at the first interval with
`low >= new.low`, so a new interval precedes existing equal-low ones. This changed enumeration
order, the overlap representative among ties, and which duplicate `remove` deletes. Now
matches C#, with a tie-order regression test mirroring the C# enumeration.

**F-6. Rust `IntervalTree::contains`/`remove`/`try_remove` were unconditional O(n) scans;**
the C# contract is O(log n) plus a scan over the equal-low run. Now implemented as a
low-endpoint binary search plus equal-low-run scan (same tie semantics: first match in
association order).

**F-7. Rust Tungsten stamp midpoint rounded toward zero** (`(left + right) / 2` in `i128`)
where C# computes the floor (`left + (gap >> 1)`). Ordering was unaffected, but stamp
sequences could diverge under mirrored histories, shifting relabel timing across ports. Now
floor-midpoint, matching C#.

**F-8. C# `RopeText.OffsetOf` accepted columns past the line end,** silently returning an
offset on a *different line* — contradicting its own parameter documentation ("column within
the line") and exception message, and diverging from the Rust port, which validates against
the line end. The C# XML docs describe the intended contract, so C# was fixed to match its
docs (and the Rust port): a column equal to the line length addresses the terminating newline
(end of rope for the last line); anything larger throws. Regression-tested.

### Low — C# contract and consistency fixes

- **`SortedSet<T>.Merge`** bypassed empty-singleton canonicalization (`new(...)` instead of
  `Wrap`); empty default-comparer results from `Intersect`/`Except`/`SymmetricExcept` now
  return the canonical `Empty`.
- **Wrong `ArgumentOutOfRangeException.ParamName`:** `Rope`/`MeasuredRope.CheckRange` blamed
  `index` for count failures; `MeasuredRope.PrefixMeasure(count)` blamed `index`;
  `PersistentList.TakeLast/Drop/DropLast` and `PersistentAssociation.Drop` validated after
  transforming arguments (`Drop(-1)` reported `index = -1`). All now report the offending
  parameter; exception *types* were always correct.
- **HAMT `CollisionNode.Create`** contained an unreachable `left is CollisionNode` branch
  that would have appended without a duplicate-key scan had it ever become live; replaced with
  the leaf-only precondition (`Debug.Assert`) and an explanatory comment for porters.
- **Rust Tungsten:** `Association::insert` masked a hypothetical entries/index desync as
  `None` via `?` where sibling sites `expect` the invariant; `PersistentListSplit` derived
  `Clone` (spurious `T: Clone` bound) — both fixed.

### Documentation contract fixes (port-parity ambiguities)

These matter because ports read the C# docs as the specification:

- **Tungsten `api-specification.md`:** the shared "no-op identity" bullet claimed
  `SetItem` with a default-equal value returns the receiver for *both* types; the list's
  `SetItem` always produces a new instance (the deque replaces unconditionally). The bullet is
  now scoped to the association, notes that `Insert` of an existing key never returns the
  receiver, and rule 2 documents the `Append`/`Prepend` terminal no-op fast path that retains
  the *stored* key instance (previously the rule promised the supplied instance
  unconditionally — two ports reading it differently would diverge observably). The XML docs
  were aligned and a test now locks the fast-path behavior under a case-insensitive comparer.
- **Facade complexity claims:** `Count`/`Measure`/`TryPeekPriority` on `Rope`, `MeasuredRope`,
  `PriorityQueue`, `IntervalTree`, `SortedBag`, `SortedSet`, and `SortedDictionary` were
  documented plain O(1) but delegate to `FingerTree.Measure`, which is O(1) *amortized* (the
  first read of a fresh spine forces memoized deferred work). Docs now say so; the deque's
  `Count` remains worst-case O(1).

### C# Numerics parity fix

**Hex parsing rejected redundant leading zeros** once the raw digit count exceeded the type's
budget (`UInt256.Parse("0"×64 + "FF", HexNumber)` threw `OverflowException`), in all six wide
types. The BCL trims leading zeros first — verified against .NET 10 for signed types too
(`Int128.Parse("0" + "F"×32, HexNumber)` is −1, no overflow). All six `TryParseHex`
implementations now trim leading zeros before the digit-budget check; a theory covers all six
types including full-width two's-complement patterns.

### Rust API-quality improvements (HAMT crate)

- **Per-operation trait bounds.** Every method — including pure reads — previously required
  `K: Eq + Hash + Clone`, `V: Clone + PartialEq`, so a map with a non-`Clone`, non-`PartialEq`
  value type (e.g. a boxed closure) could not even be constructed, while the C# reference
  imposes no constraints. Bounds are now per-operation: none for construction, length,
  iteration, or sharing probes; `Eq + Hash + BuildHasher` for lookups; `Clone` added only for
  removal; `V: PartialEq` only for the insert family's no-op value check.
- **Missing trait impls added:** `Debug`, content-based `PartialEq`/`Eq` (documented as an
  intentional Rust-idiomatic difference — C# uses reference equality), and `Index<&K>` on the
  map; `Debug`, `PartialEq`/`Eq`, `hasher()`, and a named `SetIter` with `IntoIterator` on the
  set (previously `for x in &set` did not compile); `Clone` + `FusedIterator` on both
  iterators; `Display` + `std::error::Error` on `DuplicateKey` (previously unusable with `?`
  into `Box<dyn Error>`); `Default`/`FromIterator` generalized from `RandomState`-only to any
  `S: Default` hasher policy. Manual `Clone` impls replace derives so cloning never demands
  `K: Clone`/`V: Clone`.
- **Set `try_remove`** made single-pass via a new `try_remove_entry` on the map (one hash and
  one trie walk instead of two); `PriorityQueue::peek_priority` now returns the cached tree
  measure in O(1) (matching C# `TryPeekPriority`) instead of a locate-plus-get; deque and
  measured-tree borrowing iterators now report exact `size_hint` from the cached root length
  and implement `ExactSizeIterator` (deque's was `(0, usize::MAX)`).

## Verified sound (traced in detail; no defects found)

- **C# HAMT:** popcount slot indexing, hash-fragment shift depth (branches only at shifts
  0..30), collision-bucket add/replace/remove with 2→1 leaf demotion, removal canonicalization
  (single-child collapse rules), comparer preservation across every mutation path, checked
  count arithmetic, the 7-frame inline enumerator stack bound, and copied-enumerator
  independence.
- **C# FingerTree engine cores:** deque digit overflow/underflow rebalancing against the
  simplified 2-3 finger-tree paper (size identities verified), `ToNodes`/`Nodes` chunker carry
  bounds, Hinze–Paterson split accumulation by monoid associativity, interval-tree overlap
  decision completeness, priority-queue FIFO stability at equal minima, text-rope ill-formed
  UTF-16 handling versus `string.EnumerateRunes`, and reversible-tree orientation composition
  in all four cases.
- **C# FingerTree concurrency:** the lazy-middle publication protocol (`Volatile.Read` +
  `Interlocked.CompareExchange`, losers adopt the winner), the boxed-measure CAS memoization,
  single-field readonly-struct wrappers (no torn multi-field publication anywhere in the
  persistent graph), and the enumerator stack-resize hazard ordering. The "benign race" claim
  in the docs is accurate.
- **C# Tungsten:** all eight kernel-verified ordering rules including rule 5's pre-removal
  index interpretation, stamp arithmetic (`unchecked (ulong)(right - left)` midpoint is exact
  across the full `long` range; relabel triggers exactly on `gap < 2`), `Relabeled`/`Rebuilt`
  slot assignment, `GetRange`'s dual-branch index reconciliation, and `Sort`/`KeySort`
  stability via unique-stamp tiebreaks.
- **Rust HAMT core:** bitmap math, collision promotion/demotion order, removal
  canonicalization byte-for-byte with C# `Rebuild`, count tracking, DFS iteration order,
  32-bit hash truncation consistency, and `Send`/`Sync` auto-derivation soundness.
- **Rust FingerTree core (deque.rs, measured.rs):** no correctness bug found under ~10k
  randomized model-checked operations with per-step invariant validation, including
  reverse-heavy mixed-orientation fuzz and `usize::MAX` range-arithmetic probes. The AVL-join
  balancing, concat, predicate splits, measure caching, and `Reversed`-node iteration are
  correct.
- **Rust Tungsten:** all eight ordering rules, stamp overflow guards, relabel path, slicing
  reconciliation, and key-instance retention — traced end-to-end and matching C#.
- **C# Numerics wide types:** zero errors in the 3,000-iteration `BigInteger` fuzz across all
  six types; carry/borrow, checked-operator sign logic, schoolbook multiplication with signed
  `BigMul` high-half correction, restoring division, shift/rotate masking, two's-complement
  `BigInteger` interop, checked narrowing conversions, and declaration parity across
  256/512/1024 (independent normalize-and-diff found no width skew).

## C# / Rust parity status

Beyond the divergences fixed above, the remaining known gaps, by family:

**HAMT — at parity.** All C# public operations have Rust equivalents with matching semantics
(`Result`/`Option` instead of exceptions, per the crate convention documented in
`api-notes.md`). Rust additionally returns the stored element from `try_remove` and exposes
`try_remove_entry` (supersets).

**FingerTree — functional parity for the deque and sorted facades; documented complexity
checkpoint elsewhere.** The Rust crate deliberately implements both cores as balanced binary
trees rather than lazy-spine 2-3 finger trees; `docs/api-notes.md` and the crate README record
that endpoint operations are O(log n) rather than O(1) amortized and that the C# allocation
profile is not claimed. Confirmed instances of that documented boundary (not bugs): push/pop
via concat/split, two-pass `try_split_find`/`try_locate`, and O(Δheight) concat. Remaining
*undocumented* gaps, deferred (see follow-ups): missing sorted-deque split/insert operations,
`ReversibleDeque` results degrading to the plain deque type, no `ReversibleDeque` iteration,
missing `MeasuredRope` positional editing (`insert`/`remove_at`/`slice`/builder), no
`RopeTextExtras` counterpart (code-point/grapheme addressing, CRLF-aware helpers), and
`bound_index`'s O(log² n) node visits versus C#'s cached rightmost-leaf signposts.

**Tungsten — at parity.** Full member-by-member verification found matching semantics for
every list and association operation, including the relabel path. Remaining conventions, now
either documented or inherent: no-op paths return `self.clone()` rather than a
reference-identical instance (the Rust structs are not `Arc`-wrapped; `shares_storage_with`
probes exist at the substrate level), `keys()`/`values()` are eager `Vec`s, and `add_range`
lacks the C# persistent-list `Join` fast path (callers can use `join` directly).

**Numerics — no Rust port exists.** The C# Numerics library has no counterpart in any other
workspace; this is the largest cross-language surface gap in the repository.

## Deferred follow-ups

Ranked; none are correctness bugs in shipped behavior.

1. **Rust FingerTree facade completions** (functional parity): sorted-deque
   `insert_sorted`/`remove_all_sorted`/`split_at_sorted_*`; reversible-typed results and
   iteration for `ReversibleDeque`; `MeasuredRope` positional editing and builder;
   `RopeTextExtras` equivalents. Scope: one Rust crate, ~6 API clusters.
2. **C# Numerics format/parse surface** (documented "mirrors built-in primitives" but
   diverges): precision specifiers (`X8`, `D5`, `N0`) throw; `NumberStyles.Number`/
   `AllowThousands` rejected; no `IParsable`/`ISpanParsable`/`IBinaryInteger`/`INumber`/
   `IMinMaxValue` implementations; no `float`/`double`/`decimal` conversions; cross-width
   conversion gaps (512↔128, 1024↔256/128). Decide whether to implement generic-math
   interfaces or document their absence; note that `GetShortestBitLength`/`GetByteCount`
   currently use non-BCL conventions and the `UInt256.GetShortestBitLength` XML doc's claim of
   "integral interface conventions" is wrong — aligning *before* interfaces are added avoids a
   silent breaking change later.
3. **Unchecked `IntN.MinValue / -1` wraps** while BCL `Int128` throws `OverflowException` even
   unchecked; behavior is test-locked and consistent across widths but contradicts the stated
   Int128-alignment contract — either throw or amend the docs.
4. **`UIntN.Log2(0)` throws** where BCL `UInt128.Log2(0)` returns 0; signed types expose no
   `Log2`.
5. **Rust FingerTree `bound_index` signpost caching** (O(log² n) → O(log n) node visits) —
   the deque is described as past the semantic checkpoint, so this is worth closing.
6. **Test-coverage gaps worth closing** (from the review passes): comparer-equal-but-distinct
   keys across the C# sorted facades and interval tree; interval-tree duplicate-low stress;
   middle `GetRange` property coverage and `KeyTake`/`Sort` property tests for the Tungsten
   association; `Rope.Split` sub-`MinChunkSize` tolerance and `Concat` re-coalescing;
   `RopeText.AsTextReader` `Peek`/`Read` interleaving; HAMT set-wrapper enumerator states.
7. **Minor:** `MeasuredChunkBuilder.Add(span)` degrades to per-element adds (bulk-copy like
   the unmeasured builder); `SortedDictionary.Builder.Keys/Values` capture the fail-fast
   version at property access rather than enumeration start (document or align);
   `Digit.ChildAt` clamps out-of-range positions in Release (note for porters);
   empty-tree `TryGetChild` throws different exception types in the two C# engine cores.

## Test evidence

- `cargo test --workspace` (src/Rust): 69 tests pass (FingerTree 46, Hamt 15, Tungsten 8);
  `cargo clippy --all-targets` clean on all three crates.
- `dotnet test Durable7.sln` (src/CSharp): 731 tests pass (FingerTree 367,
  Numerics 266, Hamt 50, Tungsten 48).
- New regression tests added in this review: 13 (6 Rust, 7 C#), including two randomized
  model-based suites (SparseInteger vs `BigInteger`; measured-rope split partitioning).

## Resolution addendum — 2026-07-10

All seven deferred follow-up groups above are resolved. This addendum records the current state
without rewriting the review's original findings or contemporaneous evidence.

### Rust FingerTree completion

- `PersistentDeque<T>` now exposes sorted lower/upper/equal-range splits, stable upper-bound
  insertion, complete equal-range removal, and custom-ordering variants. Balanced deque nodes cache
  first/last leaf signposts, including mirrored views, so `bound_index` descends in O(log n) node
  visits rather than recursively rediscovering subtree endpoints.
- `ReversibleDeque<T>` now has borrowed and owned logical-order iteration. `split_at` and endpoint
  pops return `ReversibleDequeSplit<T>` / `ReversibleDequePop<T>`, preserving reversal and
  orientation-aware operations on every result.
- `MeasuredRope<T, P>` now provides persistent point/range insertion and removal plus slicing, with
  deterministic vector-model and structural-sharing coverage. `MeasuredRopeBuilder<T, P>` stages one
  measured chunk behind an immutable prefix and publishes isolated, prefix-sharing snapshots.
- Character and text ropes now expose Rust-native Unicode-scalar addressing, UAX #29 extended
  grapheme enumeration and offset conversion, LF/CRLF/CR/mixed newline classification, and
  CRLF-aware line text. `unicode-segmentation` 1.13.3 is pinned in `src/Rust/Cargo.lock`; active docs
  record its non-vendored `MIT OR Apache-2.0` licensing.

The balanced-tree/lazy-spine complexity boundary remains an intentional, actively documented port
choice; the previously *undocumented* functional and signpost gaps are closed.

### C# Numerics contracts

- All six wide integers accept `D`/`N`/`X` precision formats and the documented thousands/fractional
  zero parsing styles. They implement `IParsable<T>`, `ISpanParsable<T>`, `IMinMaxValue<T>`, and
  `IUtf8SpanFormattable`; active API docs explicitly state that the full `INumber<T>` /
  `IBinaryInteger<T>` surface is not claimed rather than implying generic-math parity.
- Explicit `float`, `double`, and `decimal` conversions now follow probed .NET 10 `Int128`/`UInt128`
  checked and unchecked behavior, including fractional truncation, non-finite values, and finite
  boundaries. Direct 512↔128 and 1024↔256/128 conversions complete the skipped-width matrix; adjacent
  declaration parity remains guarded separately.
- `GetShortestBitLength` / `GetByteCount` now follow built-in conventions. Every signed width throws
  for `MinValue / -1` even outside a checked context, unsigned `Log2(0)` returns zero, and signed
  `Log2` rejects negatives while handling non-negative inputs consistently.

### Coverage and minor findings

- New C# coverage pins comparer-equal but instance-distinct values across the sorted facades and
  interval tree, duplicate-low interval runs, rope split/re-coalescing boundaries, text-reader
  `Peek`/`Read` interleaving, Tungsten association `GetRange`/`KeyTake`/stable `Sort` properties, and
  every HAMT set-wrapper enumerator state.
- `MeasuredChunkBuilder.Add(ReadOnlySpan<T>)` now bulk-copies each staged span while folding its
  measure. Sorted-dictionary builder key/value views capture versions when enumeration starts.
  `Digit.ChildAt` rejects invalid positions in Release, and both empty C# engine cores report
  impossible enumerator descent as `InvalidOperationException` invariant failures.

### Replacement validation evidence

- `.\test.ps1` (`src/CSharp`): 773 passed, 0 failed, 0 skipped — FingerTree 382,
  Numerics 286, HAMT 54, Tungsten 51 — with inherited no-dialog process error mode.
- `cargo test --workspace` (`src/Rust`): 78 passed — FingerTree 55, HAMT 15, Tungsten 8 — with all
  doc-tests passing.
- `cargo fmt --all -- --check` and
  `cargo clippy --workspace --all-targets -- -D warnings` both pass.
