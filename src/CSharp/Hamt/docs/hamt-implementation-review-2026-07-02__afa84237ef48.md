# HAMT Implementation Review and Improvement Report

- Status: Completed review with implemented improvements
- Created (UTC): 2026-07-02T15:40:49Z
- Repository HEAD: 95820012c35a2eb7c16ea62254ed80746694a415
- Audience: Maintainers of `Tools.DataStructures.Hamt`
- Scope: Review findings, implemented changes, declined findings, and validation results for
  `PersistentHashMap<TKey, TValue>` and `PersistentHashSet<T>`

## Methodology

The review ran as a 102-agent workflow over the implementation as of the HEAD above: six parallel
review dimensions (algorithmic correctness; persistence/immutability/aliasing; API contract vs
documentation vs BCL conventions; performance and allocation; test coverage; C# design and idiom),
each raw finding then judged by two independent adversarial verifiers — one attacking the factual
claim against the real code, one attacking whether acting on it improves the library. The sweep
produced 48 raw findings: 46 confirmed, 2 contested, 0 refuted. Several findings were duplicates
across dimensions; deduplicated, they reduce to the items below. Two verifiers built and executed
repros against the compiled library (enumerator copy corruption; `Clear()` identity).

## Correctness assurance (no defects found)

The correctness dimension traced every flagged hazard and found the core algorithms sound. In
particular:

- **Shift arithmetic**: `Index` is never invoked with `shift >= 32`. Bitmap nodes exist only at
  shifts 0–30; children of a shift-30 node are hash-terminal nodes, so the C# shift-count masking
  hazard (`hash >> 35 == hash >> 3` for `uint`) is unreachable. The `shift >= 32` guard in
  `MergeHashNodes` is a provably unreachable invariant assertion (two differing 32-bit hashes must
  split at or before shift 30); it now carries a comment saying so.
- **Collapse logic**: `Rebuild` hoists only hash-terminal nodes (leaves and collision buckets),
  which is level-independent and therefore safe; no bitmap node can ever end up with a single
  hash-terminal child, which proves `FromRoot`'s null-forgiving `root!` can never observe null with
  a positive count.
- **Collision buckets** never shrink below two entries; two-to-one removal demotes to a leaf;
  index splicing, count bookkeeping, `checked` arithmetic, and value-equal identity returns were all
  confirmed by adversarial model runs (hashes differing only in bits 30–31, and equal-hash buckets).
- **Safe publication**: all node state is written before publication and never mutated afterward;
  no memory-model hazard for concurrent readers of published versions was found.

## Implemented improvements

All changes below are implemented, and the full suite (47 tests, up from 20) passes in Debug and
builds warning-free in Release.

### API contract and correctness of annotations

1. **`[MaybeNullWhen(false)]` on try-pattern outs** (major). `TryGetValue` and `TryRemove` assigned
   `value = default!` on the miss path while their signatures promised a never-null out, defeating
   nullable analysis for the library's most-used members and mismatching the
   `IReadOnlyDictionary<TKey, TValue>.TryGetValue` annotation. Both now carry the attribute.
2. **`PersistentHashSet<T>` implements `IReadOnlySet<T>`** (major), adding the two missing
   predicates `IsProperSubsetOf` and `IsProperSupersetOf`. The set now interoperates with any API
   typed `IReadOnlySet<T>`, matching `HashSet<T>`/`ImmutableHashSet<T>`/`FrozenSet<T>`. Note the
   consequence: set-aware consumers (including xUnit's set-overloaded asserts) now honor the set's
   own comparer semantics.
3. **`TryGetKey` on the map and `TryGetValue` on the set** (minor). The docs promised retention of
   the originally stored key/item object, but no API could retrieve it short of O(n) enumeration.
   Both members mirror `ImmutableDictionary.TryGetKey`/`ImmutableHashSet.TryGetValue` semantics
   (echo the query object back on miss).
4. **`Clear()` no-op identity** (minor). `Clear()` on an already-empty custom-comparer map
   allocated a fresh instance, breaking the type's otherwise-universal no-op identity convention;
   it now returns `this` when empty, and the set wrapper's compensating guard was simplified to
   plain delegation.
5. **Exception messages include the key** (contested finding, adopted in part). `Add` and the
   indexer now format the offending key into `ArgumentException`/`KeyNotFoundException` messages,
   matching BCL conventions. The stricter-than-`ImmutableDictionary` `Add` semantics (throw on any
   existing equivalent key) were deliberately kept and are now called out explicitly in the XML docs
   and API specification.

### Performance and allocation

6. **Single-pass `Add`/`TryAdd`** (major). Both previously ran `ContainsKey` then `SetItem` — two
   hash computations and two full trie descents per successful insertion, doubled comparer cost for
   expensive comparers. The internal `Node.Set` protocol gained an `overwrite` flag (the BCL
   `InsertionBehavior` pattern): in add-only mode an existing key returns the current node with
   nothing allocated. `Add` is now a thin wrapper over `TryAdd`. `PersistentHashSet.TryAdd`
   inherits the single pass.
7. **Frame-based, allocation-free, copy-safe enumerator** (fixes one major and three minor
   findings at once). The old enumerator pushed all (up to 32) children of each branch onto a
   growable heap `Node[]` starting at 8 slots — guaranteed `Array.Resize` churn on any non-trivial
   map, ~218-slot worst case, and a real hazard verified by repro: struct copies shared the stack
   array while diverging on the count, so interleaved advancement silently dropped entries. The new
   design keeps at most seven `(children, index)` frames in an `[InlineArray]` buffer inside the
   struct: zero heap allocation, no growth path, and copies advance fully independently (shared
   child arrays are immutable; per-frame indices are inline). The same state machine yields
   identical enumeration order. A new test pins copy independence at the previously-corrupting
   interleaving, and a full-depth (seven-frame) trie is now enumerated in tests.
8. **Struct enumerator for the set** (major). `PersistentHashSet.GetEnumerator()` returned
   `_map.Keys.GetEnumerator()` — an iterator-object allocation plus double interface dispatch per
   element on the set's primary enumeration path, paid internally by `Intersect`, `IsSubsetOf`, and
   `SetEquals`. The set now exposes a public struct `Enumerator` wrapping the map's, with explicit
   interface fallbacks, in the exact pattern of the map.
9. **`Unit : IEquatable<Unit>`** (minor). The empty value struct previously fell back to
   `ObjectEqualityComparer`, boxing on every duplicate-`Add` fast path through
   `EqualityComparer<TValue>.Default.Equals`; it now implements `IEquatable<Unit>` and the
   comparison devirtualizes.
10. **Iterative lookup path** (minor). `TryGetValue` previously recursed through up to seven
    virtual `Node.TryGet` dispatches. Lookup is now a flat loop in the map class over sealed node
    types (`while (node is BitmapIndexedNode …)` plus terminal leaf/collision checks), which also
    serves `TryGetKey` through a shared `TryGetEntry`. The abstract `TryGet` and its three overrides
    were deleted (~70 lines).
11. **`Hash` as a base-class readonly field** (minor). `HashNode` declared `Hash` as an abstract
    auto-property overridden identically in both subclasses — a virtual call at merge sites where
    the receiver type is not statically known. It is now a single readonly field on
    `HashNode(uint hash)` with primary-constructor chaining.
12. **`CollisionNode.Create(HashNode, LeafNode)`** (minor/info, two findings). The old
    `ToEntries()` protocol allocated throwaway single-element arrays for leaves and handed out the
    collision bucket's internal array — a latent aliasing trap flagged by the immutability
    dimension. `Create` now writes entries directly from typed operands; `ToEntries` is deleted, so
    the trap is gone structurally rather than by convention.

### Design and ergonomics

13. **`DebuggerDisplay` + `DebuggerTypeProxy`** (minor) on both public types, flattening contents
    into an entries array in debugger watch windows, as the BCL immutable collections do.
14. **`Enumerator.Reset` is now an explicit interface implementation** (minor) on both enumerators
    instead of a public always-throwing member, matching `List<T>.Enumerator` and the FingerTree
    sibling's convention.
15. **`RootForTesting` typed and node classes made assembly-internal** (minor). The map's hook is
    now `internal Node?` and the node classes (`Node`, `HashNode`, `LeafNode`, `CollisionNode`,
    `BitmapIndexedNode`, `Entry`) are `internal`, so white-box tests can assert trie shapes and
    reference-level structural sharing — which they now do (see below).
16. **Idiom polish** (info): collection expressions in `MergeHashNodes`, removal of the redundant
    `_ = shift;` discards, and the unreachable-guard comment in `MergeHashNodes`.
17. **Per-member complexity and allocation remarks** (minor). XML docs on all core map operations,
    both enumerators, and every set-algebra member now state trie-depth bounds, allocation
    behavior, and — on the four probe-building set operations — the O(m) `HashSet<T>`
    materialization, per the repository documentation standard.

### Documentation contract fixes

18. **`api-specification.md`**: added the missing `SetItems` bullet (the Map Contract list omitted
    a public member); added `TryGetKey`, set `TryGetValue`, `IReadOnlySet<T>`, and the proper
    subset/superset predicates; documented single-pass `Add`/`TryAdd` and no-op identity as
    contract; stated the value-identity caveat on last-wins semantics (an equal incoming value
    retains the stored value object); corrected the update-allocation claim from O(depth + c) to
    O(b·depth + c) array storage / O(depth + c) node objects (the old figure was inconsistent with
    the spec's own parameterization); tightened the enumeration bound from O(branch-factor × depth)
    auxiliary stack to seven inline frames with no heap allocation; documented `SetItems` and
    `Union`/`Except` as the sanctioned bulk updates (no `AddRange`/`RemoveRange`); and called out
    the `Add`-strictness deviation from `ImmutableDictionary`.
19. **`README.md`**: notes `IReadOnlySet<T>`, allocation-free lookups, single-pass adds, and the
    struct enumerators.

### Test-suite expansion (20 → 47 tests)

The tests dimension found five major gaps; all are closed.

- **Collision-bucket enumeration had zero coverage** — the `case CollisionNode` branch of
  `MoveNext` was never executed by any test. Now covered deterministically (bucket at the root,
  bucket under branches, bucket mixed with leaves) and by a new CsCheck property test replaying
  random histories under a four-bucket hash (`hash = key & 3`) against `Dictionary`, which also
  enumerates on every step. The collision-test `AssertMatches` helper now round-trips enumeration
  against the model in both directions.
- **Collision hash-mismatch paths were never exercised** — probes, removals, and inserts whose hash
  shares a bucket's trie path but differs (including the deep `MergeHashNodes(collision, leaf)`
  split down to shift 10) are now covered.
- **Set algebra ran only under the default comparer** — a comparer-propagation bug in the six
  probe-building methods was undetectable. A case-insensitive algebra test now pins semantics,
  result-comparer preservation, and stored-object retention; the CsCheck algebra test gained the
  two proper-subset/superset predicates.
- **Structural sharing and collapse shapes were asserted nowhere** (`RootForTesting` was dead
  surface). A new white-box suite asserts: root shape transitions (null → leaf → collision →
  bitmap), collapse of buckets and full-depth single-child chains back to a leaf on removal,
  reference-shared sibling subtrees and shared collision buckets across versions, and root
  preservation on no-op updates.
- **Enumerator contract semantics were untested** — `default(Enumerator)` robustness, `Current`
  bracketing, `MoveNext` after exhaustion, `Reset` throwing, copy independence, full-depth
  (seven-frame) trie enumeration, and order stability are all pinned now.
- Also added: null-guard tests for all thirteen sequence-accepting members, `TryGetKey`/set
  `TryGetValue` object-identity tests, bucket identity-return and key-retention tests, `Keys`/
  `Values` alignment with pair enumeration, set empty-singleton canonicalization, and direct
  struct-enumerator use on the set.

One pre-existing test changed meaning: `CustomComparer_DefinesEqualityAndRetainsFirstItem` used
`Assert.DoesNotContain("ALPHA", set)`, which under `IReadOnlySet<T>` now routes through the set's
own case-insensitive comparer and correctly reports the item as contained. The assertion was
rewritten to state its actual intent (the literal `"ALPHA"` object is not stored) via an ordinal
comparer over enumeration plus a `TryGetValue` retention check.

## Declined and deferred findings

- **Comparer devirtualization (null-comparer trick)** (contested): storing `null` for default
  comparers and branching at leaf comparisons could devirtualize hashing for value-type keys, as
  `Dictionary<TKey, TValue>` does. Declined for now: the win is bounded, .NET 10 dynamic PGO
  recovers part of it, and the change touches every comparison site. Revisit only with
  BenchmarkDotNet evidence on int- and string-keyed workloads.
- **`Add` alignment with `ImmutableDictionary.Add`** (contested): keeping the strict
  throw-on-any-duplicate semantics was judged correct — the behavior is documented, tested, and
  arguably more predictable; the deviation is now disclosed in docs instead.
- **Struct `KeyCollection`/`ValueCollection` views** (minor): `Keys`/`Values` remain yield-return
  iterators. This matches the BCL's own `ImmutableDictionary` behavior, the allocation is now
  documented on the properties, and callers wanting allocation-free traversal can foreach the map
  directly.
- **`AddRange`/`RemoveRange`** (info): not added; `SetItems` (map) and `Union`/`Except` (set) are
  documented as the sanctioned bulk updates for this library's lean scope.
- **Mutable builder / transient bulk construction** (info): `CreateRange`-style building generates
  O(n·depth) transient garbage by design. A `Builder` with edit-token-owned nodes (the
  `ImmutableDictionary.Builder`/Clojure-transient pattern) is the standard remedy and would be a
  self-contained follow-up; benchmark 10⁴–10⁶-entry builds first to size the win. The existing
  mutable-builder proposal in this repository covers FingerTree only.

## Validation

- `dotnet test Hamt.sln` (Debug): 47/47 passed (baseline before changes: 20/20).
- `dotnet build Hamt.sln -c Release`: succeeded with zero warnings
  (`GenerateDocumentationFile=true` with CS1591/CS1573 as errors remains enforced).
- Public API changes: additive (`TryGetKey`, set `TryGetValue`, `IsProperSubsetOf`,
  `IsProperSupersetOf`, `IReadOnlySet<T>`, set `Enumerator`) except for two pre-publication
  breaks: both `Enumerator.Reset` members moved to explicit interface implementations, and the
  set's `GetEnumerator()` return type changed from `IEnumerator<T>` to the new struct enumerator.

## Change inventory

| Area | Files touched |
| --- | --- |
| Core implementation | `src/Tools.DataStructures.Hamt/PersistentHashMap.cs`, `src/Tools.DataStructures.Hamt/PersistentHashSet.cs` |
| Tests (extended) | `PersistentHashMapTests.cs`, `PersistentHashSetTests.cs`, `PersistentHashMapCollisionTests.cs`, `PersistentHashMapPropertyTests.cs` |
| Tests (new) | `PersistentHashMapEnumeratorTests.cs`, `PersistentHamtStructureTests.cs` |
| Documentation | `docs/api-specification.md`, `README.md`, this report |
