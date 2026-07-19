# Persistent Cursor Cross-Language Design And Implementation Review

- Created (UTC): 2026-07-19T15:40:14Z
- Repository HEAD: 4178ca44c4e135a5074eb6d59bdd59dc5dad545b
- Audience: Maintainers and port authors of the repository-wide persistent cursor tier
- Scope: Design conformance, feature and behavior parity, correctness, and complexity honesty for
  every public cursor family across all nine language ports
- Normative baseline:
  [repository-wide persistent cursor design](../proposals/repository-wide-persistent-cursor-design.md)

## Executive Summary

The cursor tier is in far better shape than a shipment of this size would suggest. **Twenty-two
cursor families ship in all nine ports with no family missing anywhere**, and the highest-risk
properties hold universally: no port exposes a forbidden operation, no cursor edit bypasses its
collection's canonical operation, gap-after-edit conventions are correct in all 45 sequence
port×family combinations, the Merkle wire and digest behavior is identical across ports, and the
range-update lazy-tag invariant — the most delicate thing in the shipment — is right everywhere.

The defects cluster in four places, in descending order of severity:

1. **Native memory safety in C** — one undefined-behavior path, three reference leaks, and two
   facade functions that destroy their own source under documented aliasing. Five are fixed here.
2. **A shared-contract violation replicated nine times** — the neutral Ordered multimap cursor is a
   flat global pair rank, which the design explicitly forbids inventing, and which costs O(total
   pairs) per operation.
3. **Complexity claims that the code does not deliver** — including two paragraphs of the *design
   document itself* describing frame-based representations that exist in zero ports.
4. **A near-total absence of workspace-level documentation** — of twenty-two cursor families across
   nine workspaces, exactly two workspace documents describe a non-rope cursor family.

Feature parity is high, but it is not the same as *documented* parity: the goal's requirement that
Haskell and C deviations be "clearly documented" is met for the rope tier and essentially unmet
everywhere else.

### What was fixed and validated in this review

| # | Defect | Ports | Validation |
| --- | --- | --- | --- |
| 1 | `Position`/`IsAtStart`/`Seek`-identity silently succeed on the invalid default cursor | C# ×15 types | Full C# gate green; 3 new regression tests |
| 2 | Measured rope `Seek` discards the lineage fragment cache (up to 2,048 spurious callbacks) | C# | Measure-cache suite green; assertion added |
| 3 | `destroy` hook invoked on uninitialized `malloc` memory (UB) | C | GCC build clean, 11/11 CTest |
| 4 | `ft_sorted_multiset_cursor_snapshot` self-alias leaks the whole tree | C | ditto |
| 5 | `ft_sorted_map_cursor_snapshot` self-alias leaks tree + entry context | C | ditto |
| 6 | `ft_interval_tree_copy` / `_i64_copy` destroy the source on self-copy | C | ditto |
| 7 | Ordered multimap group copy leaves raw heap bytes later freed as pointers | C | GCC build clean, 15/15 CTest |
| 8 | `locate` reports a miss when the predicate already holds at the identity | OCaml | Behaviorally verified before/after; regression test added |

Everything else in this report is recorded rather than changed: the remaining items either need a
design decision, span all nine ports, or could not be validated in this environment.

## Method And Confidence

Seven cross-language reviewers each took one family group and read implementation bodies (not
signatures) in all nine ports, plus tests and docs. I independently verified every finding I acted
on against the source before editing, and validated each fix through its language's gate.

Confidence is uneven and worth stating plainly:

- **Strong** — findings confirmed by execution: the C fixes (CTest), the C# fixes (full 1,529-test
  gate), the OCaml `locate` fix (compiled before/after driver), and the Rust/TypeScript/Kotlin/Python
  findings that reviewers reproduced by running code.
- **Moderate** — findings established by careful source reading in C++, Haskell, and OCaml. A clean
  bill of health there is weaker evidence than an executed probe.
- **Explicitly unverified** — the OCaml `join` rebalancing finding (§5.1) was measured against a
  mirrored reimplementation, not the shipped module. It must be reproduced in-tree before action.

## Part 1 — Parity

### 1.1 Feature parity is essentially complete

Every family in the design's applicability matrix ships a cursor in all nine ports. Specifically
checked and found present everywhere: all four Patricia families (int/long × map/set — Rust's are
macro-generated, which defeats a naive type search), all five sequence families including the
reversible deque and range-update sequence, all three rope tiers, all four sorted families, all four
augmented families, all three neutral Ordered families, and Merkle.

The gaps are narrow and mostly principled:

| Gap | Ports | Assessment |
| --- | --- | --- |
| `InsertRange` on the **measured** cursor | all 9 | **Correct.** The design lists only `Insert` for this family and permits an "applicable subset". |
| `InsertRange` on the **range-update** cursor | all 9 | **Genuine gap.** The design says "in addition to the ordinary positional operations", and `InsertRange` is one. Most ports' *collections* also lack it, so this is a substrate gap needing a contract decision first. |
| `TrySeekValue` / `TrySeekKey` / `InsertRange` on Ordered set/map | all 9 | **Genuine gap.** Named in the design as the defining additions to the positional protocol; zero hits repo-wide for every casing. |
| Group navigation on the Ordered multimap | all 9 | See §2.1 — the cursor is the wrong shape. |
| OCaml measured-rope cursor: no `measure_after`, no cursor measure seek, no `replace_next`, no movement, no `count` | OCaml | **Facade gap, not a substrate gap.** `Measured_tree` in the same port has the full surface. Without `measure_after` the law `combine(before, after) == whole` is not expressible through the public measured-rope cursor at all. |
| C text cursor has no counted `insert_array` | C | Text with an embedded `NUL` is uninsertable; both C siblings expose a counted form. |
| TypeScript text cursor has no newline-measure seek | TS | "Seek past N newlines" is unreachable. |

Because the misses are uniform rather than scattered, they read as contract-level decisions that were
never taken, not as nine independent slips. That is the right way to fix them too: decide once,
implement nine times.

### 1.2 Behavior parity is high, with a documented-deviation deficit

Verified identical across all nine ports: gap conventions after every edit; exact-seek miss
returning a usable lower-bound cursor plus a separate hit discriminator; empty-range insert
preserving the version; clean-snapshot identity; signed-key ordering at `INT_MIN`/`0`/`INT_MAX`;
measure combination order (never swapped); tag composition order and non-double-application; and
`SeekNextOverlap`'s exclusive continuation rule — the infinite-loop hazard is absent in all eighteen
tree×map×port combinations.

The real divergences:

**Error channels split four ways and are internally inconsistent within ports.** Rust's FingerTree
crate is all `Option` while its RangeUpdate crate mixes `Result` and `Option` on one type. Kotlin
returns `null` everywhere except `measure*`/`apply*`, which throw. Python uses `IndexError` for
sequence bounds but `ValueError` for range-update bounds. TypeScript uses `RangeError` for both
"bad argument" and "already at boundary", so callers cannot distinguish them. Most consequentially,
**Haskell and OCaml collapse duplicate-key and wrong-gap into a single `Nothing`/`None`** on Patricia
and Merkle, where five other ports distinguish them by type or code — this is a legitimate
immutability-adjacent simplification but it is documented nowhere.

**Three legitimate reversal strategies ship** (per-node mirror bits; eager materialization; an
orientation-aware core), all sanctioned by the design. Logical results are correct in all nine;
costs are not — see §5.3.

**`SetItem`'s stored-key rule splits three ways** with nothing recording the split: the supplied key
replaces the stored key in C#, Rust, C, C++, and Haskell; the stored key is retained in TypeScript
and OCaml. The design delegates this to "the owning port's stored-key contract", but no port states
its choice.

### 1.3 Naming drift

Minor but worth one decision: the design says `SetItem`, ports spell it `put` (C, C++, Haskell),
`set` (OCaml, TypeScript), or `setItem`/`set_item` (C#, Kotlin, Rust, Python). C++ is the only
self-inconsistent case — `cursor.put()` delegates to `tree.set_item()` on the next line. Python's
cursor exposes `count` while its collections expose `size`; TypeScript renames `count`→`size` on the
five sequence cursors while its own `RopeCursor` keeps `count`.

## Part 2 — Contract Violations

### 2.1 The Ordered multimap cursor is the shape the design forbids (all nine ports)

The design is explicit: *"Do not invent `PairPosition` or logarithmic random pair rank when the outer
structure does not cache value-group prefix counts."* Every port ships exactly a flat global pair
rank, and several announce it in their own doc comments ("root-plus-flattened-key-grouped-pair-rank
multimap gap cursor"). The precondition the design names is met — no port caches value-group prefix
counts — so the prohibition applies squarely.

The specified state is a sum type (`Empty | FocusedGroup {…}`) with group navigation, group-index
seek, and inner value movement. None of that exists in any port. The consequences are concrete:

- **Cost.** Resolving a rank walks the entire pair flattening: O(total pairs). A multimap with one
  1,000,000-value group plus a one-value group needs 1,000,000 steps to reach the second group, and a
  linear walk is O(P²). Group-seek-by-key invokes the user's equality policy once per *pair* instead
  of once per *group*.
- **Correctness pressure.** Because the flat encoding cannot name a group directly, OCaml and Haskell
  re-derive the gap by *content re-scan* after an edit. OCaml then calls `Option.get` on the result,
  which raises `Invalid_argument` for any value non-reflexive under the hash policy — inserting
  `Float.nan` through the cursor crashes on a pair the collection just accepted. Haskell has the same
  defect with an `error` call and a pure signature that gives callers no failure channel at all.
- **Silent data loss.** The same re-scan drives deletion: OCaml and Haskell peek, delete by content
  re-lookup, and **discard the removed flag**. Deleting `("k", nan)` reports success, leaves the
  multimap unchanged, and `peek_next` returns the same pair forever. This is precisely the
  "validate correspondence, then publish one version atomically or nothing" step, and it is missing.

The empty-group reanchor rule *is* satisfied in all nine ports — but only incidentally, because under
a flat encoding the rank is the prefix sum of group sizes. It is neither modeled nor observable, and
two of its three branches are untested everywhere.

**This needs a decision, not nine patches.** Either implement the `FocusedGroup` state — which fixes
the OCaml/Haskell crashes structurally, makes the reanchor rule observable, and yields the complexity
fix for free — or amend the design with a scoped Ordered-v1 exception. The design's own gating clause
(ordinary positional group operations must ship first) points at the second path being the honest
near-term answer.

### 2.2 C's sorted-set cursor is a typedef of the bag cursor

`typedef ft_sorted_multiset_cursor ft_sorted_set_cursor;` means the named set API's careful
duplicate-rejecting `add` sits alongside `ft_sorted_multiset_cursor_add`, which compiles against the
same handle with no diagnostic and inserts an unconditional duplicate — destroying the
one-representative-per-class invariant. This is effectively the forbidden bag `InsertHere` reachable
on a set. Fixing it means a distinct wrapper struct plus test rework, since the C tests currently
reach into the shared representation.

### 2.3 C leaks private sparse stamps through the cursor surface

`tds_ordered_set_cursor` has a public by-value `tds_ordered_set set;` whose `stamps` field is a
`tds_hamt_map`, so `cursor.set.stamps` is reachable from any consumer. The design forbids this twice
("never exposes sparse stamps"; "private sparse labels never enter the cursor contract"). The C tests
make the leak load-bearing, so the fix is an opaque handle plus test rework. **All eight other ports
are clean** — verified by grepping every cursor surface for stamp/label.

### 2.4 A near-miss worth recording

The single highest-risk property in the Merkle family — that a cursor edit produces byte-identical
`MST2` blocks and root digests to the ordinary operation — **holds in all nine ports**, because every
cursor edit delegates to the canonical `SetItem`/`Remove`. But Rust and C++ duplicate the canonical
replace sequence in a sibling helper rather than calling it (Rust for a real reason: no `K: Clone`
bound), and **no port asserts the property**. The strongest existing assertions are inequality
(`assert_ne!` on root hashes). Two lines per port would pin it:

```csharp
Assert.Equal(source.SetItem(0, "zero").RootHash, exact.SetNextValue("zero").Snapshot().RootHash);
Assert.Equal(source.Remove(0).RootHash, exact.DeleteNext().Snapshot().RootHash);
```

Relatedly, the mandated cross-language golden vectors for cursor edit histories do not exist, and the
cursor tests use a *different policy domain id per port*, so their snapshots are not byte-comparable
even in principle.

## Part 3 — Defects Fixed In This Review

### 3.1 C#: the invalid default cursor was only half-invalid (15 cursor types)

The design requires that "every member of an invalid default throws the same documented exception",
and the mature rope cursor implements it with a `_ = Version;` guard on a computed `Position`. Fifteen
checkpoint cursor types instead declared `public int Position { get; }` as an auto-property, so on
`default(SortedBagCursor<T>)`:

- `Count`, `IsAtEnd`, both peeks, both moves, every edit, and `Snapshot()` threw, but
- `Position` returned `0`, `IsAtStart` returned `true`, and `SeekRank(0)` returned the invalid cursor
  itself via the `position == Position ? this` identity shortcut.

Patricia and Merkle — shipped last — already had the correct guard, which is how the idiom drifted:
it was established late and never back-propagated. The existing tests only asserted
`default(X).Snapshot()` throws, which is exactly the hole that let this through; the reference rope
test `DefaultValue_AllMembersRejectItExplicitly` asserts `Position` and `IsAtStart` explicitly.

Fixed by moving each to a backing field behind the established guard, and by adding
`CursorDefaultValueContractTests` to both affected test projects covering all fifteen types.

### 3.2 C#: measured rope `Seek` silently discarded the lineage fragment cache

`MeasuredRopeCursorEngine.Seek` called `CreateContext(...)` without the `fragmentCache` argument,
whose default is `null`, so a fresh cache was allocated and the destination chunk re-measured — up to
2,048 `Measure` callbacks. `SeekByMeasure` and `Materialize` both thread it through correctly; `Seek`
was the sole outlier. This broke the API specification's claim that a seek on an existing lineage
"prepares at most the selected ordinary chunk and shares its element measures with descendant edit
versions".

Fixed by passing `source.FragmentCache`, and locked with a new zero-callback assertion in
`MeasuredRopeCursorMeasureCacheTests` alongside the existing ones for measure-seek and movement.

### 3.3 C: a `destroy` hook ran on uninitialized memory

In `ft_sorted_map_entry_init`, when the key allocation succeeds and the value allocation fails, the
cleanup path called `ft_sorted_map_entry_destroy_value`, which invokes the caller's `key_type.destroy`
hook on a buffer that `ft_value_copy` had not yet written. For any key type with a destroy hook this
frees a garbage pointer. Reachable from four cursor entry points. Fixed by releasing the raw storage
directly.

### 3.4 C: three self-alias reference leaks and two source-destroying copies

`ft_sorted_multiset_cursor_snapshot` and `ft_sorted_map_cursor_snapshot` lacked the
`result == &cursor->set` guard that every sibling cursor has (`ft_tree_cursor_snapshot`,
`ft_rope_cursor_snapshot`, `ft_measured_rope_cursor_snapshot`, `ft_reversible_deque_cursor_snapshot`).
Since `ft_tree_copy` retains unconditionally, a self-aliased snapshot leaked the tree permanently;
the map variant additionally leaked its entry context and left the cursor reporting invalid.

`ft_interval_tree_copy` and `ft_interval_tree_i64_copy` lacked the `source == destination` guard, so
a self-copy re-initialized the destination (leaking the original rep) and then read a disposed source
— the type-erased variant `memset`s the source first. All four fixed with the sibling pattern.

### 3.5 C: the Ordered multimap group copy could free raw heap bytes as pointers

`tds_ordered_multimap_group_copy` discards the status of `tds_ordered_set_clone`, which writes its
destination **only on success**. On refcount saturation or a deque-copy failure the destination stays
whatever the HAMT allocated; `tds_ordered_set_destroy` then guards on non-`NULL` fields, which pass on
garbage, and releases wild pointers. Reachable directly through `tds_ordered_multimap_cursor_try_add`.

Fixed by zeroing the destination first, which converts undefined behavior into a wholly uninitialized
group that `destroy` correctly rejects — matching the design's rule that a newly initialized output
must be "either wholly valid or wholly uninitialized and safe to dispose". This is a mitigation, not a
cure: the underlying `ft_copy_fn` signature has no failure channel, so the clone failure is still
silent data loss. A fallible copy callback is the real fix.

### 3.6 OCaml: `locate` reported a miss when the predicate already held at the identity

`measured_tree.ml` guarded with `if predicate (Measures.empty tree.tree_policy) then None`, which is
the opposite of both the repository contract ("a predicate already true at the identity selects zero
for a nonempty rope") and the module's own `.mli` ("returns the **first** element whose inclusive
prefix measure satisfies `predicate`"). `cursor_by_measure` maps the `None` to an *end* cursor with
`found = false`, so an identity-true predicate landed at position `n` instead of `0`. This leaked to
`Measured_sequence.locate`, `Measured_rope.locate`, and `cursor_seek_by_measure` — the whole OCaml
measured family. Two reviewers found it independently; all eight sibling ports behave correctly.

Verified by compiling the module before and after and running a driver:

```text
                            before          after
identity-true, nonempty  :  None            Some(index=0, prefix=0, value=2)
identity-true, empty     :  None            None
sum >= 6                 :  Some(index=2)   Some(index=2)
sum >= 2                 :  Some(index=0)   Some(index=0)
sum >= 17                :  Some(index=3)   Some(index=3)
sum >= 99                :  None            None
```

The guard was redundant for the empty case — `descend` already returns `None` for `Empty` — and both
existing OCaml tests use predicates false at the identity, so neither changes behavior. A regression
test was added with every assertion checked against the compiled module.

## Part 4 — Defects Recorded, Not Fixed

Ordered by severity. Each was verified against the source; none was changed because it needs a design
decision, spans all nine ports, or could not be validated here.

### 4.1 Correctness

| Port | Defect | Consequence |
| --- | --- | --- |
| OCaml | Multimap cursor insert calls `Option.get` on a content re-scan | `Invalid_argument` on `nan` and any value non-reflexive under the hash policy |
| OCaml, Haskell | Multimap cursor delete discards the removed flag | Reports success, changes nothing, loops forever on the same pair |
| Kotlin | `PersistentIntervalMap` keeps two indexes with **different orders** (`IntervalTree` low-only vs `SortedMap` lexicographic) | `findOverlap` and `findOverlapCursor` return different "first overlaps" for equal-low intervals; `validateStructure` only checks set equality so it cannot catch this |
| OCaml | Interval-tree equal-low placement is inverted vs all eight siblings, **and** the miss cursor returns the lower bound while insert lands at the upper bound | A `found = false` cursor does not identify where the matching insert goes |
| Kotlin | `FingerTreeCursor.measureBefore` is `prefixMeasure(position)!!` | NPE for `MaxMeasure`/`MinMeasure` — shipped policies whose monoid identity *is* `null`. `measureAfter` has no `!!`, so `combine(before, after) == measure` is unsatisfiable |
| Python | `MeasuredSequence.replace_next` short-circuits on element `__eq__` | Replacement silently discarded and the measure not recomputed. The design forbids this twice ("a generic cursor has no element-equality shortcut"). TypeScript's equivalent uses `Object.is`, which is safe |
| OCaml | Zero-length `apply` fires n measure callbacks and returns a fresh version | Design requires "zero length returns the same cursor without callbacks" |
| Rust | `OrderedCursorSearch.found` has **inverted polarity** in `try_insert` vs `find_cursor` | Generic code over the shared type gets silently wrong answers; the test locks the inversion in |
| Python | PSQ value no-op uses `is`, not equality | `set_next(3.0/2, v)` on an entry holding `1.5` publishes a new version where every sibling returns the receiver |
| C++ | Moved-from cursor retains `position_` while `tree_` is moved-from | `count()` returns 0 while `position()` returns the old value; `insert` dereferences a null policy. The design *requires* C++ moved-from behavior be documented; the class has one comment line |
| C | `ft_interval_tree_insert`/`_remove_one` prepare the result before reading the source | Exact result/source aliasing destroys the caller's tree — contradicting a written API-notes guarantee. Not reachable through the cursor layer, which uses a distinct local |
| C | `ft_sorted_map_entry_copy` calls `abort()` on OOM | **Every** sorted-map cursor edit terminates the process under memory pressure. Structural: `ft_value_copy` returns `void`. This falsifies `api-notes.md:376` |

### 4.2 Complexity

Silently super-logarithmic operations, all with the required metadata already cached:

| Port | Operation | Actual | Expected |
| --- | --- | --- | --- |
| C++ | `bound_rank` backing **every** sorted-set/map/canonical bound factory | O(n) | O(log n) |
| C++ | `item_at` (canonical peeks, PSQ rank) | O(rank) | O(log n) |
| Rust, Python | Canonical-set peeks (`iter().nth`, `tuple(self.set)[i]`) | O(n), O(n) **with O(n) allocation** | O(h) |
| TypeScript | Sorted-bag cursor delete (`toArray`+`splice`+`SortedBag.from`) | O(n log n), zero sharing | O(log n) |
| TypeScript | Interval-tree cursor delete (rebuilds the tree) | O(n log n), zero sharing | O(log n) |
| Rust, TS, Python | PSQ rank lookup (`iter().nth`, `Array.from`, `tuple`) | O(n) | O(log n) |
| OCaml | `Persistent_chunked_bit_set` is `Set.Make(Int)` — no words, no chunks; `select` has no early exit | Θ(n) per cursor step | O(log w) |
| OCaml | Every sorted bag/set/map edit copies the whole array | O(n) | documented checkpoint, but the array bounds are undocumented |
| TS, Python | Reversed-orientation range insert loses all sharing (measured 125× and 93× slowdowns) | O(n) | O(m + log n) |
| Haskell, OCaml | All four reversible-deque cursor edits round-trip through a list | Θ(n) | O(log n) |

A complete key-order traversal is **O(n²)** in C++, Rust, TypeScript, and Python, against the design's
explicit "a complete key-order traversal after one seek is O(n)".

Two systemic patterns:

- **The `MaxHigh` augmentation is abandoned by the cursor overlap path in seven of nine ports.** Only
  C# does a real augmented descent. Everyone else falls back to a rank scan while the augmentation
  sits unused in the same object; OCaml's `cached_maximum_high` is maintained on every edit and read
  by nothing but an accessor. Haskell's is worst (Θ(n log n), no early exit) *and* its README claims
  the pruning behavior — true of `IntervalTree.findOverlap`, false of `findOverlapCursor`.
- **`measureAfter` does a full structural split where `measureBefore` does a read-only descent** in
  Kotlin, Rust, TypeScript, and Python. Instrumented: 22 vs 44 combines plus discarded nodes for a
  *read*. C# and Haskell are immune via a split-pair representation.

### 4.3 The design document over-claims

Three passages describe representations that exist in **zero** ports:

1. **Merkle** (`:1585-1593`, `:1632-1640`) specifies frames retaining "original trusted node/block
   identity", "entries and complete in-memory child subtrees on both sides", and concludes
   "within-block movement is O(1) … a complete traversal is O(n)". All nine ports store `(tree,
   position)` and re-descend from the root on every peek, so a traversal is **O(n · Σ(e_i + 1))**.
2. **Patricia** (`:1560`) states "one move is O(W) worst and **O(1) amortized over a complete linear
   in-order traversal**". Movement is O(1) (it rewrites an integer), but *reading* after moving is an
   unconditional O(W) root descent, so traversal is Θ(n·W). The amortized bound requires the retained
   frame stack at `:1523-1535`, which no port implements.
3. **RRB** (`:708-733`) specifies radix frames with cumulative sizes and packed/relaxed flags.
   Implemented nowhere; every port is Profile R.

Two of these are correctly disclaimed *elsewhere* — `data-structure-catalog.md` and
`semantic-contracts.md` both say the shipped cursors are snapshot-plus-gap/rank checkpoints — but the
family sections remain unscoped, and a reader consulting the normative design for a bound gets the
wrong answer. Also over-claimed: `semantic-contracts.md:821-822` attributes "ordered before/after
measures, absolute prefix search, unconditional replacement" to OCaml's measured-rope cursor, three of
which it does not have.

Two workspace documents are **actively false** rather than merely absent:

- `src/CSharp/docs/FingerTree/api-specification.md:626-627` — *"No deque, RRB, reversible-deque,
  raw-finger-tree, bookmark, or rebase cursor is implied by this surface."* The reference port ships
  four of those five.
- `api-specification.md:621-622` claims `RopeText.LineColumnOf(cursor)` works *"without first
  materializing a snapshot"*; the implementation calls `Snapshot()` unconditionally. The XML doc on
  the method is correct, so the spec is the outlier.

Plus `src/C/FingerTree/docs/api-notes.md:376` (allocator failures "leave cursor state unchanged" —
contradicted by the `abort()`), and `:332`/`:455-456` (universal result/source aliasing — false for
four functions).

### 4.4 Documentation is the largest single gap

Of twenty-two cursor families × nine workspaces, exactly **two** workspace documents describe a
non-rope cursor family: `src/C/Hamt/docs/merkle-search-tree.md` and `src/C/Hamt/docs/api-specification.md`
(Patricia). A repo-wide grep for the sequence, sorted, augmented, Ordered, Patricia, and Merkle cursor
type names across every `src/**/*.md` returns essentially nothing outside the design proposal itself.

This is uniform, not port-specific — including in C#, the semantic reference, whose
`docs/Hamt/api-specification.md` and `docs/Ordered/api-specification.md` contain zero occurrences of
"cursor" while the projects ship five and three public cursor types respectively.

The rope tier by contrast is documented exhaustively per port, with per-language deviation records and
disclaimers. That is the template; it simply was not applied to the twenty other families. Shipment-unit
items 4 ("honest complexity and allocation documentation") and 5 ("workspace usage/API/validation
updates") are unmet for the entire post-rope shipment.

**This is where the goal's Haskell/C exception requirement fails.** The deviations that most need
recording have no home anywhere:

- Haskell's `Eq`-free Patricia replacement is documented *in source* but not in the workspace README.
- Haskell and OCaml collapsing duplicate-key and wrong-gap into one failure value.
- C's ownership rules: the "`result` must be uninitialized or the exact source" precondition is in
  `api-specification.md` for HAMT but **not in the header a consumer reads**, and passing a distinct
  live cursor silently leaks its retained version.
- C's borrowed-peek lifetime versus aliased publish — the two documented guarantees combine into a
  use-after-free that nothing warns about.
- OCaml's array-backed Ordered family (no hash index at all; `Hash_policy.hash` is never called under
  `lib/ordered`), making the root `CLAUDE.md` claim that the indexes "compose public CHAMP and
  FingerTree surfaces" false for OCaml.
- Kotlin's map cursor hashing on every positional peek, where the contract says navigation must not
  hash.
- The O(log² n) stamp-location tier in Rust, Python, C, and C++, where C and C++ docs print
  O(w + c + log n).

### 4.5 Test coverage

The asymmetry is quantifiable: C# rope-cursor tests total ~3,232 lines; the sequence-cursor suite is
165. Most ports have roughly one happy-path test per family.

Untested in **all nine ports**: the interval-tree cursor's returned position with an equal-low run
(the one behavior with a written normative rule, and exactly what OCaml gets wrong);
`SeekNextOverlap` driven to exhaustion; `AtRank(Count)` end-gap construction; PSQ `SetItem`'s
hit/miss position asymmetry; cursor `set-item` on Merkle; the structural validator after any cursor
edit; relabel-through-cursor; two of the multimap's three reanchor branches; a comparer that throws
mid-operation; and comparer retention on an *empty* collection.

Three test-quality problems worth naming:

- **Vacuous assertions.** TypeScript and Python assert upper bound `4` on a 4-element bag — identical
  to `size`, so an implementation returning "end" for every query passes. C# appends a sentinel.
- **Circular assertions.** An OCaml sorted-bag test compares the post-delete list against a filter of
  a list derived from the bag itself.
- **Commutative-measure blind spots.** C#'s measured-cursor test uses `SizeMeasure` with
  `MeasureBefore == MeasureAfter == 2`, so swapping the combine arguments would be undetectable. The
  design requires a noncommutative monoid. Kotlin and Haskell do this correctly.

C has **zero allocation-failure injection** in the FingerTree and Ordered cursor tests, which is
precisely why §3.3, §3.5, and the `abort()` path are invisible to the gate — while the C HAMT
workspace ships exhaustive failpoints.

## Part 5 — Items Needing Independent Reproduction

### 5.1 OCaml `join` may never rebalance the outer combination

`measured_tree.ml`'s `join` descends and rebuilds with the non-rebalancing `make_node` when one side
exceeds the weight bound, so the invariant may never be restored. Measured depth for repeated `snoc`
was reported as ≈ n/5 rather than log n, which would make every cursor peek, seek, and edit across
the entire OCaml FingerTree workspace Θ(n) for end-built sequences — exactly what repeated
`cursor_insert` at an end gap produces. `validate` checks cached lengths but not balance, and every
test uses the balanced `of_list` builder, so the suite cannot see it.

**These figures came from a mirrored reimplementation, not the shipped module.** Reproduce in-tree
before acting. If confirmed, the repair is an Adams-style rotation-based `balance` smart constructor
plus a weight-invariant assertion in `validate` — not a one-line change.

### 5.2 The C FingerTree and Ordered workspaces do not build with the local MSVC toolchain

`.\build.ps1 -Workspace FingerTree -RunTests`, the command documented in `CLAUDE.md`, fails:

```text
vcruntime_c11_stdatomic.h(12): fatal error C1189: #error: "C atomic support is not enabled"
```

`persistent_interval_map.c` includes `<stdatomic.h>`, and neither the CMake lists nor the presets pass
`/experimental:c11atomics`. This is pre-existing and unrelated to the cursor tier, but it means the
documented C gate is currently red on this machine. I validated the C changes with GCC 16.1 instead
(clean build, 11/11 FingerTree and 15/15 Ordered CTest suites). Either add the flag to the MSVC preset
or record GCC/Clang as the supported local C lane.

## Part 6 — Recommended Order

1. **C memory safety** — the remaining items: the `abort()`-on-OOM copy hook (needs a fallible
   `ft_copy_fn`), the interval-tree facade aliasing, and the set-cursor typedef hole.
2. **The three reachable crashes/data-loss bugs** — OCaml multimap `Option.get`, OCaml/Haskell silent
   no-op delete, Kotlin `measureBefore` NPE. All have minimal patches and concrete failing inputs.
3. **Python `replace_next`** — delete the two-line `__eq__` shortcut; every derived caller already
   does its own no-op test.
4. **Kotlin's two-index interval map** — two public surfaces on one object return different first
   overlaps.
5. **The four self-contained complexity fixes** — C++ `bound_rank`/`item_at` (one function fixes every
   ordered-search cursor in that header), Rust and Python canonical peeks, TypeScript bag and
   interval deletes. All required metadata is already cached.
6. **Design-document honesty** — scope the Merkle, Patricia, and RRB complexity paragraphs to the
   unshipped focused tier and state the actual checkpoint costs; correct the four false workspace
   claims, starting with the C# API specification.
7. **Documentation** — one cursor section per family group per workspace, using
   `src/C/Hamt/docs/merkle-search-tree.md` as the template. Prioritize the deviations in §4.4; this is
   the goal's "clearly documented exceptions" requirement and it is currently unmet outside the rope
   tier.
8. **The Ordered multimap decision** (§2.1) — implement `FocusedGroup` or amend the design. Do not
   leave a nine-port violation of an explicit prohibition undocumented.
9. **Tests** — the Merkle digest-parity assertion (two lines, highest value per line in the whole
   list), then the universal gaps in §4.5, then C allocation-failure injection.

## Validation Performed

| Gate | Result |
| --- | --- |
| C# `dotnet build` (Debug, serialized) | Succeeded, 0 warnings, 0 errors |
| C# `test.ps1` (full solution) | 1,529 passed, 0 failed, 0 skipped (Numerics 319, HAMT 354, FingerTree 724, Ordered 80, Tungsten 52) |
| C FingerTree, GCC 16.1 + Ninja + CTest | Built clean; 11/11 suites passed |
| C Ordered, GCC 16.1 + Ninja + CTest | Built clean; 15/15 suites passed, including `ordered_c.cursor` |
| C FingerTree/Ordered, MSVC | **Blocked pre-existing** — see §5.2 |
| OCaml `measured_tree` | Type-checked standalone; behavior verified before/after with a compiled driver; all new test assertions checked against the compiled module |
| OCaml `dune build` (full) | **Not run** — the available switch lacks `uutf`, `alcotest`, `qcheck`, `zarith`, `digestif` |
| Rust, TypeScript, Python, Kotlin, Haskell, C++ | **Not run** — no changes were made to those ports |
