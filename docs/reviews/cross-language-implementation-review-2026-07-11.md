# Cross-Language Data-Structure Implementation Review — 2026-07-11

- Created (UTC): 2026-07-11T02:56:24Z
- Repository HEAD: 867ae9a3a7eff0d262bb56e86f90a6698bc869c5
- Audience: Maintainers and AI coding agents working on repository-owned data structures
- Scope: Correctness, semantic parity, and complexity parity of the FingerTree, HAMT, Tungsten, and
  Numerics families across C, C++, C#, Haskell, Kotlin, and Rust; fixes applied during this review

## Method

Seven parallel deep-review passes covered every workspace: C# FingerTree; C# HAMT + Tungsten +
Numerics; and the full C, C++, Haskell, Kotlin, and Rust workspaces (one pass per language). Each
pass read the implementation sources in full, treated the C# libraries as the semantic reference,
was told to disregard prior review reports and to report only findings it could substantiate with a
concrete failure scenario. Several findings were reproduced empirically before any fix was written:

- The C# FingerTree pass ran a model-based fuzz harness (~50k randomized operations per family
  across ~40 seeds, with persistence snapshots) over the deque, reversible deque, sorted facades,
  ropes, interval tree, and priority queue, and reproduced the enumerator-copy defect with a
  compiled probe (a copied enumerator silently skipped element 10 and yielded 11).
- The C# Numerics pass ran ~150k randomized differential checks against `BigInteger` and the BCL
  (arithmetic, shifts, comparisons, G/D/N/X formatting across four cultures, UTF-8 paths, parse
  round-trips) and reproduced all three parity gaps with concrete inputs.
- The Haskell pass compiled differential probes (~20k randomized operations against naive models,
  GHC 9.12.4) and reproduced both defects, including the cross-policy symmetric-difference case.
- The Kotlin pass transliterated the measured-AVL engine and reversible-deque concat/rebalance to
  Python and model-checked them (6,000 mixed operations with per-step AVL/measure validation;
  90,000+ adversarial operations across 30 seeds with height tracking).
- The C++ pass compiled two differential stress programs with clang++ `-std=c++23` (~25k randomized
  operations against `std::` models, including dense-collision HAMT snapshots and relabel-forcing
  association churn), all passing.
- The Rust pass wrote and ran seven adversarial probe tests (deque depth via comparison counts under
  adversarial concat/split patterns, HAMT deep-chain collapse with an identity hasher, 3000-step
  clustered-hash model fuzz with retained snapshots), all passing, and removed them afterwards.
- The C pass traced refcount pairing and error paths across all split/concat/rebalance/force code
  (hamt.c 2,022 lines, fingertree.c 10,337 lines, tungsten.c 2,794 lines read in full).

Every fix below was validated by rerunning the affected workspace's full test suite and landed as
its own commit on `claude/data-structures-review-b6512e` with validation evidence in the commit
message.

## Verdict summary

After the 2026-07-09/10 review cycles, the engine cores are converged: no defect was found in any
deque/measured-tree core, HAMT bitmap or canonicalization logic, Tungsten stamp arithmetic, or
ordering-rule implementation in any language. This round's genuine defects sit one layer out — a C#
struct-enumerator aliasing hazard, three C# numerics BCL-parity gaps, one Haskell cross-policy set
relation, one Haskell bucket-representative staleness, and one C ownership leak on a callback
boundary. Everything found was fixed in this round.

| Workspace | Findings (this round) | Outcome |
| --- | --- | --- |
| C# FingerTree | 1 aliasing defect, 2 contract nits | Fixed: fail-fast enumerator copies; facade overflow guards; `Measure` doc |
| C# Hamt / Tungsten | none | Clean; differential harnesses passed |
| C# Numerics | 2 medium, 1 low BCL-parity gaps | Fixed: `N`-format negative patterns; checked double→unsigned; `TryParse` style validation |
| C Hamt | none | Clean, including all aliasing paths fixed last round |
| C FingerTree | 1 doc gap | Documented: abort-on-OOM boundary includes entry-copy read paths |
| C Tungsten | 1 medium leak, 1 low complexity | Fixed: `list_map` owning-payload leak; O(n) relabel collection |
| C++ Hamt / FingerTree / Tungsten | 3 low (perf/API parity) | Fixed: O(1) append/prepend fast paths, `sorted_map::at` by reference; bulk-builder parity recorded |
| Haskell Hamt | 1 high | Fixed: `symmetricDifference` receiver-policy dedup |
| Haskell FingerTree | 1 low | Fixed: `SortedBag` bucket re-keying after `deleteOne`/rank slice |
| Kotlin | 2 low | Fixed: streamed reversible-deque iteration; shared natural comparator |
| Rust | 2 low (perf parity) | Fixed: O(1) append/prepend fast paths; bulk-builder parity recorded |

## Findings and fixes

### C# FingerTree — enumerator copies silently skipped elements (fixed, `1feb4f5`)

`FingerTreeDeque<T>.Enumerator`, `FingerTree<...>.Enumerator`, and `ReversibleDeque<T>.Enumerator`
kept their `Frame[]` traversal stack on the heap while `_depth`/`_current` lived in the struct.
Copying an in-progress enumerator therefore produced two enumerators sharing one mutable stack:
advancing one made the next `MoveNext` on the other skip the element the first consumed — silently.
Empirically confirmed: advance an enumerator 10 times, copy it, advance the original once (yields
10), then advance the copy — it yields 11, and element 10 is never produced by the copy. This was
worse than both BCL precedents: `List<T>.Enumerator` copies are fully independent, and
`ImmutableList<T>` detects misuse and throws.

Fix: each enumerator's frames, depth, and a yield cursor now live in one shared `TraversalState`
object; each struct copy tracks the cursor value it last observed, and a copy left behind by another
copy's advance throws `InvalidOperationException` on its next `MoveNext`. Single-active-copy usage
(including copy-then-consume) still works. The copy contract is documented on all three enumerator
types, and `EnumeratorCopyDivergenceTests` covers divergence, single-active-copy, reversed-snapshot,
exhausted, empty, and default-enumerator cases.

Two contract nits fixed in the same commit: the facade single-add paths (`PriorityQueue.Enqueue`,
`SortedSet.Add`, `SortedBag.Add`, `SortedDictionary.Add`/`TryAdd`/`SetItem` insert branch,
`IntervalTree.Insert`) now apply the same explicit `OverflowException`-at-`int.MaxValue` convention
the deques, ropes, and `Meld` already used, instead of letting their measure counts wrap; and the
`FingerTree<...>.Measure` XML doc now carries the "O(1) amortized; the first read of a fresh spine
may force memoized deferred work" qualifier that the class remarks and every facade `Count` doc
already state. The general `FingerTree` itself has no int count (its measure is an arbitrary
monoid), so no guard applies there.

### C# Numerics — three BCL-parity gaps (fixed, `19d51e7`)

1. **`N`-format ignored `NumberNegativePattern` in span/UTF-8 `TryFormat` (medium).** With
   `NumberNegativePattern = 0`, `((Int256)(-1234567)).ToString("N", nfi)` produced
   `"(1,234,567.00)"` (the string path delegates to `BigInteger`) while `TryFormat` on the same
   value produced `"-1,234,567.00"`. `TryFormatDecimal`'s number branch now lays out all five
   patterns — `(n)`, `-n`, `- n`, `n-`, `n -` — with prefix/suffix included in the buffer precheck.
   The UTF-8 overloads route through the char path and inherit the fix.
2. **Checked double→unsigned conversion accepted (-1, 0) (medium).** The conversion truncated
   before range-checking, so `checked((UInt256)(-0.9))` silently returned 0 where
   `checked((UInt128)(-0.9))` throws `OverflowException`. The checked path now rejects negative
   inputs to unsigned destinations before truncation; unchecked saturation and the signed types
   (where 0 is the BCL answer) are unchanged.
3. **String `TryParse` skipped style validation for null input (low).**
   `UInt256.TryParse((string?)null, (NumberStyles)0x40000000, ...)` returned false where the BCL
   (and the library's own documented contract) throws `ArgumentException`. All six wide types now
   validate the style before the null check.

New regression tests cover all five negative patterns (UTF-16 + UTF-8, signed and unsigned widths),
the (-1, 0) checked/unchecked conversion cases across all six types, and null-string style
validation across all six types.

### Haskell — cross-policy symmetric difference and stale bag representatives (fixed, `d0334b8`)

1. **`HashSet.symmetricDifference` skipped the receiver-policy probe (high).** Every sibling
   relation (`isSubsetOf`, `setEquals`, `intersection`, the proper-relation strictness counts)
   routes the argument through `probeWithReceiverPolicy`, matching the C# reference, whose
   `SymmetricExcept` materializes its probe with `this.Comparer` so argument elements that are
   duplicates under the receiver's policy toggle once. `symmetricDifference` folded over the raw
   argument, letting such duplicates cancel pairwise: with a case-insensitive receiver `{"a"}` and
   default-policy argument `{"a", "A"}`, C# yields `{}` but Haskell yielded `{"A"}` (reproduced with
   a compiled probe). The fold now consumes the receiver-policy probe.
2. **`SortedBag` left a stale bucket key after `deleteOne` and rank `slice` (low).** Both sites
   kept the old bucket key after dropping front instances, so `toCounts` could report a
   representative value no longer contained in the bag (observable with comparer-equal but
   distinguishable values). Both now re-key from the first surviving instance via a shared
   `rekeyedBucket` helper, restoring the documented "first stored equal instance is the
   representative" contract. Ordering was never affected (the stale key was comparer-equal).

### C Tungsten — `list_map` leaked owning payloads (fixed, `7e1e587`)

`tds_tungsten_list_map` never destroyed the values its `map` callback constructed into the staging
buffer: `push_back` deep-copies the buffer into the result list, so a result value type with an
owning copy/destroy pair leaked one payload per element (only the raw buffer was freed once at the
end). The existing tests exercised only POD ints, so this was unexercised. The visit callback now
destroys the buffer value right after each `push_back` (success and failure alike), the
`tds_tungsten_map_fn` typedef documents the ownership contract, and a new owning-result-type test
drives `list_map` with heap-owning payloads under balanced allocation/destruction counters (the old
code fails it by five unmatched allocations).

Also in that commit: the stamp-exhaustion relabel path of `tds_tungsten_insert_absent` collected
entries with a per-index `tds_tungsten_tree_at` loop (O(n log n)); it now uses the O(n) in-order
`tds_tungsten_tree_fill_views` walk plus a `memmove` splice, matching reverse/sort/slice. And the C
FingerTree api-notes now spell out that the infallible-copy abort-on-OOM boundary includes read
paths that copy compound entries out (`ft_rope_at`, `ft_measured_rope_at`,
`ft_sorted_map_entry_at`, priority/interval entry reads), which prior wording left implicit.

### Kotlin — traversal cost and comparator identity (fixed, `9f7749c`)

1. **Reversible-deque concat iteration was O(n · height) with coroutine overhead (low).**
   `ReversibleDequeConcatNode` built iterators from recursively nested
   `sequence { yieldAll(...) }` blocks, pulling every element through one `SequenceBuilderIterator`
   delegation per tree level — the same shape the C++ port fixed in `aae1f6d`, in a workspace where
   every other traversal streams via an explicit stack. Both iterators now use an explicit-stack
   traversal over `logicalParts()` (orientation handled by the existing part flipping), giving O(1)
   amortized per element with O(height) state.
2. **`naturalComparator()` singleton-ness was a compiler accident (low).** `PriorityQueue.meld`
   accepts only queues whose comparators are identical or equal, and JVM `Comparator` equality is
   identity; the helper returned a lambda whose per-call-site caching is a Kotlin/JVM compiler
   detail. It now returns the stdlib's `naturalOrder()` shared singleton, and the identity
   requirement is documented in the api-notes.

### Rust and C++ Tungsten — O(1) no-op fast paths; C++ `sorted_map::at` (fixed, `867ae9a`)

`append`/`prepend` in both ports paid an O(log n) `index_of_stamp` binary search before their
return-same-instance fast paths, where the C# reference decides "key already at the end with an
equal value" in O(1) via the deque's end entry. Stamps are unique, so both ports now compare the
keyed slot's stamp against `entries.back()`/`front()` first; only the real removal path pays the
position search. Results are identical — this was a constant-factor parity gap only.

C++ `sorted_map::at` also returned `mapped_type` by value (through `try_get`'s optional copy of the
whole entry) while `sorted_set::at` and `entry_at` return references into shared node storage;
`locate_key` now uses `try_locate_reference` and `at` returns `const mapped_type&` under the
documented snapshot-lifetime rule.

## Verified clean (highlights)

Each pass listed the areas it re-derived and found correct; the recurring, load-bearing ones:

- **Engine cores in every language**: deque overflow-shape discipline (`ToNodes` chunking, `Glue`
  carries, Cons/Snoc potential rules), measured split/locate invariants ("predicate false at
  accumulator, true at accumulator ⊕ measure" at every recursion), lazy-middle CAS publication
  (C#, C, C++), subtraction-free deep-size arithmetic, and the Kotlin/Rust balanced-tree
  substitutes' height bounds (empirically ≤ 1.51 · log₂ n Kotlin; comparison-count-bounded Rust).
- **HAMT family**: bitmap/popcount slotting, collision-bucket lifecycle including single-entry
  collapse and re-expansion, `mergeTwo`/`MergeHashNodes` termination before shift 32 (identity-hash
  probes in Rust/C++), stored key/value instance retention, policy-preserving factories, iterator
  frame bounds (7 frames for 32-bit hashes), and C aliasing/refcount pairing on every error path.
- **Tungsten associations**: all seven kernel-verified ordering rules re-verified against models in
  C#, C++, Haskell, Kotlin, and Rust (including `Insert`'s pre-removal index interpretation,
  `Join`'s first-position/last-value rule, `KeyTake` dedup/stored keys, `GetRange` small-side
  reconciliation, stable sorts with stamp tie-breaks); stamp midpoint arithmetic overflow-safe in
  every port (unsigned-gap subtraction / `std::midpoint` / `checked_add` variants).
- **Numerics core**: Knuth division normalization/correction/add-back, carry chains, checked
  operators, shift/rotate boundary masking, hex/decimal digit budgets, `SparseInteger` canonical
  invariants — all differential-tested against `BigInteger` without divergence beyond the three
  fixed findings.
- **Ropes and text facades**: chunk-size invariants through concat/split coalescing, newline
  measures and line/offset round-trips at boundary shapes (`""`, `"\n"`, trailing newlines),
  grapheme/surrogate handling.

## Recorded backlog (not fixed this round)

- **Transient HAMT bulk-builder parity (Rust, C++).** The C# reference gained transient bulk
  construction (`c092016`) used by `CreateRange`, set intersection, and the association's
  relabel/sort/reverse index rebuilds. The Rust `rebuilt` path and the C++ `from_range`/
  intersection/rebuild paths still do per-item persistent inserts — same asymptotics (hash depth is
  bounded), deliberate allocation-profile divergence. Worth porting the builder to both HAMTs.
- **Kotlin sorted-facade bounds are O(log² n).** Honestly documented in the Kotlin api-notes, but
  Haskell was upgraded to true O(log n) measure-guided bounds (`e05b76e`, `58c300f`); Kotlin is now
  the only port with the log² rank path.
- **C# `SortedSetReads_AllocateNothing` is load-sensitive.** It failed once under heavy parallel
  build load and passed on quiet reruns (twice); the 256-byte budget can be crossed by
  tiered-JIT/OSR noise on a saturated machine. If it flakes again, consider warming OSR explicitly
  or widening the budget with a comment.

## Fixes landed (chronological)

1. `1feb4f5` — Make C# FingerTree enumerator copies fail fast and align facade overflow contracts
2. `19d51e7` — Close three BCL-parity gaps in wide-integer formatting, conversion, and parsing
3. `d0334b8` — Fix Haskell symmetric-difference policy handling and stale bag representatives
4. `7e1e587` — Stop leaking mapped payloads in C Tungsten list_map and speed up relabel
5. `9f7749c` — Stream Kotlin reversible-deque traversal and share the natural comparator
6. `867ae9a` — Give Rust and C++ association fast paths O(1) end checks; return map refs

## Validation

- C#: `dotnet build` clean (0 warnings); FingerTree suite 393/393; Numerics suite 319/319.
- Haskell: all three cabal test suites pass (hamt-test, ft-test, tungsten-test) under GHC 9.12.4.
- C: Tungsten Debug and Release CTest suites pass, including the new owning-type map test.
- Kotlin: `build.ps1` compiles all three workspaces; all executable tests pass (exit 0).
- Rust: workspace suite passes (81 tests across the three crates).
- C++: FingerTree CTest suite passes (18 tests); Tungsten CTest suite passes.
