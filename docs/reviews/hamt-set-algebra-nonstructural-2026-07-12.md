# Issue: `PersistentHashSet<T>` Set Algebra Is Element-Wise, Not Structural

- Status: Resolved on 2026-07-12 — structural same-type algebra shipped across all six ports
- Created (UTC): 2026-07-12T19:38:09Z
- Repository HEAD: 02ebb40dd0f15a1a1f147cb1ad09320b52b4fd8d
- Audience: Maintainers of the HAMT family and consumers doing set algebra over `PersistentHashSet<T>`
- Scope: The complexity and API-shape gap in `PersistentHashSet<T>`'s `Union`/`Intersect`/`Except`/
  `SymmetricExcept` (and the subset/superset relations), why it is a genuine gap rather than a defect,
  the intra-repository inconsistency it creates, and the scheduled remediation

## Summary

This document's original source observations are retained below as the pre-remediation record.
`PersistentHashSet<T>`'s set-algebra operations formerly combined two collections **element by element**: they
accept an `IEnumerable<T>`, re-hash and re-probe every element, and cannot exploit structural sharing
between two set versions. Merging or intersecting two large sets that share ancestry therefore costs
work proportional to the operand size (`O(m)` single-item operations, or a full probe-set
materialization), not proportional to the divergence between the two versions.

This is **not a correctness defect** — results are correct and the XML documentation states the cost
honestly — but it is a real performance ceiling and an **inconsistency within the repository**: the
sibling Patricia integer sets (`PersistentIntSet`/`PersistentLongSet`) already provide structural,
reference-equality-pruned set algebra that takes another set of the same type, and the HAMT *map*
layer already ships the reference-pruned lockstep node traversal that a structural combine would
reuse. The missing piece is a node-layer combine, which is exactly Phase 2 of the already-planned
[proposal A2](../proposals/new-data-structures-2026-07-09.md) (HAMT structural diff / equality /
set-vs-set algebra).

This document was surfaced while writing the
[persistent-set-of-GUIDs design study](../proposals/persistent-guid-set-design-study-2026-07-12.md),
where element-wise set algebra was one of two limitations of `PersistentHashSet<Guid>`; the gap is
general to every `T` and is recorded here on its own.

## Resolution

The scheduled Phase 2 work is complete in C#, Kotlin, Rust, C++, C, and Haskell:

- every port now exposes same-type map/set union, intersection, difference, and symmetric difference;
- compatible operands are combined by aligned CHAMP slots with root/subtrie reference pruning and
  cached subtree cardinalities;
- same-type set relations use the structural core, while iterable/range APIs remain available;
- cross-policy behavior retains each port's receiver-policy contract or explicit compatibility gate;
- union retains receiver key representatives and uses right values for unequal overlaps;
- self/no-op cases preserve the receiver root or instance where the language surface exposes identity;
- C's implementation is failure-atomic and balances callback ownership through exhaustive allocation
  failpoints; and
- shared-ancestry tests prove the structural path performs zero rehashing, with collision-heavy model
  histories covering the complete truth tables.

The shipped complexity is `O(divergence)` for reference-shared ancestry after pruned nodes and
`O(n + m)` for independent compatible operands, with the documented equal-hash collision scan cost.
The historical line references and analysis sections below describe the old implementation and
should be read as provenance, not current-state instructions.

## Historical observed behavior

Before remediation, all four set-algebra methods on `PersistentHashSet<T>` took `IEnumerable<T>` and operated element-wise
([`PersistentHashSet.cs`](../../src/CSharp/src/Durable7.Hamt/PersistentHashSet.cs)):

| Method | Lines | Strategy | Cost |
| --- | --- | --- | --- |
| `Union(IEnumerable<T>)` | 210–219 | `foreach` item `result = result.Add(item)` | `O(m)` single-item updates, each a hash + path walk + path copy |
| `Intersect(IEnumerable<T>)` | 234–247 | materialize a probe `HashSet<T>` (`O(m)` time/space), then rebuild from `this`'s matching items via a bulk builder | `O(m)` probe build + `O(n)` iteration + full rebuild |
| `Except(IEnumerable<T>)` | 256–265 | `foreach` item `result = result.Remove(item)` | `O(m)` single-item removals |
| `SymmetricExcept(IEnumerable<T>)` | 280–290 | materialize a probe `HashSet<T>`, then toggle each item's membership | `O(m)` probe build + `O(m)` toggles |

The subset/superset/overlap relations that follow (from line 292) likewise materialize an
`IEnumerable<T>` argument into a probe `HashSet<T>` before testing.

Two structural facts follow from the method shapes:

1. **The operand type is `IEnumerable<T>`, never `PersistentHashSet<T>`.** Even when the argument
   *is* a `PersistentHashSet<T>` that shares most of its node graph with the receiver, the operation
   cannot see that — it re-enumerates and re-hashes every element as if the argument were an
   arbitrary sequence.
2. **No structural short-circuit exists.** There is no path where a reference-equal subtree shared by
   both operands is skipped in `O(1)`; the work always scales with the number of elements touched, not
   the number of elements that actually differ.

The XML documentation is accurate about this: `Union`/`Except` are documented as "Runs in `O(m)`
single-item updates," and `Intersect`/`SymmetricExcept` are documented as "Materializes … into a
probe `HashSet<T>` — `O(m)` time and space." So the gap is a **documented capability limitation**,
not a hidden regression.

## Historical impact

Let `A` be a `PersistentHashSet<T>` of one million items, and let `B = A` after a handful of
`Add`/`Remove` operations, so `B` shares nearly all of `A`'s node graph and differs from `A` in only a
few dozen elements.

- `A.Union(B)` enumerates all ~1,000,000 elements of `B` and performs a single-item `Add` for each —
  ~1,000,000 hash computations and path walks — even though the true union differs from `A` in only
  the few elements `B` added. A structural combine would visit only the diverging regions:
  `O(divergence)`.
- `A.Intersect(B)` allocates a probe `HashSet<T>` holding all ~1,000,000 elements of `B` (transient
  `O(m)` memory), then iterates all of `A` and rebuilds a fresh map, discarding the structural sharing
  entirely.
- Because the argument is `IEnumerable<T>`, none of this improves when the caller happens to pass
  another `PersistentHashSet<T>`.

For independently built operands (no shared ancestry) a structural combine would still be a
single-pass `O(n + m)` merge that reuses whole reference-equal subtrees where they coincidentally
align, versus the current `O(m)` re-hash-and-insert. The largest win, however, is the
version-vs-version case above, which is common in snapshot/undo, incremental-recompute, and
change-tracking workloads.

## Historical classification

- **Results are correct.** Comparer semantics, first-stored-instance retention, and no-op identity are
  all preserved; only the *cost* is higher than the structure allows.
- **The docs are honest.** The `O(m)` and probe-set costs are stated on each method, so no caller is
  misled about the current contract.
- **It is a missed optimization plus an API-shape limitation**, appropriate to schedule rather than
  hotfix.

## Historical intra-repository inconsistency

The repository already contained the better shape for a sibling family. `PersistentIntSet` exposes set
algebra that takes **another set of the same type** and delegates to the reference-pruned Patricia
core ([`PersistentIntSet.cs:57`](../../src/CSharp/src/Durable7.Hamt/PersistentIntSet.cs),
`:66`, `:75`):

```csharp
public PersistentIntSet Union(PersistentIntSet other)      // structural, O(divergence) for shared ancestry
public PersistentIntSet Intersect(PersistentIntSet other)
public PersistentIntSet Except(PersistentIntSet other)
```

The Patricia core short-circuits reference-equal operands and subtrees throughout
([`PatriciaMapCore.cs`](../../src/CSharp/src/Durable7.Hamt/Internal/PatriciaMapCore.cs):
`ReferenceEquals` guards at the whole-tree level and recursively per subtree), so its algebra is
proportional to the non-shared structure. `PersistentLongSet` is the 64-bit analogue.

So within one assembly, the integer-keyed persistent sets have structural set algebra over a same-type
operand, and the general hash-keyed persistent set does not — an avoidable inconsistency in the family
surface.

## Why the fix was well-supported by existing machinery

A structural combine on the HAMT is a node-layer feature, and the two pieces it needs already exist:

1. **Reference-pruned lockstep traversal.** `PersistentHashMap<TKey, TValue>` already ships
   `MapEquals` ([`PersistentHashMap.cs:412`](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs))
   and `Diff` (`:441`), whose node walk short-circuits on `ReferenceEquals` at the root (`:448`) and at
   every aligned subtree pair (`:573`, `:641`). This is precisely the traversal a structural
   `Union`/`Intersect`/`Except` would perform — the current `Diff` produces a change list
   (`MapDifference` records) rather than a combined map, but the pruned traversal itself is done.
2. **Bulk (transient) construction.** The one-freeze bulk builder used by `Intersect` today
   (`CreateBulkBuilder` / `ToImmutable`) is the same facility a node-layer combine would use to
   materialize any newly-built regions without per-item path allocation.

What was missing was only the *combine* operation itself (produce the unioned/intersected/differenced
node graph, reusing shared subtrees), and its surface on `PersistentHashSet<T>`.

## Historical recommended remediation (completed)

This was Phase 2 of the scheduled [proposal A2](../proposals/new-data-structures-2026-07-09.md), which
lays out the phasing explicitly:

- Phase 1 — `MapEquals` + `Diff(other)` enumerator. **Shipped** (present on `PersistentHashMap` today).
- Phase 2 — structural `Union`/`Intersect`/`Except` between two maps/sets sharing a comparer.
  **Shipped across all six ports.**
- Phase 3 — three-way `Merge` with an explicit conflict matrix (deferred until a consumer exists).

Concretely:

1. **Add a node-layer combine** to `PersistentHashMap<TKey, TValue>` mirroring the Patricia core's
   `UnionRight`/`IntersectLeft`/`Except`: a lockstep walk over two roots that returns a shared subtree
   unchanged when the two aligned subtrees are reference-equal, and otherwise merges bitmap slots. Gate
   it on comparer identity (`ReferenceEquals(_comparer, other._comparer)`), matching the existing
   `MapEquals`/`Diff` precondition.
2. **Surface same-type overloads on `PersistentHashSet<T>`**: `Union(PersistentHashSet<T>)`,
   `Intersect(PersistentHashSet<T>)`, `Except(PersistentHashSet<T>)`, and
   `SymmetricExcept(PersistentHashSet<T>)`, routed through the node-layer combine. Keep the existing
   `IEnumerable<T>` overloads for arbitrary sequences; the same-type overloads are the fast path.
3. **Preserve the existing contracts**: first-stored-instance retention, no-op identity (return the
   same instance when the result equals the receiver), and the documented empty/self cases.
4. **Complexity caveat to document honestly**: the `O(divergence)` bound holds for operands that share
   node ancestry. Independently built maps share no nodes, so structural combine is a single-pass
   `O(n + m)`; and because the HAMT is only *topology*-canonical (see the
   [frontier catalog](../reference/frontier-structure-catalog.md) CHAMP notes — equal maps built by
   different histories have identical trie topology but not reference identity), independent operands
   cannot benefit from reference pruning. State both bounds rather than claiming `O(divergence)`
   universally.

### Historical parity note

Per the [porting-and-semantic-parity guide](../guides/porting-and-semantic-parity.md), Phase 2 is
node-layer work in each language port. Proposal A2 already flags this as the priciest Tier-A item and
notes the C port's reference-counted nodes need real design (a combine must not retain both operands'
spines). The 2026-07-11 review's backlog also records that the Rust and C++ HAMTs still lack the
transient bulk builder the combine's materialization path would want — worth sequencing alongside.

## Historical severity and classification

- **Type**: performance/capability gap + intra-family API inconsistency. Not a correctness defect.
- **Severity**: Medium. It bounds a common workload (version-vs-version set algebra) at `O(m)` and
  leaks transient `O(m)` allocations for `Intersect`/`SymmetricExcept`/relations, but every result is
  correct and the cost is documented.
- **Blast radius of the fix**: ~3 map members (combine) + ~4 set overloads per language, plus tests;
  reuses the shipped pruned-traversal and bulk-builder machinery.

## How to verify the resolution

- Inspect the same-type map and set overloads in
  [`PersistentHashMap.cs`](../../src/CSharp/src/Durable7.Hamt/PersistentHashMap.cs) and
  [`PersistentHashSet.cs`](../../src/CSharp/src/Durable7.Hamt/PersistentHashSet.cs).
- Run the C# HAMT suite and its shared-ancestry structural-algebra tests documented in
  [`src/CSharp/tests/Durable7.Hamt.Tests/README.md`](../../src/CSharp/tests/Durable7.Hamt.Tests/README.md).
- Run the sibling HAMT suites. Their structural tests cover cached cardinalities, collision-heavy
  truth tables, representative/bias rules, cross-policy behavior, and zero-rehash pruning; the C
  suite additionally sweeps allocation failures.
- Compare the current cross-language status and complexity bounds in the
  [frontier structure catalog](../reference/frontier-structure-catalog.md#champ-canonicalization-upgrade-to-the-shipped-hamt).

## Relationship to other documents

- [Next data structures proposal (2026-07-09)](../proposals/new-data-structures-2026-07-09.md) — item
  A2 schedules this exact work; this issue is the concrete, source-grounded statement of its Phase 2.
- [Persistent set of GUIDs design study (2026-07-12)](../proposals/persistent-guid-set-design-study-2026-07-12.md)
  — where the gap was surfaced; element-wise algebra is one of the two `PersistentHashSet<Guid>`
  limitations discussed there.
- [Frontier structure catalog](../reference/frontier-structure-catalog.md) — the CHAMP canonicalization
  notes explain why the HAMT is topology-canonical but not reference-canonical, bounding what structural
  pruning can achieve for independently built operands.
- [Cross-language implementation review (2026-07-11)](cross-language-implementation-review-2026-07-11.md)
  — records the implementation state that preceded this remediation wave.
