# Persistent Monotone-Action Heap: Temporal Bulk Priority Transforms Without Rebuilding

- Status: Experimental C# reference prototype; scoped ADT/lifting synthesis, not a priority claim
- Created: 2026-07-29
- Base repository HEAD: `67a67f9f553a9194db9d8dd3cf9c7bd670f9b981`
- Branch: `experimental/persistent-monotone-action-heap`
- Prototype:
  [`PersistentMonotoneActionHeap.cs`](../../src/CSharp/src/Durable7.FingerTree/PersistentMonotoneActionHeap.cs)
- Tests:
  [`PersistentMonotoneActionHeapTests.cs`](../../src/CSharp/tests/Durable7.FingerTree.Tests/PersistentMonotoneActionHeapTests.cs)
- Scope: one C# research implementation; no cross-language shipment commitment

## Result

`PersistentMonotoneActionHeap<TElement, TPriority, TAction>` is a fully persistent meldable
min-heap whose priority domain admits a fixed-size, constant-time-composable family of monotone
actions. In addition to the ordinary heap operations, it supports:

```text
TransformAll(action): apply action to every entry present in this version
```

`TransformAll` takes O(1) worst-case time and allocates O(1) new structure. Entries inserted later
are not retroactively transformed. Two heaps transformed independently can still be melded in O(1)
worst-case time, including when their actions are noninvertible. The ordinary purely functional heap
bounds remain unchanged:

- `Insert`, `Minimum`, and `Meld`: O(1) worst-case;
- `DeleteMinimum`: O(log n) worst-case;
- enumeration: Theta(n); and
- retaining any version as a fork: O(1).

The supplied exact action family contains identity, lower clamp (floor), upper clamp (cap),
two-sided clamp, and constant. Unlike the familiar additive heap offset, clamp is noninvertible:
after flooring old priorities, there is generally no raw priority that can be assigned to a later
insertion so that one flat inverse normalization recovers its requested value.

The production Haskell [`Data.Heap`](https://hackage.haskell.org/package/heaps-0.4.1/docs/Data-Heap.html)
is itself a persistent Brodal-Okasaki heap: it documents O(1) worst-case insert, minimum, and union,
O(log n) worst-case delete-min, and O(n) `mapMonotonic`. For any clamp in the action family, this
prototype specializes that weakly monotone map from O(n) to O(1) worst-case while retaining those
standard bounds. This repository's state-only `BrodalOkasakiHeap<T>` gives the same comparison:
emulating a clamp through its API requires enumerating and rebuilding `n` entries in Theta(n).
The result is therefore a strict asymptotic improvement for the common extended ADT, with no
asymptotic regression on its shared operations.

This is deliberately a scoped result. Recursively tagged heaps are an equal-bound construction by
definition, and addition-specific lazy heaps are established. The claim is that lifting a
worst-case-optimal persistent Brodal-Okasaki heap through a normalized monoid of possibly
noninvertible monotone priority actions yields a useful aggregate ADT not found in the targeted
search. It is not a new comparison-heap lower bound, a claim that lazy propagation is new, or proof
of first publication.

## Workload And Surface

The motivating workload is a branchable scheduler or search frontier whose existing jobs undergo
bulk policy changes:

- impose a deadline floor on every task currently in one speculative schedule;
- cap priorities in another branch;
- meld the branches;
- insert new tasks under the current external priority scale; and
- retain and continue all source versions.

The public surface is:

```csharp
public interface IMonotoneHeapAction<TPriority, TAction>
{
    TAction Identity { get; }
    bool IsIdentity(TAction action);
    TAction Compose(TAction outer, TAction inner);
    TPriority Apply(TAction action, TPriority priority);
}

public interface IComparerBoundMonotoneHeapAction<TPriority, TAction>
    : IMonotoneHeapAction<TPriority, TAction>
{
    IComparer<TPriority> Comparer { get; }
}

public readonly record struct MonotoneHeapEntry<TElement, TPriority>(
    TElement Element,
    TPriority Priority);

public sealed class PersistentMonotoneActionHeap<TElement, TPriority, TAction>
    : IEnumerable<MonotoneHeapEntry<TElement, TPriority>>
{
    public static PersistentMonotoneActionHeap<TElement, TPriority, TAction> Create(...);
    public static PersistentMonotoneActionHeap<TElement, TPriority, TAction> CreateRange(...);

    public int Count { get; }
    public bool IsEmpty { get; }
    public MonotoneHeapEntry<TElement, TPriority> Minimum { get; }
    public IComparer<TPriority> Comparer { get; }
    public IMonotoneHeapAction<TPriority, TAction> ActionPolicy { get; }

    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> Insert(...);
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> TransformAll(TAction action);
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> Meld(...);
    public PersistentMonotoneActionHeap<TElement, TPriority, TAction> DeleteMinimum();
    public bool TryGetMinimum(out MonotoneHeapEntry<TElement, TPriority> minimum);
    public bool TryDeleteMinimum(out MonotoneHeapEntry<TElement, TPriority> minimum, out ... remainder);
}
```

Payload and priority are separate. Only priorities participate in heap order, so collapsing two
priorities does not accidentally reorder their payloads. There is intentionally no stable
payload-based tie break, handle-based decrease-key, arbitrary nonmonotone transform, or destructive
meld.

## Action Algebra

The action policy must obey, for all actions `a`, `b`, and priorities `p`:

```text
Apply(Identity, p) = p
Apply(Compose(a, b), p) = Apply(a, Apply(b, p))
p <= q  =>  Apply(a, p) <= Apply(a, q)
```

`Compose(a, b)` therefore means the newer action `a` after the older action `b`. Identity testing,
composition, application, and comparison must each take O(1), and every composed action must stay
fixed-size. Storing a linked expression of transformations would make `Minimum` linear in the
number of retained transforms and would invalidate the advertised bound.

`OrderClampPolicy<T>` supplies one closed family. A nonconstant member denotes:

```text
clamp(x, lower?, upper?)
```

Missing bounds are infinities. Disjoint compositions produce an explicit `Constant(value)` member,
not merely `[value,value]`. That distinction is observable for a comparer that considers distinct
priority values equal: an inclusive `[k,k]` clamp preserves an input comparer-equal to `k`, whereas
the constant function must return the exact `k` representative. For overlapping clamps,
composition chooses the older boundary when two boundary values compare equal. Thus the built-in
policy preserves exact sequential results, not only comparison equivalence.

The clamp policy retains the exact comparer for which it is monotone. Heap creation adopts that
comparer and rejects a different comparer object. Generic policies remain trusted because no finite
runtime check can establish associativity and monotonicity over an arbitrary domain.

## Representation

The structural skeleton is the repository's strict Brodal-Okasaki implementation: a global
minimum root above a skew-binomial forest. A version retains:

```text
Heap = (root?, count, comparer, actionPolicy)

Tree = (rank, payload, rawPriority, rawChildren, pendingAction)
Forest = (rawHead, rawTail, pendingActionForWholeSuffix)
```

Trees and forest cells are immutable. A tree tag applies to its root and all descendants. A forest
tag applies uniformly to every tree in that suffix. These two tag locations are both load-bearing:
a tree can be moved independently by linking, while a forest-wide tag can be pushed one spine cell
at a time without traversing the remaining suffix.

The primitive logical views are:

```text
TagTree(t, newer) =
    Tree(t.rank, t.payload, t.rawPriority, t.rawChildren,
         Compose(newer, t.pending))

TagForest(f, newer) =
    Forest(f.rawHead, f.rawTail, Compose(newer, f.pending))

Expose(t) =
    Tree(t.rank,
         t.payload,
         Apply(t.pending, t.rawPriority),
         TagForest(t.rawChildren, t.pending),
         Identity)

Head(f) = TagTree(f.rawHead, f.pending)
Tail(f) = TagForest(f.rawTail, f.pending)
Cons(head, tail) = Forest(head, tail, Identity)
```

Every structural consumer uses logical `Head` and `Tail`; detaching a raw forest tail would lose
its uniform action. Rank fields are unaffected by priority actions.

## Algorithms And Temporal Semantics

`TransformAll(a)` returns the receiver for an empty heap or identity action. Otherwise it returns a
new wrapper around `TagTree(root, a)`. Composition flattens the new action with any pending root
action, so work and structure are constant.

`Minimum` applies only the global root tag. Monotonicity means a uniform action preserves every
existing heap-order relation, so the same root stays minimum.

`Insert(x, p)` compares the untagged singleton with the logical root. If the singleton wins, it
becomes a fresh identity-tagged global root and the old tagged root becomes its child. Otherwise the
old root is exposed before the new singleton is placed into its children. The exposure is crucial:
the new child must not inherit a transform that occurred before it existed.

`Meld(left, right)` compares logical roots, exposes only the winner, and skew-inserts the losing
tagged root into the winner's logical child forest. Each operand therefore retains its independent
action history. `Link` and `SkewLink` follow the same rule: compare logical roots, expose only the
winner, and attach losing trees through identity-tagged forest cells.

`DeleteMinimum` exposes the global root, finds the least logical tree root in its child forest,
then exposes that selected tree before splitting its fused children. `GetMinimum`, `SplitForest`,
`Uniquify`, `InsertRanked`, `UnionUnique`, and the reversal pass consume forests only through
logical head/tail views. The original O(log n) number of touched forest cells and links is
unchanged.

## Correctness

### Tag composition

Induct on tag pushes. `TagTree(t,a)` replaces pending `b` by `Compose(a,b)`, whose action law is
exactly `a(b(p))`. `TagForest` does the same for every member of a suffix. `Expose` applies the
pending action once to the root and moves the same action to the entire raw child forest, so every
logical priority in the tree is unchanged.

### Heap order

If `parent <= child`, monotonicity gives `a(parent) <= a(child)` when one action is applied to their
whole component. Existing tagged components therefore remain heap ordered. Whenever components
with different histories meet, insert, meld, link, and skew-link compare their logical roots and
make a logical winner the parent. Exposing that winner prevents its earlier action from leaking to
newly attached children. By induction, every edge is heap ordered and the global root is minimum.

### Structural invariants and deletion

Tags do not change ranks or forest shape. The underlying skew-binomial rank encoding is therefore
preserved by tagging. During deletion, logical forest access retains every suffix action; exposing
the selected minimum before `SplitForest` distributes its tree action to all detached children.
The remaining structural operations are the original Brodal-Okasaki operations over logically
viewed trees, so the rebuilt root has the correct minimum and rank encoding.

### Temporal behavior

An action reaches exactly the component beneath the root at the instant of `TransformAll`.
Subsequent insertions enter through identity-tagged cells. Independently transformed meld operands
remain separately tagged until an ordinary structural operation exposes a component. Thus old
entries receive their historical transforms in order, while later entries receive only later
transforms.

### Persistence and failure atomicity

All nodes, forest cells, actions, and heap wrappers are immutable. An operation allocates a result
without mutating either input, so every retained version remains valid and independently editable.
If a comparer or action-policy call throws, no successor is published; existing versions are
unchanged. External side effects performed by a user-supplied comparer or policy cannot be rolled
back.

## Complexity And Comparator Family

Let `n` be the number of entries and assume fixed-size actions with O(1) identity, compose, apply,
and comparison operations.

| Operation | Persistent `Data.Heap` / repository state-only Brodal-Okasaki heap | Flat single-action wrapper for noninvertible temporal actions | Persistent monotone-action heap |
| --- | ---: | ---: | ---: |
| Empty/create | O(1) | O(1) | O(1) |
| Insert | O(1) worst-case | not closed under the full semantics | O(1) worst-case |
| Minimum | O(1) worst-case | O(1) while flat | O(1) worst-case |
| Meld | O(1) worst-case | not closed for independently transformed operands | O(1) worst-case |
| Delete minimum | O(log n) worst-case | not closed under the full semantics | O(log n) worst-case |
| Weakly monotone map restricted to floor/cap/clamp | O(n) `mapMonotonic` / Theta(n) rebuild | O(1) until insert/meld requires history separation | O(1) worst-case |
| Enumerate | Theta(n) | Theta(n) while representable | Theta(n) |
| Fork by retaining a version | O(1) | O(1) | O(1) |
| Live structure | O(n) | O(n) while representable | O(n) |

A flat wrapper suffices for translations because a later insertion can subtract the current offset.
It does not implement the declared ADT for a noninvertible clamp: a later priority may have no
preimage, and differently clamped meld operands require different histories. Normalizing all old
entries repairs it only by spending Theta(n). Recursive fixed-size tags are precisely what close
that representation under the full operation set.

Fresh structural allocation is O(1) for insert, meld, and transform, and O(log n) for delete-min.
Starting from `n0` entries, `u` retained updates use O(n0 + u log N) worst-case total structural
space for maximum encountered size `N`; this safe upper bound is smaller for operation sequences
dominated by constant-allocation updates. A single current version occupies O(n).

## Strict Witness

Let heap A contain `{-10, 100}` and clamp it to `[0,10]`. Insert `50` afterward. The result must
drain as `0,10,50`; applying A's old root tag to the new child incorrectly produces `0,10,10`.
In another retained branch, inserting `-20` must produce `-20,0,10`, so inverse-normalizing into one
flat clamp is impossible.

Let heap B contain `{50,200}` and independently clamp it to `[60,70]`. Melding transformed A and B
must drain as `0,10,60,70` before any later insertion. Losing B's tag exposes `50,200`; leaking A's
tag into B turns its entries into tens. Scaling each operand to Theta(n) entries leaves
`TransformAll` and `Meld` constant-time in this structure while `mapMonotonic` or an explicit
state-only rebuild touches every entry.

## Prior Art And Novelty Boundary

The components and closest neighbors are established:

- Brodal and Okasaki's
  [*Optimal Purely Functional Priority Queues*](https://doi.org/10.1017/S095679680000201X)
  gives the base comparison bounds: O(1) worst-case find-min, insert, and meld, and O(log n)
  worst-case delete-min, in a purely functional representation.
- The Haskell `heaps` package's
  [`Data.Heap`](https://hackage.haskell.org/package/heaps-0.4.1/docs/Data-Heap.html) is a direct
  production comparator based on that design. Its documented bounds are all worst-case: O(1)
  insert/minimum/union, O(log n) delete-min, and O(n) `mapMonotonic`. Its map accepts any promised
  monotone function; this proposal gains O(1) only by restricting the operation to one closed,
  fixed-size, constant-composable action family.
- Tarjan's
  [*Data Structures and Network Algorithms*, heap chapter](https://doi.org/10.1137/1.9781611970265.ch3)
  describes key differences and O(1) `addtokeys`, with the other heap bounds unaffected. That
  action is an invertible translation; it is important prior art, not the noninvertible temporal
  clamp surface implemented here.
- Stanford CS166's
  [*Meldable Heaps with Addition*](https://web.stanford.edu/class/archive/cs/cs166/cs166.1266/psets/ps5/)
  explicitly asks for a lazy binomial heap with amortized O(1) new, insert, find-min, meld, and
  add-to-all, plus amortized O(log n) extract-min. It demonstrates the independently shifted meld
  problem for addition, but not persistence, worst-case Brodal-Okasaki bounds, or generic
  noninvertible monotone actions.
- Driscoll, Sarnak, Sleator, and Tarjan's
  [*Making Data Structures Persistent*](https://doi.org/10.1016/0022-0000(89)90034-2) supplies the
  standard persistence vocabulary and techniques. Pure immutability gives persistence directly in
  this prototype.

Targeted searches on 2026-07-29 combined “persistent/purely functional priority queue,” “meldable
heap,” “add-to-all/addtokeys,” “global key update,” “monotone action/map,” “floor/cap/clamp,” and
“lazy propagation,” followed citations and inspected persistent heap library APIs. They found
optimal functional heaps, O(n) weakly monotone maps, translation-specific lazy heaps, and general
lazy range-action patterns, but not a source presenting this exact aggregate:

- a worst-case-optimal fully persistent meldable heap;
- a generic normalized action policy that may be noninvertible;
- O(1) transformations with version-local temporal semantics;
- unchanged worst-case insert/minimum/meld/delete-min bounds; and
- exact priority representatives under comparer-equivalent clamp boundaries.

The defensible novelty statement is therefore:

> The searched sources did not expose this exact persistent monotone-action heap ADT. Relative to
> persistent heap APIs whose weakly monotone map is O(n), the recursive tag lifting specializes a
> closed floor/cap/clamp family to O(1) worst-case while matching the standard Brodal-Okasaki
> bounds. An exact recursively tagged Brodal-Okasaki heap is an equal-bound construction of the
> same idea.

This negative search is not proof of historical priority, broad novelty, or patentability.

## Rejected Direction In This Round

An ancestral well-nested-trace slice oracle initially appeared to offer constant-time reduction
queries through a maintained history path-minimum. It was rejected before implementation. With a
fresh immutable frame for every push, the reduced slice between ancestor endpoints is determined by
the LCA of their endpoint frames plus level-ancestor queries. Established incremental LCA and level
ancestor structures therefore match the proposed bounds, and the extra path-minimum index is
redundant. Recording this rejection narrows the research claim rather than hiding a failed lead.

## Validation

The focused suite covers:

- exact clamp composition laws, both composition orders, infinities, constants, and distinct
  comparer-equal representatives;
- insertion after a noninvertible transform, including both winning-root cases;
- melding independently transformed heaps;
- retained source versions and arbitrary retained-branch randomized histories;
- ranked delete-min paths after repeated transform/meld operations;
- payload preservation when priorities collapse;
- O(1) action-policy call/allocation witnesses for `TransformAll`;
- incompatible comparer/policy rejection and failure atomicity; and
- internal rank, logical heap-order, tag, and count validation.

Final serialized branch validation passed:

- focused `PersistentMonotoneActionHeapTests`: 11/11 in Debug and 11/11 in Release;
- complete FingerTree project: 735/735 in Debug and 735/735 in Release;
- complete C# solution: 1,541/1,541 in Debug and 1,541/1,541 in Release;
- clean full-solution builds: zero warnings and zero errors in both configurations;
- `dotnet format --verify-no-changes` over both changed C# files; and
- the repository stale-path scan, every repository-owned Markdown link, and `git diff --check`.

The randomized oracle executed 3,000 operations chosen from arbitrary retained versions, compared
every logical entry and minimum with an immutable model, and validated the internal tagged
Brodal-Okasaki shape. The allocation witness repeated `TransformAll` on heaps of 1, 1,024, and
65,536 entries and observed the same fixed allocation and policy-call counts. The independently
transformed deletion witness fully drained 514 entries in nondecreasing priority order. Benchmarks
were not run because the result is asymptotic and no elapsed-time claim is part of the experiment.

## Deliberate Limits

- Actions must be monotone. Negation, arbitrary permutation, and other order-reversing transforms
  can invalidate a heap edge and require rebuilding.
- Actions must normalize to fixed size with O(1) composition and application. Arbitrary function
  closures or expression chains do not meet the bound.
- The policy, comparer, and comparer-observable priority state must remain semantically stable.
- Priorities are transformed; payload identity is preserved. Stable ordering among equal priorities
  is not promised.
- There are no element handles, decrease-key, deletion by handle, arbitrary heap mapping, or
  serialization format in this experiment.
- `ValidateStructure` and enumeration expose pending actions and therefore take Theta(n).
- Retaining many versions retains their path-copied nodes; persistence does not make total history
  space O(n).
- Addition-only offset heaps can have smaller constants. A recursively tagged implementation can
  match every bound here, and no universal superiority claim is made.
- This branch is experimental and does not merge or modify `main`.
