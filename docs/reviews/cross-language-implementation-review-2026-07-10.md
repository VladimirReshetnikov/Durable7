# Cross-Language Data-Structure Implementation Review — 2026-07-10

- Created (UTC): 2026-07-10T22:34:56Z
- Repository HEAD: e7de55422e8cf337d19e9bfb2523c2b84d2ef04c
- Audience: Maintainers and AI coding agents working on repository-owned data structures
- Scope: Correctness, semantic parity, and complexity parity of the FingerTree, HAMT, Tungsten, and
  Numerics families across C, C++, C#, Haskell, Kotlin, and Rust; fixes applied during this review

## Method

Nine parallel deep-review passes covered every workspace, each instructed to report only findings it
could substantiate with a concrete failure scenario, and to verify suspicions against the code (and,
where practical, empirically) before reporting. The C# libraries were treated as the semantic
reference throughout. Several findings were reproduced with compiled or executed probes before any
fix was written: the C# rope count overflow was executed against the built library, the three C HAMT
aliasing defects were reproduced with compiled test programs, the Haskell stamp-tree imbalance was
measured under GHC 9.12, and the C#/Kotlin/Rust reviews ran large randomized differential harnesses
(BigInteger, BCL collections, brute-force models) that all passed.

Every fix below was validated by rerunning the affected workspace's full test suite, and each landed
as its own commit with the validation evidence recorded in the commit message.

## Verdict summary

The portfolio is in strong shape: after multiple prior review cycles, no critical defects remain,
and the engine cores (deque overflow shape, measured split/concat, lazy-middle publication, HAMT
bitmap math and canonicalization, Tungsten stamp arithmetic and ordering rules 1–7) were re-derived
and found correct in every language. The genuine defects found this round cluster at the facade
boundaries: overflow/aliasing/moved-from contracts, policy-sensitive set relations, and complexity
regressions relative to the documented contracts.

| Workspace | Bugs found (fixed) | Notable outcome |
| --- | --- | --- |
| C# FingerTree | 1 major, 1 minor (both fixed) | Count-overflow guard on rope/PQ concatenation; eager iterator validation |
| C# Hamt / Tungsten | none | Differential harnesses passed; improvement backlog recorded |
| C# Numerics | 3 minor (all fixed) | G-precision / NumberStyles parity with Int128; dead helper |
| C FingerTree | 1 major doc/parity, 3 minor (fixed/documented) | O(log² n) facade class documented; OOM-safe size queries; init validation; threaded test |
| C Hamt | 2 major, 1 minor (all fixed) | Aliasing contract violations (leak, data loss, use-after-free) |
| C Tungsten | 3 minor (all fixed) | Aliased out-param corruption rejected; status classification; concat policy check |
| C++ FingerTree | 1 major perf-parity (recorded), 2 minor (fixed/documented) | Reversible-deque enumeration gap recorded; O(1) duplicate peeks |
| C++ Hamt / Tungsten | 1 minor (fixed) | Moved-from map count; depth static_assert added |
| Haskell | 3 major, 1 minor (all fixed) | Proper-subset probe, stamp-tree AVL join, lazy spine restoration, bag representative |
| Kotlin | 3 minor (all fixed) | Streaming copyTo, locate found flag, lazy Tungsten iterators |
| Rust | 1 major parity (fixed), several complexity gaps (2 fixed, 2 recorded) | from_iter last-entry-wins; O(n) lines(); interval-tree scan recorded |

Separately, the review found and fixed a build regression on `main`: the C++ Tungsten smoke tests no
longer compiled against the shared FingerTree test runner after it gained grouped registration and
replay options.

## Fixes landed (chronological)

1. `2750ebb` — Fix C++ Hamt moved-from state and repair Tungsten test harness
2. `aa8ae3b` — Guard rope and priority-queue concatenation against count overflow (C#)
3. `3a09606` — Align Rust SortedMap bulk construction with C# and speed up text rope
4. `ea33c14` — Trim discarded views and document interval comparison policy in C++ FingerTree
5. `42c5f9b` — Streamline C# SortedSet algebra and IntervalTree counting
6. `d8567c3` — Fix C HAMT aliasing contract violations
7. `445c5a5` — Fix Haskell set relations, stamp-tree balance, lazy spine, and bag keys
8. `cf90b78` — Restore Kotlin streaming reads and disambiguate locate results
9. `76a4fb1` — Harden C FingerTree facades and size reporting
10. `200e6d7` — Align wide-integer parsing and G-format contracts with the BCL (C# Numerics)
11. `e7de554` — Reject destructive out-param aliasing in C Tungsten collections

## Findings by workspace

### C# FingerTree (semantic reference)

**F-CS-1 (major, fixed in `aa8ae3b`)** — `Rope<T>.Concat`, `MeasuredRope<...>.Concat`, and
`PriorityQueue<...>.Meld` performed no combined-count check, so the `int` count component of the
measure silently wrapped, where `FingerTreeDeque.ConcatCore` and `ReversibleDeque.Concat` throw
`OverflowException`. Because these are persistent structures, the wrap was cheap to reach: 31
self-concatenations of a two-element rope produced `Count == 0` with `IsEmpty == false` and every
element unreachable through the bounds-checked indexer (verified by execution). As the semantic
reference, this gap would have been faithfully replicated into every port. All three entry points
now throw `OverflowException`; `CountOverflowGuardTests` exercises each via 29 structural doublings
to 2^30 elements.

**F-CS-2 (minor, fixed in `aa8ae3b`)** — `RopeText.Lines` and `RopeTextExtras.LinesText` were
iterator methods, so their `ArgumentNullException` deferred to first enumeration, contradicting the
documented contract and the file's own eager-wrapper pattern. Both are now eager-validation wrappers
over private iterators.

**Improvements applied (`42c5f9b`)** — `SortedSet` binary operations short-circuit empty operands in
O(1); this also fixes the `Aggregate(SortedSet<T>.Empty, ...)` footgun where the result carried the
empty accumulator's default comparer while being ordered by the other operand's custom comparer.
`IntervalTree.CountOverlaps` counts without materializing the hit list. The class remarks now
document why `IntervalTree` (alone among the sorted facades) has no runtime comparer: the max-high
measure combines inside the shared measured tree where no per-instance comparer is available —
ports should replicate the asymmetry or thread the comparer consistently, not "fix" it ad hoc.

Everything else audited clean, including: digit-boundary transitions, `Glue`/`ToNodes` carry
accounting, split-at-boundary short-circuits, lazy middles under concurrency (CAS-published,
re-runnable pendings), priority-queue FIFO stability among equal priorities, interval-tree touching
endpoints and negative coordinates, surrogate-pair offset math, comparer plumbing, and monoid laws
of the built-in measures.

### C# Hamt, Tungsten, Numerics

**No correctness defects found.** Verification included ~100k randomized `UInt256`/`Int256`
operations against `BigInteger` plus 512/1024-bit boundary matrices (`MinValue/-1`,
`MinValue >> 511`, shift-by-{0, limb multiple, ≥ width}, checked narrowing chains), a 30k-op HAMT
differential against `Dictionary` under adversarial comparers (`hash = k mod 17`, `hash ≡ 0`), and a
20k-op Tungsten differential against an exact kernel-ordering model with forced relabels, prepend
storms walking stamps negative, and old-version immutability. All passed.

**F-NUM-1..3 (minor, fixed in `200e6d7`)** — three Int128-alignment gaps in Numerics.
`"G<precision>"` formats were silently ignored rather than applying G-precision rounding
(`((Int256)12345).ToString("G3")` → `"12345"` where `Int128` → `"1.23E+04"`); rather than replicate
Int128's rounding/scientific path by hand in six widths, G with a precision component now throws
`FormatException`, with the reduced contract documented on every formatting overload. Invalid
`NumberStyles` combinations reported a data error (`TryParse == false`) instead of the BCL's
`ArgumentException`; a shared `ValidateStyle` now applies the BCL rules from every Parse and
TryParse overload, and the decimal path delegates the full style to `BigInteger.TryParse`, gaining
`NumberStyles.Any` (parentheses, trailing signs, currency, exponents) with Int128-identical
behavior verified by an exhaustive 14,364-pair differential over all defined-flag combinations. The
dead private `IsAllDecimalDigits` helper was removed from the three signed widths.

**Improvement backlog (recorded, not applied)** — bit-at-a-time division makes `UInt1024.ToString()`
perform ~17 full-width long divisions (limb-based or divide-by-10^19 specialization would be orders
of magnitude faster); `TryFormat` allocates intermediate strings; `SparseInteger.operator+` is O(k²)
array churn for dense values; the HAMT would benefit from a mutable bulk builder (largest consumer:
`PersistentAssociation.Rebuilt`, which rebuilds the keyed index on every relabel/sort/reverse);
`KeyTake` and the stamp-keyed mutations each pay a double O(log n) walk that a fused entry lookup
would halve.

### C FingerTree

**F-C-1 (major, complexity parity; documented in `76a4fb1`)** — the sorted multiset/set/map facades
and the priority queue binary-search over `ft_tree_at` probes, so membership/add/remove and
`ft_priority_queue_push` cost O(log² n) tree work (each probe also deep-copies one entry through the
facade's copy callbacks), where C# and C++ perform a single O(log n) measured split (and O(1)
amortized PQ push). Results and equal-key ordering are identical — this is a complexity class
divergence only — but `api-notes.md` claimed restored O(log n) parity for exactly these facades. The
notes now state the actual class and what restoring parity requires (key-carrying measures plus
comparator predicates, as the interval facade already models with its `(count, maxHigh)` measure).
The full facade rework is the largest open parity item in the C port.

**F-C-2 (minor, fixed in `76a4fb1`)** — the rope facades' size queries collapsed internal allocation
failure to 0, making a non-empty rope read as empty (`ft_rope_at` then returned `FT_STATUS_EMPTY`),
and `ft_text_rope_line_count` returned 0 — an otherwise-impossible value. New status-returning
`ft_rope_try_size` / `ft_measured_rope_try_size` / `ft_text_rope_try_size` /
`ft_text_rope_try_line_count` expose the failure as `FT_STATUS_NO_MEMORY`; the `size_t` conveniences
delegate to them and document the collapse.

**F-C-3 (minor, fixed in `76a4fb1`)** — `ft_reversible_deque_init` skipped the `value.size == 0`
validation `ft_tree_init` performs, silently producing a deque whose reads copy nothing.

**F-C-4 (validation gap, fixed in `76a4fb1`)** — the concurrent-snapshot refcount test degraded to a
sequential loop on every non-Windows toolchain; it now races 8 workers via C11 `threads.h` when
available, exercising the `stdatomic` lazy-middle CAS publication paths under real contention.

**Recorded, not fixed** — read-only lookups in the sorted map, priority queue, and generic interval
facade copy a full entry per probe through abort-on-OOM callbacks, so a transient allocation failure
during a pure read aborts the process rather than returning `FT_STATUS_NO_MEMORY`. The documented
abort-on-OOM policy (`api-notes.md`) covers this, but the blast radius shrinks to near zero if
F-C-1's key-measured search lands, which removes most per-probe copies. Memory management otherwise
audited clean: every traced retain/release pair balances, failure paths dispose partially built
structures correctly, and the interval measure's borrowed leaf storage is kept safe by leaf
re-measure on clone.

### C Hamt

**F-CH-1 (major, leak; fixed in `d8567c3`)** — `tds_hamt_set_add` skipped the aliased-release step
every sibling mutator performs, leaking the entire previous version on each aliased add (reproduced:
99 of 100 items never released across an aliased add loop with a counting policy).

**F-CH-2 (major, data loss; fixed in `d8567c3`)** — aliased `tds_hamt_map_add` destroyed the
caller's map on `TDS_HAMT_DUPLICATE_KEY` (reproduced: a duplicate add left the source with count 0
and all entries destroyed). A rejected duplicate publishes the source root re-retained, so the
aliased result already holds the original version; only a distinct result owns a reference to
destroy.

**F-CH-3 (minor, use-after-free; fixed in `d8567c3`)** — aliased `tds_hamt_map_try_remove` released
the old root before publishing `removed_value`, whose pointer refers into the old tree's nodes, so
an owning value policy had already freed the payload the caller received. The aliased form now
reports `removed_value = NULL`, and the API specification documents that callers needing the value
must pass a distinct result. The new tests cover the full aliasing matrix with counting policies and
fail against the unfixed code.

Bitmap/index math, canonicalization, allocation-failure unwinding, and the depth-7 shift bound were
audited and found correct (the `shift >= 32` guard in merge is provably unreachable).

### C Tungsten

**F-CT-1 (minor, fixed in `e7de554`)** — out-parameters silently corrupted or leaked when `result`
aliased the source: `tds_tungsten_list_prepare` zeroed the result before the source was read
(leaking the deque rep and leaving the caller's list empty even on failure), and
`tds_tungsten_association_take_parts` overwrote `result`'s root/index/context without releasing
prior contents. Unlike the C HAMT, aliasing was never a supported mode here (every internal caller
uses a distinct temporary plus move), so it is now uniformly rejected with
`TDS_TUNGSTEN_INVALID_ARGUMENT` across all list and association entry points (including two-operand
forms rejecting `result == right`), and the README documents the contract and the
temporary-plus-move idiom for update-in-place callers.

**F-CT-2 (minor, fixed in `e7de554`)** — `tds_tungsten_list_drop` returned
`TDS_TUNGSTEN_INVALID_ARGUMENT` for an out-of-range count where `take`/`slice` and the association's
`drop` return `TDS_TUNGSTEN_OUT_OF_RANGE`; now classified consistently.

**F-CT-3 (minor, fixed in `e7de554`)** — `tds_tungsten_list_concat` checked only payload *size* before
spoofing the right operand's policy pointer to share nodes under the left policy, so two lists with
same-size payloads but different copy/destroy callbacks concatenated successfully and then released
some shared nodes through the wrong callbacks (leak or shallow-copy double-free). The compatibility
check now compares the full value type (size, copy, destroy, context).

Stamp math (`tds_tungsten_pick_stamp` mirrors C# `TryPickStamp` exactly, including the
unsigned-subtraction midpoint and end guards), AVL rotations/join/split refcounts on success and OOM
paths, and Association ordering rules 1–7 were all audited and found correct.

### C++ FingerTree

**F-CPP-1 (major, performance parity; recorded)** — `reversible_deque` iteration performs an
O(log n) `get_leaf` descent per element, and each reversed level allocates a mirrored `rev_node`
(plus copied child vectors), so a full traversal is O(n log n) with per-step allocations where the
C# enumerator is O(n) total and allocation-free via an orientation bit per stack frame. `copy_to`
inherits the cost while `to_vector` on the same object is O(n) — an inconsistency inside one facade.
Results are correct (probe-verified against a model with interleaved reversals). The fix is a port
of the C# orientation-bit stack enumerator; recorded as the main open C++ parity item rather than
rushed here, since it touches iterator category semantics.

**F-CPP-2 (minor, documented in `ea33c14`)** — `interval<T>::contains`/`overlaps` default their
comparison policy independently of the owning tree's policy, so with a custom-policy tree,
`tree.try_find_containing(p)` can return an interval for which the defaulted `iv.contains(p)` is
false. The default is kept (correct for the common case and widely used); both members now document
the explicit-policy requirement. C# hardcodes `Comparer<T>.Default` throughout, so the trap is
C++-only surface.

**F-CPP-3 (minor, fixed in `ea33c14`)** — `sorted_set::add` and `sorted_map::insert`/`try_insert`
probed for duplicates by materializing a discarded left view (rebuilding the whole rest tree,
possibly forcing the lazy middle); they now use the worst-case O(1) `front()` peek, with direct C#
precedent.

The adversarial probe suite (randomized model checks across chunk/digit boundaries with retained
snapshots, priority stability, interval stabbing at touching endpoints, iterator lifetime past
facade destruction) passed; memory-ordering of the lazy publication (seq_cst throughout) is strictly
stronger than the C# scheme.

### C++ Hamt / Tungsten

**F-CPPH-1 (minor, fixed in `2750ebb`)** — a moved-from `persistent_hash_map` nulled its root but
kept its stale `count_`, so `count()`/`is_empty()` disagreed with enumeration for the map and
everything composed over it (set, association index). User-defined move operations now exchange the
count to zero, with a self-assignment guard and conditional noexcept; a `static_assert` ties
`max_depth` to `bits_per_level` so the iterator's inline 7-frame stack cannot be silently
overflowed by a future radix change.

Also fixed in `2750ebb` (found while validating): the C++ Tungsten smoke tests no longer compiled on
`main` — the shared FingerTree test runner had gained grouped registration and `run(argc, argv)`
replay options, and `tungsten_tests.cpp` still used the old API. The Tungsten harness now registers
grouped tests and forwards command-line arguments.

Everything else audited clean, including max-depth frame sufficiency (proved, both sides), lifted
leaves after erase, collision buckets, exception safety (strong guarantee verified structurally
against the injected-failure tests), Association ordering rules, and const-correctness (zero
`const_cast`/`mutable`).

### Haskell

**F-HS-1 (major, fixed in `445c5a5`)** — `HashSet.isProperSubsetOf`/`isProperSupersetOf` compared
the argument's raw size (counted under the argument's own policy) where C# counts the probe
materialized under the receiver's comparer. A mod-10-policy receiver `{1}` versus default-policy
`{11, 21}` reported both `setEquals` and `isProperSubsetOf` — mutually contradictory. Both relations
now size the receiver-policy probe, like `setEquals` already did.

**F-HS-2 (major, fixed in `445c5a5`)** — the Tungsten Association's `treeSplit` constructed nodes
with arbitrarily unequal subtree heights, which the single-rotation `treeBalance` never repairs;
repeated positional inserts degraded the stamp tree super-logarithmically (measured height 48 at
n = 16384 versus the AVL bound of ~20, growth per doubling increasing). A height-aware AVL join now
descends the taller spine and rebalances on unwind; a stress test drives 2000 appends plus 2000
same-point midpoint inserts across ~100 relabels and asserts the 1.4405·log₂(n+2) height bound
(the old code fails it).

**F-HS-3 (major, fixed in `445c5a5`)** — `Measured.cons`/`snoc` routed new `Deep` nodes through the
`deep` smart constructor, which measures the new middle; combined with the strict cached-measure
field this forced the entire overflow cascade eagerly, defeating the suspended middle the type
itself documents and losing the Hinze–Paterson persistent amortized O(1) endpoint bounds (ephemeral
use kept its bounds, which is why tests never caught it). Both now extend the cached measure
incrementally (`measure value <> v` for cons, `v <> measure value` for snoc — order matters for
non-commutative monoids, locked by a new trace-measure test) and construct `Deep` directly.

**F-HS-4 (minor, fixed in `445c5a5`)** — `SortedBag.insert` used `Map.insertWith`, which replaces
the stored map key, so `toCounts` reported the newest equal instance as representative while
`toList`/`minValue`/`deleteOne` present first-stored semantics (and C# keeps the first stored
instance). The existing-key path now uses `Map.adjust`.

**Recorded, not fixed** — `SortedBag` rank/positional operations are linear (`index`,
`countLessThan`, `slice` walk buckets) where C# documents O(log n); deque sorted searches are
O(log² n) probes over `index` versus C#'s single signpost-guided descent. The `containers`-backed
storage choice is documented in the package README; the complexity part of that trade is now the
main open Haskell parity item.

### Kotlin

No correctness bugs — the measured-AVL substrate, reversible-deque orientation algebra, HAMT bit
paths (including shift-30 boundary and negative hashes), and Tungsten stamp arithmetic all verified,
empirically backed by ~1,800 lines of adversarial differential probes (all passing) plus a
height-stability probe (16–18 for 64K–100K elements under adversarial histories).

**Fixed in `cf90b78`** — three efficiency/API findings: `copyTo` did a full O(log n) descent per
copied element (now a seek-once streaming iterator, O(k + log n)); `tryLocate`/`locateByMeasure`
dropped the internal `found` flag, making stored nulls indistinguishable from misses (both result
types now carry `found`, matching C# `TryLocate`'s boolean); and Tungsten's iterators materialized
the whole sequence up front (now lazy stack-based in-order iterators).

**Recorded parity gaps** — `MeasuredRope` lacks most of the C# editing surface (`AddFirst`,
`Insert`, `RemoveRange`, `Slice`, `Compact`, ...); `SortedMap` has no comparator bulk factory;
`PersistentHashSet.add` throws on duplicate (documented as intentional; `put` is the C# `Add`
equivalent); endpoint operations are O(log n) versus C#'s amortized O(1) lazy spine (documented as
intentional); `IntSumMeasure` is fixed at `Int` versus the generic C# `SumMeasure<T>`.

### Rust

**F-RS-1 (major parity, fixed in `3a09606`)** — `SortedMap::from_iter` compacted equal-key runs by
keeping the first key instance and overwriting only the value, where C#'s
`SortedDictionary.CreateRange` keeps the last entry wholesale — and the crate's own `set_item`
already follows last-wins, so the bulk constructor disagreed with the point write. Fixed by
replacing the retained entry in full (the stable sort makes last-wins well-defined), with a
comparer-distinguishable-key regression test.

**Fixed in `3a09606`** — `TextRope::lines()` was O(n·L) (per-line `get_line`, each paying an
O(start) iterator skip); it is now a single O(n) pass, and `get_line` slices the measured rope
(O(log n + length)). `RopeBuilder::len()` recounted the whole string per call; it now maintains a
running char count (O(1), matching the C# contract).

**Recorded, not fixed** — the interval-tree queries walk `iter().skip(index)` and scan every
candidate with `low <= probe.high` (O(n) worst case; 100k intervals stabbed at the far end walk
~100k elements for one hit) where C# does measured-tree descent at O(log n)/O(k log n); the fix is
the same restrict-then-`try_split_find` walk the C# code uses, and the order-statistic split it
needs already exists in the crate. `SortedSet` algebra drives its merge with per-index `get` calls
(O((n+m) log n) constant-factor overhead versus C#'s O(n+m) enumerator merge). Both fall half-inside
the crate's documented "semantic checkpoint" boundary, but the interval-tree doc comments claim the
C# complexity for neighboring operations, so these are the main open Rust parity items alongside
the documented lazy measured-spine boundary.

Everything else verified, including panic-freedom from the public API (all `expect`/`unwrap` behind
validated bounds or proven invariants; `checked_add/sub` on stamp and range math; no usize
underflow found), UTF-8 boundary safety in the text helpers (only `&str` slicing sites use
`grapheme_indices` boundaries), Hamt shift bound (debug_assert genuinely unreachable), and the full
Tungsten ordering-rule matrix.

## Cross-language parity matrix (state after this review)

| Contract | C# | C++ | C | Haskell | Kotlin | Rust |
| --- | --- | --- | --- | --- | --- | --- |
| Sorted-facade equal-key rules (set first-wins, map last-wins incl. bulk, bag stable) | ref | parity | parity | parity (bag representative fixed) | parity | parity (from_iter fixed) |
| Priority queue FIFO-stable among equal minima | ref | parity | parity | parity | parity | parity |
| Interval tree: equal-low insert order, closed-endpoint stabbing | ref | parity (custom-policy superset) | parity | parity | parity | parity (complexity gap recorded) |
| Tungsten Association ordering rules 1–7, stamp scheme, relabel | ref | parity | parity | parity (balance fixed) | parity | parity |
| HAMT canonicalization, max-depth bound, stored-key retention | ref | parity | parity | parity (proper relations fixed) | parity | parity |
| Count-overflow behavior on concat/meld | throws (fixed) | checked_add throws | size_t | Int (documented) | Int | usize checked |
| Endpoint amortized O(1) via lazy spine | ref | parity | parity | parity (restored) | O(log n), documented | checkpoint boundary, documented |
| Sorted facade keyed ops O(log n) | ref | parity | O(log² n), now documented | bag rank ops linear, recorded | parity | parity |
| Failure model | exceptions | exceptions | status codes (documented) | total functions / Maybe | exceptions | Option/Result |
| Out-param aliasing | n/a (GC) | n/a (values) | HAMT supported+fixed; Tungsten rejected+documented | n/a | n/a | n/a |

## Validation evidence

- C#: `dotnet test` — FingerTree 387/387 (4 new tests), Numerics 313/313 (27 new tests); solution
  builds clean.
- C: `build.ps1 -Workspace Hamt -RunTests` Debug and Release — 26/26 (3 new aliasing tests);
  `-Workspace FingerTree -RunTests` — 3/3 CTest suites; `-Workspace Tungsten -RunTests` Debug
  (7/7, including the new aliasing test) and Release — all pass.
- C++: `build.ps1 -Workspace Hamt -RunTests` — 28/28 (2 new moved-from tests); `-Workspace
  FingerTree -RunTests` — all CTest groups; `-Workspace Tungsten -RunTests` — tungsten.smoke passes
  (was failing to compile on `main` before this review).
- Haskell: `test.ps1` — hamt-test, ft-test, tungsten-test all pass (GHC 9.12.4, `-Wall` clean),
  including the new AVL-height stress, non-commutative trace measure, cross-policy relations, and
  bag-representative tests.
- Kotlin: `build.ps1` — Hamt 11/11, FingerTree 20/20, Tungsten 10/10 (3 new tests).
- Rust: `cargo test --workspace` — all suites green (new `from_iter` regression test included).

## Open items (prioritized backlog)

1. **C FingerTree**: key-measured search for the sorted/PQ facades (restores O(log n) and removes
   most abort-on-OOM read-path copies) — the largest cross-port complexity gap.
2. **Rust FingerTree**: interval-tree measured descent (`split` + repeated `try_split_find` over the
   max-high measure) and an enumerator-driven `SortedSet` merge.
3. **C++ FingerTree**: orientation-bit stack enumerator for `reversible_deque` (O(n) allocation-free
   traversal, also fixes `copy_to`).
4. **Haskell FingerTree**: O(log n) rank/positional operations for `SortedBag` (or document the
   deviation in the package README like the storage choice already is).
5. **Kotlin FingerTree**: `MeasuredRope` editing-surface parity; comparator bulk factory for
   `SortedMap`.
6. **C# Numerics**: limb-based division (biggest formatting win), allocation-free `TryFormat`,
   merge-based `SparseInteger` addition.
7. **C# Hamt**: internal mutable bulk builder (biggest consumer: `PersistentAssociation.Rebuilt`).

## Resolution addendum — 2026-07-10

This addendum preserves the review's original findings, parity matrix, and contemporaneous evidence
above while recording the subsequent remediation. **All actionable open, recorded-not-fixed, and
recorded-not-applied items are resolved.** The older language-pair reviews in this directory already
carry their own complete resolution addenda.

1. **C FingerTree structural search — `a1f3ad0`.** The generic tree now caches rightmost-leaf
   signposts and performs monotone bound descent in O(log n). Sorted bag/set/map membership and
   edits, priority insertion, and both interval lower-bound paths no longer binary-search through
   `ft_tree_at` payload copies. Counting-policy and 4,096-element operation ceilings lock in the
   class and the read-path copy reduction.
2. **Rust interval and set algebra — `19848eb`.** Interval storage carries low/max-high product
   annotations and queries descend/prune the measured tree. `SortedSet` algebra now streams two
   iterators rather than issuing indexed tree reads.
3. **C++ reversible traversal — `aae1f6d`.** `reversible_deque` uses an orientation-bit inline
   cursor, so iteration and `copy_to` stream in logical order without materializing a temporary
   vector.
4. **Haskell rank and sorted-bound descent — `58c300f`, `e05b76e`.** `SortedBag` is an
   order-statistic measured tree of `Data.Sequence` buckets, giving logarithmic rank/count/slice
   boundaries even for a single 100,000-instance bucket. `Deque` now measures size plus its
   rightmost leaf, so runtime-comparator lower bound, upper bound, and binary search follow one
   measured root-to-leaf path rather than O(log² n) indexed probes.
5. **Kotlin facade parity — `85da13a`.** `MeasuredRope` has the C# positional/editing surface,
   builder, slicing, and compaction behavior on the measured AVL substrate. `SortedMap.from` now
   accepts an explicit comparator while preserving stable last-wins construction.
6. **C# Numerics hot paths — `7115a65`.** The three unsigned widths share normalized Knuth
   limb division with a single-limb fast path; signed division reuses magnitude wrappers.
   UTF-16/UTF-8 `TryFormat` writes `G`/`D`/`N`/`X` directly from limbs without an intermediate
   string on ordinary paths. `SparseInteger` addition merges ordered bit streams with integrated
   carry instead of repeated middle-array edits. Wide carry/borrow propagation also no longer
   converts literals through `BigInteger`.
7. **C# HAMT/Tungsten bulk and fused edits — `c092016`, `d2f4d6d`.** An internal transient HAMT
   mutates unpublished leaf/collision/bitmap nodes and freezes detached immutable nodes once;
   map/set factories, set intersection, and `PersistentAssociation.Rebuilt` use it. `KeyTake`
   obtains canonical key plus slot in one HAMT probe, while stamp-keyed set/append/prepend/insert/
   remove operations fuse signpost location and persistent deque reconstruction in one descent.

The reports also record deliberate, non-actionable differences—such as strict measured-AVL
endpoint bounds in Kotlin/Rust, language-native error/result conventions, the absence of a Rust
Numerics port, and intentionally reduced generic-math surface. Those remain documented design
choices rather than postponed defects.

### Replacement validation evidence

- C FingerTree: MSVC Debug/Release, GCC, and Clang lanes, 3/3 CTests in each.
- Rust workspace: 81 tests; `cargo fmt --check` and `cargo clippy --workspace --all-targets` clean.
- C++ FingerTree: MSVC Debug/Release, 18/18 CTests in each; affected GCC and Clang lanes pass.
- Haskell workspace: FingerTree, HAMT, and Tungsten suites pass under GHC 9.12.4 with `-Wall`.
- Kotlin workspace: HAMT 11, FingerTree 22, and Tungsten 10 executable test groups pass.
- C# workspace: HAMT 58, Numerics 317, Tungsten 51, and FingerTree 388 tests pass (814 total).
