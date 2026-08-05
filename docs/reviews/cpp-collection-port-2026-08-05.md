# C++ Collection Port — 2026-08-05

- Created (UTC): 2026-08-05
- Repository HEAD (reviewed): `experimental` branch, post Kotlin port
- Audience: Maintainers reviewing the six-language shipment of the seven research-derived collections
- Scope: The C++ port of all seven collections plus the level-ancestor seam they share, the
  intentional divergences it makes, and the verification evidence

## Decision Recorded

The seven research-derived collections are now ported to C++, making coverage **C#, Rust, C,
Haskell, Kotlin, and C++**. The four earlier port reviews carry current-state notes pointing here.
The remaining three workspaces (OCaml, Python, TypeScript) stay unported under the same
parity-economics rule.

## What Shipped

| Collection | C# namespace | C++ type |
| --- | --- | --- |
| `AncestralSliceQueue<T>` | `Durable7.FingerTree` | `durable7::finger_tree::ancestral_slice_queue<T, Arena>` |
| `BilateralAncestralDeque<T>` | `Durable7.FingerTree` | `durable7::finger_tree::bilateral_ancestral_deque<T, Arena>` |
| `ContextualRankSequence<TElement, TMachine>` | `Durable7.FingerTree` | `durable7::finger_tree::contextual_rank_sequence<T, Machine>` |
| `PersistentDeltaMap<TKey, TValue>` | `Durable7.FingerTree` | `durable7::finger_tree::persistent_delta_map<K, V, KeyLess, ValueEqual>` |
| `PersistentRunDeltaVector<T>` | `Durable7.FingerTree` | `durable7::finger_tree::persistent_run_delta_vector<T, EqualityPolicy>` |
| `PersistentMonotoneActionHeap<TElement, TPriority, TAction>` | `Durable7.FingerTree` | `durable7::finger_tree::persistent_monotone_action_heap<E, P, Policy>` |
| `PersistentAncestralConnectionForest` | `Durable7.Hamt` | `durable7::hamt::persistent_ancestral_connection_forest` |

`incremental_ancestor.hpp` carries the level-ancestor seam the first two share. All are header-only
and reachable through the aggregate `finger_tree.hpp` / `hamt.hpp`.

## The Policy Regime Is the Story

Where every other port injects the level-ancestor backend, the action algebra, the comparers, and
the event machine as runtime objects, this workspace takes all four as **compile-time template
parameters constrained by concepts** — its stated regime. The consequence goes beyond inlining: the
managed reference's runtime policy-identity gates stop being throwing checks and become compile
errors. A heap melded with a differently-policied heap does not compile; two sequences built on
different machines cannot be concatenated; a zero-state machine is a constraint failure rather than
a runtime initialization error. The backend seam survives as `incremental_ancestor_arena`, a concept
the consuming collections default to `myers_incremental_ancestor_arena<T>`, so C#'s injection point
is preserved without virtual dispatch. The arena itself is otherwise a faithful port — mutex
serialized, integer handled, odd-block stored — held through `std::shared_ptr` because it is
deliberately non-copyable, which is what keeps the collections ordinary copyable values.

## Where C++ Is Better Than Its Siblings

Two claims here are **stronger** than in other ports, and both were verified against the substrate
rather than assumed:

- **The contextual rank sequence keeps the managed reference's bounds exactly** — O(s) amortized
  endpoint updates and O(s log(min(n, m))) concatenation — because `detail/measured_tree.hpp` is a
  genuine Hinze–Paterson finger tree: 1..4-element digits, a lazy middle that answers a measure probe
  without forcing, and a concatenation that descends both middles in lockstep and terminates at the
  shallower operand. Rust and Kotlin must state weaker bounds because their substrates are
  height-balanced join trees. Only Haskell shares this property.
- **The connection forest's CHAMP path factor is worst case, not expected.** The port pins the
  MurmurHash3 32-bit finalizer as its vertex hash, every step of which is a bijection of the 32-bit
  word, so no collision node can hold two distinct vertices and every trie path is at most seven
  levels. Pinning is what earns it: `std::hash<int>` is identity on libstdc++ but truncated FNV-1a on
  MSVC, which genuinely collides, so inheriting the default would have reduced the bound to expected
  — exactly the caveat Rust and Haskell must carry. The suite substantiates injectivity
  constructively, building the explicit left inverse and round-tripping probe vertices chosen to
  agree on every low-order trie level.

The delta map's `min_entry`/`max_entry` are also O(1) here, where Haskell and Kotlin must state
Θ(log N), because the finger tree's outer digits hold the extremes.

## Honest Cost Statements

`persistent_run_delta_vector`'s dirty membership, rank selection, and accept/revert-of-one-run are
**amortized**, not worst case: they route through the run index's lazily measured finger tree, whose
cached-measure read may force a pending spine. The same is true of `dirty_run_count()`. The managed
and Rust ports state these unqualified because their run indexes are not lazily measured; the claims
are narrowed here to what this substrate delivers, per the repository's standing rule that a bound is
a per-port promise.

## Adversarial Parity Audit

Each of the eight units was audited member-by-member against its C# baseline by an independent
reader, and every reported finding was passed to a second reader instructed to refute it by default.
`PersistentMonotoneActionHeap` was found faithful with nothing to report. Ten findings survived; all
are fixed. **Three were genuine code defects; the rest were over-claims, missing coverage, or a
vacuous assertion.**

| Severity | Unit | Finding | Fix |
| --- | --- | --- | --- |
| Medium | `IncrementalAncestorArena` | No test pinned the O(log M) hop bound, and all three hop counters were dead outside the header. Deleting the jump branch leaves every ancestor *answer* correct and only changes work per query, so the entire suite still passed — the Myers skip machinery could have been dead and shipped green | New `incremental_ancestor_tests.cpp` porting the C# and Rust guards. Mutation-proven: real max hops 29 versus the mutant's 32,768 against a bound of 64, and 0.5 s versus 2 m 12 s |
| Low | `IncrementalAncestorArena` | The odd-block layout and `integer_square_root` — the one helper genuinely rewritten here — were unpinned | Square-layout assertions across 0..4096 plus a multiply-only oracle for the rewritten helper |
| Low | `IncrementalAncestorArena` | No arena error contract or the failure-atomicity guarantee was exercised | Covered in the same file, including byte-identical statistics across a rejected addition |
| Low | `IncrementalAncestorArena` | **`append` was not exception-safe**: it published an empty block and reserved it afterwards, so a throwing reservation left an unreserved block behind and permanently desynchronized the statistics, breaking the seam's own "a failed addition publishes nothing" promise | Reserve before publishing; the vector's nothrow move keeps the push all-or-nothing |
| Minor | `AncestralSliceQueue` | **`const_iterator` equality keyed on the identity of the buffer a given `begin()` call minted**, so two `begin()` calls on the same queue compared *unequal* — violating the forward-iterator equality requirement the test file itself `static_assert`s (the assert passes because the standard cannot check semantic requirements) | Equality now names the logical position within a queue version; regression case confirmed to fail before and pass after |
| Minor | `ContextualRankSequence` | **The `initializer_list` constructor was the one path that did not force the root summary**, so a braced sequence could publish in a state where every later read throws | Delegates to the forcing constructor |
| Low | `PersistentDeltaMap` | The class-level claim that change enumeration invokes no ordering callback dropped the carve-out C# states for the range-restricted form | Claim narrowed, with the baseline's O(log(k + 1)) carve-out restored |
| Low | `PersistentRunDeltaVector` | Three shipped bounds dropped the "amortized" qualifier that C#, Rust, and this substrate all carry | Qualified at the table rows and every restating member; genuinely worst-case bounds left alone |
| Minor | `BilateralAncestralDeque` | The port shipped with no workspace documentation and the catalogs still claimed five-language coverage | Workspace API notes and all three repository catalogs updated |
| Low | `PersistentAncestralConnectionForest` | A height-ceiling assertion could not fail, because `validate_structure` already enforces that ceiling before returning | Replaced with the exact expected depth |

The recurring self-comparison defect that appeared four times in Kotlin and three times in Haskell
did **not** recur here: the ports were briefed on it and each guarded against it explicitly, several
by pairing every sharing assertion with a negative control.

## Verification

- `build.ps1 -Workspace FingerTree -RunTests`: **35/35 CTest cases pass** (7 new groups), zero MSVC
  diagnostics under `/W4 /WX /permissive-`.
- `build.ps1 -Workspace Hamt -RunTests`: **81 + 19 + 25 tests pass**, zero diagnostics.
- Every port was additionally verified on GCC 16.1 under `-Wall -Wextra` before integration, and the
  arena seam on both compilers independently.
- The monotone action heap's cases for composition direction, minimum tie-breaking, and
  expose-before-attach ordering were each confirmed to fail against a deliberately mutated kernel;
  so were the run-delta vector's representative-identity case and the arena's hop bound. One of
  those mutation runs found a *weak test* rather than a bug: the run-delta identity case initially
  passed against its mutant because single-position canonicalization masked the wrong
  representative, and it was rewritten to keep a second dirty position so canonicalization cannot
  fire.

## Follow-Up Filed

While following the C#/Rust `uniquify`/`union_unique` pairing order for the tagged heap, the port
observed that the pre-existing `brodal_okasaki_heap.hpp` uses a rank-bin loop that links in a
different order and can therefore select a different representative on equal-priority ties. That is
a question about already-shipped code rather than about this port, and is tracked separately.
