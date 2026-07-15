# Benchmark-Independent Next Data Structures: Detailed C# Implementation Proposal

- Status: Historical design and active execution — Steps 1–3 shipped in C#, TypeScript, and Python; C# Step 4 focused/project gates green, full C# solution gate pending; benchmark-independent work only
- Created (UTC): 2026-07-14T19:23:49Z
- Repository HEAD: ab9a73c6ae20a3b0ee0627bfe810117450e20c3e
- Revised (UTC): 2026-07-14T21:14:47Z at faf53286375109fc598e40d5e6da7d1bff7e7415
- Shipment update (UTC): 2026-07-15T02:27:01Z at 6dbabd71db65ea2771a0b6581c119a367d96d106
- Audience: Maintainers selecting the next C# persistent-collection work after Axis 1 and the shipped Axis 2 tranches
- Scope: Repository-wide plan/proposal audit, candidate disposition, detailed contracts and validation gates for the next C# data structures that do not depend on postponed benchmark evidence, and their required sibling-language follow-through

> **Current shipment note.** This document preserves the proposal-time design reasoning and staged
> exit criteria. Since that reasoning was written, the one-descent HAMT factory operations,
> `PersistentHashBag`, and the independently owned neutral `PersistentOrderedSet` have shipped in
> C#, TypeScript, and Python. TypeScript and Python also carry the public construction-only CHAMP
> builder and complete transient-set relation surface added during parity work. The C#
> `RangeUpdateSequence` source, tests, and documentation now form a green focused-project
> checkpoint, but Step 4 remains unshipped until the full serialized C# solution gate passes. No
> sibling-language `RangeUpdateSequence` port has started. Future-tense wording in the completed
> sections is retained as historical implementation guidance, not current status.

## Decision

Proceed in this order:

1. Add persistent-HAMT single-pass `GetOrAdd`/`AddOrUpdate` operations — **shipped in C#,
   TypeScript, and Python**.
2. Implement `PersistentHashBag<T>` over `PersistentHashMap<T, int>` — **shipped in C#,
   TypeScript, and Python** (with language-appropriate wide total-count types).
3. Implement `PersistentOrderedSet<T>` as an independently owned composite in a new general
   `Tools.DataStructures.Ordered` project. Fork the useful dual-index and sparse-label mechanics;
   do not reference, wrap, or inherit semantics from Tungsten `PersistentAssociation` — **shipped
   in C#, TypeScript, and Python neutral Ordered packages**.
4. Implement `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` as the next genuinely new
   structure core, after locking its tag-action algebra and executable laws. Prefer a separate
   path-copied implicit AVL core over adding lazy tags to the existing measured finger-tree engine
   — **C# source, tests, documentation, and focused/project gates are complete; the full C# solution
   gate remains pending, and sibling ports have not started**.
5. After C# Step 4 fully ships, complete the remaining parity matrix: port Steps 1–3 to C, C++,
   Haskell, Kotlin, and Rust, and port `RangeUpdateSequence` to all seven sibling workspaces,
   including TypeScript and Python.
6. Keep `PersistentBiMap<TKey, TValue>` and a value-carrying interval map as reserve candidates.
   Give either a dedicated contract pass before promoting it into the active sequence.

This order deliberately distinguishes two notions of “next”:

- **Lowest implementation risk:** the persistent HAMT point kernel and `PersistentHashBag<T>` stay
  inside one shipped family and can be validated entirely through semantic models, callback counts,
  and deterministic structural guards.
- **Lowest-risk independent composite:** `PersistentOrderedSet<T>` still reuses public HAMT and
  FingerTree foundations, but it now owns a new assembly, dual-index invariant, sparse labels,
  contract, tests, and evolution policy. That makes it larger than a thin facade and correctly
  places it after the HAMT/bag tranche.
- **Next new core in the current frontier roadmap:** `RangeUpdateSequence` is the only candidate
  that the current frontier catalog calls Strong and actively sequences without a benchmark
  pre-gate. Its focused C# checkpoint is green, but it is not shipped until the full C# solution
  gate passes.

The detailed sections discuss the Ordered design before Steps 1 and 2 because correcting its
ownership boundary is the central revision to this document. The numbered execution steps above,
the section labels, and the implementation tranches remain the authoritative landing order.

### C#-First Cross-Language Follow-Through

C# remains the reference implementation. TypeScript and Python parity for the already-complete
Steps 1–3 landed before the new-core tranche finished; that completed work does not relax the gate
for Step 4. The C# `RangeUpdateSequence` must pass its full solution gate before any sibling
`RangeUpdateSequence` rollout begins. Cross-language completion remains required, not a
discretionary follow-up, and covers all four surfaces introduced in this execution sequence:

- the persistent-HAMT single-pass `GetOrAdd`/`AddOrUpdate` APIs and their one-descent callback and
  representative contracts;
- `PersistentHashBag`;
- `PersistentOrderedSet`; and
- `RangeUpdateSequence`.

Each surface must ultimately ship in all seven sibling workspaces: C, C++, Haskell, Kotlin, Rust,
TypeScript, and Python. The remaining work is Steps 1–3 in C, C++, Haskell, Kotlin, and Rust, plus
`RangeUpdateSequence` in all seven siblings. TypeScript and Python remain mandatory members of the
parity set, including tests and documentation; they are not optional trailing ports. Each
implementation should preserve the semantic contract while using language-local naming, ownership,
error, enumeration, and policy idioms rather than mechanically copying the C# API shape. Validate
one language workspace at a time with its checked-in single-worker or otherwise serialized
build/test path, and never overlap sibling toolchains.

No benchmark is required to begin or ship these structures. This proposal does not authorize
performance comparisons against BCL collections or claims that one representation beats another.
It requires correctness, persistence, invariant, asymptotic-work, failure, and documentation
evidence instead.

## Authority And Audit Method

This proposal follows the repository documents in this authority order:

1. The [frontier structure catalog](../reference/frontier-structure-catalog.md) is the current-state
   record for shipped Axis 1 cores, shipped Axis 2 surfaces, unshipped frontier candidates, and
   remaining sequencing.
2. The [data-structure catalog](../reference/data-structure-catalog.md) defines the shipped public
   surface.
3. The [Axis 2 final plan](axis2-lifecycle-and-sequence-cursors.md) remains normative for unshipped
   frozen-hash and later-cursor phases.
4. The [2026-07-09 next-structures proposal](new-data-structures-2026-07-09.md) and
   [derived-structure catalog](../reference/derived-structure-catalog.md) retain historical
   candidate rationale, but their implementation-status language is not authoritative. Their
   Tungsten consumer case study is design provenance only, never semantic or dependency authority
   for a general collection.
5. The normative
   [Tungsten application-leaf dependency boundary](../reference/tungsten-application-leaf-boundary.md)
   controls ownership, dependency direction, independent-fork requirements, test-oracle isolation,
   and future extraction.

The audit covered:

- every document under `docs/proposals`;
- the shipped, derived, and frontier structure catalogs;
- every report under `docs/reviews`;
- every C# FingerTree proposal and cursor decision;
- every C# HAMT transient/frozen decision and the HAMT implementation review;
- the C++ FingerTree port plan, editorial notes, and all three port-review reports; and
- Numerics future-width and code-generation plans, which were classified as outside this
  data-structure proposal.

At proposal-audit time, exact-name searches confirmed that `PersistentOrderedSet`,
`PersistentHashBag`, `RangeUpdateSequence`, `PersistentBiMap`, and a value-carrying interval-map
facade were not shipped C# types. Execution Steps 1 and 2 subsequently shipped the persistent-HAMT
`GetOrAdd`/`AddOrUpdate` kernel and `PersistentHashBag<T>`, Step 3 shipped the neutral C# Ordered
project, and TypeScript/Python parity now covers all three surfaces. Step 4 now has C# source, tests,
documentation, and green focused/project gates, but `RangeUpdateSequence` remains unshipped pending
the full serialized C# solution gate; no sibling Range port has started. `PersistentBiMap` and the
value-carrying interval-map candidate remain unshipped until their own complete
source/test/documentation tranches land.

## Current Baseline

The following work is complete and must not be mistaken for pending implementation:

- CHAMP canonical nodes, structural equality/diff, and structural map/set algebra;
- one-descent persistent-HAMT map factories in C#, TypeScript, and Python, plus public reusable
  construction-only CHAMP builders in C++, Rust, TypeScript, and Python;
- `PersistentHashBag` facades in C#, TypeScript, and Python with explicit multiplicity,
  receiver-policy algebra, and expanded/distinct enumeration contracts;
- neutral `PersistentOrderedSet` packages in C#, TypeScript, and Python with independent contracts,
  dependency graphs, models, and validation; the hardened C# implementation additionally locks its
  dual-index invariant, explicit movement, stable representatives, receiver-policy algebra,
  deterministic and failure-atomic relabel fallback, and strict Ordered-to-Tungsten dependency
  guard;
- C# owner-token CHAMP transients and semantic one-way editing sessions in the sibling languages;
- 32-bit and 64-bit Patricia maps and sets;
- RRB vectors;
- the full Merkle search-tree persistence, proof, synchronization, and merge tier;
- canonical zip-zip sorted sets;
- Brodal–Okasaki heaps and priority-search queues;
- DABA Lite in every applicable imperative language;
- managed Ctries in C# and Kotlin/JVM; and
- positional, measured, and text rope cursors through the current cross-language checkpoint scope.

All repository review reports close their correctness findings. The latest
[CHAMP editing-session review](../reviews/champ-edit-sessions-review-2026-07-14.md) retains only
optional coverage suggestions, not implementation blockers. The three C++ FingerTree review reports
likewise have resolution addenda and are historical evidence rather than an open backlog.

Local performance measurement remains intentionally postponed. The benchmark definitions may stay
in the tree, but this proposal neither runs them nor interprets existing results collected under
contention.

## Selection Rules

A candidate enters this proposal only when all of the following are true:

1. Its public capability is not already shipped under another name.
2. No current plan makes benchmark evidence a prerequisite to implementation or shipment.
3. Its semantic contract can be fixed from shipped repository conventions or a bounded design
   decision, without an unknown consumer deciding the meaning of the type.
4. Correctness and complexity can be guarded deterministically through models, invariants, callback
   counts, node-visit counts, height bounds, or structural-sharing assertions.
5. The C# reference implementation can land and stabilize first, after which its required semantic
   parity can be expressed through language-local APIs in every sibling workspace.
6. Its value is a capability or meaningful collection vocabulary, not merely a speculative
   constant-factor optimization.
7. It can live in a repository-general owner whose production code, tests, examples, benchmarks,
   and contract do not depend on an application-specific Tungsten artifact or live behavioral
   oracle. Tungsten-derived mechanics require an independent fork.

“Low risk” means more than a small file count. It means that the representation invariant, policy
source, representative rules, order, failure atomicity, no-op identity, and model oracle can all be
stated before implementation.

## Historical Independent Composite Design (Execution Step 3): `PersistentOrderedSet<T>` — Shipped In C#, TypeScript, And Python

The design and exit criteria in this section are retained because they define the ownership boundary
that the shipped C#, TypeScript, and Python ports follow. The C# hardening and shipment evidence are
retained as the reference checkpoint for the remaining ports.

### Why It Was Selected And Remains General

The historical next-structures proposal correctly noticed that a hashed membership index plus a
persistent ordered sequence can support a valuable insertion-ordered set. It incorrectly concluded
that the general type should be a thin wrapper around Tungsten `PersistentAssociation`. Tungsten is
an application-specific leaf whose behavior can change with new kernel evidence or move to another
repository. It cannot be a general collection's assembly, representation, contract, or test oracle.

The corrected structure is benchmark-independent because the repository already ships the two
general foundations it needs:

- comparer-preserving hashed lookup;
- stored-representative recovery from the HAMT;
- a persistent deque with positional access, split/concat, stable enumeration, and sorted lower
  bounds over private stamp entries; and
- canonical public bulk construction on both sides.

The set did not need a new balancing algorithm, but it did require an independently owned composite:
a new assembly, a set-specific dual-index invariant, private sparse-label/relabel code, an explicit
general contract, an independent model suite, and its own evolution policy. The shipped C# tranche
owns and hardens all of those pieces; the TypeScript and Python ports preserve the neutral ownership
and semantic contract. That bounded work is why the set was selected and placed behind the HAMT
point-update kernel and hash bag.

### Placement And Representation

The shipped neutral general-purpose project and corresponding tests/docs are:

- `src/CSharp/src/Tools.DataStructures.Ordered/Tools.DataStructures.Ordered.csproj`;
- namespace `Tools.DataStructures.Ordered`;
- `src/CSharp/tests/Tools.DataStructures.Ordered.Tests`; and
- `src/CSharp/docs/Ordered`.

The project references public HAMT and FingerTree projects. Neither foundation references Ordered,
and Ordered does not reference Tungsten:

```text
Tools.DataStructures.Ordered
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree

Tools.DataStructures.Tungsten
├── Tools.DataStructures.Hamt
└── Tools.DataStructures.FingerTree
```

Do not initially refactor Tungsten to consume Ordered. Similar mechanics do not imply shared
semantic ownership, and keeping the two consumers independent preserves the extraction boundary.

Shipped representation:

```csharp
public sealed class PersistentOrderedSet<T> : IReadOnlySet<T>
{
    private readonly FingerTreeDeque<Entry> _order;
    private readonly PersistentHashMap<T, long> _stamps;

    private readonly record struct Entry(long Stamp, T Item);
}
```

The general set follows the HAMT set family's comparer-defined null semantics rather than importing
Association's `where TKey : notnull` annotation. `PersistentHashMap<T, long>` stores only membership
and stamp; the sequence owns ordered representatives. Do not add a unit-valued Association, duplicate
the item in a map slot, share a Tungsten source file, or request a Tungsten-related friend grant.

### Independently Owned Invariants

The Ordered project owns and test-checks all of these invariants:

1. `_order.Count == _stamps.Count`.
2. `_order` contains exactly one representative of every comparer equivalence class.
3. entry stamps strictly ascend in `_order`.
4. every ordered entry has one index entry carrying the same stamp.
5. every index entry has exactly one ordered entry.
6. the HAMT's stored representative and the ordered entry are the same logical representative.
7. every derived version retains the receiver's equality comparer.
8. retained earlier versions remain immutable and safe for concurrent reads.

Private Ordered helpers own stamp comparison, label selection, lower-bound location, relabeling,
stable rebuild, and any slice reconciliation. The implementation may copy the idea of gapped labels
and midpoint insertion with provenance, but it must not expose Association's exact `2^20` gap or its
“20 same-point inserts” threshold as a public promise.

### Public Surface

| Area | Members |
| --- | --- |
| Construction | `Empty`, `Create(comparer)`, `CreateRange(items, comparer)` |
| State | `Count`, `IsEmpty`, `Comparer`, `First`, `Last` |
| Lookup | `Contains`, `TryGetValue`, `GetAt`, indexer by position, `IndexOf` |
| Addition without implicit movement | `Add`, `AddFirst`, `Insert` |
| Explicit movement | `MoveToFirst`, `MoveToLast`, `MoveTo` |
| Removal | `Remove`, `TryRemove`, `RemoveAt`, `RemoveFirst`, `RemoveLast`, `Clear` |
| Range/order | `GetRange`, `Take`, `Drop`, `Reverse`, stable one-shot `Sort` |
| Set algebra | Same-type and `IEnumerable<T>` overloads of `Union`, `Intersect`, `Except`, `SymmetricExcept` |
| Relations | `IsSubsetOf`, `IsProperSubsetOf`, `IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, `SetEquals` |
| Enumeration | struct `Enumerator`, `ToArray` |

Avoid sorted-set vocabulary such as `Min`, `Max`, lower-bound, or range-by-value. “Ordered” here means
insertion/explicit-position order, never comparison order.

`GetAt(index)` and the positional indexer return the same stored representative and accept exactly
`0 <= index < Count`. `IndexOf(equalValue)` returns that representative's position or `-1` when the
equivalence class is absent. `Contains` and `TryGetValue` are comparer-defined membership probes;
`TryGetValue` returns the stored representative rather than the lookup argument.

### Construction And Representative Contract

- `CreateRange` processes the input in order.
- The first representative of an equivalence class fixes its position and stored representative.
- Later equivalent inputs are logical no-ops.
- The supplied comparer defines both membership and duplicate collapse.
- Empty values retain the supplied comparer object; the default singleton is used only for the
  reference-default comparer.
- `TryGetValue(equalValue, out actualValue)` returns the stored representative.

These are Ordered-owned set rules. The HAMT's public stored-key contract supports them, but
Association construction is neither their definition nor their oracle.

### Addition And Movement Contract

Addition never hides movement or representative replacement:

- `Add(item)` appends an absent item. If an equivalent item already exists, it returns the current
  set instance and retains both position and representative.
- `AddFirst(item)` prepends only an absent item. An equivalent existing item is an
  identity-preserving no-op regardless of its position.
- `Insert(index, item)` accepts `0 <= index <= Count`, validates `index` eagerly, and inserts only
  when the equivalence class is absent. An existing item remains at its current position with its
  stored representative.
- `MoveToFirst(equalValue)` and `MoveToLast(equalValue)` move an existing equivalence class while
  retaining its stored representative. An absent class throws `KeyNotFoundException`.
- `MoveTo(index, equalValue)` accepts `0 <= index < Count`, validates `index` eagerly, and interprets
  it as the item's final index in the result. It retains the stored representative and throws
  `KeyNotFoundException` when absent.
- Moving an item already at its requested destination returns the current instance.
- The first version exposes no API that implicitly replaces an existing stored representative.

These names deliberately avoid Association's `Append`/`Prepend` move-and-replace behavior and its
pre-removal interpretation of positional insertion.

### Removal, Range, And Reorder Contract

The shipped result shapes and boundary behavior are:

```csharp
public PersistentOrderedSet<T> Remove(T equalValue);
public bool TryRemove(T equalValue, out PersistentOrderedSet<T> result);
public PersistentOrderedSet<T> RemoveAt(int index);
public PersistentOrderedSet<T> RemoveFirst();
public PersistentOrderedSet<T> RemoveLast();

public PersistentOrderedSet<T> GetRange(int index, int count);
public PersistentOrderedSet<T> Take(int count);
public PersistentOrderedSet<T> Drop(int count);
public PersistentOrderedSet<T> Reverse();
public PersistentOrderedSet<T> Sort(IComparer<T>? comparer = null);
```

- `Remove` returns the receiver when the equivalence class is absent.
- `TryRemove` returns `false` and the receiver through `result` when absent; on success it returns
  `true` and the successor. Stored-representative recovery remains available separately through
  `TryGetValue`; `TryRemove` has no second item `out` parameter.
- `RemoveAt` accepts `0 <= index < Count`. `RemoveFirst` and `RemoveLast` throw
  `InvalidOperationException` on empty input.
- `GetRange` requires `0 <= index <= Count`, `0 <= count`, and `index + count <= Count`, using
  overflow-safe validation. The full range returns the receiver; an empty range returns an empty set
  retaining the receiver's comparer.
- `Take` and `Drop` require `0 <= count <= Count`. `Take(Count)` and `Drop(0)` return the receiver;
  `Take(0)` and `Drop(Count)` return a comparer-preserving empty set.
- `Clear` returns the receiver when already empty and otherwise a comparer-preserving empty set.
- `Reverse` returns the receiver for counts zero and one.
- `Sort` is stable. It returns the receiver for counts zero and one and whenever stable sorting leaves
  the representative sequence unchanged; otherwise it rebuilds both indexes. Comparer failure never
  publishes a partial result.

### Set-Algebra Ordering

Same-type algebra uses the receiver’s comparer and the following deterministic order:

- `Union(other)`: receiver elements in receiver order, followed by argument elements not equivalent
  to any retained receiver element, in argument order.
- `Intersect(other)`: retained receiver elements in receiver order.
- `Except(other)`: retained receiver elements in receiver order.
- `SymmetricExcept(other)`: receiver-only elements in receiver order, followed by argument-only
  elements in argument order after collapsing them under the receiver comparer.

Receiver representatives win wherever a receiver element remains. Argument representatives are
installed only for new argument-only classes. Both same-type and arbitrary-`IEnumerable<T>`
set-producing overloads ship and use the same receiver-policy rules rather than delegating equality
to a temporary default `HashSet<T>`.

Normalize every argument-side sequence under the receiver's comparer in its enumeration order.
If elements distinct under the argument's own policy collapse into one receiver equivalence class,
the first encountered argument representative wins. This normalization governs algebra and set
relations alike and makes comparer-mismatched behavior independent of the argument's internal
membership probes.

`Sort(orderComparer)` is a stable one-shot reorder using the supplied ordering comparer while
retaining the set's equality comparer. The result remains an ordinary insertion-ordered set:
subsequent additions append and do not maintain sorted order.

### Persistence, Identity, And Failure

- Every update returns a new immutable set or the current instance.
- Retained older sets remain unchanged and safe for concurrent reads.
- Logical no-ops return the same set instance.
- Any comparer exception occurs before a new facade is published; the input remains unchanged.
- Invalid positional arguments throw `ArgumentOutOfRangeException` before any result is published.
- Empty `First`, `Last`, `RemoveFirst`, and `RemoveLast` throw `InvalidOperationException` under this
  type's own contract.
- Failed movement of an absent equivalence class throws `KeyNotFoundException` without publishing a
  partial successor.

### Independent Complexity Contract

Publish bounds owned by Ordered and derived from its representation plus public HAMT/FingerTree
contracts. Here `n` is the receiver or distinct result size as applicable, and `m` is the number of
enumerated construction inputs or argument elements:

| Operation | Bound |
| --- | --- |
| `CreateRange` | O(m (w + c) + n) with one ordered/index rebuild; duplicate inputs still count in m |
| Hashed membership / stored representative | O(w + c) |
| Positional lookup | O(log n) worst case; O(1 + log min(index + 1, n - index)) amortized; endpoints O(1) worst case |
| `IndexOf` | O(w + c + log n) |
| Ordinary end insertion | O(w + c) amortized on a linear history; O(w + c + log n) ordinary worst case |
| Positional insertion or movement while a label gap exists | O(w + c + log n) |
| Any insertion requiring relabel | O(n (w + c)) |
| `Remove` / successful `TryRemove` | O(w + c + log n); absent removal O(w + c) |
| `RemoveAt` | O(w + c + log min(index + 1, n - index)) amortized; O(w + c + log n) worst case |
| `Clear` | O(1) |
| Reverse | O(n (w + c)) rebuild |
| `GetRange` / `Take` / `Drop` | O(log n) sequence work plus O(min(kept, removed) (w + c)) index reconciliation |
| Stable one-shot sort | O(n log n) ordering-comparer calls plus O(n (w + c)) rebuild |
| Set algebra producing a set | O((n + m) (w + c + log(n + m + 1))) conservative worst case, including receiver-policy normalization and ordered reconstruction |
| Set relations | O((n + m) (w + c)) after receiver-policy normalization; no structural same-policy fast path promised initially |
| Enumeration / `ToArray` | O(n) |

Ordered must derive and test these bounds independently. Its private relabel design retains the
honest per-produced-version worst case: no amortization claim spans siblings branched before a
relabel. No claim about beating another ordered-set representation is made without isolated
benchmark evidence.

### Validation Plan

Use a comparer-aware model containing an ordered `List<T>` plus explicit equivalence-class lookup.
Among repository collection projects, the Ordered test project references only Ordered and its
general dependencies; ordinary test infrastructure remains allowed. It must not call
`PersistentAssociation`, reference Tungsten tests, or generate expected values from Tungsten.

Applicable adversarial scenarios may be adapted once from `PersistentAssociationTests.cs`,
`PersistentAssociationPropertyTests.cs`, and `PersistentAssociationDerivedPropertyTests.cs`, with
provenance recorded at commit `e199f6a6a2071e6d5f13b734dc426bd21a7741e8`. The independent Ordered
contract decides every expected result. Do not copy assertions for last-value construction,
`Join`, implicit append/prepend movement, supplied-representative replacement, or pre-removal insert
indexing.

The test matrix must cover:

- example tests for every member;
- generated command histories including retained branches;
- constant-hash comparers and collision-heavy equivalence classes;
- comparison-equivalent but object-distinct representatives;
- add-versus-explicit-movement distinctions;
- every same-type and enumerable algebra operation under differing comparer instances;
- no-op reference identity;
- relabel-boundary histories;
- direct invariant checks across both indexes;
- enumerator order and concurrent read-only enumeration; and
- eager argument validation.

A dependency audit must reject an Ordered-to-Tungsten project/test reference, source import, `using`,
linked file, compiled Tungsten symbol use, or live test oracle. Non-normative provenance comments are
allowed. No benchmark is an exit criterion.

### Exit Criteria

The type ships when:

- the public contract above is reflected in XML documentation;
- the command model, representative matrix, algebra matrix, retained-version tests, and no-op identity
  tests pass;
- invariant diagnostics and the Ordered/Tungsten dependency audit pass;
- the new project, test project, solution entries, overview, usage guide, API specification, and
  validation guide all identify Ordered—not Tungsten—as the owner;
- workspace overview/usage/API/validation docs and repository catalogs are updated; and
- the complete C# suite passes with one build/test worker.

### C# Shipment Evidence

The complete source, tests, documentation, solution integration, invariant diagnostics, and
Ordered-to-Tungsten dependency guards have landed. Serialized focused validation passed **62/62**
tests in Debug and **62/62** tests in Release. The full serialized C# Release solution build
completed with **0 warnings and 0 errors**, and the complete C# test gate passed **1,355/1,355**
tests. Benchmarks were not run and no performance result is inferred; benchmark execution remains
postponed until it can run in isolation without competing agents or machine contention.

## Execution Step 1: Persistent HAMT Single-Pass Updates — Shipped In C#, TypeScript, And Python

### Why This Is Separate From Builders And Transients

The historical A1 proposal grouped `Update`, `GetOrAdd`, a builder, and transient construction. The
current repository vocabulary makes that grouping obsolete:

- `BulkBuilder` is an internal staging mechanism used by canonical one-freeze construction.
- `Transient` is the public one-way owner-token editing session.
- `GetOrAdd`/`AddOrUpdate` are ordinary persistent point operations returning immutable versions.

Canonical bulk construction and C# transients already shipped before this proposal. Step 1 closed
the C# single-pass persistent point-operation gap, and TypeScript/Python parity has since shipped;
this section records the C# reference contract and validation boundary for the remaining ports.

### Recommended Surface

Prefer names already used by `ConcurrentHashTrie<TKey, TValue>`:

```csharp
public PersistentHashMap<TKey, TValue> GetOrAdd(
    TKey key,
    Func<TKey, TValue> addFactory,
    out TValue value);

public PersistentHashMap<TKey, TValue> AddOrUpdate(
    TKey key,
    Func<TKey, TValue> addFactory,
    Func<TKey, TValue, TValue> updateFactory,
    out TValue value);
```

Value-taking convenience overloads may be added only if they remove common caller ceremony without
creating ambiguous overload resolution. Do not expose a nullable-value convention that conflates an
absent key with a present null value.

Validate delegate arguments eagerly, before hashing or selecting a branch. In particular,
`GetOrAdd` rejects a null `addFactory` even on a hit, and `AddOrUpdate` rejects either null factory
even when the other branch would have been selected. This matches the Ctrie's current argument
contract.

The return shape deliberately differs from the mutable Ctrie's value-returning methods: a
persistent operation must return the successor map, while the `out` parameter reports the selected
stored value without requiring a second lookup.

### Callback And Representative Contract

- The key is hashed once.
- Exactly one trie descent determines presence and produces the successor path.
- `GetOrAdd` invokes no factory on a hit and invokes `addFactory` exactly once on a miss.
- `AddOrUpdate` invokes exactly one of its two factories exactly once. Unlike the Ctrie, neither
  persistent operation has a CAS retry loop.
- On absence, `addFactory` receives the caller’s key and the resulting entry stores that key.
- On presence, `updateFactory` receives the caller's lookup key and the stored value, matching the
  Ctrie's same-named API; the successor nevertheless retains the originally stored key
  representative.
- A replacement equal under `EqualityComparer<TValue>.Default` retains the stored value
  representative and returns the current map instance.
- The `out` value is the actual stored or newly selected value.
- Factory or comparer failure publishes nothing and leaves the source unchanged.

### Implementation Shape

Add one internal node operation that combines lookup and successor construction. Each leaf,
collision, and bitmap node must support the same outcome vocabulary:

```text
Absent -> selected add value + new path + count increment
Present unchanged -> original node + original value
Present changed -> selected update value + rebuilt path
Failure -> no result published
```

Do not implement the public method as `TryGetValue` followed by `SetItem`; that would preserve
semantics but fail the enabling API’s single-descent purpose.

### Deterministic Validation

- hash-callback count exactly one for valid delegates and zero on eager delegate-validation failure;
- equality-callback ceilings for leaf, collision, hit, and miss cases;
- zero/one `GetOrAdd` factory invocation on hit/miss and exact `AddOrUpdate` factory selection;
- leaf/collision/bitmap transition coverage;
- exact caller-key callback identity plus stored-key and stored-value representative cases;
- present-null values;
- factory, comparer, and value-equality exceptions at every applicable node shape;
- source/root retention on failure and logical no-op; and
- randomized invariant validation after generated histories.

These are operation-count and correctness gates, not wall-clock gates.

## Execution Step 2: `PersistentHashBag<T>` — Shipped In C#, TypeScript, And Python

### Representation

Place the bag in `Tools.DataStructures.Hamt` beside `PersistentHashMap` and `PersistentHashSet`.
It adds no cross-project dependency.

```csharp
public sealed class PersistentHashBag<T> : IEnumerable<T>
{
    private readonly PersistentHashMap<T, int> _counts;
    private readonly long _totalCount;
}
```

The map contains one entry for each equivalence class. Its positive `int` value is the multiplicity.
`_totalCount` is the sum of all multiplicities and may exceed `int.MaxValue`.

`CreateRange` should aggregate through a small internal extension of the existing HAMT
`BulkBuilder`, retaining the first equivalent key while incrementing a checked count, and freeze
once into canonical CHAMP shape. The combiner receives both the stored value and incoming add value,
so checked aggregation can use a capture-free static delegate. This remains an internal staging
path, not a second public builder or transient lifecycle.

### Count And Enumeration Contract

Expose both notions explicitly:

- `DistinctCount : int` — number of equivalence classes;
- `TotalCount : long` — expanded element count;
- `CountOf(item) : int` — multiplicity of one class;
- default `IEnumerable<T>` enumeration — each stored representative repeated by its multiplicity;
- `DistinctItems` — one representative per class in stable-but-unspecified HAMT order; and
- `Entries` — representative/multiplicity pairs in the same distinct order.

Do not implement `IReadOnlyCollection<T>` if its `int Count` would disagree with expanded
enumeration or silently truncate `TotalCount`.

### Proposed Public Surface

The C# reference surface is exact rather than illustrative:

```csharp
public sealed class PersistentHashBag<T> : IEnumerable<T>
{
    public static PersistentHashBag<T> Empty { get; }

    public static PersistentHashBag<T> Create(
        IEqualityComparer<T>? comparer = null);

    public static PersistentHashBag<T> CreateRange(
        IEnumerable<T> items,
        IEqualityComparer<T>? comparer = null);

    public int DistinctCount { get; }
    public long TotalCount { get; }
    public bool IsEmpty { get; }
    public IEqualityComparer<T> Comparer { get; }

    public bool Contains(T item);
    public int CountOf(T item);
    public bool TryGetValue(T equalValue, out T actualValue);

    public PersistentHashBag<T> Add(T item);
    public PersistentHashBag<T> AddCopies(T item, int count);
    public PersistentHashBag<T> Remove(T item);
    public PersistentHashBag<T> RemoveCopies(T item, int count);
    public PersistentHashBag<T> RemoveAll(T item);
    public PersistentHashBag<T> Clear();

    public PersistentHashBag<T> Union(PersistentHashBag<T> other);
    public PersistentHashBag<T> Intersect(PersistentHashBag<T> other);
    public PersistentHashBag<T> Except(PersistentHashBag<T> other);
    public PersistentHashBag<T> Sum(PersistentHashBag<T> other);

    public IEnumerable<T> DistinctItems { get; }
    public IEnumerable<KeyValuePair<T, int>> Entries { get; }

    public T[] ToArray();
    public Enumerator GetEnumerator();
}
```

The type deliberately exposes neither `Count` nor `IReadOnlyCollection<T>`. It also has no
enumerable algebra overload, public mutable builder or transient facade, or content-equality
override. `Entries` uses the standard `KeyValuePair<T, int>` representation and follows the same
distinct trie order as `DistinctItems`. Interface enumeration remains available through
`IEnumerable<T>` and the non-generic `IEnumerable` base interface. `TryGetValue` returns the stored
representative on a hit and assigns the lookup argument itself to `actualValue` on a miss, matching
the persistent-set convention. Null items are accepted or rejected only through the selected
comparer's behavior.

The nested public `Enumerator` is a mutable struct that wraps the map's struct enumerator and keeps
the current representative plus its unexpanded repetition count. Obtaining and draining it through
the concrete surface allocates nothing; copying it creates an independently advancing value. Its
`Current` is `default` before the first successful `MoveNext` and after exhaustion, `Dispose` is a
no-op, and `IEnumerator.Reset` throws `NotSupportedException`. Enumeration through an interface may
box the struct. `DistinctItems` is the map's key view and `Entries` is the map's pair view, so each
view is version-bound and immutable. Each representative's expanded occurrences are contiguous;
the relative order of first expanded occurrences, `DistinctItems`, and `Entries` is identical. The
order is stable for an unchanged version but otherwise unspecified.

The debugger proxy exposes a distinct-entry array, never expanded enumeration. Debugger inspection
therefore remains bounded by `DistinctCount` even when one multiplicity or `TotalCount` is very
large.

### Multiplicity And Overflow Rules

- Stored multiplicities are in `1 .. int.MaxValue`; zero never appears in `_counts`.
- `Add(item)` and `Remove(item)` change one copy; `AddCopies` and `RemoveCopies` change the
  requested number; `RemoveAll` removes the entire equivalence class.
- `AddCopies(item, 0)` returns the current bag.
- Negative copy counts throw `ArgumentOutOfRangeException`.
- Per-key overflow throws `OverflowException` before publishing a result.
- Removal saturates at zero and removes the map entry when the remaining count is zero.
- Removing a missing item or zero copies is an identity-preserving no-op.

Construction processes elements in input order. The first representative of an equivalence class is
retained while later elements only increment its count. `Clear` and every result that becomes empty
retain the receiver's comparer object; only the reference-default comparer uses the shared `Empty`
instance.

`Create()` and `Create(EqualityComparer<T>.Default)` return `Empty`; reference identity with that
default comparer object, not semantic comparer equivalence, selects the singleton. Point additions
retain an existing representative, while an absent class stores the caller's item.

`TotalCount` cannot overflow for a valid bag: `DistinctCount <= int.MaxValue` and every
multiplicity is at most `int.MaxValue`, so the maximum representable total is
`(int.MaxValue * (long)int.MaxValue) < long.MaxValue`. Keep internal total arithmetic checked as a
defensive invariant guard, but do not advertise an unreachable public total-overflow case.

`AddCopies` and `RemoveCopies` validate a negative `count` before hashing or equality callbacks.
They return the receiver for zero without hashing or invoking equality. Positive `AddCopies` uses
the map's one-descent persistent `AddOrUpdate`; positive `RemoveCopies` may perform a lookup followed
by one changed map update because the map combinator cannot delete an entry. This is two bounded
searches but only one rebuilt path and retains the stated O(w + c) bound. `RemoveAll` uses the map's
single-descent `TryRemove` and obtains the removed multiplicity from that operation.

`ToArray` returns expanded enumeration in exactly the bag enumerator's order. Before allocating, it
throws `OverflowException` when `TotalCount > Array.MaxLength`; checking only `int.MaxValue` is not
sufficient because the CLR's maximum single-dimensional array length is lower. An empty bag returns
an empty array. No partially populated array is observable if enumeration unexpectedly fails.

`CreateRange` rejects a null source before enumeration. Its internal bulk combine validates its
update delegate before hashing, hashes each source item once, scans one full-hash bucket, invokes the
update delegate exactly once only for an equivalent stored key, retains the first key
representative, retains an equal stored value representative, and leaves builder state unchanged if
hashing, key equality, count increment, or value equality throws. Freezing owns all published arrays;
later builder changes cannot mutate an earlier immutable snapshot.

### Algebra

Use conventional multiset operations and name the additive operation separately:

- `Union`: maximum multiplicity per class;
- `Intersect`: minimum multiplicity;
- `Except`: `max(0, receiverCount - argumentCount)`;
- `Sum`: checked addition of multiplicities.

The receiver’s comparer governs equivalence. Receiver representatives win for surviving classes;
argument representatives appear only for classes absent from the receiver. Logical no-op results
return the receiver instance.

When comparer objects are not reference-identical, first normalize the argument under the
receiver's comparer. Treat the argument as an expanded multiset: argument classes that become one
receiver class contribute the checked sum of their multiplicities, and the first argument
representative encountered in the map's stable-but-unspecified enumeration order represents that
normalized class. This rule prevents algebra from silently using the argument's equality policy.
It also means structural lockstep algebra is available only when the implementations explicitly
prove policy compatibility; normalization otherwise takes element-wise distinct-entry work.
Because map order is not semantic, two logically equal argument versions may select different
representatives when their observed entry orders differ; only the rule relative to the particular
argument version's observed order is guaranteed.

This normalization is semantically eager: after null validation, a comparer-mismatched argument is
fully normalized before operation-specific empty or identity short-cuts. Consequently, comparer,
hash, equality, or checked-collapse failures during normalization remain observable even for an
intersection with an empty receiver or another case whose mathematical answer could be known
without examining the argument. Reference-identical comparer objects skip normalization because
each input already has one entry per receiver equivalence class.

All four operations are failure-atomic because they build only immutable intermediate versions.
`Union` and `Sum` introduce the normalized argument representative only for a class absent from the
receiver; every surviving receiver class keeps the receiver representative. `Intersect` and
`Except` never introduce an argument representative. `Sum` checks every per-class addition before
publishing that changed version. If the complete logical result equals the receiver, including its
multiplicities and representatives, the exact receiver instance is returned. Every empty result
retains the receiver comparer object and canonicalizes only when that object is
`EqualityComparer<T>.Default` by reference.

In particular, `Union(this)` and `Intersect(this)` return `this`, `Except(this)` returns the
receiver-comparer empty bag, and `Sum(this)` actually doubles each multiplicity and can overflow.
Logical equality for the no-op rule is receiver-policy class membership plus multiplicity; the
argument's representative identities do not displace surviving receiver representatives.

### Complexity

| Operation | Bound |
| --- | --- |
| `CountOf` / `Contains` | O(w + c), allocation-free |
| Point add/remove | O(w + c), one rebuilt search path when changed |
| Expanded enumeration | O(`TotalCount`) |
| Distinct/entry enumeration | O(`DistinctCount`) |
| Same-comparer `Union` / `Sum` | O(argument `DistinctCount` (w + c)) element-wise work |
| Same-comparer `Intersect` / `Except` | O(receiver `DistinctCount` (w + c)) element-wise work |
| Mismatched-comparer algebra | O((receiver + argument `DistinctCount`) (w + c)) including normalization |

The initial implementation is deliberately element-wise and preserves receiver sharing as each
class is processed. Do not claim structural bag algebra merely because the underlying map has
structural algebra. A combining multiplicity operation needs an explicit lockstep implementation
before receiving that bound.

### Validation Plan

Use a small comparer-aware linear equivalence-class model that stores each first representative and
its count. Unlike `Dictionary<T, int>`, this oracle can model comparer-defined null keys exactly.
Cover:

- checked per-key boundaries, the maximum-total invariant, and `ToArray` overflow when expanded
  enumeration cannot fit an array;
- construction duplicate/representative behavior;
- expanded versus distinct enumeration;
- all algebra multiplicity tables;
- receiver-policy behavior with different comparer instances;
- collision buckets, null keys where supported, and present representatives;
- no-op identity and retained versions;
- comparer and input-enumerator exceptions; and
- randomized command histories with invariant validation.

Internal invariant diagnostics must first validate canonical CHAMP routing and ownership sealing,
then verify that every stored multiplicity is positive and that their checked sum equals
`TotalCount`. API-shape tests lock the absence of `Count` and `IReadOnlyCollection<T>`; enumerator
tests lock default, before-first, active, copied, exhausted, interface, and reset behavior; debugger
tests lock distinct rather than expanded projection.

No benchmark is an exit criterion.

## Execution Step 4: `RangeUpdateSequence` — C# Focused Checkpoint Green, Full Solution Gate Pending

The C# source, tests, and documentation are implemented, and the focused project validation gates
pass. This is an intermediate checkpoint rather than shipment: the full serialized C# solution gate
is still pending, and no sibling-language `RangeUpdateSequence` port has started.

### Why It Is The Next Core

The frontier catalog describes a persistent sequence supporting logarithmic range assignment and
range addition through lazily composed subtree tags. Its differentiator is capability, not a claimed
constant-factor win: the shipped sequences cannot transform a whole range without visiting its
elements.

The load-bearing requirement is an action of the tag monoid on both elements and cached measures.
Applying a tag to a subtree must update the subtree’s aggregate from its current aggregate and count,
without enumerating its elements.

### Representation Decision

Use a separate implicit-key AVL tree with immutable path copying and lazy tags.

| Option | Advantages | Risks / disposition |
| --- | --- | --- |
| Add tags to the existing measured finger tree | Reuses its endpoint and concatenation vocabulary | Tags would cross-cut measured leaves, 2–3 nodes, digits, lazy middles, cached measures, split, concat, and enumeration. This changes the most load-bearing shipped core. Reject for the first implementation. |
| Fixed-size persistent segment tree | Simple range update/query proof | Does not support insertion, deletion, split, or concatenation needed by a sequence and later styled-text composition. Reject as too narrow. |
| Persistent implicit treap | Compact split/merge implementation | Random priorities add policy, determinism, adversarial, and reproducibility questions unrelated to range actions. Do not choose while a deterministic alternative is available. |
| Persistent implicit AVL | Deterministic logarithmic height; split/join and path copying; cached count/measure fit naturally; repository already has immutable AVL rotations | New sibling core, but its invariants are local and deterministically testable. Select. |

This is an implementation inference from the current C# architecture, not a change to the existing
FingerTree public contract. The FingerTree project already owns non-finger-tree sequence/streaming
cores such as RRB, priority-search, and DABA Lite, so a sibling AVL-backed sequence fits its scope.

### Algebra Surface

```csharp
public interface IRangeUpdateAlgebra<TElement, TMeasure, TTag>
    : IMeasure<TElement, TMeasure>
{
    static abstract TTag IdentityTag { get; }
    static abstract bool IsIdentity(TTag tag);
    static abstract TTag Compose(TTag newer, TTag older);
    static abstract TElement ApplyElement(TTag tag, TElement element);
    static abstract TMeasure ApplyMeasure(TTag tag, TMeasure measure, int count);
}
```

`Compose(newer, older)` means “apply `older`, then `newer`.” Naming the order in the API and tests is
mandatory; assign/add bugs commonly hide in an unspecified composition direction.

The implementation may internally represent the absence of a pending tag with a boolean rather than
relying on `EqualityComparer<TTag>`. `IsIdentity` exists to preserve public no-op identity when a
caller supplies the algebraic identity.

### Required Laws

For every element `x`, measures `a` and `b`, tags `p`, `q`, and `r`, and subtree counts `ca` and
`cb`, the tag operations form a monoid whose composition order is the one stated above:

```text
IsIdentity(IdentityTag) = true

Compose(IdentityTag, p) = p
Compose(p, IdentityTag) = p
Compose(r, Compose(q, p)) = Compose(Compose(r, q), p)

ApplyElement(IdentityTag, x) = x
ApplyMeasure(IdentityTag, a, ca) = a
ApplyMeasure(p, Empty, 0) = Empty

ApplyElement(Compose(q, p), x)
    = ApplyElement(q, ApplyElement(p, x))

ApplyMeasure(Compose(q, p), a, ca)
    = ApplyMeasure(q, ApplyMeasure(p, a, ca), ca)

ApplyMeasure(p, Measure(x), 1)
    = Measure(ApplyElement(p, x))

ApplyMeasure(p, Combine(a, b), ca + cb)
    = Combine(ApplyMeasure(p, a, ca), ApplyMeasure(p, b, cb))
```

`Combine` remains ordered; arbitrary noncommutative measures are permitted when their action obeys
the distribution law. Whenever `IsIdentity(p)` is true, `p` must obey the same element, measure,
and composition equations as `IdentityTag`; this makes the implementation's identity fast path
sound for value-distinct identity tags. The library cannot prove user algebra purity or these laws,
but executable law tests and XML documentation must make them explicit policy preconditions, like
the existing monoid laws.

### Node Invariants

Each immutable node stores:

```text
Value
Left, Right
Height
Count
Measure
HasPendingTag, PendingTag
```

Invariant:

- `Value` and `Measure` already reflect the node’s own pending tag.
- Child nodes do not yet reflect that tag.
- `Measure` is the ordered combination of the logical left sequence, logical `Value`, and logical
  right sequence.
- `Count` is one plus child counts.
- child heights differ by at most one.

This convention makes whole-subtree application O(1): transform the root value and measure, compose
the pending tag, and retain both children.

### Core Algorithms

#### `ApplySubtree`

For a non-empty node and tag `t`:

1. Return the original node if `t` is the identity.
2. Compute `ApplyElement(t, node.Value)`.
3. Compute `ApplyMeasure(t, node.Measure, node.Count)`.
4. Compose `t` after any existing pending tag.
5. Allocate one new node retaining both children.

All user-policy calls complete before the new facade is published.

#### `Push`

Before structurally descending through or rotating a node with a pending tag:

1. apply the pending tag to each non-empty child root;
2. retain the already-transformed node value;
3. clear the pending marker on the rebuilt node.

The invariant harness separately recomputes the pushed aggregate and compares it with the cached
logical aggregate through the test algebra's equality relation. The production core does not add a
measure-equality requirement to the action interface.

Push is immutable. It never mutates nodes reachable from an older version.

#### Split, Join, And Rebalancing

- `SplitAt(index)` descends by cached left counts, pushes only the traversed spine, and rejoins the
  fragments through AVL-aware join.
- `Concat` joins roots by height, path-copying and rotating only the taller spine.
- Insert, delete, and replacement are compositions of split/join or direct indexed descent.
- Rotations operate only on pushed nodes, so no pending tag is stranded above a child that moved out
  of its tagged subtree.

#### Range Update

`ApplyRange(index, count, tag)`:

1. eagerly validates the range;
2. returns the source for an empty range or identity tag;
3. applies the tag directly to the root when the validated range is the whole sequence, preserving
   the O(1) whole-sequence bound;
4. otherwise splits before `index` and after `count`;
5. applies the tag once to the isolated middle root; and
6. rejoins the three pieces.

The two boundary spines are copied, and applying the update replaces the isolated middle root.
Untouched, untagged subtrees remain reference-shared. When a split or join descends through a tagged
node, however, pushing that tag may replace an off-spine child's wrapper root so the child carries
the inherited tag; that child's interior nodes remain shared. The guarantee is therefore logarithmic
path copying with retained subtree interiors, not universal root identity for every outside subtree.

#### Range Query

`MeasureRange` should use a read-only traversal that decomposes a contiguous interval into O(log n)
fully covered subtrees. It carries inherited tags logically and applies them to cached measures; it
does not need to split the tree or allocate permanent nodes.

#### Indexed Read And Enumeration

Reads must not push by allocating. Carry the composed inherited tag down the path and apply it to the
already locally transformed node value. When an inherited tag and the current node's pending tag
both exist, the inherited tag is newer: descending to a child therefore carries
`Compose(inherited, node.PendingTag)`. Enumeration uses a stack frame containing the node and the
inherited tag for that subtree, yielding logical values in sequence order.

### Proposed Public Surface

```csharp
public sealed class RangeUpdateSequence<TElement, TMeasure, TTag, TOps>
    : IReadOnlyList<TElement>
    where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
{
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Empty { get; }
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Create(
        ReadOnlySpan<TElement> items);
    public static RangeUpdateSequence<TElement, TMeasure, TTag, TOps> CreateRange(
        IEnumerable<TElement> items);

    public int Count { get; }
    public bool IsEmpty { get; }
    public TMeasure Measure { get; }
    public TElement this[int index] { get; }

    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Prepend(TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Append(TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Insert(int index, TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> SetItem(int index, TElement item);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> RemoveAt(int index);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Concat(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> other);
    public (
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Left,
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> Right) SplitAt(int index);
    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> GetRange(
        int index,
        int count);

    public RangeUpdateSequence<TElement, TMeasure, TTag, TOps> ApplyRange(
        int index,
        int count,
        TTag tag);
    public TMeasure MeasureRange(int index, int count);
}
```

Do not expose “add” or “assign” methods on the generic core. Those are tag-policy meanings. Provide a
small built-in or documentation example whose tag supports assign and add, and use it as the primary
test oracle before deciding whether a convenience facade merits public surface.

### Complexity Contract

| Operation | Bound |
| --- | --- |
| `Count`, `IsEmpty`, `Measure` | O(1) worst case |
| Index lookup | O(log n) worst case, no permanent allocation |
| Insert/remove/replace | O(log n) worst case, O(log n) new nodes |
| Split/concat | O(log n) worst case, O(log n) new nodes |
| Whole-sequence tag application | O(1) |
| Arbitrary range tag application | O(log n) worst case, O(log n) new nodes |
| Arbitrary range measure query | O(log n) worst case, no permanent allocation |
| Enumeration | O(n), stack proportional to tree height |

These are structural bounds. They do not assert lower elapsed time or allocation than `Rope<T>`, an
array-backed segment tree, or any external library.

### Failure And Concurrency Contract

- Invalid indices/ranges fail eagerly.
- Arithmetic overflow in `Count` or policy code publishes no result.
- A throwing measure/tag policy leaves every input version unchanged.
- Empty-range and `IsIdentity`-recognized updates return the source instance. The generic type has
  no element-equality policy, so `SetItem` does not promise an equal-value identity shortcut.
- Measuring an empty range returns the cached empty measure without invoking element-measure or tag
  callbacks after generic initialization.
- The type is immutable and safe for concurrent reads.
- No mutable cache is required for tag propagation.
- User policies are expected to be deterministic and side-effect-free; concurrent invocation is a
  policy responsibility, matching existing measure/comparer conventions.

### Validation Plan

#### Algebra-law tests

- tag-monoid identity/associativity, identity-recognition soundness, action composition,
  singleton consistency, empty-measure preservation, and distribution;
- noncommutative string/tuple measures;
- assign-after-add and add-after-assign histories; and
- identity tags that are value-distinct but algebraically recognized by `IsIdentity`.

#### Example and boundary tests

- empty, singleton, whole-range, empty-range, prefix, suffix, and one-element updates;
- every split/concat boundary;
- index and count overflow;
- enumeration and range-measure order; and
- retained old versions after every operation.

#### Model-based command tests

Use a mutable array/list model. Generated commands include insert, remove, set, split, concat, range
assign/add, point/range query, snapshot retention, and branching from arbitrary old versions.

#### Invariant tests

Validate recursively:

- AVL height and balance;
- cached count;
- cached logical measure under pending tags;
- tag-composition order;
- logical enumeration;
- no mutable aliasing; and
- a logarithmic height ceiling.

#### Deterministic complexity guards

Instrument node visits, node allocations, rotations, tag applications, and policy calls. Assert
operation-specific bounds as functions of observed tree height. Prefer these guards to elapsed-time
assertions.

#### Failure and concurrency tests

- failpoint policies throwing from measure, apply, and compose;
- failure on every path/rotation position;
- concurrent enumeration, indexing, range query, and aggregate reads over shared versions; and
- policies returning object-distinct but semantically equal measures.

### Exit Criteria

Before shipment:

- the algebra and composition order are documented and law-tested;
- an executable array/list model covers generated branching histories;
- AVL/tag/measure invariants pass after every generated command;
- deterministic height/node/allocation ceilings pass;
- failure atomicity and concurrent reads pass;
- public XML and FingerTree overview/usage/API/validation documents are updated; and
- repository catalogs and semantic contracts record the new sibling core.

Benchmark evidence is intentionally absent from this exit list. Performance positioning remains a
later isolated activity.

## Reserve Candidate: `PersistentBiMap<TKey, TValue>`

This remains a Strong derived candidate and is implementable as two HAMTs, but its semantic matrix is
larger than its representation suggests. Before implementation, decide:

- key and value comparer sources;
- whether adding a pair conflicting on either side throws or displaces an existing pair;
- whether replacement may change both representatives;
- how a reverse view preserves identity and avoids rebuilding;
- which receiver supplies policy during algebra; and
- how callback failure preserves the two-map bijection invariant.

The lowest-risk first contract is strict:

- `Add` rejects an equivalent key or equivalent value;
- `SetItem(key, value)` may replace the value for `key` only when the new value is not owned by a
  different key;
- conflicts throw before publication;
- forward and reverse comparers are retained independently; and
- a facade is constructed only after both persistent successor maps have been produced.

This candidate should follow the bag unless a concrete bidirectional-lookup consumer promotes it.

## Reserve Candidate: Value-Carrying Interval Map

A value-carrying interval map can reuse the measured interval-tree pattern, but it needs a contract
for cases the current interval-only collection can avoid:

- multiple entries with equal low/high endpoints;
- interval equality versus payload equality;
- replacement versus duplicate retention;
- which representative survives removal and coalescing;
- whether coalescing equal/overlapping keys combines, selects, or rejects payloads; and
- whether lookup returns the first ordered overlap or all matching entries.

The safest first type is an interval dictionary that stores one payload per exactly equivalent
interval and rejects ambiguous coalescing. Do not add it merely by changing `Interval<T>` into a pair
and inheriting undocumented duplicate behavior.

## Candidates To Keep Parked

| Candidate | Blocking reason |
| --- | --- |
| `FrozenHashMap` / `FrozenHashSet` F0–F3 | F0 and F1 require isolated measurement; F2 remains unauthorized; F3 depends on a shipped F2 contract. |
| Automatic flat/tree size tiers | Thresholds and large-case dispatch costs are benchmark-derived. |
| GUID-specific full-key CHAMP/Patricia | No current consumer; the design study explicitly requires a representative GUID benchmark first. |
| Persistent chunked bitset | Must first be compared with the shipped Patricia composition and needs a concrete client. |
| RRB transient or persistent tail | Current vector ships; remaining adoption/performance questions are measurement-driven. |
| C4 deque/RRB/reversible/raw-FingerTree/Tungsten cursors | Explicitly consumer- and benchmark-gated. |
| ART and automatic key-type dispatch | Requires a real byte-prefix/range consumer after explicit Patricia consideration. |
| Order-maintenance list | The independent ordered-set fork may keep simple labels private. A public precedes-query core still needs a general consumer or evidence that independently owned private labeling is inadequate; Tungsten is provenance, not its foundation. |
| Native Ctrie ports | Require an independent safe-reclamation architecture, not ordinary porting. |
| Generic ordered map | `PersistentAssociation` is implementation evidence only. A general map needs a named consumer plus an independent project, contract, representation choice, values-in-HAMT versus values-in-both decision, model, and tests. |
| Three-way HAMT merge | Needs a consumer-defined conflict matrix. |
| `PersistentHashMultimap` | Straightforward after `AddOrUpdate`, but consumer-gated by parity economics. |
| Addressable priority queue | The shipped priority-search queue covers most keyed-priority needs; the remaining handle/timer niche needs a named caller. |
| Graph, union-find, table, workspace, overlay, versioned store | Feasible compositions whose modal/API choices require concrete clients. |
| `MerkleHamt` | Large trust-boundary and wire-policy project substantially overlapped by the shipped Merkle search tree. |

## Rejected Structures Remain Rejected

Do not re-enter these into the schedule without new evidence answering their recorded objection:

- hollow and strict-Fibonacci heaps;
- self-adjusting/splay-style persistent structures;
- Kaplan–Tarjan real-time deque;
- `Atom<T>` as a repository data-structure family;
- persistent LRU/`SnapshotCache`; and
- the HAMT-backed sparse-bitset form.

## Review Reports And Open Work

The review corpus contributes no new structure to this sequence:

- Axis 1 first- and second-round findings are closed.
- Structural HAMT set algebra is complete across all six ports.
- The C/C++, C#/Rust, Haskell/Kotlin, and cross-language deferred lists have resolution addenda.
- C++ FingerTree’s port plan and review reports are historical; their API, validation, packaging,
  and documentation gaps were remediated.
- The CHAMP editing-session review has no open correctness finding.

Optional Haskell asynchronous-exception injection and Rust `catch_unwind` coverage may be added as
ordinary hardening. They neither block nor precede the selected C# structures.

## Documentation Corrections Discovered By The Audit

Update these records in or before the first implementation tranche:

1. The derived catalog’s status says none of its candidates have shipped even though Association and
   structural CHAMP work have shipped.
2. Its bulk-construction gap predates the current canonical one-freeze HAMT `BulkBuilder` path.
3. Historical A1 conflates staging builders, transients, and persistent single-pass updates; the
   current lifecycle vocabulary separates them.
4. The GUID study describes hash-set algebra as element-wise even though same-type structural CHAMP
   set algebra now ships.
5. The 2026-07-09 proposal’s RRB deferral and cursor sequencing have been superseded.
6. The Axis 2 final plan’s status prose predates C1/C2/C3/T2 shipment; it remains normative only for
   its unshipped phases where the frontier catalog points to it.
7. The GUID study understates the work required to feed a raw 128-bit key into the current 32-bit
   CHAMP path. That option requires a widened path/hash representation and is not a thin facade.
8. The derived/frontier catalogs sometimes describe Tungsten shipment as though it completed or
   supplied a general ordered-map substrate. They must distinguish application evidence from a
   reusable general family.
9. The original ordered-set recommendation violated the Tungsten application-leaf boundary. This
   revision replaces the wrapper with an independently owned general composite and records which
   Tungsten-specific guarantees it deliberately drops.

Preserve historical conclusions while adding explicit current-state notes rather than silently
rewriting proposal-time reasoning.

## Implementation And Commit Tranches

If this proposal is accepted, use these self-contained tranches:

1. **Persistent HAMT single-pass update kernel — shipped in C#, TypeScript, and Python**
   - node operation, public API, exhaustive transition/callback tests, docs.
2. **Hash-bag facade — shipped in C#, TypeScript, and Python**
   - source, count/algebra/representative model tests, docs and catalogs.
3. **Complete independent ordered set — shipped in C#, TypeScript, and Python**
   - new Ordered source/test projects and solution entries;
   - independent API, invariants, representative/movement/algebra decisions, complexity contract;
   - forked private sparse-label mechanics, comparer-aware model, invariant and dependency guards;
   - Ordered overview/usage/API/validation docs and repository catalogs;
   - no Tungsten reference, friend grant, linked source, or live test oracle;
   - land no public stub or throwing placeholder: source, tests, and docs pass together;
   - no benchmark changes or claims.
4. **Range-update algebra and private core — C# focused checkpoint implemented and green**
   - law harness, node/tag invariants, split/join, deterministic counters.
5. **Range-update public facade — C# focused checkpoint implemented; full solution gate pending**
   - API, model/failure/concurrency tests, docs, catalogs, semantic contracts.
6. **Remaining seven-sibling parity rollout — only after C# Step 4 ships**
   - port the single-pass HAMT APIs, hash bag, and ordered set to C, C++, Haskell, Kotlin, and Rust;
   - port the range-update sequence to C, C++, Haskell, Kotlin, Rust, TypeScript, and Python;
   - preserve the shared semantic contracts through language-local naming, ownership, error,
     enumeration, and policy idioms;
   - treat TypeScript and Python source, tests, and documentation as mandatory parity work; and
   - validate every workspace serially with no overlapping build or test toolchains.
7. **Audit-derived documentation cleanup**
   - may be folded into the first affected tranche when the correction is directly relevant; keep
     unrelated historical-status cleanup in one dedicated documentation commit.

Use detailed multi-line commit messages with the repository’s co-author trailer. Run restore, build,
and tests with one worker and no build servers. Do not overlap language workspaces. Do not invoke
BenchmarkDotNet or interpret benchmark artifacts during these tranches.

## Validation Commands

For each C# implementation tranche:

```powershell
cd src/CSharp
dotnet restore --disable-parallel --disable-build-servers -m:1 -nr:false `
    -p:RestoreDisableParallel=true -p:BuildInParallel=false -p:UseSharedCompilation=false
dotnet build --no-restore --disable-build-servers -m:1 -nr:false `
    -p:BuildInParallel=false -p:UseSharedCompilation=false
.\test.ps1
```

Run the workspace sequentially. The selected exit criteria rely on the complete suite plus
deterministic guards; they do not require a Release benchmark run.

For documentation-only tranches, run the repository stale-path scan, Markdown link checker, and
`git diff --check` from the [documentation maintenance guide](../guides/documentation-maintenance.md).

## Final Recommendation

The persistent HAMT's single-pass point-update kernel and `PersistentHashBag<T>` facade are complete
in C#, TypeScript, and Python. The bag consumes that enabling API inside the shipped HAMT family and
supplies explicit multiplicity, receiver-policy normalization, representative, overflow, identity,
and expanded-enumeration semantics without depending on benchmark evidence.

`PersistentOrderedSet<T>` has likewise shipped in independently owned neutral C#, TypeScript, and
Python packages. Each port reuses public general foundations but owns its dual-index invariant,
sparse-label/relabel implementation, movement and representative contract, independent model, and
evolution. The hardened C# tranche also locks deterministic, failure-atomic relabel fallback and
strict dependency guards. Tungsten `PersistentAssociation` remains useful provenance and a source
of adversarial cases, never a dependency or semantic oracle. Its focused Debug and Release suites
each pass 62/62 tests; the serialized C# Release solution build reports 0 warnings and 0 errors, and
the corresponding pre-Range full C# test gate passes 1,355/1,355 tests.

`RangeUpdateSequence` is the active next new core. Its C# source, tests, documentation, and focused
project gates are green, but final C# shipment awaits the full serialized solution gate and no
sibling Range port has started. It is not as low-risk as the HAMT facade or Ordered composite, but
it is specification-driven rather than benchmark-driven. A separate
persistent implicit AVL keeps that risk local, provides deterministic worst-case bounds, and leaves
the shipped measured finger tree untouched.

After the C# `RangeUpdateSequence` tranche completes, Steps 1–3 must be ported to C, C++, Haskell,
Kotlin, and Rust, while `RangeUpdateSequence` must be ported to all seven siblings. Use language-local
APIs and serialized per-workspace validation. TypeScript and Python remain mandatory parts of that
rollout; their outstanding surface is `RangeUpdateSequence`.

Benchmarks were not run and remain postponed for an isolated machine session.

Everything dependent on frozen-layout evidence, representation thresholds, a missing consumer, or a
new reclamation architecture remains parked.

## Primary References

- [Frontier structure catalog](../reference/frontier-structure-catalog.md)
- [Derived structure catalog](../reference/derived-structure-catalog.md)
- [Data-structure catalog](../reference/data-structure-catalog.md)
- [2026-07-09 next-structures proposal](new-data-structures-2026-07-09.md)
- [Axis 2 final plan](axis2-lifecycle-and-sequence-cursors.md)
- [Persistent GUID-set design study](persistent-guid-set-design-study-2026-07-12.md)
- [Semantic contracts](../reference/semantic-contracts.md)
- [Tungsten application-leaf dependency boundary](../reference/tungsten-application-leaf-boundary.md)
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md)
- [C# Ordered documentation](../../src/CSharp/docs/Ordered/README.md)
- [C# Ordered validation](../../src/CSharp/docs/Ordered/validation.md)
- Brent Yorgey, [*You Could Have Invented Fenwick Trees*](https://www.cambridge.org/core/journals/journal-of-functional-programming/article/you-could-have-invented-fenwick-trees/B4628279D4E54229CED97249E96F721D), Journal of Functional Programming, 2025
- Taiki Kaneda, Hiroki Arimura, and Shunsuke Inenaga,
  [*Fully Persistent Dynamic LCE via AVL Trees and AVL Grammars*](https://arxiv.org/abs/2607.01580), 2026
- Ralf Hinze and Ross Paterson,
  [*Finger Trees: A Simple General-Purpose Data Structure*](https://www.cs.ox.ac.uk/people/ralf.hinze/publications/FingerTrees.pdf), Journal of Functional Programming, 2006

## Relationship To Other Documents

- The frontier catalog remains authoritative for current implementation status. This proposal
  selects and refines benchmark-independent work; it does not replace the catalog.
- The derived catalog remains the broad composition survey. This proposal selects a small subset and
  resolves additional API and ordering details. Its Tungsten case study is historical design evidence,
  not authority to depend on an application workspace.
- The [Tungsten application-leaf dependency boundary](../reference/tungsten-application-leaf-boundary.md)
  is normative for ownership and dependency direction. This proposal's independent Ordered design
  is subordinate to that policy.
- The Axis 2 final plan remains authoritative for frozen F0–F3 and C4 gating. None of those phases is
  advanced here.
- Workspace API, usage, and validation documents become authoritative only when each proposed public
  surface ships.
