# Benchmark-Independent Next Data Structures: Detailed C# Implementation Proposal

- Status: Proposed execution sequence — benchmark-independent work only
- Created (UTC): 2026-07-14T19:23:49Z
- Repository HEAD: ab9a73c6ae20a3b0ee0627bfe810117450e20c3e
- Audience: Maintainers selecting the next C# persistent-collection work after Axis 1 and the shipped Axis 2 tranches
- Scope: Repository-wide plan/proposal audit, candidate disposition, and detailed contracts and validation gates for the next C# data structures that do not depend on postponed benchmark evidence

## Decision

Proceed in this order:

1. Implement `PersistentOrderedSet<T>` as a thin facade over the shipped Tungsten association
   substrate.
2. Add persistent-HAMT single-pass `GetOrAdd`/`AddOrUpdate` operations, then implement
   `PersistentHashBag<T>` over `PersistentHashMap<T, int>`.
3. Implement `RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` as the next genuinely new
   structure core, after locking its tag-action algebra and executable laws. Prefer a separate
   path-copied implicit AVL core over adding lazy tags to the existing measured finger-tree engine.
4. Keep `PersistentBiMap<TKey, TValue>` and a value-carrying interval map as reserve candidates.
   Give either a dedicated contract pass before promoting it into the active sequence.

This order deliberately distinguishes two notions of “next”:

- **Lowest implementation risk:** `PersistentOrderedSet<T>` and `PersistentHashBag<T>` reuse
  shipped, heavily tested cores and can be validated entirely through semantic models and
  deterministic structural guards.
- **Next new core in the current frontier roadmap:** `RangeUpdateSequence` is the only unshipped
  candidate that the current frontier catalog calls Strong and actively sequences without a
  benchmark pre-gate.

No benchmark is required to begin or ship these three structures. This proposal does not authorize
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
   candidate rationale, but their implementation-status language is not authoritative.

The audit covered:

- every document under `docs/proposals`;
- the shipped, derived, and frontier structure catalogs;
- every report under `docs/reviews`;
- every C# FingerTree proposal and cursor decision;
- every C# HAMT transient/frozen decision and the HAMT implementation review;
- the C++ FingerTree port plan, editorial notes, and all three port-review reports; and
- Numerics future-width and code-generation plans, which were classified as outside this
  data-structure proposal.

Candidate status was then checked against the C# source and test trees. Exact-name searches confirm
that `PersistentOrderedSet`, `PersistentHashBag`, `RangeUpdateSequence`, `PersistentBiMap`, and a
value-carrying interval-map facade are not currently shipped C# types.

## Current Baseline

The following work is complete and must not be mistaken for pending implementation:

- CHAMP canonical nodes, structural equality/diff, and structural map/set algebra;
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
5. A C# reference implementation can land without committing sibling-language parity prematurely.
6. Its value is a capability or meaningful collection vocabulary, not merely a speculative
   constant-factor optimization.

“Low risk” means more than a small file count. It means that the representation invariant, policy
source, representative rules, order, failure atomicity, no-op identity, and model oracle can all be
stated before implementation.

## Candidate 1: `PersistentOrderedSet<T>`

### Why It Leads

The historical next-structures proposal identifies an insertion-ordered set as the cheapest new
family because the association already supplies the complete dual-index substrate. That assessment
still holds. The shipped `PersistentAssociation<TKey, TValue>` provides:

- comparer-preserving hashed lookup;
- one persistent sequence entry per key;
- stable order-maintenance stamps;
- positional lookup and index lookup;
- append, prepend, arbitrary insert, removal, slicing, reverse, and stable sorting; and
- stored-key representative rules inherited from CHAMP.

An ordered set erases the association’s value dimension. It does not need a new node type, a new
balancing algorithm, a new ownership model, or a new amortization argument.

### Placement And Representation

Place the type in `Tools.DataStructures.Tungsten`, the existing project that composes HAMT and
FingerTree storage. Do not add a dependency from the HAMT project to FingerTree or from FingerTree to
HAMT merely to host the facade.

Recommended representation:

```csharp
public sealed class PersistentOrderedSet<T> : IReadOnlySet<T>
    where T : notnull
{
    private readonly PersistentAssociation<T, Unit> _items;
}
```

`Unit` is an internal zero-state value whose equality is unconditional. Using the association as the
single source of truth avoids duplicating its stamp/relabel code. The extra zero-sized logical value
does not alter the collection semantics.

### Proposed Public Surface

| Area | Members |
| --- | --- |
| Construction | `Empty`, `Create(comparer)`, `CreateRange(items, comparer)` |
| State | `Count`, `IsEmpty`, `Comparer`, `First`, `Last` |
| Lookup | `Contains`, `TryGetValue`, `GetAt`, indexer by position, `IndexOf` |
| Stable-position update | `Add` |
| Explicit repositioning | `Append`, `Prepend`, `Insert` |
| Removal | `Remove`, `TryRemove`, `RemoveAt`, `RemoveFirst`, `RemoveLast`, `Clear` |
| Range/order | `GetRange`, `Take`, `Drop`, `Reverse`, stable one-shot `Sort` |
| Set algebra | `Union`, `Intersect`, `Except`, `SymmetricExcept` |
| Relations | `IsSubsetOf`, `IsProperSubsetOf`, `IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, `SetEquals` |
| Enumeration | struct `Enumerator`, `ToArray` |

Avoid sorted-set vocabulary such as `Min`, `Max`, lower-bound, or range-by-value. “Ordered” here means
insertion/explicit-position order, never comparison order.

### Construction And Representative Contract

- `CreateRange` processes the input in order.
- The first representative of an equivalence class fixes its position and stored representative.
- Later equivalent inputs are logical no-ops.
- The supplied comparer defines both membership and duplicate collapse.
- Empty values retain the supplied comparer object; the default singleton is used only for the
  reference-default comparer.
- `TryGetValue(equalValue, out actualValue)` returns the stored representative.

These rules match `PersistentAssociation.CreateRange` with unit values and the HAMT’s first-key
representative contract.

### Update And Repositioning Contract

Three verbs must remain distinct:

- `Add(item)` appends an absent item. If an equivalent item already exists, it returns the current
  set instance and retains both position and representative.
- `Append(item)` places the supplied representative at the end. If an equivalent item exists at
  another position, it is removed and the supplied representative is re-added at the end. If its
  equivalence class is already last, the operation is an identity-preserving no-op and retains the
  stored representative, even when the supplied object is distinct.
- `Prepend(item)` is symmetric at the front.
- `Insert(index, item)` interprets the index against the pre-removal sequence, matching Association.
  If an equivalent item exists, that occurrence is removed and the supplied representative occupies
  the requested logical position.

This distinction gives callers both ordinary persistent-set behavior and explicit order-editing
behavior without hidden movement on `Add`.

### Set-Algebra Ordering

Same-type algebra uses the receiver’s comparer and the following deterministic order:

- `Union(other)`: receiver elements in receiver order, followed by argument elements not equivalent
  to any retained receiver element, in argument order.
- `Intersect(other)`: retained receiver elements in receiver order.
- `Except(other)`: retained receiver elements in receiver order.
- `SymmetricExcept(other)`: receiver-only elements in receiver order, followed by argument-only
  elements in argument order after collapsing them under the receiver comparer.

Receiver representatives win wherever a receiver element remains. Argument representatives are
installed only for new argument-only classes. Arbitrary-`IEnumerable<T>` overloads, if included,
must use the same receiver-policy rules rather than delegating equality to a temporary default
`HashSet<T>`.

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
- Empty first/last and removal operations follow the nearest Association/List precedent and must be
  documented consistently.

### Complexity

Publish only complexity inherited from `PersistentAssociation`:

| Operation | Bound |
| --- | --- |
| Hashed membership / stored representative | O(w + c) |
| Positional lookup | O(log min(index + 1, n - index)) worst case |
| `Add` of an absent item | O(w + c) amortized on a linear history; O(w + c + log n) worst case |
| Explicit append/prepend | O(w + c + log n) |
| Positional insert/remove | O(w + c + log n), plus the documented O(n (w + c)) relabel path for insertion |
| Reverse/slice | Exact Association bounds, including index reconciliation for slices |
| Stable one-shot sort | O(n log n) ordering-comparer calls plus O(n (w + c)) rebuild |
| Enumeration | O(n) |

Order-maintenance relabeling retains the Association’s honest per-produced-version worst case; do not
silently advertise a branching-persistence amortization that the substrate does not provide.

### Validation Plan

Use a comparer-aware model containing an ordered `List<T>` plus explicit equivalence-class lookup.
The test matrix must cover:

- example tests for every member;
- generated command histories including retained branches;
- constant-hash comparers and collision-heavy equivalence classes;
- comparison-equivalent but object-distinct representatives;
- add-versus-reposition distinctions;
- every same-type and enumerable algebra operation under differing comparer instances;
- no-op reference identity;
- relabel-boundary histories;
- enumerator order and concurrent read-only enumeration; and
- eager argument validation.

No benchmark is an exit criterion.

### Exit Criteria

The type ships when:

- the public contract above is reflected in XML documentation;
- the command model, representative matrix, algebra matrix, retained-version tests, and no-op identity
  tests pass;
- workspace overview/usage/API/validation docs and repository catalogs are updated; and
- the complete C# suite passes with one build/test worker.

## Enabling API: Persistent HAMT Single-Pass Updates

### Why This Is Separate From Builders And Transients

The historical A1 proposal grouped `Update`, `GetOrAdd`, a builder, and transient construction. The
current repository vocabulary makes that grouping obsolete:

- `BulkBuilder` is an internal staging mechanism used by canonical one-freeze construction.
- `Transient` is the public one-way owner-token editing session.
- `GetOrAdd`/`AddOrUpdate` are ordinary persistent point operations returning immutable versions.

Canonical bulk construction and C# transients already ship. Only the single-pass persistent point
operation remains an enabling API gap.

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

## Candidate 2: `PersistentHashBag<T>`

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
once into canonical CHAMP shape. This remains an internal staging path, not a second public builder
or transient lifecycle.

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

| Area | Members |
| --- | --- |
| Construction | `Empty`, `Create(comparer)`, `CreateRange(items, comparer)` |
| State | `DistinctCount`, `TotalCount`, `IsEmpty`, `Comparer` |
| Lookup | `Contains`, `CountOf`, `TryGetValue` for the stored representative |
| Update | `Add`, `AddCopies`, `Remove`, `RemoveCopies`, `RemoveAll`, `Clear` |
| Algebra | `Union`, `Intersect`, `Except`, `Sum` |
| Enumeration | expanded enumerator, `DistinctItems`, `Entries`, `ToArray` with explicit overflow behavior |

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

`TotalCount` cannot overflow for a valid bag: `DistinctCount <= int.MaxValue` and every
multiplicity is at most `int.MaxValue`, so the maximum representable total is
`(int.MaxValue * (long)int.MaxValue) < long.MaxValue`. Keep internal total arithmetic checked as a
defensive invariant guard, but do not advertise an unreachable public total-overflow case.

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

### Complexity

| Operation | Bound |
| --- | --- |
| `CountOf` / `Contains` | O(w + c), allocation-free |
| Point add/remove | O(w + c), one rebuilt search path when changed |
| Expanded enumeration | O(`TotalCount`) |
| Distinct/entry enumeration | O(`DistinctCount`) |
| Same-type structural algebra | Inherit the CHAMP structural bound where implemented directly; otherwise document element-wise work honestly |

Do not claim structural bag algebra merely because the underlying map has structural algebra. A
combining multiplicity operation needs an explicit lockstep implementation before receiving that
bound.

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

No benchmark is an exit criterion.

## Candidate 3: `RangeUpdateSequence`

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
3. splits before `index` and after `count`;
4. applies the tag once to the isolated middle root; and
5. rejoins the three pieces.

The two boundary spines are copied; the range interior and all outside subtrees remain shared.

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
- Measuring an empty range returns `TOps.Empty` without invoking element or tag policy code.
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
| Order-maintenance list | Requires a second consumer or an observed Association relabel bottleneck. |
| Native Ctrie ports | Require an independent safe-reclamation architecture, not ordinary porting. |
| Generic ordered map | `PersistentAssociation` already proves the composition; a second public map needs a general consumer and a values-in-HAMT versus values-in-both decision. |
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

Preserve historical conclusions while adding explicit current-state notes rather than silently
rewriting proposal-time reasoning.

## Implementation And Commit Tranches

If this proposal is accepted, use these self-contained tranches:

1. **Ordered-set contract and implementation**
   - source, model tests, docs, catalogs, semantic contracts;
   - no benchmark changes.
2. **Persistent HAMT single-pass update kernel**
   - node operation, public API, exhaustive transition/callback tests, docs.
3. **Hash-bag facade**
   - source, count/algebra/representative model tests, docs and catalogs.
4. **Range-update algebra and private core**
   - law harness, node/tag invariants, split/join, deterministic counters.
5. **Range-update public facade**
   - API, model/failure/concurrency tests, docs, catalogs, semantic contracts.
6. **Audit-derived documentation cleanup**
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

The best immediate implementation is `PersistentOrderedSet<T>`. It has a complete substrate, a
small and explicit semantic delta, a strong model oracle, and no representation experiment hidden
inside it.

The second implementation should be the persistent HAMT’s single-pass update operation followed by
`PersistentHashBag<T>`. This closes the highest-leverage remaining API gap and produces another
Strong facade with explicit multiplicity semantics.

`RangeUpdateSequence` should then become the next new core. It is not as low-risk as the two facades,
but it is specification-driven rather than benchmark-driven. A separate persistent implicit AVL
keeps that risk local, provides deterministic worst-case bounds, and leaves the shipped measured
finger tree untouched.

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
- [Porting and semantic parity](../guides/porting-and-semantic-parity.md)
- Brent Yorgey, [*You Could Have Invented Fenwick Trees*](https://www.cambridge.org/core/journals/journal-of-functional-programming/article/you-could-have-invented-fenwick-trees/B4628279D4E54229CED97249E96F721D), Journal of Functional Programming, 2025
- Taiki Kaneda, Hiroki Arimura, and Shunsuke Inenaga,
  [*Fully Persistent Dynamic LCE via AVL Trees and AVL Grammars*](https://arxiv.org/abs/2607.01580), 2026
- Ralf Hinze and Ross Paterson,
  [*Finger Trees: A Simple General-Purpose Data Structure*](https://www.cs.ox.ac.uk/people/ralf.hinze/publications/FingerTrees.pdf), Journal of Functional Programming, 2006

## Relationship To Other Documents

- The frontier catalog remains authoritative for current implementation status. This proposal
  selects and refines benchmark-independent work; it does not replace the catalog.
- The derived catalog remains the broad composition survey. This proposal selects a small subset and
  resolves additional API and ordering details.
- The Axis 2 final plan remains authoritative for frozen F0–F3 and C4 gating. None of those phases is
  advanced here.
- Workspace API, usage, and validation documents become authoritative only when each proposed public
  surface ships.
