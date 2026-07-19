# Range-Update Sequence Contract

- Status: C# reference implementation shipped
- Created (UTC): 2026-07-15T02:50:25Z
- Repository HEAD: 472448f2675e56119f92f81bddf47caac8fd30f4
- Audience: Consumers, implementers, reviewers, and port authors for `RangeUpdateSequence`
- Scope: C# algebra, representation, public API, semantics, structural bounds, and validation boundary

`RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` is an immutable indexed sequence that can
transform every element in a contiguous range and update the range's cached measure without
visiting the range interior. It is a sibling core in `Tools.DataStructures.FingerTree`, not a
modification of either shipped finger-tree engine. A deterministic implicit-key AVL tree supplies
split, join, indexed editing, and concatenation; an algebra supplied by `TOps` supplies element
measurement, ordered measure combination, lazy-tag composition, and the action of a tag on both an
element and an already-combined subtree measure.

The capability contract is structural: a whole-sequence update is O(1), and an arbitrary
contiguous update or aggregate query is O(log n). It does not claim lower elapsed time or fewer
allocations than a rope, array-backed segment tree, or external collection. Benchmark positioning
is deliberately postponed until the machine can run the harness in isolation.

## Algebra

The policy surface extends the existing ordered measure vocabulary:

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

`Compose(newer, older)` always means **apply `older`, then apply `newer`**. The parameter order is
part of the public contract. In particular, an assignment composed as the newer operation normally
discards an older addition, while a newer addition must be folded into an older assignment.

An implementation may represent the absence of a pending tag separately from `TTag`; it does not
infer identity through `EqualityComparer<TTag>`. `IsIdentity` is the authoritative no-op predicate,
including when several value-distinct tag values represent the algebraic identity.

### Required laws

For elements `x`, measures `a` and `b`, tags `p`, `q`, and `r`, and subtree counts `ca` and `cb`, a
valid policy obeys all of the following equations:

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

The inherited `IMonoid<TMeasure>` laws still apply: `Empty` is a two-sided identity for `Combine`,
and `Combine` is associative. `Combine` is ordered; it need not be commutative. Any tag for which
`IsIdentity` returns `true` must obey the same element, measure, and composition laws as
`IdentityTag`, even when it is a different `TTag` value. The library assumes these policies are
deterministic, side-effect-free, and lawful; violating the preconditions yields unspecified logical
results.

## Representation And Lazy-Tag Invariant

Each immutable implicit-AVL node stores a value, left and right children, height, element count,
ordered cached measure, and an optional pending tag. Its invariant is:

- the node's `Value` and cached `Measure` already reflect its own pending tag;
- its child objects do not yet reflect that tag;
- the cached measure is the ordered combination of the logical left sequence, logical node value,
  and logical right sequence;
- count is one plus both child counts; and
- child heights differ by at most one.

Applying a tag to a whole subtree therefore transforms the root value and cached aggregate,
composes the new tag after any older pending tag, and retains both child references. Before a
structural descent or rotation, the implementation immutably pushes the pending tag into each
nonempty child root and clears the marker on the rebuilt parent. Rotations operate only on pushed
nodes, so a pending action cannot be stranded above a child that has moved outside its range. No
node reachable from an older version is mutated.

Indexed reads and enumeration do not push by allocating replacement nodes. They carry an inherited
tag through the traversal. If both an inherited tag and the current node's pending tag exist, the
inherited tag is newer, so the tag passed to a child is:

```text
Compose(inherited, node.PendingTag)
```

The current node value already includes `node.PendingTag`, and therefore receives only the
inherited action when it is observed.

`MeasureRange` decomposes the query into O(log n) fully covered subtrees and boundary elements. It
applies inherited tags to cached measures without splitting the persistent tree or allocating
permanent nodes. `ApplyRange` validates first, splits at its two boundaries, applies one tag to the
middle root, and AVL-joins the pieces. Boundary descent copies AVL spine nodes. When it crosses a
pending tag, immutable push may additionally allocate a replacement wrapper root for an off-spine
child so the tag follows that child; the child's interior remains shared. Once the middle is
isolated, applying its tag replaces only that middle root and retains its child references;
unaffected outside-subtree interiors likewise retain their existing nodes. A full-range update
applies directly to the root in O(1).

## Public API

The C# surface is:

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

    public Enumerator GetEnumerator();

    public struct Enumerator : IEnumerator<TElement>
    {
        public TElement Current { get; }
        public bool MoveNext();
        public void Dispose();
    }
}
```

The generic sequence intentionally does not expose methods named `AddRange` or `AssignRange`:
addition and assignment are meanings of a particular tag algebra, not universal sequence
operations.

## Operation Semantics

- `Create` and `CreateRange` preserve input order and build a deterministic balanced tree.
  `CreateRange` rejects `null`, consumes its enumerable eagerly and once, and completes enumeration
  before it begins element-measure callbacks. An empty input returns `Empty`.
- Index lookup, `SetItem`, and `RemoveAt` accept `0 .. Count - 1`. `Insert` and `SplitAt` accept
  `0 .. Count`. Range members accept `index >= 0`, `count >= 0`, and
  `index + count <= Count`, without using overflow-prone unchecked addition.
- All index and range validation precedes algebra callbacks. Invalid inputs fail eagerly even when
  a supplied update tag is recognized as identity.
- `SplitAt(0)` and `SplitAt(Count)` retain the source as the nonempty result. Empty-side `Concat`
  retains the nonempty operand. `GetRange(0, Count)` returns the source; an empty range returns
  `Empty`.
- `Prepend`, `Append`, `Insert`, and `SetItem` treat their supplied item as its current logical
  value. Earlier lazy tags are pushed away from the edit path and never retroactively transform the
  new item.
- `Concat` rejects `null` and checks combined-count overflow before tag or measure callbacks.
- `ApplyRange` returns the source instance for an empty range or a tag recognized by `IsIdentity`.
  An empty range does not call `IsIdentity`. Whole-sequence nonidentity updates allocate only the
  transformed root. The type has no element equality policy, so `SetItem` never promises an
  equal-value identity shortcut.
- `Measure` is the logical aggregate of the entire sequence. `MeasureRange` preserves sequence
  order and returns the cached empty measure for an empty range without element-measure or tag
  callbacks after generic initialization.
- Construction, editing, splitting, joining, and range updates publish a facade only after every
  required user-policy call has completed. A throwing `Measure`, `Combine`, `IsIdentity`,
  `Compose`, `ApplyElement`, or `ApplyMeasure` callback leaves all input versions unchanged.
  Likewise, checked count or policy arithmetic overflow publishes no result.
- Snapshots retain structural sharing. Every operation leaves every input version logically and
  structurally valid, including when a later operation throws.

### Enumeration

Enumeration yields logical elements in sequence order after applying all inherited tags. The
concrete `GetEnumerator` pattern does not box. Enumeration through `IEnumerable<TElement>` or
`IEnumerable` boxes the enumerator. An empty concrete enumerator has no traversal state. A nonempty
enumerator allocates shared O(log n) traversal state.

The enumerator follows the library's shared-state, fail-fast copy contract: copying an in-progress
enumerator copies its handle to the same traversal state. Advancing either copy invalidates the
other copy's expected state; using the stale copy throws `InvalidOperationException`. Independently
created enumerators have independent state and may be read concurrently. `Reset` throws
`NotSupportedException`, `Dispose` is a no-op, and `Current` returns `default` before the first
successful `MoveNext` and after enumeration ends.

The collection itself is immutable and safe for concurrent reads. It has no mutable tag-propagation
cache. Algebra callbacks may run concurrently when callers read one or more snapshots concurrently;
thread safety of callback-owned state is the policy author's responsibility.

### Positional And Range Cursor

`GetCursor(int position = 0)` returns `RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps>`, a
`public readonly struct` positional gap cursor over one sequence version. It is a **Profile R
snapshot-plus-gap checkpoint** under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md):
the value is a retained sequence reference plus a validated position, and every edit delegates to the
ordinary operation above. It claims none of the C# rope tier's focused representation, memo cell,
callback ceiling, allocation bound, or amortized-locality properties.

The surface is the positional protocol — `Count`, `Position`, `IsAtStart`, `IsAtEnd`,
`TryPeekPrevious`, `TryPeekNext`, `MovePrevious`, `MoveNext`, `Seek(int)`, `Insert(TElement)`,
`DeletePrevious()`, `DeleteNext()`, `ReplaceNext(TElement)`, `Snapshot()` — plus the measure and
range members this family owns:

```csharp
public TMeasure MeasureBefore { get; }
public TMeasure MeasureAfter { get; }
public TMeasure MeasurePrevious(int count);
public TMeasure MeasureNext(int count);
public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> ApplyPrevious(int count, TTag tag);
public RangeUpdateSequenceCursor<TElement, TMeasure, TTag, TOps> ApplyNext(int count, TTag tag);
```

`InsertRange` is deliberately absent, matching the collection, which also has no bulk positional
insert. The design names it as an expected addition; it is a recorded substrate gap shared by all
nine ports, not a local omission.

`ApplyPrevious(k, tag)` targets `[Position - k, Position)` and `ApplyNext(k, tag)` targets
`[Position, Position + k)`; both keep the gap fixed and both validate the complete range before any
`IsIdentity` test or tag action, exactly as `ApplyRange` does. `MeasurePrevious` and `MeasureNext`
use the same subtraction-safe validation. `Combine(MeasureBefore, MeasureAfter)` equals the version's
whole measure in that order, assuming associativity only — no inverse, commutativity, identity value,
or element equality.

The lazy-tag invariant is preserved unchanged: reads thread inherited-tag state down the descent
rather than pushing tags eagerly, so a navigating cursor neither mutates nor path-copies nodes, and a
cursor that only moves keeps the clean source snapshot reference-identical. A replacement or
inserted element never receives the old tags.

Identity and failure follow the collection. A zero-length or identity-tag `ApplyPrevious`/`ApplyNext`
returns the receiver cursor without callbacks. `Snapshot()` on a clean cursor returns the exact
source instance. Positions outside `0 .. Count` and negative or oversized counts throw
`ArgumentOutOfRangeException`; a boundary violation throws `InvalidOperationException`; and the
invalid `default` value throws `InvalidOperationException` from every member, including `Position`,
`IsAtStart`, and the `Seek(Position)` identity shortcut.

Cursor costs are the collection's. Creation, `Seek`, and movement are O(1) integer work, but a peek
is a full O(log n) indexed descent, so a complete traversal by move-plus-peek is O(n log n) rather
than O(n). `MeasureBefore` and `MeasureAfter` are properties but each performs an O(log n)
`MeasureRange` call and is not cached. `ApplyPrevious`/`ApplyNext` are O(log n), not O(count),
because the range apply stamps one node-level tag.

## Complexity And Sharing

Let n be the sequence length. These are worst-case structural bounds unless stated otherwise:

| Operation | Time | New persistent nodes / auxiliary state |
| --- | --- | --- |
| `Count`, `IsEmpty`, `Measure` | O(1) | O(1) |
| `Create` / `CreateRange` | O(n) | O(n) |
| Index lookup | O(log n) | no permanent allocation |
| `Prepend`, `Append`, `Insert`, `SetItem`, `RemoveAt` | O(log n) | O(log n) nodes |
| `SplitAt`, `Concat`, nontrivial `GetRange` | O(log n) | O(log n) nodes |
| Whole-sequence `ApplyRange` | O(1) | one root node |
| Arbitrary `ApplyRange` | O(log n) | O(log n) nodes |
| `MeasureRange` | O(1) for empty/full; O(log n) for a proper nonempty range | no permanent nodes; O(log n) call stack |
| Enumeration | O(n) | O(log n) traversal state |

The AVL height remains logarithmic, and node allocation stays within the stated boundary/search
bounds. In addition to copied path nodes, pushing a pending tag can allocate off-spine child wrapper
roots while preserving the structure below those roots. Instrumented tests, rather than wall-clock
timing, own the node-visit, allocation, rotation, tag-application, and policy-call ceilings.

## Affine Assignment-And-Addition Example

This policy measures a `long` sequence by its sum. A tag optionally assigns every covered element
and then adds a delta. The tag denotes the affine action
`x -> (HasAssignment ? Assignment : x) + Addition`:

```csharp
using Tools.DataStructures.FingerTree;

public readonly record struct AffineTag(
    bool HasAssignment,
    long Assignment,
    long Addition)
{
    public static AffineTag Add(long delta) => new(false, 0, delta);
    public static AffineTag Assign(long value) => new(true, value, 0);
}

public readonly struct AffineSumAlgebra
    : IRangeUpdateAlgebra<long, long, AffineTag>
{
    public static long Empty => 0;

    public static long Combine(long left, long right) => checked(left + right);

    public static long Measure(long element) => element;

    public static AffineTag IdentityTag => new(false, 0, 0);

    public static bool IsIdentity(AffineTag tag) =>
        !tag.HasAssignment && tag.Addition == 0;

    public static AffineTag Compose(AffineTag newer, AffineTag older)
    {
        // Compose(newer, older) applies older first and newer second.
        if (IsIdentity(newer))
        {
            return older;
        }

        if (IsIdentity(older))
        {
            return newer;
        }

        if (newer.HasAssignment)
        {
            return newer;
        }

        return older.HasAssignment
            ? new AffineTag(
                HasAssignment: true,
                Assignment: checked(
                    older.Assignment + older.Addition + newer.Addition),
                Addition: 0)
            : AffineTag.Add(checked(older.Addition + newer.Addition));
    }

    public static long ApplyElement(AffineTag tag, long element) =>
        checked((tag.HasAssignment ? tag.Assignment : element) + tag.Addition);

    public static long ApplyMeasure(AffineTag tag, long measure, int count)
    {
        if (count == 0)
        {
            return Empty;
        }

        return tag.HasAssignment
            ? checked((tag.Assignment + tag.Addition) * count)
            : checked(measure + tag.Addition * count);
    }
}
```

Using that policy:

```csharp
using Sequence = Tools.DataStructures.FingerTree.RangeUpdateSequence<
    long, long, AffineTag, AffineSumAlgebra>;

var original = Sequence.Create([1, 2, 3, 4]);
var added = original.ApplyRange(1, 2, AffineTag.Add(10));
// added enumerates [1, 12, 13, 4]; added.Measure == 30

var assigned = added.ApplyRange(2, 2, AffineTag.Assign(7));
// assigned enumerates [1, 12, 7, 7]; assigned.Measure == 27

long middle = assigned.MeasureRange(1, 2); // 19
// original still enumerates [1, 2, 3, 4].
```

The composition direction is observable:

```text
Compose(Assign(7), Add(10))  == Assign(7)   // add first, assignment wins
Compose(Add(10), Assign(7))  == Assign(17)  // assign first, then add
```

## Validation Contract

Integration must establish the contract with deterministic evidence:

- executable monoid/action laws, including value-distinct identity tags, noncommutative measures,
  assign-after-add, and add-after-assign;
- boundary examples for empty, singleton, whole, empty, prefix, suffix, and one-element ranges,
  every split/concat boundary, and overflow-safe index/count rejection;
- a mutable array/list oracle for generated insert, remove, replacement, split, concat, range
  assign/add, point/range query, retained snapshot, and arbitrary old-version branch commands;
- recursive AVL balance/height, cached-count, lazy-tag composition, cached logical measure,
  logical enumeration, and immutable-aliasing checks after every generated command;
- deterministic ceilings based on observed height for node visits, node allocations, rotations, tag
  applications, and policy calls;
- failpoint policies throwing from element measurement, measure combination, identity recognition,
  composition, element action, and aggregate action at every reachable path/rotation position;
- concrete and interface enumeration contracts, copied-enumerator fail-fast behavior, independent
  enumerator concurrency, and default/current/reset/dispose behavior; and
- concurrent indexing, enumeration, range measure, and whole-measure reads over shared retained
  versions, including tearable element/measure carriers where useful.

The serialized focused Debug lane passes 62/62 range-update tests. The complete FingerTree project
passes 692/692 tests in Debug and Release, and both project builds report zero warnings and zero
errors. At the pre-bimap Range shipment checkpoint, the full serialized C# solution built with zero
warnings or errors and passed 1,417/1,417 tests in both Debug and Release: 319 Numerics + 292 HAMT +
692 FingerTree + 62 Ordered + 52 Tungsten. This records the C# reference as shipped. Benchmarks are
not part of this contract gate and remain postponed until they can run in isolation.
