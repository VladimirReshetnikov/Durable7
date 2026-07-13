# C and C++ Implementation Review — 2026-07-09

- Created (UTC): 2026-07-09T00:00:00Z
- Repository HEAD: 29ea8e1806a8fcffa2b83aead36e99ba2546ece9
- Audience: Maintainers of the C and C++ ports
- Scope: Correctness, memory safety, semantic parity, and API quality of the C and C++ workspaces; fixes applied; deferred follow-ups

> **Current-state note (verified 2026-07-12): all deferred follow-ups are resolved.** The original
> backlog below is a historical snapshot of HEAD `29ea8e1`; its seven groups were subsequently
> implemented and validated as recorded in the [resolution addendum](#resolution-addendum--2026-07-10).
> Representative implementation evidence is the C measured-search and facade work in
> [`fingertree.c`](../../src/C/FingerTree/src/fingertree.c) (`e374c12`, `ca5041d`) and the C++
> iterator/cost work in [`reversible_deque.hpp`](../../src/Cpp/FingerTree/include/tools/data_structures/finger_tree/reversible_deque.hpp)
> (`29f1130`, `6120df1`). None of this report's “deferred” labels denotes current pending work.

## Summary

The companion to the [C#/Rust review](csharp-rust-implementation-review-2026-07-09.md), run the
same day with the same method: eight parallel review passes over the C workspaces (HAMT, Tungsten,
FingerTree core, FingerTree facades) and C++ workspaces (HAMT, FingerTree detail engines,
FingerTree facades, Tungsten), each traced against the C# semantic reference (and, for the C
FingerTree, its C++ ancestor), findings manually re-verified against the source before any change.
All six workspace test suites were green before the review and green after every commit.

Headline results:

- **The cores are sound.** The C FingerTree's reference-counted lazy-middle publication protocol
  (CAS winner publishes, loser releases its unpublished rep and adopts), the C++ FingerTree's
  `std::atomic<std::shared_ptr>` analogue of the C# benign-race protocol, both HAMT tries, and the
  C Tungsten AVL refcount discipline were each traced path-by-path — no leak, double-free,
  use-after-free, or torn publication was found in normal operation.
- **The real memory-safety bugs lived on error paths and policy boundaries:** the C Tungsten
  Append/Prepend/Insert error paths destroyed an uninitialized `tds_hamt_map` and double-released
  an AVL root; the C HAMT silently stored NULL from failed allocating retain callbacks (exactly how
  the C Tungsten policy drives it) and leaked the whole previous version on aliased in-place
  updates; C++ HAMT iterators dangled when obtained from temporaries.
- **One cross-port parity bug recurred:** the interval-tree equal-low tie order (new intervals
  must precede existing equal-low ones, per C#) was wrong in the C port — the same defect class
  found and fixed in Rust earlier the same day — and the C++ port had the text-rope `offset_of`
  column-validation bug found in C# and Rust.
- **The C FingerTree's largest issue is architectural, not behavioral:** the generic tree's
  O(log n) split/locate descent was never ported (split is a pop-and-reappend loop; locate scans
  every leaf), so the sorted containers, priority queue, interval trees, and ropes built on it are
  O(n) where every sibling port is O(log n). This is now documented in the workspace's api-notes
  and is the top deferred item.

## Fix commits

| Commit | Area | Content |
| --- | --- | --- |
| `2c00468` | C++ Hamt | Root-owning iterators, entry storage without per-step copies, MSVC `no_unique_address`, kind-tag dispatch, `collision_node::create` precondition |
| `365802c` | C++ Tungsten | Order-of-evaluation hazard in `append`, stamp-aware `slot_equal`, `add_range` rvalue fast path, missing include |
| `d0a40b3` | C++ FingerTree | `offset_of` line-end validation, deque iterator equality under sharing, deferred middle measure in cons/snoc, exception-message attribution, null-storage chunk view |
| `8f7dbc6` | C Hamt | Retain-failure propagation, aliasing safety, 32-bit pointer-hash UB, status-based set predicates, out-param initialization |
| `ae4afba` | C Tungsten | Error-path double-free/uninitialized destroy, rule-2 terminal no-op fast path, AVL split join, stamp-gap UB, status consistency, `key_take` NULL guard, join policy check |
| `29ea8e1` | C FingerTree | Interval equal-low tie order + run-scanning membership, overlap-scan early exit, O(1) priority peek, `remove_at` status, Windows atomics (acquire loads, 64-bit refcounts), api-notes complexity-gap record |

## Findings fixed

### High severity

**H-1. C Tungsten error-path double-free and wild destroy** (`src/C/Tungsten/src/tungsten.c`).
`append`/`prepend`/`insert_at` declared an uninitialized `tds_hamt_map` and ran
`tds_tungsten_node_release(root); tds_hamt_map_destroy(&index);` unconditionally, but the
remove-existing helper's failure paths (stamp mismatch, delete OOM, HAMT-remove OOM) returned
without writing the map — destroying garbage stack memory — and the HAMT-remove failure path
released the freshly built post-delete AVL root without nulling it, so the caller released it a
second time (double free / use-after-free). Fixed by nulling after release and zero-initializing
the maps.

**H-2. C HAMT stored NULL from failed allocating retains** (`src/C/Hamt/src/hamt.c`). Allocating
retain callbacks (the C Tungsten policy is one) report failure by returning NULL; the HAMT stored
that NULL with `TDS_HAMT_OK`, and a later lookup passed the NULL key to the user hash callback or
memcpy'd from a NULL value — a crash long after the failed operation. Every retain site now maps a
NULL result for a non-NULL input to `TDS_HAMT_OUT_OF_MEMORY` and unwinds cleanly (the
api-specification paragraph that documented NULL-as-payload was rewritten to the new contract).

**H-3. C HAMT aliased in-place updates leaked the previous version** (`src/C/Hamt/src/hamt.c`).
`tds_hamt_map_set(&map, k, v, &map)` — the natural in-place idiom for a value-struct API —
compiled and worked but overwrote the source's root reference without releasing it, leaking the
entire previous version (and all policy-owned payloads) on every call. All map and set mutators
now release the overwritten version's root when the result aliases the source; the behavior is
documented, and a new counting retain/release policy test locks refcount balance across an aliased
mixed history with a retained snapshot — the test class whose absence let this and H-2 survive.

**H-4. C++ HAMT iterators and lookup pointers dangled on temporaries**
(`src/Cpp/Hamt/include/.../persistent_hash_map.hpp`). `begin()` captured a raw node pointer with
nothing retaining the trie, so `auto it = make_map().begin();` was a use-after-free. Iterators now
hold the root `node_ptr` (regression-tested with an iterator that outlives its source), and the
`try_get`/`try_get_key` lifetime contract is documented in the header and api-specification.

**H-5. C++ text-rope `offset_of` accepted columns past the line end**
(`src/Cpp/FingerTree/include/.../rope_text.hpp`), silently returning an offset on a *later line*
(`offset_of("ab\ncd", 0, 4)` → 4, which `line_column_of` round-trips to {1,1}) — the same bug
fixed in C# and Rust in the companion review. Now validates against the line end (a column equal
to the line length addresses the terminating newline); the existing negative test only used a
column past the whole rope, so new tests pin both the boundary and the rejection.

### Medium severity

- **C FingerTree interval tie order** (both facades): intervals were ordered
  `(low, high)`-lexicographically with new duplicates after existing equals; C#/C++/Rust place new
  equal-low intervals first. Insert now uses a low-only lower bound, and `contains`/`remove_one`
  scan the equal-low run (a `(low, high)` binary search is invalid under the correct ordering).
  Regression test replays an equal-low insertion script against `_at`.
- **C Tungsten missing rule-2 fast path**: Append/Prepend of a key already terminal with an equal
  value removed and re-inserted it, replacing the stored key payload with the supplied
  comparer-equal one (observably diverging from the test-locked C# behavior) and consuming a
  stamp. Both now return a copy of the receiver; regression-tested with the mod-10 policy.
- **C Tungsten AVL split under-rebalancing**: split rejoined subtrees of arbitrary height
  difference with a single `tree_balance` (which only restores a difference ≤ 2), degrading
  sliced trees below AVL shape (results stayed correct; the height bound didn't). Split now routes
  through a new `tree_join` that descends the taller side like `tree_concat`.
- **C++ deque iterator equality compared leaf addresses**; two distinct positions can reference
  the same shared leaf (concat of a deque with itself), comparing equal. Now compares
  (exhausted, position).
- **C++ measured tree forced the middle's measure eagerly on cons/snoc overflow** where C# defers
  it until a measure is queried ("constructing a deep node never forces its middle") — user
  combine chains and their exceptions ran on pushes that never observe a measure. The read now
  happens inside the suspension's probe.
- **C++ HAMT iteration deep-copied every key and value per step** (heap allocation per step for
  string payloads, and a throwing copy mid-`++` left a dereferenceable-looking iterator with no
  element). Nodes now store entries as `value_type` pairs and the iterator yields stable
  references; advancement is noexcept and the docs' "no heap allocation while traversing" claim is
  true again.
- **C HAMT 32-bit pointer-hash UB**: the default policy ran `uintptr_t >> 33` (UB on 32-bit
  targets, where the 64-bit Murmur3 constants also truncated); now widens to `uint64_t` first.
- **C HAMT set predicates hid OOM as `false`**: the six relation predicates returned bool and
  answered `false` when building their internal probe set failed — indistinguishable from a
  genuine negative. Converted to the library-wide status + `bool *result` convention (header,
  tests, docs updated); proper-subset and equals no longer build the probe twice.
- **`[[no_unique_address]]` was silently inert on plain MSVC** (the primary toolchain): the macro
  selected the `msvc::` spelling only for clang-cl. Every map/set value carried padding for the
  three empty policy members. One-line condition fix.
- **C FingerTree Windows atomics**: `ft_atomic_ptr_load` was a locked
  `InterlockedCompareExchangePointer`, so every read of an already-published cached measure or
  forced middle dirtied the cache line — serializing the advertised concurrent-reader scenario;
  now `ReadPointerAcquire`. The Windows refcount widened from `LONG` to `LONG64` to match the
  POSIX `atomic_size_t` width.

### Low severity (selected)

C++ Tungsten: `append` evaluated `entries.size()` in the same argument list that moved `entries`
(well-defined today only because the deque's move constructor is suppressed; hoisted);
`slot_equal` ignored the stamp while the HAMT short-circuits on slot equality (a stale-stamp trap
one call site away; now compares stamps); `add_range` with an rvalue `persistent_list` silently
lost the O(log min) join fast path (now dispatches). C++ FingerTree: misleading exception
messages ("insert index" for count/offset/split failures — the C++ analog of the C# `ParamName`
fixes; distinct helpers added); `rope_chunk::view()` dereferenced null storage on a
default-constructed chunk despite `noexcept` (now returns an empty span). C Tungsten: stamp-gap
subtraction in `int64_t` was UB for spans exceeding `INT64_MAX` (now unsigned, matching C#'s
unchecked cast); `entry_at` reported bad indexes as `EMPTY` (now `OUT_OF_RANGE`); inconsistent
`INVALID_ARGUMENT` vs `OUT_OF_RANGE` mapping across slicers (now consistent); `key_take` passed
NULL keys to the user hash callback (now skipped); `association_join` copied the right operand's
payload bytes under the left's sizes without a compatibility check (now validated). C HAMT:
`try_add`/`try_remove` out-params were unwritten on error paths (now initialized first);
`collision_create` carries the C#-mirroring precondition comment. C FingerTree: priority-queue
peek used an indexed descent instead of the O(1) front read; `ft_tree_remove_at` reported bad
indexes as `EMPTY`; interval overlap scans now stop once `low > query.high`; rope split locals
zero-initialized.

## Verified sound (traced in detail; no defects found)

- **C FingerTree lazy-middle concurrency**: the CAS publication transfers the initial reference to
  the slot; the losing forcer releases its unpublished rep (single owner — no double free) and
  adopts the winner's; the OOM path re-checks for a concurrent publication and does not poison the
  cell; measure publication follows the same pattern. Refcount discipline audited on every
  push/pop/concat/set/split path including OOM unwinds, with the partial-count constructor tricks
  verified. Rebalance carry bounds (buffer fixpoints ≤ 12/4 generic, 16/8 reversible) leave no
  overflow. The reversible deque's orientation machinery (mirror bits, logical children,
  split/concat through logical orientation) is correct.
- **C++ FingerTree engines**: line-level parity with the C# reference confirmed for deque
  cons/snoc overflow, pull-left/right chop rules, both node-chunking rules, split accumulator
  threading, measure memoization, and the reversible orientation XOR. `lazy_cell` /
  `measured_lazy_cell` / `atomic_box` are exact (seq_cst) analogues of the C# Volatile+CAS
  protocol with matching benign-race and exception-safety semantics. No UB, no data race, no
  dangling iterator leaf pointers, overflow-checked size arithmetic, O(log n) recursion depth.
- **C HAMT trie mechanics**: popcount slotting, shift-depth invariant (with the same unreachable
  guard as C#), collision add/replace/remove with the 2→1 collapse, single-non-branch-child
  canonicalization, no-op root sharing, last-wins bulk build with first-key retention, and set
  algebra semantics — all matching C# with balanced refcounts.
- **C++ HAMT semantics**: no divergence from C# found (hash truncation consistency, removal
  canonicalization, no-op sharing, original-key retention, set algebra including the
  sharing-preserving `except_with`/`symmetric_except_with`, count tracking, exception safety of
  updates giving the strong guarantee).
- **C Tungsten ordering rules**: rules 1 and 3–8 verified faithful (rule 2's fast path was the M
  fix above); stamp arithmetic including overflow guards and the relabel path; slice index
  reconciliation; sort stability via merge sort with stamp tiebreak.
- **C++ Tungsten**: all eight ordering rules faithful, including the terminal no-op fast path;
  `std::midpoint` matches C#'s floor midpoint with no signed-overflow UB; `rebuilt` adds an
  overflow guard C# doesn't need. No active correctness bug.

## Cross-port parity status

- **HAMT**: C and C++ are at semantic parity with C#. The C port's callback-policy nuances
  (key-pointer retention under copying retains, removed-value lifetime) are now documented rather
  than implied.
- **Tungsten**: C and C++ are at rule-level parity with C#, including (after these fixes) the
  rule-2 fast path in C. The C++ port's exception-type mapping and eager `keys()`/`values()`
  vectors are documented conventions.
- **FingerTree**: the C++ port is a faithful engine-level translation with C#-equivalent
  amortized bounds (its strict reversible engine matches the C# reversible engine's documented
  strictness). The C port is behaviorally faithful where surface exists but carries the
  documented complexity checkpoint: O(n) split/locate in the generic tree (facades degrade
  accordingly), unmerged rope chunks, an unmeasured text rope missing
  `line_of_offset`/`line_start_offset`/`offset_of`, no priority-queue meld or O(1) peek-priority
  read, and no max-high interval annotation. These are recorded in
  `src/C/FingerTree/docs/api-notes.md` under "Known Complexity Gaps".

## Deferred follow-ups

Ranked; none are behavioral bugs in shipped operation results.

1. **Port the O(log n) split/locate descent to the C generic tree** (the reversible deque's
   `ft_rev_rep_split_tree` is the in-file template). This single item restores the documented
   complexity of every C facade: sorted containers, priority queue, interval trees, ropes.
   Scope: one C file, core machinery plus facade re-verification.
2. **C interval trees: max-high product measure** for O(log n) first-overlap / O(k log n)
   enumeration (the early-exit stopgap shipped in `29ea8e1` bounds scans by the query window).
3. **C rope chunk coalescing** (insert into the hit chunk below max size; merge undersized
   neighbors on remove/split/concat) and **rebasing `ft_text_rope` on the measured rope** with a
   newline measure plus the missing line-navigation API (including the column-validated
   `offset_of` all sibling ports now share).
4. **C++ HAMT/deque cost polish**: `kind`-tag dispatch shipped, but `sorted_set` algebra fast
   paths, by-value rank accessors in the sorted facades, and the `deep_computed` triple
   allocation noted by the engine review remain.
5. **C concurrency documentation**: non-atomic refcount derivation from a shared snapshot is safe
   for concurrent *readers* only; deriving new versions concurrently from one snapshot races the
   refcounts. Document the "concurrent readers; derive single-threaded per lineage" rule in the C
   workspaces (or move to C11 atomics across Hamt/FingerTree/Tungsten — FingerTree already is).
6. **Test-coverage gaps worth closing**: C HAMT allocation-failure injection over the node_set /
   merge paths and a depth-7 iterator traversal; C++ sorted-map comparator-equal key replacement
   assertion; C++ Tungsten exception-path and `get_range`/`key_take`/`sort` history coverage;
   C priority-queue equal-priority FIFO depth beyond one pair.
7. **Minor**: C++ reversible-tree unbounded digit loops could throw `logic_error` on fall-through
   like the deque engine (C# parity keeps them unbounded, but the C++ failure mode is UB);
   `std::atomic<std::shared_ptr>` is not lock-free on MSVC/libstdc++ (contended forcing
   serializes harder than C#'s Interlocked — worth a docs line if concurrent forcing throughput
   ever matters).

## Test evidence

- C workspaces: `build.ps1 -Workspace Hamt -RunTests` — 21 tests (was 20; new counting-policy
  balance test); `-Workspace Tungsten` — passes (new rule-2 no-op assertions); `-Workspace
  FingerTree` — 3/3 suites pass, core suite 1.8s (down from 4.7s baseline via the overlap early
  exits), new equal-low tie-order test.
- C++ workspaces: `build.ps1 -Workspace Hamt -RunTests` — 26 tests (was 25; new
  iterator-outlives-map test); `-Workspace Tungsten` — passes; `-Workspace FingerTree` —
  fingertree.smoke passes with new `offset_of` and iterator-equality tests.
- The C#, Rust, Haskell, and Kotlin workspaces were not modified by this review.

## Resolution addendum — 2026-07-10

This dated addendum preserves the original review above while recording the subsequent remediation.
**All seven deferred follow-up groups are resolved.**

1. **C structural search.** The generic measured tree now performs count/measure-guided descent for split and
   locate, reconstructing only the boundary path. Deep boundary sweeps and 4,096-element operation ceilings guard
   the restored O(log n) behavior.
2. **C interval, rope, and text facades.** Interval nodes cache maximum-high annotations; first-overlap and
   enumeration prune unreachable subtrees. Positional/measured ropes edit and coalesce bounded boundary chunks.
   `ft_text_rope` now uses newline-measured storage with line count, bidirectional line/offset navigation, and
   column-validated `offset_of`.
3. **C++ cost polish.** Computed lazy cells avoid the redundant allocation; sorted rank access returns canonical
   stored references; compatible sorted-set algebra has persistent fast paths; incompatible comparator state is
   normalized under receiver semantics.
4. **C concurrency contracts.** HAMT and Tungsten explicitly require lineage derivation to be single-threaded or
   externally synchronized because their reference counts are non-atomic. FingerTree documents its stronger
   atomic-handle contract and callback thread-safety obligations.
5. **Deferred coverage.** C HAMT now sweeps allocation failure through node-set/merge paths and exercises a real
   seven-frame iterator. C priority queues lock in 128-item equal-priority FIFO order. C++ sorted-map replacement
   preserves the incoming comparator-equivalent key, and C++ Tungsten covers invalid input, exception safety,
   ranges, `key_take`, and stable sort histories.
6. **Minor native hardening.** Reversible-tree impossible digit fall-throughs throw `logic_error` instead of
   reaching undefined behavior. Active C++ docs and samples describe `atomic<shared_ptr>` publication as atomic
   and data-race-safe, with no lock-free progress guarantee because implementations may serialize internally.
7. **Production validation and usability.** C++ FingerTree now has grouped replayable command models, streaming
   traversal, semantic result equality, deterministic samples, scale-sensitive benchmarks, install/export plus a
   relocated consumer, portable presets, static analysis, and MSVC/GCC/Clang plus ASan/UBSan/TSan CI lanes. All
   Windows native test entry points inherit no-dialog error mode, including pre-`main` failures.

Replacement local evidence:

- C FingerTree: MSVC Debug/Release, GCC, and Clang — 3/3 CTests in each validated lane.
- C HAMT: MSVC Debug/Release, GCC, and Clang — 23/23 tests in each validated lane; downstream C Tungsten 1/1.
- C++ FingerTree: MSVC Debug and Release — 18/18 CTests in each configuration; full short benchmark suite and
  aggregate-header Clang analyzer pass. Retained branching measured 4.00 allocations / 472 bytes per update at
  100, 10,000, and 1,000,000 elements.
- C++ HAMT and Tungsten: their deferred regression suites pass under the repository build wrappers.
