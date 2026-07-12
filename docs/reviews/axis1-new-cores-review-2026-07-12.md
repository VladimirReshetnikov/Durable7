# Axis 1 New-Cores Cross-Language Review — 2026-07-12

- Created (UTC): 2026-07-12T17:57:42Z
- Repository HEAD (reviewed): 6bf9d7995e54aa1b70b10d5753bab99bb85f46ab
- Audience: Maintainers and AI coding agents working on the repository-owned Axis 1 cores
- Scope: Correctness, cross-language semantic parity, complexity/allocation honesty, and memory
  safety of the "Axis 1: New Cores" from the [frontier structure catalog](../reference/frontier-structure-catalog.md),
  across C, C++, C#, Haskell, Kotlin, and Rust; the fix applied during this review

## Scope

This review covers the cores that shipped after the [2026-07-11 review](cross-language-implementation-review-2026-07-11.md)
(whose HEAD `867ae9a` predates every Axis 1 port commit) and that the frontier catalog documents as
implemented:

- **CHAMP canonicalization** — the two-bitmap / canonical-deletion upgrade to `PersistentHashMap`/`PersistentHashSet` plus structural `MapEquals`/`Diff`.
- **Patricia integer maps** — `PersistentIntMap`/`PersistentIntSet` and the 64-bit `Long` variants.
- **RRB vector** — relaxed radix-balanced persistent vector.
- **Merkle search tree** — the content-addressed core + `MST2`/`MSP2` wire, plus the persistence
  tier (proofs, verification budget, synchronization, three-way merge).
- **Canonical zip-zip set** — `CanonicalSortedSet` / `ZipTreeRankPolicy`.
- **Brodal–Okasaki heap** and **priority search queue**.
- **DABA Lite** sliding-window aggregator.
- **Ctrie** — the managed-only lock-free concurrent hash trie (C# + Kotlin/JVM).

The base branch was fast-forward-merged from `origin/main` (`f97e9b3..6bf9d79`, no conflicts) before
review.

## Method

Ten adversarial deep-review passes, one per core, each spanning all six languages with the C#
implementation as the semantic reference. Every pass read the implementations in full (the C Merkle
core alone is ~9,000 lines), read and critiqued the test suites (a weak or missing test is itself a
finding), and was required to substantiate each finding with a concrete failure scenario or a
definite contract/parity violation — discarding anything it could not. Findings are tagged CONFIRMED
(traced to source) or PLAUSIBLE (needs runtime confirmation). The C# reference workspace was built
clean (0 warnings, 0 errors) as a baseline, and the one applied fix was validated by rebuilding and
re-running the affected native workspace.

Highest-value hypotheses were pursued specifically: stale-winner-after-rotation (PSQ), accepted
forgery (Merkle proofs), non-commutative fold order and callback-atomicity corruption (DABA),
order-dependent shape/digest (zip set and Merkle), lost update / snapshot isolation break (Ctrie),
and buffer/ownership faults on native error paths. Two of these hypotheses were confirmed as real
defects (Ctrie snapshot isolation; a native buffer overflow in the zip set); the rest were traced
and cleared.

## Verdict summary

| Core | Result | Highest severity |
| --- | --- | --- |
| CHAMP canonicalization | Core correct in all six; equality/diff test-coverage and policy-parity gaps | Medium (test coverage) |
| Patricia int maps | Correct; two structural-merge parity divergences (C# reference weaker than ports; Rust missing subtree short-circuit) | Medium (parity) |
| RRB vector | Correct indexing/concat/split; no enforced density invariant; `MaximumHeight` contract inconsistent across languages | Medium (PLAUSIBLE) |
| Merkle core + wire | No confirmed byte/layer divergence; cross-language golden vector is degenerate | High (test coverage) |
| Merkle persistence/proofs/sync/merge | Sound; no forgery constructible | Low (Rust API hygiene) |
| Canonical zip-zip set | **Heap buffer overflow in C `remove` (fixed this round)**; all canonicality/parity dimensions clean | **Critical (fixed)** |
| Brodal–Okasaki heap | Clean; C/C++ meld shape differs but is equivalent | Low (note) |
| Priority search queue | Clean in all six | — |
| DABA Lite | Clean in all five shipping languages | Low (informational) |
| Ctrie | C# reference sound; **Kotlin `snapshot()` loses writes / breaks isolation** | **Critical (report)** |

Two Critical defects were found. One (the C zip-set overflow) was fixed and validated this round.
The other (Kotlin Ctrie snapshot) requires a lock-free RDCSS port and is documented below with a
precise fix recipe rather than applied blind, because a lock-free correctness change must land with
its own race reproduction test.

## Fixed this round

### C canonical zip-zip set — heap buffer overflow in the removal merge (Critical, fixed `bc8550d`)

`ft_canonical_merge` ([canonical_sorted_set.c:1314](../../src/C/FingerTree/src/canonical_sorted_set.c))
zips the right spine of a removed node's left child against the left spine of its right child, so its
scratch `path` grows to `height(left) + height(right)` steps — up to roughly `2 * root->height`.
`ft_canonical_sorted_set_remove` sized that buffer with `ft_canonical_allocate_path`, whose capacity
is `root->height + 1` (the single root-to-leaf bound used correctly for the *search* path), and the
merge loop wrote `path[path_count++]` with **no** capacity guard — unlike every sibling traversal
(digest, `nodes_equal`, `validate`), which all bounds-check and return `FT_STATUS_OVERFLOW`.

Consequence: removing an interior node of any tree taller than three overran the buffer. This is not
adversarial-only — near-root deletes in an ordinary tree of height ≥ 4 corrupt the heap. Concrete
shape: a root over an equal-height-4 right-leaning left subtree and left-leaning right subtree makes
removing the root zip a 7-element seam into a 6-element buffer. The five sibling ports (C#, C++,
Rust, Kotlin, Haskell) use growable containers and are unaffected.

The existing C suite never exercised a long seam: the deep-collision test uses a constant-rank right
chain (seam length 0 on its only removal), and the randomized-history test passed only by seed luck.

Fix: a dedicated `ft_canonical_allocate_merge_path` sizes the scratch for two spines
(`2 * (height + 1)`, matching the digest traversal's sizing) and reports its capacity;
`ft_canonical_merge` now takes that capacity and bounds-checks every push. A new regression test
(`canonical interior removal merge seam`) drains a tall pseudo-random treap in scrambled order,
repeatedly removing near-root nodes with long seams and validating structure throughout — under
AddressSanitizer the prior sizing fails it. Validated: C FingerTree MSVC Debug build + CTest 8/8
including the new test.

## Findings for follow-up

### Critical

#### Ctrie (Kotlin) — `snapshot()` loses a concurrently-committed write and breaks snapshot isolation (CONFIRMED)

[ConcurrentHashTrie.kt:82-90](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/ConcurrentHashTrie.kt).
`snapshot()` reads `main = readMain(before.node)`, wraps that captured main into a new-generation
root, and publishes it with a bare `root.compareAndSet(before, after)`. It never verifies that
`before.node`'s main is still the value it read, so a writer that commits into the old generation
between the `readMain` and the CAS is silently dropped from the live trie. The C# reference avoids
this with a root RDCSS: `Complete(RootDescriptor)` ([ConcurrentHashTrie.cs:554-568](../../src/CSharp/src/Tools.DataStructures.Hamt/ConcurrentHashTrie.cs))
commits the root transition only if `descriptor.Before.Node.Main` still equals the expected main,
and `ReadRoot()` help-completes a pending root descriptor before any writer decides its own GCAS.

Interleaving (root `R0`/gen `G0`, `R0.node = i0`, `i0.main = M0`):
1. Snapshot: `before = R0`; `main = readMain(i0) = M0`.
2. Writer `set(newKey, v)` against `R0`: `gcas(i0, M0, M1, R0)` commits because the root is still
   `R0`; returns success; `i0.main` resolves to `M1`.
3. Snapshot resumes: builds `after = Root(INode(M0, G1), G1)` from the stale `M0`; `root.CAS(R0, after)`
   succeeds. The live root's node holds `M0` — `newKey` is lost from the live trie, while the
   returned snapshot (reading `i0.main = M1`) *does* contain it (an isolation break).

`size`, `isEmpty`, and `iterator()` all call `snapshot()`, so any `.size` racing a writer can drop
that writer's update. C# ships a deterministic reproduction (`Snapshot_DoesNotLoseWriterCommittedAfterMainRead`)
that the Kotlin port would fail. Verified against both sources during this review.

Fix (must land with a race test): port the C# `RootDescriptor(before, expectedMain, after)`,
CAS `root` from `before` to the descriptor, commit in a `complete(RootDescriptor)` only if
`before.node.main === expectedMain`, and route every `root.get()` consumer (`mutate`, `clear`,
`getEntry`, `snapshot`) through a helping `readRoot()`.

### High

#### Merkle core + wire — the only cross-language golden vector is degenerate (CONFIRMED test gap)

The single shared wire vector pinned identically in all six suites (e.g.
[MerklePersistenceAlgorithmsTests.cs:12-23](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests/MerklePersistenceAlgorithmsTests.cs),
Rust `tests/merkle_core_wire.rs:238-265`, C `tests/merkle_search_tree_tests.c:374`) encodes a block
with `level = 0`, `entryCount = 1`, and both children equal to the empty digest. It never exercises a
non-zero level byte, more than one entry per block, a non-empty (nested) child digest, or multi-level
grouping. A port that emitted children before entries, used a different width for `level`/`subtreeCount`,
or ordered a multi-entry block's child digests differently would still pass its self-consistent
`forward == reverse` and cross-history tests *and* the degenerate golden vector — shipping a divergent
root undetected. The "byte-for-byte across six languages" claim currently rests on code inspection,
not on any test. (Inspection this round found no such divergence — layer assignment, `build_canonical`,
the `MST2` layout, and digest framing are faithful transliterations — but the guard is missing.)

Fix: add a shared golden vector for a tree that forces `level > 0`, ≥2 entries in one block, and at
least one non-empty child digest (the Rust `independently_discovered_hash_layers_form_wide_canonical_blocks`
key set already discovers keys across levels 0–4); pin the resulting root hash and at least the root
block bytes in all six suites.

#### Ctrie (Kotlin) — concurrency tests never race a snapshot against a writer (CONFIRMED test gap)

[HamtTests.kt:198-248](../../src/Kotlin/Hamt/test/tools/datastructures/hamt/HamtTests.kt). The tests
spawn real threads (good), but every `snapshot()` is taken in a quiescent gap between write phases,
writers use only disjoint keys, and there is no linearizability oracle or structural validator. This
is exactly why the Critical snapshot defect and the Medium tomb defect below pass undetected. Fix:
add a deterministic snapshot-vs-writer race test (a hook after `readMain` in `snapshot()`) asserting
the racing write is visible in the live trie and absent from the frozen snapshot; a same-key
contention loop; and a small-history linearizability check against a model map.

### Medium

#### Ctrie (Kotlin) — no tomb nodes, so the trie never contracts (CONFIRMED)

`MainNode` has only `CNode`/`LNode` implementers; there is no `TNode`, and removal
([ConcurrentHashTrie.kt:126-129, 152-153](../../src/Kotlin/Hamt/src/tools/datastructures/hamt/ConcurrentHashTrie.kt))
never tombs-and-contracts. After a remove, a non-root `INode` can be left pointing at a single-entry
or empty `CNode` that is never pulled up or reclaimed. The C# reference maintains canonical compact
form (`Contract`/`ContractCollision`/`CleanTombs`) and asserts `TombNodeCount == 0`. Repeated
insert/remove churn on deep-prefix-sharing keys grows an `INode → CNode(1) → INode → …` skeleton that
lookups traverse and the GC cannot collect while the map is live; depth and memory drift with
historical churn rather than live size. Lookup/insert linearizability is preserved (GCAS still
serializes each main transition), so this is a correctness-preserving space/perf leak and a divergence
from the reference, not an isolation break. Fix: port `TNode` + `Contract`/`ContractCollision` +
`CleanTombs`, including the "root INode cannot hold a tomb" invariant.

#### Patricia (C#) — combining `Union`/`Intersect` overloads are non-structural (CONFIRMED)

[PersistentIntMap.cs:125-135, 147-159](../../src/CSharp/src/Tools.DataStructures.Hamt/PersistentIntMap.cs)
(identical in `PersistentLongMap.cs:124-134, 146-158`). The C# combining overloads are naive
`foreach … SetItem` fallback loops; `PatriciaMapCore` has no combining structural merge (only the
non-combine `UnionRight`/`IntersectLeft`/`Except`). All five ports implement genuine prefix-aligned
structural combine (Rust `union_with`/`intersect_with`, Haskell `unionWithKey`/`intersectionWithKey`,
Kotlin `unionCombinedNodes`/`intersectCombinedNodes`, C++ `union_nodes_with`/`intersect_nodes_with`,
C `union_nodes`/`intersect_nodes`). So C# `Intersect(other, combine)` starts from `Empty` and can
never share a root, is O(n·log n)/O(m·log n) rather than the ports' O(n+m), and the catalog claim
that the combine overloads "make structural merge useful" holds only for the ports. The ports test
the reference-identity the C# overload lacks (e.g. Rust asserts `left.shares_root_with(left.intersect_with(superset, |_,l,_| l))`).
Fix: add combining variants to `PatriciaMapCore` (`UnionRight`/`IntersectLeft` threading a
`Func<TKey,TValue,TValue,TValue>`) and route the overloads through them; or document the C# overloads
as non-structural and soften the catalog wording. The former restores full parity.

#### Patricia (Rust) — `union`/`intersect` miss the shared-subtree short-circuit (CONFIRMED)

[patricia.rs:315-406](../../src/Rust/Hamt/src/patricia.rs) (`union_with_nodes`) and `:598-657`
(`intersect_with_nodes`) have no top-of-node `Arc::ptr_eq(left, right)` early return; only
`except_nodes` (`:667`) does. The non-combine `union`/`intersect` route through the `_with` variants
and short-circuit only at the whole-map level, so the headline Patricia property — align prefixes and
short-circuit reference-equal subtrees (implemented in C# `PatriciaMapCore.cs:247`, C++, Kotlin, C) —
is not realized for Rust union/intersect. For `B = A.insert(k)` sharing nearly all of `A`'s node
graph, `A.union(&B)` recurses the entire shared subtree (O(n)) plus a default-combine call per shared
leaf, instead of returning the shared subtree in O(depth). Results are correct; this is a
performance/contract regression, untested. Fix: add `if Arc::ptr_eq(left, right) { return Some(Arc::clone(left)); }`
at the top of both `_with` node functions — but **only** on the no-user-combine path (a user combine
must still run per shared key to match C#), matching C's `left == right && combine == NULL` guard.

#### RRB vector (all six) — concatenation enforces no fullness/rebalancing invariant (PLAUSIBLE)

`concat` merges only the single boundary child pair and re-partitions the concatenated child arrays
(splitting when > 32); it never redistributes to enforce the Stucki et al. minimum-fullness /
search-step invariant (C# `Partition`/`ConcatSameHeight`, and the analogous `partition` in every
port). Nodes can be ~50% full and leaves as small as 2. Element order and indexing stay correct, but
tree height/density are bounded only by the specific (benign) test patterns, not by any invariant. An
adversarial concat interleaving that repeatedly forces top-level `32→64→split-into-2→wrap` can inflate
height beyond `log32 n` — a regime untested in every suite. Fix: either document explicitly that
concat is a boundary-only merge with test-bounded (not guaranteed) density — consistent with the
catalog's "Plausible / benchmark-gated" verdict — or port the Stucki et al. redistribution pass. This
is a conscious design choice worth recording as one.

#### CHAMP (5 of 6 ports) — canonicalization regressions are untested (CONFIRMED)

Only the C# suite recursively asserts that two independently-built equal maps have identical node
topology and that no branch is under-full after a randomized delete/insert history
([PersistentHamtStructureTests.cs](../../src/CSharp/tests/Tools.DataStructures.Hamt.Tests)). The five
ports assert only content equality plus shallow root-kind checks. Because C# `MapEquals`/`Diff` *rely
on* canonical form (positional lockstep) they would break under a regression, but the ports' equality
is content-based and would keep returning correct results while silently accumulating non-canonical
structural bloat — the invariant CHAMP exists to provide is unguarded in five languages. Fix: port
C#'s `AssertValidStructure` (reject a bitmap node with fewer than two entries unless its sole child is
itself a bitmap node; reject leaf children) and `AssertCanonicalTopologyEqual`, run across a
randomized delete history and independent build orders, into each port's suite. The CHAMP delete/
collapse logic itself was traced and is correct in all six.

### Low

- **RRB `MaximumHeight` contract diverges across languages (CONFIRMED).** C#/Kotlin/Haskell reject
  height > 6; C/C++ reject > 12 (C `ft_rrb_branch_create` hard-fails with `FT_STATUS_OVERFLOW`); Rust
  never checks. Combined with the missing density invariant, a semantically valid tall tree would be
  reported invalid in three languages, errored in C, and accepted in two. Pick one size-derived cap
  and make all six validators agree.
- **RRB (Rust) validation omits the height cap its siblings enforce (CONFIRMED).** `validate_structure`
  checks only count/height agreement; `has_regular_layout` drops the upper bound. Internally
  self-consistent, but diverges from the reference's `1..=MaximumHeight` contract.
- **CHAMP (Haskell) `validStructure` does not certify canonical shape (CONFIRMED).**
  [HashMap.hs:113-119](../../src/Haskell/Hamt/src/Data/Structures/Hamt/HashMap.hs) accepts an
  under-full non-collapsed branch (`Branch dataMap 0 [(h,k,v)] []`), so `validStructure == True` does
  not imply canonical topology despite the docstring. Add clauses rejecting fewer than two entries
  unless the single child is itself a `Branch`.
- **CHAMP equality/diff policy-compatibility diverges (CONFIRMED).** C# requires the same comparer
  object; C++ `map_equals`/`diff`, Rust `PartialEq`/`diff`, and Haskell `mapEquals` perform no
  hash/key-policy check (Kotlin and C do). Two C++ maps with different `Hash`/`KeyEqual` but equal
  content compare equal where the reference reports inequality. Add a policy-identity precondition or
  document the intentional divergence per port.
- **Merkle (Rust) verification budget is documented "sealed" but its fields are `pub` (CONFIRMED).**
  [merkle_persistence.rs:778-795](../../src/Rust/Hamt/src/merkle_persistence.rs). A caller can
  field-mutate a `default()` instance into a state `new()` rejects; `#[non_exhaustive]` blocks struct
  literals but not field assignment. Benign — the budget is borrowed immutably during verification,
  every subtraction stays `≤` its limit so nothing underflows, and a tampered budget can only reject
  more aggressively or raise limits for trusted local data. Make the fields private with the existing
  getters (matching the C# read-only properties) or drop the "sealed" wording.
- **Merkle (C#) core test files pin no absolute wire vector (CONFIRMED).** The only absolute
  root/block-bytes assertion lives in the persistence-tier test; the reference language's wire
  contract is anchored indirectly. Add a block-level byte assertion in `MerkleEncodingWireTests.cs`
  (the values are already computed there).
- **Merkle (Haskell) hand-rolled SHA-256 is anchored by one fixed-length block (CONFIRMED).**
  [MerkleEncoding.hs:379-448](../../src/Haskell/Hamt/src/Data/Structures/Hamt/MerkleEncoding.hs). SHA-256
  bugs are overwhelmingly padding/length-boundary sensitive; no variable-length or multi-chunk bytes
  are cross-pinned. The wide/multi-level vector recommended for the High Merkle finding also closes
  this; alternatively add boundary-length SHA-256 vectors.
- **Patricia (Haskell) `insert` reallocates on equal-value reinsert (CONFIRMED).**
  [Patricia.hs:116-117](../../src/Haskell/Hamt/src/Data/Structures/Hamt/Patricia.hs) always builds a
  fresh leaf, so no-op identity holds less broadly than the other five ports. Benign — Haskell lacks
  cheap reference equality; document or add an `Eq v`-gated short-circuit.
- **Brodal–Okasaki (C, C++) `skew_meld` uses a different algorithm than the reference (CONFIRMED,
  no action).** C/C++ meld the child forest via bucket union-by-rank-with-carry; C#/Rust/Kotlin/Haskell
  use `uniquify` + `unionUnique`. Functionally equivalent (same tree multiset, same minimum, valid
  skew forest); only the unobservable internal shape differs. Record so a future parity audit does not
  misread it as a regression.
- **DABA Lite — three informational cleanups (no defect).** (1) C#/Kotlin/Rust `trimBefore`
  single-unlink is correct only because the front advances ≤ 1 block/evict; C/C++ use a defensive
  loop — align on the loop if a batch-evict is ever added. (2) Rust `read_with_overlay` is
  dead-defensive (the inserted value is never read in the same fixup) — remove or comment. (3) The
  native `clear` O(n+c) divergence from managed O(1) is documented and intentional.

## Verified clean (highlights)

Each pass listed what it re-derived and found correct; the load-bearing ones:

- **Priority search queue (all six): fully clean.** The stale-winner-after-rotation class is disproven:
  every structural change (leaf insert, replace, both single and both double rotations, two-child
  delete successor splice, and each ancestor rebuild on the unwind) funnels through one node
  constructor that recomputes the winner from its actual post-rotation children. The dual BST×heap
  invariant, keyed decrease-key, `atMost` pruning by cached winner, priority-then-key tie-break, and
  the honest O(log n + v) range bound all check out, including the C iterative implementation's
  refcount and query-stack sizing.
- **Merkle proofs/budget/sync/merge (all six): no forgery constructible.** Every decoder rejects on
  `hash(content) != claimed_digest` and non-canonical re-encode; the point-proof walk forces the child
  slot from the query key (not prover input), requires the exact canonical expansion, and binds each
  child digest into its parent. The verification budget is enforced on every decode/proof/load path
  with overflow-checked arithmetic and depth bounded before recursion; sync converges and terminates;
  three-way merge matches the conflict matrix and keeps `MergePresent(None)` distinct from deletion.
  Negative/tamper tests exist in all six suites.
- **Merkle core (all six): no confirmed byte or layer divergence.** Layer assignment (count leading
  zero base-16 digits of `SHA256(0x4b ‖ len ‖ domain ‖ len ‖ key)`, none using the language default
  hash), `build_canonical`, the `MST2` big-endian layout, and domain/empty/block digests are faithful
  across languages; the C decoder rigorously rejects malformed, non-canonical, over-long, and trailing
  input with no over-read.
- **DABA Lite (all five): clean.** Query is `combine(front, back)` — correct FIFO left-to-right order,
  no operand swap, verified against non-commutative matrix/string oracles; the six-cursor fixup and the
  bounded reclaiming ring are correct; callback atomicity (plan-then-commit, or combines-before-writes
  with rollback) leaves the window unchanged on a throwing callback; empty-window query returns identity.
- **Brodal–Okasaki (all six): clean and honestly bounded.** O(1) worst-case insert/meld/findMin with no
  hidden traversal (comparison-ceiling tests pin insert/meld to 1–5 comparisons up to 65,536 elements);
  heap order, bootstrapping boundary, comparator-identity meld gating, Haskell strictness (no lazy
  thunk leak), and native self-meld-DAG reclamation all correct.
- **CHAMP canonical delete (all six): correct.** Collapse-to-leaf/empty, unwrap of a single non-bitmap
  child, and upward re-inlining reproduce identical topology to direct construction; split-bitmap
  slotting and the unreachable `shift ≥ 32` path are correct; C refcount pairing balances on every
  merge/collapse/OOM path.
- **Patricia arithmetic (all six): correct.** Branching-bit/prefix math handles `INT_MIN`, top-bit
  prefixes, sign-bit-only splits, and empty/singleton tries; signed order via the sign-bit flip agrees
  across all six (`[MIN, -1, 0, 1, MAX]`); merge overlap cases and combine argument order match.
- **RRB indexing/concat/split (all six): correct.** Radix marking, relaxed size-table lookup, boundary
  merge element-conservation, split/slice size-table recomputation, builder isolation (no transient
  aliased into a published vector), and ≥3-level trees all check out.
- **Ctrie (C#): sound on all seven dimensions.** GCAS generation stamping, root RDCSS, contraction/tomb
  protocol, `Volatile`/`Interlocked` on every shared field, ABA prevention, and the snapshot→persistent
  bridge are correct, consistent with the in-repo linearizability oracle.

## Recommended remediation order

1. **Ctrie Kotlin snapshot RDCSS (Critical)** — port the root descriptor + helping `readRoot()`, landing
   with the deterministic snapshot-vs-writer race test (also closes the High Ctrie test gap). Until
   then, treat Kotlin `snapshot()`/`size`/`iterator()` as unsafe under concurrent writers.
2. **Merkle wide/multi-level golden vector (High)** — the one guard protecting the byte-for-byte
   cross-language claim; add it in all six suites (also closes the Haskell SHA-boundary Low).
3. **Ctrie Kotlin tomb/contraction (Medium)** and **Patricia parity (Medium ×2)** — restore reference
   parity for compaction and structural merge.
4. **RRB density invariant + `MaximumHeight` reconciliation (Medium/Low)** — decide document-vs-enforce
   and make all six validators agree on one cap.
5. **CHAMP structural-invariant tests in the five ports (Medium)** — guard the canonicalization the
   ports' equality does not.
6. **Remaining Low items** — Rust Merkle budget field privacy, Haskell `validStructure` clause, CHAMP
   equality policy checks, C# Merkle wire assertion, the DABA/ Brodal cleanups and notes.

## Remediation status (2026-07-12)

All actionable findings from this review have been resolved on `codex/axis1-new-cores`:

| Finding | Resolution |
| --- | --- |
| C canonical zip-set removal overflow (Critical) | Fixed on `main` by `bc8550d` and included by the branch merge; the interior-removal seam has a regression test. |
| Kotlin Ctrie snapshot RDCSS (Critical) | `87b763f` ports a root descriptor, helping `readRoot`, generation-safe publication, and the deterministic snapshot/writer lost-update regression. |
| Kotlin Ctrie concurrency evidence (High) | `87b763f` adds descriptor helping, same-key/deep-prefix churn, contraction checks, and a bounded short-history linearizability oracle. |
| Kotlin Ctrie tomb contraction (Medium) | `87b763f` adds tomb nodes, contraction/collision contraction, cleanup, and the root-no-tomb invariant. |
| Degenerate shared Merkle golden (High) | `a5e63c0` pins one 14-entry, four-block, three-level tree in all six ports, including exact root-block bytes and level. |
| C# core wire anchor (Low) | The same `a5e63c0` vector adds an absolute `MST2` block assertion to `MerkleEncodingWireTests`. |
| Haskell SHA-256 boundary evidence (Low) | `a5e63c0` pins 55-, 56-, 64-, and 65-byte vectors across padding and multi-block boundaries. |
| C# Patricia combining algebra (Medium) | `3e23cf4` adds prefix-aligned combining union/intersection for both widths with root-reuse and randomized model tests. |
| Rust Patricia shared-subtree pruning (Medium) | `3e23cf4` prunes `Arc`-identical subtrees only for built-in union/intersection; user-combining paths retain mandatory callback invocation. |
| Haskell Patricia equal-value replacement (Low) | `3e23cf4` documents the deliberate `Eq`-free path rebuild in Haddock and workspace guidance. |
| RRB density contract (Medium) | `bae48c0` records the deliberate boundary-only redistribution design in the catalog and every port's local contract; density ceilings are test/benchmark gates, not validator invariants. |
| RRB maximum height (Low) | `bae48c0` standardizes `(count-storage bit width - 1) / 5`; Haskell is corrected to 12 on 64-bit targets and Rust now enforces/tests the cap. |
| Five-port CHAMP canonicalization evidence (Medium) | `dfa89d7` adds recursive canonical validators, topology comparison, and independent-build plus delete/reinsert convergence gates in C, C++, Haskell, Kotlin, and Rust. |
| Haskell CHAMP validator (Low) | `dfa89d7` rejects under-full branches unless the sole slot is the intentional bitmap-child depth bridge. |
| CHAMP policy compatibility (Low) | C++ and Haskell now state the compatible-policy caller precondition necessitated by opaque function objects; Rust documents intentional semantic equality across `BuildHasher` state because key equivalence is fixed by `Eq`. |
| Rust Merkle budget API wording (Low) | The API now explicitly documents public field mutation as a trusted local-policy escape hatch and checked constructors as the untrusted configuration boundary; the former false sealed-construction claim is removed. |
| Native Brodal meld shape (informational) | The catalog and C/C++ API notes record rank-bucket-with-carry as equivalent to `uniquify`/`unionUnique`. |
| DABA Lite cleanups (informational) | Rust's overlay-aware read now explains its future batch-fixup role; single-unlink trim remains tied to one-block-per-evict and native O(n+c) clear remains the documented ownership cost. |

## Validation

- Base branch merged from `origin/main` (`f97e9b3..6bf9d79`), fast-forward, no conflicts.
- C# reference workspace: `dotnet build` clean, 0 warnings / 0 errors (baseline).
- C canonical-set fix (`bc8550d`): C FingerTree MSVC Debug build + CTest 8/8 pass, including the new
  `canonical interior removal merge seam` regression test. The Clang AddressSanitizer lane is the
  definitive catch for the prior overflow; the corrected sizing (`2*(height+1)` ≥ the two-spine seam)
  plus the in-merge capacity guard make it safe by construction.
- All other findings are static-analysis findings with concrete failure scenarios; none was fixed in
  code this round, so no further build was required for them.

## Relationship to other documents

- [Frontier structure catalog](../reference/frontier-structure-catalog.md) — the intended per-language
  contract for each core reviewed here; the Patricia (C# combine), RRB (density/`MaximumHeight`), and
  Merkle (byte-for-byte) findings each note where the catalog wording should be tightened or the code
  brought up to it.
- [2026-07-11 cross-language review](cross-language-implementation-review-2026-07-11.md) — reviewed the
  engine cores (which it found converged) at a HEAD predating every Axis 1 port; this report covers the
  Axis 1 cores it did not.
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md) — the parity workflow the
  cross-language divergences above should be resolved through.
