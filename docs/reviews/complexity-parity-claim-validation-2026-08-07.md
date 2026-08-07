# Complexity-Parity Claim Validation — 2026-08-07

- Created (UTC): 2026-08-07
- Repository HEAD (reviewed): `d5d6835`
- Audience: Maintainers deciding what the complexity-parity campaign still owes
- Scope: An independent test of the campaign's closing claim — that every asymptotic divergence
  across the nine workspaces was fixed except two ruled exceptions — and a work order for what
  survives. Supersedes nothing; it audits
  [the census](complexity-parity-census-2026-08-05.md) and
  [the retrospective](complexity-parity-retrospective-2026-08-06.md) rather than replacing them.
- Method: code-first audit of all nine workspaces by structure family, then two rounds of
  adversarial re-verification in which every surviving finding had to be backed by an executed
  measurement rather than an argument. 108 agent-hours across two fan-outs, plus direct
  measurement of the load-bearing claims.

## Verdict

**The claim is false.** Not because the campaign failed to do what it documented — it did, and its
headline evidence reproduces under independent measurement — but because the census it rested on
surveyed substrates and suspected structures rather than the library, and because a defect of the
exact class the campaign existed to eliminate survives in all five cores it rebuilt.

The two documented exceptions are not the only ones. Roughly a hundred additional asymptotic
divergences survive at HEAD, of which **64 have been confirmed by executed experiment**.

## What is genuinely true

Independently verified, not taken on trust:

1. **All nine measured cores are real Hinze–Paterson finger trees.** Empty/Single/Deep with 1–4
   item digits at both ends and 2–3 nodes below. No join-tree impostor survives in any workspace's
   *measured core*. The Class-B rebuild landed structurally.
2. **The probe fingerprint is real.** Re-measured against Rust with a counting measure policy:
   512 endpoint appends cost 342 combines at n=1,024 *and* n=32,768; a 4,096-element concat costs
   0 combines against a 1-element operand and 2 against a 1,024-element one; a 10,000-append spine
   branched into 49 retained versions costs 3,304 combines on first force and **0** on all 48
   others; `front`/`back`/`len` cost 0. A separate agent re-measured Python and reproduced the
   census's exact published numbers (340 / 0 / 4 / 3,336 then 5 per branch).
3. **Every struck census row (A1–A9, B, C1, C2, D1, E) has its claimed mechanism present at HEAD**
   and delivering the stated bound, including the arena documentation ruling in all seven arena
   workspaces and the injective vertex hash in Rust and Haskell.
4. **Push-side suspension memoization publishes into the shared cell in all nine workspaces.** The
   "memoize into a local copy" trap was specifically looked for and is not there.

So the campaign's own work order was executed faithfully. The problem is its scope.

## The five structural failures

### 1. Endpoint *pop* was never deferred in any of the five rebuilt cores

The campaign implemented half of the Hinze–Paterson suspension discipline. Push overflow is
suspended and memoized into the shared cell. The endpoint view is not: `view_left` forces the
middle and then **recurses**, wrapping the result in a freshly-forced cell reachable only from the
version just returned.

- Rust `src/Rust/FingerTree/src/measured.rs:863-899` — `Susp::ready(rest)`
- Python `src/Python/src/durable7/finger_tree/measured_sequence.py:351-370` — same shape; `_Susp`
  defers only `PUSH_FRONT`/`PUSH_BACK`/`CONCAT`/`REVERSE`. There is no pop opcode.
- Kotlin, OCaml and TypeScript carry the identical shape.

C# does not: `Internal/Measured/MeasuredTreeLevels.cs:284-310` builds a `PendingMeasuredPopFront`
and publishes it with `Interlocked.CompareExchange`, under the comment *"deferring the pop so it is
paid once even under branching."* No recursion.

Python claims precisely the bound it fails (`measured_sequence.py:9-15`): *"the endpoint views are
O(1) amortized… The amortized bounds hold under fully persistent branching histories, not merely
ephemeral linear use, because a forced suspension is memoized in a cell shared by every version
that references it: work deferred by one version and forced by another is never repeated."*

Measured — replay the pop on one retained, already-warmed version:

| n | Python frames/call | Python µs/call | Rust allocs/call |
|---|---|---|---|
| 2^8 | 3.00 | 22.8 | 39.00 |
| 2^12 | 6.00 | 56.9 | 51.00 |
| 2^16 | 8.00 | 80.4 | 59.00 |
| 2^18 | 10.00 | 96.0 | 64.00 |

Exactly integral, exactly flat over 20,000 replays: zero amortization, cost tracking log n. The
ephemeral drain *does* amortize (0.469 / 0.497 / 0.500 frames per pop) — which is why every
behavioural test passes. C# `TryViewLeft` replayed 200,000 times on the worst retained version is
flat at 94.7 / 75.2 / 38.1 ns across the same size range.

This is the persistence-defeats-amortization failure the retrospective's own background section
calls *"the correctness condition of the amortization argument."* It was invisible to the campaign
because **the probe fingerprint has no pop probe** — it pins pushes, concat, front/back and
cross-branch memoization, and nothing else.

### 2. OCaml has at least six more placeholder structures than the census found

The census named six OCaml placeholders and declared the class closed. These were never opened:

| module | representation at HEAD | measured |
|---|---|---|
| `finger_tree/priority_queue.ml:6-10` | `entries : entry array` + cached best index; `enqueue` is `Array.append` | Θ(n) per enqueue, Θ(n²) to build: 0.007 / 0.119 / 2.03 / 44.6 s at n = 1K / 4K / 16K / 64K, with exactly n−1 comparator calls — the cost is the copy |
| `finger_tree/interval_tree.ml:8-12` | `intervals : interval array` and **one** global `cached_maximum_high` | Θ(n) queries and writes |
| `finger_tree/persistent_interval_map.ml:10` | `entries : entry array` | Θ(n) queries and writes |
| `finger_tree/persistent_chunked_bit_set.ml:6-12` | `module Bit_set = Set.Make (Int)` — no words, no chunks, no popcounts | `count` is Θ(n): 70 µs → 129,312 µs from n=1,000 to 512,000, where four siblings read a cached root measure in O(1) |
| `hamt/merkle_search_tree.ml:210-247` | `set`/`remove` flatten every entry, splice a list, `build` the whole tree and re-hash every block | Θ(n) per single-key edit: 8.6 / 33.6 / 159.5 ms at n = 500 / 2,000 / 8,000 while height moves only 2→4 |
| `finger_tree/daba_lite.ml:37-48` | two-list queue; `try_evict` calls `to_list` then folds the entire remaining window | exactly n−1 combines per eviction: 100 / 400 / 1,600 / 6,400 combines/step at window n |

Most carry headers describing the structure they do not have. `daba_lite.ml:3-4` says the aggregate
is *"maintained under insertion and eviction without ever recomputing it from scratch, so no single
operation pays for the whole window"* — directly contradicted by the fold three lines of code away,
in a structure whose entire purpose is worst-case O(1). `persistent_chunked_bit_set.ml:3-4` claims
*"cached population counts turn rank and select into a descent."* `priority_queue.mli:4-6` asserts
a *"measure-directed descent"* over a measured tree that does not exist.

`text_rope.ml` is the same pattern and the sharpest instance: `type t = Uchar.t Rope.t` with **no
newline measure at all**, `line_column` materializing the whole rope via `Rope.to_list`, under a
header reading *"Newline counts are cached in the measure, which is what makes converting between a
character offset and a line/column position logarithmic rather than a scan."* Measured at index 0 —
the cheapest possible query — 14 µs → 22,902 µs from n=1,000 to 256,000. C# answers it in O(log n)
(`RopeText.cs:109,139`), and OCaml already ships the right substrate one module away in
`measured_rope.ml`.

This is the campaign's *founding* defect pattern: a doc that is not conservative but false.

### 3. A defect shape found in one workspace was declared closed library-wide

Census row A7 identified Python's ordered-map position lookup as *"a binary search whose every
probe is an O(log n) tree descent,"* fixed it, copied the fix to OCaml, and struck the row. The
identical shape survives untouched in:

- Rust `src/Rust/Ordered/src/ordered_map.rs:222-234` — `index_of_stamp` binary-searches with
  `self.order.get(middle)`, where `order` is `PersistentDeque` (the AVL tree). The `get` method one
  line above at `:203` is documented **"O(1) expected."**
- C `src/C/Ordered/src/ordered_set.c:607-627` — `d7_ordered_locate_stamp`, same shape.

Measured in Rust against a genuine expected-O(1) CHAMP lookup on the same map:

| n | `map.get` | `contains_key` (CHAMP) | `deque.get` (one descent) |
|---|---|---|---|
| 2^12 | 6,862 ns | 159 ns | 648 ns |
| 2^16 | 14,979 ns | 450 ns | 1,420 ns |
| 2^20 | 76,735 ns | 2,018 ns | 11,522 ns |

`map.get` costs ~10.6× a single positional descent at both 2^12 and 2^16 — i.e. it performs about
log₂ n descents. The reference reads the value straight out of the hash index
(`PersistentOrderedMap.cs:140-149`) and never consults the order sequence. The single-descent
primitive already exists unused in the same Rust workspace at `deque.rs:1262`.

### 4. Two sequence types were never rebased onto the rebuilt cores

- **Rust `PersistentDeque` / `ReversibleDeque`** (`src/Rust/FingerTree/src/deque.rs`) is a separate
  `enum DequeTree` with `height: u8` and AVL rotations; the module header calls it *"a
  size-annotated binary tree."* `push_front` is `DequeTree::concat(leaf, root)` (`:650`) — literally
  the "join against a singleton rebuilds the spine" pattern the retrospective's table says was
  eliminated. Measured with a counting global allocator, 20,000 replays on one retained version:
  `PersistentDeque::push_front` costs 5 / 9 / 13 / 17 / 21 allocations at n = 2^4 … 2^20 — exactly
  log₂(n)+1 — while `FingerTree::prepend` in the same binary is **4.00 at all five sizes**.
  `git log` shows `deque.rs` was last touched by a documentation commit, not by Wave 1.
- **Kotlin `ReversibleDeque`** (`Core.kt:353-588`) is a private Empty/Leaf/Concat/Reversed hierarchy
  with `height = max(left, right) + 1`. Measured with `getThreadAllocatedBytes` over a 65,536× size
  range: `prepend` 337 → 792 bytes/op (grows with height) while Kotlin's own finger-tree
  `PersistentDeque.prepend` is **flat at 143.9 bytes** — the in-workspace control.
  `concat(singleton)` grows +128 bytes per +4 levels of the *larger* operand.
- **Kotlin `PersistentDeque.reverse()`** (`Core.kt:321`) is `from(toList().asReversed())` — Θ(n),
  where Wave 1 gave Python, TypeScript and OCaml an O(1) structural mirror.

### 5. An equalize-upward obligation was missed in the row that had the evidence in hand

Census A6 rebuilt Haskell's sorted set and celebrated preserving the hedge bound
O(m log(n/m + 1)). It did not notice that this makes Haskell **stronger than the reference**, which
should have fired the charter's own "a stronger guarantee anywhere becomes the target everywhere"
rule. Measured comparer calls for `union(big, {one element above max})`:

| n | C# `SortedSet.Union` | Haskell `unionTrees` |
|---|---|---|
| 1,024 | 1,024 | **2** |
| 16,384 | 16,384 | **2** |
| 262,144 | 262,144 | **2** |

C# is documented honestly at O(n+m) (`SortedSet.cs:258-280`), so this is under-delivery against an
accurate contract rather than a false doc — but `docs/reference/data-structure-catalog.md:250`
states the whole family was kept in the hedge class, which is false for C#, Rust, and C++ on
overlapping operands. C#'s merge also appends into a fresh tree, discarding all structural sharing.

## Scale and confidence

| tier | count | basis |
|---|---|---|
| A — verified by me personally with executed probes | ~10 | the items quoted above |
| B — upheld in adversarial retest, every one backed by an executed experiment | 64 real parity gaps (+3 reclassified as doc-only or a shared ceiling) | 68 retested of 74 candidates; 67 upheld, 1 overturned |
| C — found in a second sweep, verified by the finding agent but not independently re-tested | 41 | recovered from the 48 candidates a cap dropped in the first pass |
| D — not retested (an agent died on an API error) | 6 | ids 4, 28, 32, 33, 56, 64 |

Tier C is heavily weighted toward two recurring shapes worth calling out because they cut across
workspaces: **interval cursors** that materialize or linearly scan the whole collection to reach a
rank (C++, Haskell, Kotlin, Python, Rust, TypeScript), and **Merkle sync/proof paths** that build a
whole-tree block map or omit an equal-root short-circuit (Haskell, OCaml, Python, TypeScript).

## One finding the re-verification pass demoted

Haskell's `ContextualRankSequence.toList` was first raised as a Θ(s·n)-versus-O(n) asymptotic gap.
The re-verification reproduced the mechanism (`Deep !v` is strict at `Measured.hs:87`, so every
`viewL` rebuild forces two width-`s` summary appends) and reproduced the measurement, but placed it
better: cost is **flat in n** — 14.4 → 16.1 µs per element across a 16× range in n — and linear only
in `s`, at 9.7 / 14.1 / 42.4 / 253.7 / 1400.3 µs per element for s = 2 / 8 / 32 / 128 / 512.

Since the library states bounds in `s` throughout, and the other eight ports promise enumeration at
O(n) with no `s` factor, this remains a real divergence in the library's own parameter space. But it
is honestly documented in its own haddock, and for any fixed machine it is a constant factor in `n`.
It is recorded at low severity, and is the one candidate of 68 that adversarial re-verification
overturned.

## Why these were missed

These are systematic causes, not bad luck:

1. **The census surveyed substrates, plus the structures it already suspected.** Whole modules were
   never opened — OCaml's priority queue, interval tree, interval map, chunked bit set, Merkle tree,
   text rope and DABA Lite among them. Six of those are placeholders; the census's own list of
   OCaml placeholders had six entries and was treated as complete.
2. **A per-workspace fix was generalized to a class without checking the other eight.** A7 in
   Python and OCaml; the delta-map extremes documentation in Haskell. In both cases the identical
   shape sits in workspaces the campaign never grepped.
3. **The evidence discipline measured what was rebuilt, not what was claimed.** The probe
   fingerprint is genuinely excellent for pushes, concat, extremes and cross-branch memoization —
   and has no pop probe, no cursor probe, and no set-algebra probe. The retrospective's own Lesson 1
   warns that "a probe that has never been seen to fail proves nothing"; the corollary is that an
   operation with no probe at all is unconstrained.
4. **Code landed after the claim.** `be7d186` added ~4,300 lines of new TypeScript modules *after*
   `ac107dc`/`5b64932` declared "All nine workspaces conform," and `d5d6835` then had to fix two
   stale divergence docstrings in OCaml and Python that the retrospective had already declared
   flipped.

## On the exception count

The census's "Deliberate non-violations" section lists four bullets: two are rulings (OCaml
rank-select, the arena's Θ(√M) spike) and two are declared out of scope (rope chunking as a constant
factor, DABA Lite coverage). Reading that as "two exceptions" is fair.

One further class is genuinely language-forced and unrecorded: `DabaLite::clear` is O(n + c) in
Rust, C and C++ because they destroy retired slots deterministically, against O(1) under a tracing
GC. All three document it; none appears in the exceptions list. That one deserves to be recorded
rather than fixed.

## Suggested remediation order

1. Add a **pop probe** to all nine core suites — replay an endpoint view on one retained version and
   assert the per-call cost is independent of n. Then defer the pop in the five rebuilt cores, the
   way C# already does. This is the only finding that touches the library's load-bearing substrate.
2. Rebase Rust's `PersistentDeque`/`ReversibleDeque` and Kotlin's `ReversibleDeque` onto their
   workspaces' finger trees; give Kotlin's deque the O(1) reversed view.
3. Treat OCaml's remaining placeholders as a Wave 2c: priority queue, interval tree, interval map,
   chunked bit set, Merkle search tree, text rope, DABA Lite. Fix the headers that describe absent
   structure first — they are actively misleading today.
4. Grep all nine workspaces for the A7 shape (a binary search whose probe is an indexed access) and
   for cursor seeks that reach a rank by iteration.
5. Decide the equalize-upward question on sorted-set algebra: either adopt Haskell's hedge merge
   across the family or record it as a third ruling and correct
   `docs/reference/data-structure-catalog.md:250`.

## Appendix — the surviving work order

Confidence tiers: **B** = upheld under adversarial re-verification, every row backed by an executed
experiment. **C** = found in a second sweep and verified by its finding agent, not independently
re-tested. **D** = not re-tested (an agent died on a transport error). A `*` marks a row upheld as
real but reclassified as documentation-only or a shared design ceiling rather than a divergence
between workspaces. Severity is the re-verifier's, not the finder's.

| # | Tier | Sev | Workspace | Structure | Operation | Delivered | Achievable | Site |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | B | high | C | PersistentIntervalMap | visit_overlaps (enumerate all overlapping ent… | Theta(n) per overlap enumeration, independent of the number o… | O(k log n): the pruned repeated-split walk already im… | `src/C/FingerTree/src/persistent_interval_map.c:647` |
| 2 | B | high | C | PersistentIntervalMap cursor | find_overlap_cursor / find_containing_cursor … | Theta(n) rank descents at O(log n) each = Theta(n log n); mea… | O(log n): split off the first `start` entries, then o… | `src/C/FingerTree/src/persistent_interval_map.c:875` |
| 3 | B | high | C | PersistentOrderedSet (and PersistentO… | d7_ordered_set_index_of / d7_ordered_set_try_… | Theta(log^2 n) per keyed position lookup, keyed removal and e… | O(log n) — one measure-directed descent. The C Finger… | `src/C/Ordered/src/ordered_set.c:607` |
| 4 | B | high | C#, C++, Rust (and Python/TypeScript/Kotlin via iterate-and-insert) | SortedSet (order-statistic finger-tre… | Union / Intersect / Except / SymmetricExcept | C# and Rust: Theta(n+m) unconditionally (only empty-operand f… | O(m log(n/m + 1)) for m ≤ n — O(log n) for two disjoi… | `src/CSharp/src/Durable7.FingerTree/SortedSet.cs:412` |
| 5 | B | high | C#, C, C++, Haskell, Kotlin, Python, Rust, TypeScript (all but OCaml) | PersistentOrderedSet | Union / union_with / union_many | Theta(n + m), with total loss of structural sharing with the … | O(m log n) — append the argument-only classes to the … | `src/CSharp/src/Durable7.Ordered/PersistentOrderedSet.Algebra.cs:238` |
| 6 | B | high | C++ | sorted_set | union_with / intersect / except / symmetric_e… | Θ(n+m) time with a from-scratch Θ(n+m) rebuild for union_with… | O(m log(n/m + 1)); `tree_.split(predicate)`, `try_vie… | `src/Cpp/FingerTree/include/durable7/finger_tree/sorted_set.hpp:381` |
| 7 | B | high | Kotlin | PersistentDeque (finger-tree deque) /… | reverse | Theta(n) time and Theta(n) fresh allocation, exactly as the f… | O(1) — a lazy structural mirror that flips the outer … | `src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt:321` |
| 8 | B | high | Kotlin | ReversibleDeque (orientation-aware pe… | prepend / append / concat / tryViewLeft / try… | prepend/append Theta(log n) worst case AND amortized (confirm… | O(1) amortized prepend/append and endpoint views, O(l… | `src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt:769` |
| 9 | B | high | Kotlin | SortedSet | intersect / except / symmetricExcept / isSubs… | Theta((n + m) log m) comparisons plus a Theta(m log m) operan… | O(m log(n/m + 1)) with no operand rebuild, using `spl… | `src/Kotlin/FingerTree/src/durable7/fingertree/Sorted.kt:349` |
| 10 | B | high | Kotlin | measured finger tree (`PersistentMeas… | endpoint view / pop (`viewLeftTree`, `viewRig… | Θ(log n) worst case per pop; Θ(log n) amortized when pops are… | O(1) amortized under persistent branching, as the thr… | `src/Kotlin/FingerTree/src/durable7/fingertree/PersistentMeasuredTree.kt:384` |
| 11 | B | high | Kotlin | reversible deque | prepend / append / tryViewLeft / tryViewRight | Theta(log n) per endpoint push/pop, worst case and amortized | O(1) amortized under persistent branching, O(log n) w… | `src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt:769` |
| 12 | B | high | Kotlin | reversible deque | concat (all four orientation pairings) | Theta(\|height(left) - height(right)\|), which is Theta(log n… | O(log min(n, m)) | `src/Kotlin/FingerTree/src/durable7/fingertree/Core.kt:780` |
| 13 | B | high | OCaml | ContextualRankSequence (measured fing… | cons / snoc (endpoint update) | Theta(s * log n) monoid appends per cons/snoc when the push i… | O(s) worst case: a `Push_front`/`Push_back` pending c… | `src/OCaml/lib/finger_tree/contextual_rank_sequence.ml:234` |
| 14 | B | high | OCaml | DabaLite | evict / try_evict (and the sliding-window ste… | Theta(n) time and exactly n-1 combine calls per eviction; The… | O(1) worst case per eviction with at most two `combin… | `src/OCaml/lib/finger_tree/daba_lite.ml:43` |
| 15 | B | high | OCaml | DabaLite | aggregate / evict (documented cost model) | Theta(n) combines on every eviction - one operation does pay … | Either the code becomes real DABA Lite (O(1) worst ca… | `src/OCaml/lib/finger_tree/daba_lite.mli:24` |
| 16 | B | high | OCaml | IntervalTree | find_overlap / find_all_overlaps / query_point | Θ(n) comparator calls for query_point, find_overlap and find_… | O(log n) for a single overlap/stabbing hit and O(k lo… | `src/OCaml/lib/finger_tree/interval_tree.ml:113` |
| 17 | B | high | OCaml | IntervalTree | insert / remove | Theta(n) worst case and average for both insert and remove (a… | O(log n) amortized insert and remove by splitting a m… | `src/OCaml/lib/finger_tree/interval_tree.ml:72` |
| 18 | B | high | OCaml | Merkle search tree | set / add / remove (and every Cursor edit tha… | Theta(n) SHA-256 re-hashing plus Theta(n) list work per singl… | O(log_16 n) expected: descend the block path, split/j… | `src/OCaml/lib/hamt/merkle_search_tree.ml:234` |
| 19 | B | high | OCaml | Merkle search tree | range ~minimum ~maximum | Exactly n+1 key comparisons for an EMPTY range and 2n+1 for a… | O(log_16 n + output): descend to the range's left bou… | `src/OCaml/lib/hamt/merkle_search_tree.ml:253` |
| 20 | B | high | OCaml | Merkle search tree — MSP2 proofs | create_proof / create_range_proof (and verify… | Theta(block_count) = Theta(n / 16) proof steps and Theta(n) g… | O(log_16 n) proof steps and O(log_16 n) generation: w… | `src/OCaml/lib/hamt/merkle_proof_merge.ml:75` |
| 21 | B | high | OCaml | Merkle search tree — three-way merge | Merkle_proof_merge.merge | Θ(n²) key comparisons. Measured: 0.0050 s at n=250, 0.0186 s … | O(n log n), dominated by the final canonical rebuild:… | `src/OCaml/lib/hamt/merkle_proof_merge.ml:227` |
| 22 | B | high | OCaml | PersistentChunkedBitSet | count / cardinality | Θ(n) in the number of set bits, on every call | O(1) — read a cached population count off the root me… | `src/OCaml/lib/finger_tree/persistent_chunked_bit_set.ml:12` |
| 23 | B | high | OCaml | PersistentChunkedBitSet | rank | Θ(n) in the number of set bits | O(log c) in the number of stored chunks: one measure-… | `src/OCaml/lib/finger_tree/persistent_chunked_bit_set.ml:43` |
| 24 | B | high | OCaml | PersistentChunkedBitSet | select (nth set bit by population rank) | Theta(n) in the number of set bits, paid twice (cardinal for … | O(log c): locate the first chunk whose cumulative pop… | `src/OCaml/lib/finger_tree/persistent_chunked_bit_set.ml:46` |
| 25 | B | high | OCaml | PersistentChunkedBitSet | select / next_set_bit / representation | select and rank are Theta(n) (full traversal, no early exit);… | the documented O(log c) descent — the doc side is the… | `src/OCaml/lib/finger_tree/persistent_chunked_bit_set.mli:52` |
| 26 | B | high | OCaml | PersistentHashMap (CHAMP/HAMT core) | diff / equal | diff: Θ(n+m) hashes and equality calls for ANY two roots incl… | O(divergence) — O(log32 n) for a one-entry divergence… | `src/OCaml/lib/hamt/persistent_hamt.ml:241` |
| 27 | B | high | OCaml | PersistentHashSet | union / inter / diff / symmetric_diff / subse… | Theta(m) rehash + path-copy for union/diff/symmetric_diff and… | O(divergence) when the operands share node ancestry —… | `src/OCaml/lib/hamt/persistent_hash_set.ml:23` |
| 28 | B | high | OCaml | PriorityQueue (stable measured min-pr… | enqueue | Theta(n) copy per enqueue (O(1) comparator calls, not Theta(n… | O(1) amortized (finger-tree/measured-sequence append) | `src/OCaml/lib/finger_tree/priority_queue.ml:32` |
| 29 | B | high | OCaml | PriorityQueue (stable measured min-pr… | meld | Exactly n+m comparator calls and an Array.append copy of both… | O(log min(n,m)) amortized with O(1) comparator calls … | `src/OCaml/lib/finger_tree/priority_queue.ml:45` |
| 30 | B | high | OCaml | PriorityQueue (stable measured min-pr… | dequeue / delete-min | Theta(n) time, Theta(n) allocation and exactly n-2 comparator… | O(log n) - a measure-directed split to the cached min… | `src/OCaml/lib/finger_tree/priority_queue.ml:55` |
| 31 | B | high | OCaml | PriorityQueue (stable measured min-pr… | dequeue / representation contract | Removal is Theta(n); representation is a flat array | O(log n) measure-directed descent, as the doc states … | `src/OCaml/lib/finger_tree/priority_queue.mli:5` |
| 32 | B | high | OCaml | Sorted_set | intersect / difference / union | Theta(n log m) for intersect/difference where n = \|left\| (e… | O(m log(n/m + 1)) with run-adopting boundary splits; … | `src/OCaml/lib/finger_tree/sorted_set.ml:122` |
| 33 | B | high | OCaml | TextRope | line_column / offset→(line, column) | Theta(n) time and Theta(n) allocation per call, unconditional… | O(log n) — one prefix-measure descent over a rope who… | `src/OCaml/lib/finger_tree/text_rope.ml:46` |
| 34 | B | high | OCaml | TextRope | index_of_line_column / (line, column)→offset | Θ(n) time and Θ(n) allocation per call | O(log n) — locate the `line`-th newline by measure, t… | `src/OCaml/lib/finger_tree/text_rope.ml:58` |
| 35 | B | high | OCaml | TextRope | line_count | Θ(n) time and Θ(n) allocation per call — the whole rope is ma… | O(1) — root measure read (O(1) amortized where the sp… | `src/OCaml/lib/finger_tree/text_rope.ml:40` |
| 36 | B | high | OCaml | TextRope | line_count / line_column / index_of_line_colu… | Theta(n) for line_count, line_column and index_of_line_column… | the documented O(1) cached read and O(log n) conversi… | `src/OCaml/lib/finger_tree/text_rope.mli:44` |
| 37 | B | high | Python | Merkle search tree | diff | Exactly n comparer calls plus two full tuple() materializatio… | O(divergence · log n) for the common case: recurse on… | `src/Python/src/durable7/hamt/merkle_search_tree.py:395` |
| 38 | B | high | Python | PersistentHashMap / PersistentHashSet… | union / intersect / except_ / symmetric_excep… | Theta(n + m) hash and equality calls and Theta(m) path copies… | O(divergence) for operands sharing node ancestry (the… | `src/Python/src/durable7/hamt/persistent_hamt.py:1028` |
| 39 | B | high | Python | PersistentPatricia / PersistentIntMap… | union / intersect / except_ | Θ(m·w) probes and path rebuilds (w = key width) whatever the … | O(divergence) for operands sharing node ancestry — th… | `src/Python/src/durable7/hamt/persistent_patricia.py:266` |
| 40 | B | high | Python | SortedSet | intersect / symmetric_except / is_subset_of /… | Theta(m log m + n log m) with no structural sharing, driven b… | O(m log(n/m + 1)) by boundary splits over the same `M… | `src/Python/src/durable7/finger_tree/sorted.py:428` |
| 41 | B | high | Python | measured finger tree (MeasuredSequenc… | endpoint view / pop (view_left, view_right; t… | Θ(d) work per pop where d = number of consecutive singleton p… | O(1) amortized under fully persistent branching, O(1)… | `src/Python/src/durable7/finger_tree/measured_sequence.py:362` |
| 42 | B | high | Rust | PersistentDeque / ReversibleDeque (pe… | push_front / push_back / remove_first / remov… | push_front/push_back: Θ(log n) worst case AND amortized under… | O(1) amortized endpoint push/pop (valid under persist… | `src/Rust/FingerTree/src/deque.rs:650` |
| 43 | B | high | Rust | PersistentOrderedMap | get (keyed value lookup), index_of, set_item,… | Theta(log^2 n) for index_of/index_of_stamp, and Theta(log^2 n… | O(log n) via `PersistentDeque::sorted_lower_bound_by`… | `src/Rust/Ordered/src/ordered_map.rs:222` |
| 44 | B | high | Rust | SortedSet | union / intersect / except / symmetric_except | Theta(n+m) element clones and a from-scratch rebuild with zer… | O(m log(n/m + 1)) using the `front()`, `split`, `conc… | `src/Rust/FingerTree/src/sorted.rs:483` |
| 45 | B | high | Rust | measured finger tree | endpoint view / pop | Endpoint views: Theta(log n) worst case; O(1) amortized ONLY … | Either fix the code to defer the pop (as C#/C++/C do)… | `src/Rust/FingerTree/src/measured.rs:15` |
| 46 | B | high | Rust | measured finger tree (`FingerTree<T, … | endpoint view / pop (`view_left`, `view_right… | Θ(log n) worst case per pop and Θ(log n) amortized under pers… | O(1) amortized under persistent branching (Θ(N + log … | `src/Rust/FingerTree/src/measured.rs:891` |
| 47 | B | high | Rust | persistent deque / reversible deque | push_front / push_back / remove_first / remov… | Theta(log n) per endpoint push/pop, worst case and amortized … | O(1) amortized (valid under persistent branching) wit… | `src/Rust/FingerTree/src/deque.rs:650` |
| 48 | B | high | Rust | persistent deque / reversible deque | concat / insert_at / remove_at / add_range | Theta(\|height(left) - height(right)\|): exactly log2 n alloc… | O(log min(n, m)) amortized (the suspended-middle recu… | `src/Rust/FingerTree/src/deque.rs:233` |
| 49 | B | high | TypeScript | PersistentHashMap / PersistentHashSet… | union / intersect / except / symmetricExcept … | Θ(n+m) hash and equality calls regardless of sharing. Measure… | O(divergence) for operands sharing node ancestry, O(n… | `src/TypeScript/src/hamt/persistent-hamt.ts:916` |
| 50 | B | high | TypeScript | PersistentPatricia / PersistentIntMap… | union / intersect / except | union: Θ(m) full-depth leaf probes plus Θ(m·w) path-copy node… | O(divergence) for operands sharing node ancestry (O(1… | `src/TypeScript/src/hamt/persistent-patricia.ts:211` |
| 51 | B | high | TypeScript | PriorityQueue (stable measured min-pr… | meld | Theta(n + m) time, allocation and comparator calls, with zero… | O(log min(n,m)) amortized via MeasuredSequence.concat | `src/TypeScript/src/finger-tree/priority-interval.ts:58` |
| 52 | B | medium | C | PersistentDeltaMap | min_entry / max_entry (ft_delta_map_try_min_e… | Theta(log N) per min/max entry read - the finding's stated bo… | O(1) worst case. Either cache the leftmost/rightmost … | `src/C/FingerTree/src/persistent_delta_map.c:528` |
| 53 | B | medium | C# | SortedSet | Union / Intersect / Except / SymmetricExcept … | Theta(n+m) worst case confirmed for Union/Except/SymmetricExc… | O(m log(n/m + 1)) for m <= n, i.e. O(log n) for that … | `src/CSharp/src/Durable7.FingerTree/SortedSet.cs:412` |
| 54 | B | medium | OCaml | Merkle search tree — MSP2 proofs | create_proof / create_range_proof | Documented O(log n) path-only proof; code produces Θ(n) block… | Either make the code path-only (as all eight siblings… | `src/OCaml/lib/hamt/merkle_proof_merge.ml:3` |
| 55 | B | medium | OCaml | Rope | concat | Θ(log max(n, m)) worst case and amortized; e.g. Θ(log n) to a… | O(log min(n, m)) amortized — the Hinze–Paterson `app3… | `src/OCaml/lib/finger_tree/rope.ml:14` |
| 56 | B | medium | OCaml | Sorted_set (order-statistic finger-tr… | union / intersect / difference | union Theta(m log(n+m)) and intersect/difference Theta(n log … | O(m log(n/m + 1)) — the hedge/run-adopting class, whi… | `src/OCaml/lib/finger_tree/sorted_set.ml:116` |
| 57 | B | medium | Python | ContextualRankSequence (measured fing… | evaluate / root-summary read after an endpoin… | Theta(s log n) worst case per root-summary read on a freshly … | O(s) worst case, via the push-suspension measure shor… | `src/Python/src/durable7/finger_tree/measured_sequence.py:270` |
| 58 | B | medium | Python | TextRope | lines | Θ(n + L·log n), i.e. Θ(n log n) when lines are short (L = Θ(n… | Θ(n) — one streaming pass splitting on '\n' | `src/Python/src/durable7/finger_tree/rope.py:668` |
| 59 | B | medium | Rust | ContextualRankSequence (measured fing… | evaluate / root-summary read after an endpoin… | Not Theta(s log n) on every call -- the cost averaged over ba… | O(s) worst case, via a `Pending::PushFront/PushBack` … | `src/Rust/FingerTree/src/measured.rs:722` |
| 60 | B | medium | Rust | PersistentOrderedSet | index_of / try_remove / move_to / move_to_fir… | Theta(log^2 n) exactly - measured as (log2(n)+1)^2 tree-node … | O(log n) — a single monotone-predicate descent. Rust'… | `src/Rust/Ordered/src/lib.rs:836` |
| 61 | B | medium | Rust | reversible deque | front / back | Theta(log n) worst case for the first/last element of a rever… | O(1) worst case - a cached endpoint field read | `src/Rust/FingerTree/src/deque.rs:1493` |
| 62 | B | medium | TypeScript | ContextualRankSequence (measured fing… | evaluate / root-summary read after an endpoin… | Same correction as id 63: O(s) typical (~6-7 compositions, fl… | O(s) worst case. `measure(snoc(T,x)) = measure(T) ⊕ m… | `src/TypeScript/src/finger-tree/measured-sequence.ts:247` |
| 63 | B | medium | TypeScript | measured finger tree (`MeasuredSequen… | endpoint view / pop (`viewLeft`, `viewRight`,… | Theta(log n) per endpoint pop, worst case and under persisten… | O(1) amortized under persistent branching. | `src/TypeScript/src/finger-tree/measured-sequence.ts:319` |
| 64 | B | medium | all | PersistentChunkedBitSet | union / intersect / except / symmetricExcept … | Delivered Theta(c_left + c_right) + Theta(c_result) rebuild i… | O(c_min · log(c_max/c_min + 1)) worst case by split/j… | `src/CSharp/src/Durable7.FingerTree/PersistentChunkedBitSet.cs:343` |
| 65 | B* | medium | all | PersistentOrderedSet / PersistentOrde… | Insert(index, item) and MoveTo(index, item) a… | Theta(n) amortized per interior insertion/move under an adver… | O(log n) amortized per insertion with the standard or… | `src/CSharp/src/Durable7.Ordered/PersistentOrderedSet.cs:621` |
| 66 | B* | low | C | PersistentChunkedBitSet | union | Theta(c_left + c_right) chunk visits plus a Theta(c_result) p… | the documented behavior — O(divergence) with referenc… | `src/C/FingerTree/include/durable7/finger_tree/persistent_chunked_bit_set.h:100` |
| 67 | B* | low | Haskell | ContextualRankSequence | toList / iteration | Theta(n) in n, with a per-element factor equal to s (the mach… | Θ(n) with zero summary compositions — a direct in-ord… | `src/Haskell/FingerTree/src/Durable7/FingerTree/ContextualRankSequence.hs:252` |
| 68 | B* | low | Rust | PersistentDeltaMap / SortedMap | min_entry / max_entry | min_entry / max_entry are O(1) worst case; the documented The… | The claim should read O(1) worst case (matching the o… | `src/Rust/FingerTree/src/delta_map.rs:289` |
| 69 | C | high | C | Lazy Hinze-Paterson finger tree (`ft_… | push / enqueue | Θ(log n) per push, with Θ(log n) invocations of the caller-su… | O(1) amortized push with O(1) amortized comparator wo… | `src/C/FingerTree/src/fingertree.c:11855` |
| 70 | C | high | Cpp | interval_tree / persistent_interval_m… | find_overlap_cursor / find_containing_cursor … | Θ(n) per seek; Θ(k·n) to stream k overlaps through repeated `… | O(log n) — the map already ships `count_keys_less_tha… | `src/Cpp/FingerTree/include/durable7/finger_tree/ordered_search_cursors.hpp:880` |
| 71 | C | high | Haskell | IntervalTree / IntervalMap (cursor la… | findOverlapCursor / findContainingCursor / in… | Θ((n-start)·log n) per seek, unconditionally on a miss; Θ(k·n… | O(log n): the same `IntervalMeasure` already caches `… | `src/Haskell/FingerTree/src/Durable7/FingerTree/OrderedSearchCursor.hs:594` |
| 72 | C | high | Haskell | Merkle search tree (canonical wide B=… | createProof / createPointProof (membership + … | Θ(B log B) per proof, B = block count ≈ n/16, i.e. Θ(n log n)… | O(height · block width) = O(log n), the length of the… | `src/Haskell/Hamt/src/Durable7/Hamt/MerklePersistence.hs:1007` |
| 73 | C | high | Haskell | Merkle search tree (canonical wide B=… | createRangeProof / createRangeProofInternal (… | Θ(B log B) per range proof independent of the range width | O(number of blocks intersecting the range) = O(log n)… | `src/Haskell/Hamt/src/Durable7/Hamt/MerklePersistence.hs:1035` |
| 74 | C | high | Haskell | Merkle search tree (canonical wide B=… | planSync (frontier synchronization round); cr… | Θ(B log B) per call regardless of frontier size; with an empt… | O(present blocks walked + frontier) — O(1) when the r… | `src/Haskell/Hamt/src/Durable7/Hamt/MerklePersistence.hs:836` |
| 75 | C | high | Haskell | Reversible deque cursor (Durable7.Fin… | cursorInsert / cursorInsertList / cursorDelet… | Θ(n) time and Θ(n) fresh allocation per single-element cursor… | O(log n) — the same split-then-concat path the plain … | `src/Haskell/FingerTree/src/Durable7/FingerTree/ReversibleDeque.hs:209` |
| 76 | C | high | Kotlin | IntervalTree / PersistentIntervalMap … | findOverlapCursor / findContainingCursor / In… | Θ((n-start)·log n) per seek; Θ(k·n log n) to stream k overlap… | O(log n): split at the start rank and run one `locate… | `src/Kotlin/FingerTree/src/durable7/fingertree/OrderedSearchCursors.kt:710` |
| 77 | C | high | Python | IntervalTree / PersistentIntervalMap … | IntervalTreeCursor.peek_next / peek_previous,… | Θ(n) time and Θ(n) allocation per peek; an ordinary cursor wa… | O(log n) — one measured `locate(count > rank)` descen… | `src/Python/src/durable7/finger_tree/priority_interval.py:565` |
| 78 | C | high | Python | IntervalTree / PersistentIntervalMap … | find_overlap_cursor / find_containing_cursor … | Θ(n) per call regardless of where the answer lies; Θ(k·n) to … | O(log n): rank split plus one `locate(maximum_high >=… | `src/Python/src/durable7/finger_tree/priority_interval.py:474` |
| 79 | C | high | Python | Lazy Hinze-Paterson measured finger t… | meld | Θ(n+m) — full traversal of both queues plus a whole-sequence … | O(log(min(n, m))) amortized, by calling the `Measured… | `src/Python/src/durable7/finger_tree/priority_interval.py:123` |
| 80 | C | high | Python | Merkle search tree (canonical wide B=… | range (inclusive key-range enumeration) | Θ(rank(minimum) + output) — Θ(n) for a window near the top of… | O(depth · block width + output) = O(log n + output) | `src/Python/src/durable7/hamt/merkle_search_tree.py:449` |
| 81 | C | high | Rust | IntervalTree / PersistentIntervalMap … | find_overlap_cursor / find_containing_cursor … | Θ(n) per seek — and Θ(k·n) to stream k matches with repeated … | O(log n): split at the start rank, then one `try_loca… | `src/Rust/FingerTree/src/interval_tree.rs:451` |
| 82 | C | high | Rust | MeasuredRope<T, P> (chunked measured … | copy_to(index, destination) — copy a run of e… | Θ(index + count) — linear in the start offset regardless of h… | O(log n + count) via the count component of the cache… | `src/Rust/FingerTree/src/rope.rs:1819` |
| 83 | C | high | Rust | Rope<T> (chunked measured finger tree… | copy_to(index, destination) — copy a run of e… | Θ(index + count) — a full-length prefix walk for a copy near … | O(log n + count): descend by the cached length measur… | `src/Rust/FingerTree/src/rope.rs:582` |
| 84 | C | high | TypeScript | CanonicalSortedSet / CanonicalSortedS… | cursor peekNext / peekPrevious (and therefore… | Θ(n) time and Θ(n) allocation per peek; a k-step cursor walk … | O(log n) worst case — a single count-directed descent… | `src/TypeScript/src/finger-tree/canonical-sorted-set.ts:343` |
| 85 | C | high | TypeScript | IntervalTree / PersistentIntervalMap … | IntervalTreeCursor.peekNext / peekPrevious, P… | Θ(n) time and allocation per peek; a full cursor traversal is… | O(log n) — one `MeasuredSequence.locate(count > rank)… | `src/TypeScript/src/finger-tree/priority-interval.ts:276` |
| 86 | C | high | TypeScript | IntervalTree / PersistentIntervalMap … | findOverlapCursor / findContainingCursor / se… | Θ(n) per call even when the match is at the start rank; Θ(k·n… | O(log n): rank split plus one `locate(maximumHigh >= … | `src/TypeScript/src/finger-tree/priority-interval.ts:227` |
| 87 | C | high | TypeScript | Merkle search tree (canonical wide B=… | diff (MerkleMapDifference computation) | Θ(n + m) key comparisons for any pair of distinct roots | O(divergence): O(depth · block width + differing entr… | `src/TypeScript/src/hamt/merkle-search-tree.ts:315` |
| 88 | C | high | TypeScript | Merkle search tree (canonical wide B=… | range (inclusive key-range enumeration) | Θ(rank(minimum) + output) — Θ(n) for a window near the top of… | O(depth · block width + output) = O(log n + output) | `src/TypeScript/src/hamt/merkle-search-tree.ts:328` |
| 89 | C | high | TypeScript | SortedSet (order-statistic finger tre… | intersect, isSubsetOf, isProperSubsetOf, isPr… | intersect: Θ(n log n + m log m). isSubsetOf/setEquals/isPrope… | Θ(n + m) by the linear two-way merge the C# reference… | `src/TypeScript/src/finger-tree/sorted.ts:200` |
| 90 | C | medium | C# | Reversible deque cursor (ReversibleDe… | InsertRange(IEnumerable<T>) | Θ(m log n) for m inserted elements (m separate root-to-leaf s… | Θ(m + log n): build the m-element run into a reversib… | `src/CSharp/src/Durable7.FingerTree/ReversibleDeque.Cursor.cs:89` |
| 91 | C | medium | Cpp | interval_tree (cursor layer) | get_cursor_at_interval | Θ(n), dominated by the probe's rank rather than by the equal-… | O((1+r) log n): `tree.try_interval_at_rank(rank)` — d… | `src/Cpp/FingerTree/include/durable7/finger_tree/ordered_search_cursors.hpp:858` |
| 92 | C | medium | Haskell | IntervalMap | delete (and intervalMapDeleteNext / intervalM… | Θ(r) view/snoc steps for an equal-low run of length r — Θ(n) … | O(log n): split the entry sequence on the complete `(… | `src/Haskell/FingerTree/src/Durable7/FingerTree/IntervalMap.hs:90` |
| 93 | C | medium | Haskell | IntervalTree | upperBoundRank / intervalTreeUpperBoundCursor | Θ(r·log n) for an equal-low run of length r; Θ(n log n) when … | O(log n) with a single split on the `lastLow > probe`… | `src/Haskell/FingerTree/src/Durable7/FingerTree/IntervalTree.hs:170` |
| 94 | C | medium | Haskell | IntervalTree (cursor layer) | findIntervalCursor | Θ((n-start)·log n) on every miss. | O((1+r) log n) for an equal-low run of length r — bre… | `src/Haskell/FingerTree/src/Durable7/FingerTree/OrderedSearchCursor.hs:534` |
| 95 | C | medium | Haskell | TextRope (Durable7.FingerTree.Rope.Te… | lines (enumerate every line's text) | Θ(n + L·log n) for L lines over n characters — Θ(n log n) on … | Θ(n): one left-to-right traversal that cuts at each '… | `src/Haskell/FingerTree/src/Durable7/FingerTree/Rope/Text.hs:148` |
| 96 | C | medium | Kotlin | IntervalTree | cursorUpperBound / IntervalTree.cursorAtUpper… | Θ(n log n) when the stored intervals share a low endpoint. | O(log n) with one `locate { lastLow != null && lastLo… | `src/Kotlin/FingerTree/src/durable7/fingertree/PriorityAndInterval.kt:358` |
| 97 | C | medium | Kotlin | PersistentIntervalMap | remove (and PersistentIntervalMapCursor.delet… | Θ(r·log n) for an equal-low run of length r; Θ(n log n) when … | O(log n): split on the complete `(low, high)` key, as… | `src/Kotlin/FingerTree/src/durable7/fingertree/PersistentIntervalMap.kt:101` |
| 98 | C | medium | Kotlin | TextRope (durable7.fingertree.Rope.kt) | lines() (enumerate every line's text) | Θ(n + L·log n) — Θ(n log n) on newline-dense text. | Θ(n): one traversal splitting at each '\n'. | `src/Kotlin/FingerTree/src/durable7/fingertree/Rope.kt:831` |
| 99 | C | medium | OCaml | CanonicalSortedSet | statistics (the port's structural-audit / sha… | Θ(n log n) expected for `statistics` (Θ(n²) for an adversaria… | Θ(n) for the audit (one visit per node), O(1) for hei… | `src/OCaml/lib/finger_tree/canonical_sorted_set.ml:105` |
| 100 | C | medium | OCaml | Merkle search tree (canonical wide B=… | missing_pack (the OCaml spelling of createSyn… | Θ(B) node visits and Θ(B) store probes on every call, includi… | O(size of the missing closure) — O(1) when the receiv… | `src/OCaml/lib/hamt/merkle_persistence.ml:434` |
| 101 | C | medium | Python | CanonicalSortedSet (zip-zip tree) | set_equals | Θ(m log m + n) whenever the answer is `True`, independent of … | O(divergence) via a pointer-pruning lockstep node wal… | `src/Python/src/durable7/finger_tree/canonical_sorted_set.py:587` |
| 102 | C | medium | Python | Merkle search tree (canonical wide B=… | plan_merkle_sync (frontier synchronization ro… | Θ(B) block examinations (and Θ(total block bytes) accounted) … | O(1) — the six guarded ports never touch a node in th… | `src/Python/src/durable7/hamt/merkle_persistence.py:774` |
| 103 | C | medium | Python | Reversible deque (durable7.finger_tre… | __iter__ (and therefore to_list, and any part… | Θ(n) work and Θ(n) auxiliary allocation before the first elem… | `yield from self._items.reversed_view()` — O(log n) t… | `src/Python/src/durable7/finger_tree/core.py:471` |
| 104 | C | medium | Rust | IntervalTree (cursor layer) | IntervalTree::find_cursor | Θ(n) — the cost is dominated by the rank of the probe, not by… | O((1+r) log n) for a run of length r: address each ca… | `src/Rust/FingerTree/src/interval_tree.rs:423` |
| 105 | C | medium | TypeScript | CanonicalSortedSet (zip-zip tree) | setEquals | Θ(m log m + n) whenever the answer is `true` (or whenever the… | O(divergence) — a lockstep node walk that stops at ev… | `src/TypeScript/src/finger-tree/canonical-sorted-set.ts:249` |
| 106 | C | medium | TypeScript | Merkle search tree (canonical wide B=… | planMerkleSync (frontier synchronization roun… | Θ(B) block examinations (and Θ(total block bytes)) when the t… | O(1) — the six guarded ports never touch a node in th… | `src/TypeScript/src/hamt/merkle-persistence.ts:347` |
| 107 | C | medium | TypeScript | TextRope (src/finger-tree/rope.ts) | lines() (enumerate every line's text) | Θ(n + L·log n) — Θ(n log n) on newline-dense text. | Θ(n): one iteration over the characters cutting at ea… | `src/TypeScript/src/finger-tree/rope.ts:299` |
| 108 | C | low | C# | SortedSet.Builder and SortedDictionar… | ToImmutable after any staged edit (the edit-t… | Θ(n) per dirty snapshot; Θ(k·n) for k interleaved edit-then-p… | O(1) per snapshot with O(log n) per edit — the persis… | `src/CSharp/src/Durable7.FingerTree/SortedSet.Builder.cs:118` |
| 109 | C | low | docs (claim about C) | documentation claim vs. `ft_priority_… | whole-type representation claim | A published cross-workspace uniformity claim that is false fo… | Either the C rebase described in the preceding findin… | `docs/reference/data-structure-catalog.md:268` |
| 110 | D | high | Kotlin | ContextualRankSequence (measured fing… | evaluate / root-summary read after an endpoin… | Θ(s log n) per call, no amortization, for `ContextualRankSequ… | O(s) worst case, via a measure probe on `SuspOperatio… | `src/Kotlin/FingerTree/src/durable7/fingertree/PersistentMeasuredTree.kt:277` |
| 111 | D | high | OCaml | IntervalTree / PersistentIntervalMap | module documentation for queries | Theta(n) unpruned full scans in interval_tree.ml:113/122 and … | The documentation is the wrong side: it asserts a sub… | `src/OCaml/lib/finger_tree/interval_tree.mli:5` |
| 112 | D | high | OCaml | PersistentIntervalMap | query_point / query_overlap / add / set / rem… | Theta(n) for query_point/query_overlap even when one entry ma… | O(log n) single-overlap lookup and O(k log n) enumera… | `src/OCaml/lib/finger_tree/persistent_interval_map.ml:87` |
| 113 | D | high | OCaml | measured finger tree (`Measured_tree`… | endpoint view / pop (`view_left`, `view_right… | Θ(log n) worst case per pop and Θ(log n) amortized under pers… | O(1) amortized under persistent branching. (This is n… | `src/OCaml/lib/finger_tree/measured_tree.ml:339` |
| 114 | D | high | Rust | PersistentOrderedMap / PersistentOrde… | get, contains (documented complexity) | Theta(log^2 n) | O(1) expected as documented (reference layout), or O(… | `src/Rust/Ordered/src/ordered_map.rs:203` |
| 115 | D | high | TypeScript | PrioritySearchQueue cursor | cursor peekNext / peekPrevious (and everythin… | Theta(n) time and Theta(n) allocation per peek | O(log n) - one count-directed descent using the cache… | `src/TypeScript/src/finger-tree/priority-search-queue.ts:182` |
