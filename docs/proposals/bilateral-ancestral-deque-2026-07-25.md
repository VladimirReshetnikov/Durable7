# Bilateral Ancestral Deque: Scoped Research And C# Reference Design

- Created: 2026-07-25
- Status: Experimental C# reference and proved reduction; not a broad priority claim
- Audience: Maintainers evaluating persistent sequence designs and level-ancestor backends
- Scope: Fully persistent deque ends, reverse, indexing, slicing, and splitting over two oriented ancestry intervals
- Namespace: `Durable7.FingerTree.Experimental`

## Decision

Prototype a **Bilateral Ancestral Deque** (`BilateralAncestralDeque<T>`). A version is not a
balanced sequence tree. It is a constant-sized handle over one append-only ancestry arena:

```text
H = (leftAnchor, leftTail, rightAnchor, rightTail)

[[H]] = reverse(path(leftAnchor, leftTail))
        ++ path(rightAnchor, rightTail)
```

Here `path(a, t)` is the sequence of labels strictly after ancestor `a` through descendant `t`.
The left interval grows downward when a value is pushed at the front; reversing that interval puts
the newest leaf first. The right interval grows downward when a value is pushed at the back. A
logical reverse swaps the intervals. Most importantly, every contiguous slice intersects each
interval at most once, so the result is another handle of the same form.

The resulting reduction has a useful and unusual Pareto point. Given an incremental
level-ancestor backend with worst-case-constant leaf addition and queries, every scalar operation in
the restricted API is worst-case `O(1)`: both-end insertion/removal, both endpoint reads, reverse,
index, arbitrary slice, and split. Enumeration remains `Theta(n)`. On their shared operations this
is no worse in time than persistent real-time deques, and it improves random access and slicing
from logarithmic or linear time to constant time. It obtains the result by deliberately omitting
unrelated concatenation, point replacement, and middle editing.

The complexity claim has two layers which must not be conflated:

1. The deque performs at most one arena leaf addition and at most two level-ancestor queries per
   scalar operation.
2. Alstrup and Holm proved an incremental tree supporting both `AddLeaf` and `LevelAncestor` in
   `O(1)` worst-case time and linear space. Instantiating the reduction with that result yields the
   all-constant theorem.

The C# reference does **not** implement Alstrup--Holm. It exposes the backend contract and ships a
smaller Myers two-link arena with `O(1)`-amortized leaf addition and `O(log M)` worst-case ancestor
queries after `M` historical pushes. The reference validates semantics, persistence, query counts,
and the extension seam; it does not turn the Myers backend into evidence for the stronger theorem.

This is best described as an apparently new **ADT synthesis and slice-closure observation**, not a
new deque, random-access stack, or level-ancestor algorithm. The exact construction was not found
in the targeted literature search, but absence from a search is not proof of first publication.

## Candidate Trail

The investigation did not stop at the first plausible idea.

The suggested nested type

```haskell
data XList a = Nil | Cons a (XList (XList a))
```

is the nested `Bush` family studied by Bird and Meertens. Raw bushes can have structural size and
height unrelated to a useful flat element count. Normalizing the nesting recovers familiar
random-access lists, flexible arrays, or bootstrapped queues rather than a new closed algebra with a
strictly better common-operation bound. See Bird and Meertens,
[“Nested Datatypes”](https://www.cs.ox.ac.uk/richard.bird/online/BirdMeertens98Nested.pdf).

A second candidate stored per-branch bit ranks on append paths. Exact-semantic searches reduced it
to labeled/colored level-ancestor queries, an established problem rather than a new structure. A
windowed variant remained a direct composition of the same primitive. Myers's applicative
random-access stack and the incremental-level-ancestor literature made the collision especially
clear.

The bilateral candidate survived because the interesting object is not one ancestor query. It is
the closed representation algebra:

- two independently branchable ancestry intervals;
- opposite logical orientations;
- reverse by swapping arms;
- endpoint deletion by moving a tail or an anchor;
- arbitrary slice as at most one interval intersection per arm; and
- cached arm bases making both logical endpoint reads query-free.

A fallback boundary-transducer rope and a bounded-gap persistent monotone frontier were also
considered. The former is useful but is a standard finite-state monoid augmentation; the latter has
a narrower numeric precondition. The bilateral deque has the broadest useful restricted API and
the cleanest comparison against structures already in this repository.

## Workload And Non-Goals

The target workload keeps many related deque versions, branches from old versions, reverses views,
and repeatedly indexes or carves subranges. Examples include bidirectional search traces, parser
frontiers, speculative histories, work-stealing simulations whose histories are inspected after
the fact, and versioned windows over event streams.

The supported algebra is:

- add or remove at either end;
- read either end or an indexed value;
- reverse;
- take, drop, arbitrary contiguous slice, and split; and
- front-to-back enumeration.

The intentional non-goals are:

- concatenating unrelated versions;
- point replacement, insertion, or removal in the middle;
- a cursor that performs arbitrary local edits;
- confluently merging two histories;
- canonicalizing equal contents reached by different histories;
- a self-contained purely functional value with no shared manager; and
- reclaiming individual historical nodes while arbitrary handles might still exist.

No comparison treats an omitted operation as faster. `ReversibleDeque<T>` remains the general
choice when concatenation or middle editing is needed.

## Public Surface

The experimental C# type is an immutable `IReadOnlyList<T>` with this restricted surface:

| Member | Meaning |
| --- | --- |
| `Create(arena)` | Create an empty handle owned by a supplied incremental-level-ancestor arena. |
| `CreateMyers()` | Create an empty handle with the shipped reference backend. |
| `CreateRange(values)` | Enumerate once into a fresh Myers-backed right arm. |
| `Count`, `IsEmpty` | Read the cached logical length. |
| `First`, `Last`, indexer | Read a logical endpoint or indexed element. |
| `AddFirst`, `AddLast` | Publish one leaf below the selected arm's tail. |
| `RemoveFirst`, `RemoveLast` | Move one tail or advance the opposite arm's anchor. |
| `TryRemoveFirst`, `TryRemoveLast` | Return a removed value and remainder without an empty exception. |
| `Take`, `Drop`, `Slice` | Select a persistent contiguous view. |
| `SplitAt` | Return the prefix and suffix at a validated boundary. |
| `Reverse` | Swap the two oriented arm descriptors. |
| `Clear` | Return an empty handle in the same arena. |
| `ToArray`, enumeration | Materialize or traverse logical order in `Theta(n)` time. |

There is no process-global `Empty`: an empty value belongs to an arena, and retaining it retains
that manager. Producing operations leave their source and every earlier version usable.

## Representation

### Arena

Give an arena a virtual unlabeled node `bottom` at depth `-1`. A successful
`AddLeaf(parent, value)` publishes a fresh node `u` with immutable fields:

```text
u.Parent = parent
u.Depth  = parent.Depth + 1
u.Value  = value
```

The parent can be any historical node. Nodes and handles are never recycled. `LA(v, d)` returns the
unique ancestor of `v` at absolute depth `d`, including `LA(v, -1) = bottom`.

### Oriented segment

An arm is a constant-sized record:

```text
Segment = (Anchor, Base, Tail, Count)
```

Its invariant is:

```text
Anchor is an ancestor of Tail
Count = depth(Tail) - depth(Anchor)

Count = 0  => Anchor = Base = Tail
Count > 0  => Base.Parent = Anchor
              Base lies on the path from Anchor to Tail
```

`Base` is redundant but important. It caches the first physical node after the anchor. For the
left arm the logical first is `Tail` and logical last is `Base`; for the right arm those roles are
`Base` and `Tail`. Consequently both public endpoint reads require only a cached-handle value read,
even when the other arm is empty.

### Deque handle

For a handle `H = (L, R)`, define:

```text
[[H]] = reverse(P(L.Anchor, L.Tail)) ++ P(R.Anchor, R.Tail)
Count = L.Count + R.Count
```

where `P(a, t)` contains the labels at depths `depth(a)+1` through `depth(t)`. The
implementation caches `Count` and validates it against both arm counts in its internal audit.
Empty arms may retain different historical anchors while denoting the same empty sequence; exact
anchor provenance is not part of the public value semantics.

## Operation Equations

Let `l = L.Count`, `r = R.Count`, and let `child(a, t)` mean
`LA(t, depth(a) + 1)` for nonempty `(a, t]`.

| Operation | Handle equation | LA calls |
| --- | --- | ---: |
| `First` | `L.Tail.Value` if `l>0`, else `R.Base.Value` | 0 |
| `Last` | `R.Tail.Value` if `r>0`, else `L.Base.Value` | 0 |
| `AddFirst(x)` | append below `L.Tail`, increment `l`, and set `L.Base` too when `l=0` | 0 |
| `AddLast(x)` | append below `R.Tail`, increment `r`, and set `R.Base` too when `r=0` | 0 |
| `RemoveFirst`, `l>0` | move `L.Tail` to its parent and decrement `l`; make the arm empty when `l=1` | 0 |
| `RemoveFirst`, `l=0` | advance `R.Anchor` to `R.Base`, decrement `r`, and cache the next base | at most 1 |
| `RemoveLast`, `r>0` | move `R.Tail` to its parent and decrement `r`; make the arm empty when `r=1` | 0 |
| `RemoveLast`, `r=0` | advance `L.Anchor` to `L.Base`, decrement `l`, and cache the next base | at most 1 |
| `H[i]`, `i<l` | `LA(L.Tail, depth(L.Tail)-i).Value` | at most 1 |
| `H[i]`, `i>=l` | `LA(R.Tail, depth(R.Anchor)+1+i-l).Value` | at most 1 |
| `Reverse` | `(R,L)` | 0 |

Making an arm empty sets its `Anchor`, `Base`, and `Tail` to one node; when the whole deque becomes
empty, the C# reference uses the arena bottom. Every row updates the affected arm count and the
cached total. Cached `Tail`/`Base` special cases make endpoint indices query-free, but the bound
does not depend on those shortcuts.

Adding below a left tail changes its physical path from `P` to `P ++ [x]`; reversing gives
`[x] ++ reverse(P)`, exactly a logical prepend. Adding below the right tail is ordinary append.
Moving a tail to its parent removes the newest physical label, which is the left logical first or
right logical last. Advancing an anchor past its base removes the oldest physical label, which is
the opposite logical endpoint. Swapping arms gives:

```text
reverse(reverse(L) ++ R) = reverse(R) ++ L
```

so `Reverse` preserves the representation without lazy flags or rebuilding.

## Slice Closure

Let a requested nonempty half-open slice be `[s,e)`, let `dL = depth(L.Tail)`, and let
`dRA = depth(R.Anchor)`.

### Wholly in the reversed left arm (`e <= l`)

The selected physical endpoints are:

```text
newTail = LA(L.Tail, dL - s)
newBase = LA(L.Tail, dL - e + 1)
newAnchor = parent(newBase)
```

The output left arm `(newAnchor,newTail]`, read in reverse, is exactly the requested order. The
right arm is empty. There are at most two LA calls; cached endpoint equality removes some calls.

### Wholly in the forward right arm (`s >= l`)

Set `p = s-l` and `q = e-l`:

```text
newBase = LA(R.Tail, dRA + p + 1)
newTail = LA(R.Tail, dRA + q)
newAnchor = parent(newBase)
```

This is one forward interval and again uses at most two LA calls.

### Crossing the center (`s < l < e`)

The left intersection is a suffix of the reversed left arm, so it retains `L.Anchor` and `L.Base`
and moves only its tail to `LA(L.Tail, dL-s)`. The right intersection is a prefix of the right arm,
so it retains `R.Anchor` and `R.Base` and moves only its tail to
`LA(R.Tail, dRA+e-l)`. The two calls produce another bilateral handle.

Thus every nonempty slice uses at most two level-ancestor queries. An empty slice uses none.
`Take` and `Drop` are slices. `SplitAt(k)` makes the complementary prefix and suffix: if `k` lies
inside an arm, each result moves one opposite endpoint at the same boundary; if it is the center or
an outer boundary, fewer calls are needed. The implementation therefore uses at most two total LA
calls even though it expresses split as `Take(k)` plus `Drop(k)`.

The closure is the central contribution. A usual two-stack deque occasionally transfers an entire
stack; here the two intervals never rebalance and a slice never creates a third fragment.

## Persistence Proof

**Lemma 1 — representation preservation.** `AddFirst` and `AddLast` extend exactly one ancestry
path and update its tail/base/count. Same-arm removal shortens a tail by one. Cross-arm removal
advances the anchor to the cached base and selects the next base on the unchanged tail path. The
slice formulas select ancestor endpoints in order, and reverse only swaps valid segments. Each
operation therefore preserves the arm and total-count invariants.

**Lemma 2 — semantic preservation.** The operation equations above add, remove, select, or reverse
exactly the corresponding labels in `reverse(P(L)) ++ P(R)`. The three slice cases are disjoint,
cover every valid nonempty slice, and preserve logical order.

**Lemma 3 — retained versions are stable.** Arena node records, stored value slots, parents,
depths, and ancestry metadata are immutable after publication; caller-owned payload objects may
still be mutable. A successful push adds a child but never changes its parent. Every producing
operation returns an immutable constant-sized handle, possibly the source for an identity case.
Later branches below any old tail cannot change an earlier root-to-tail path.

**Theorem — full persistence for the restricted algebra.** By Lemmas 1 and 2 every operation
returns a valid value with the advertised sequence denotation. By Lemma 3 every old value remains
valid and can itself receive either end insertion. The structure is therefore fully persistent for
the declared API. It is not confluently persistent because unrelated histories cannot be merged.

## Parameterized Complexity

Let:

- `M` be the number of labeled nodes ever published by one arena;
- `n` be the current deque length;
- `H` be the number of retained deque handles;
- `U(M)` be one arena `AddLeaf` time;
- `Q(M)` be one arena level-ancestor query time; and
- `S(M)` be arena structure space, excluding payloads and handles.

Parent, depth, and value reads are required to be `O(1)`. Bounds are sequential RAM-model bounds;
they exclude allocator latency, lock waiting, and work performed by mutable caller payloads.

| Operation | Time | New arena nodes |
| --- | ---: | ---: |
| `Create`, `Count`, `IsEmpty`, `First`, `Last`, `Reverse`, `Clear` | `O(1)` | 0 |
| `AddFirst`, `AddLast` | `U(M)` | 1 |
| `RemoveFirst`, `RemoveLast`, `TryRemove*` | `O(1 + Q(M))` | 0 |
| indexer | `O(1 + Q(M))` | 0 |
| `Take`, `Drop`, `Slice`, `SplitAt` | `O(1 + Q(M))` | 0 |
| build `k` values by end insertion | `O(k U(M+k))` | `k` |
| enumerate | `Theta(n)` with parent buffering; alternatively `Theta(n Q(M))` with constant iterator state | 0 |

Every live version is an `O(1)`-word handle. Total retained structural space is
`S(M) + O(H)`, not `O(n)` for one current deque. With the optimal incremental backend,
`U(M)=Q(M)=O(1)` worst case and `S(M)=O(M)`, so every scalar operation is `O(1)` worst case and
enumeration is output-optimal `Theta(n)`.

The primary backend theorem is Stephen Alstrup and Jacob Holm,
[“Improved Algorithms for Finding Level Ancestors in Dynamic Trees”](https://doi.org/10.1007/3-540-45022-X_8),
ICALP 2000. Its constant bound applies specifically to a tree growing by additions of leaves, which
matches this arena. Its more general dynamic-forest result has different bounds and is not used.

## Shipped Myers Reference

`MyersLevelAncestorArena<T>` adapts Eugene Myers's applicative random-access-stack links to every
branch of the append tree. Each node stores one parent and one coalesced jump with its distance.
Leaf addition performs constant link work; an ancestor lookup follows `O(log M)` links in the worst
case. See Myers,
[“An Applicative Random-Access Stack”](https://publications.mpi-cbg.de/Myers_1983_6328.pdf).

Records are stored in blocks of sizes `1,3,5,...`. The first index in block `b` is `b^2`, so lookup
uses `floor(sqrt(index))` and array addressing. After `M` labeled nodes plus `bottom`, allocated
capacity is `M+1+O(sqrt(M+1))`; the directory has `O(sqrt(M+1))` entries. Block allocation and
directory growth make insertion amortized, not worst-case, constant time in managed C#.

The honest shipped bounds are:

| C# operation | Myers-backed bound |
| --- | ---: |
| end insertion | `O(1)` amortized |
| count, endpoint read, reverse, clear | `O(1)` worst case |
| end removal | `O(log M)` worst case; zero queries on its own nonempty arm |
| index | `O(log M)` worst case |
| slice or split | `O(log M)` worst case, at most two queries |
| enumerate `n` items | `Theta(n)` time, up to `Theta(n)` temporary value slots |
| arena storage after `M` pushes | `Theta(M)` records and values |

The arena serializes all operations with one private lock. Nodes and deque handles are immutable,
so retained values are semantically safe under concurrent access through this backend, but reads
are not lock-free. The manager retains every historical node and payload until the manager itself
is collected. A single handle retains its manager and therefore the whole arena.

Integer node handles are arena-relative. Range validation cannot detect an in-range integer copied
from another arena; direct backend clients must keep handles with their owner. Private deque
segments cannot be forged through the public collection API.

`GetStatistics()` exposes publication, block/slot, query, and hop counts for deterministic tests.
Those counters validate routing and representation seams; they are not empirical proof of an
asymptotic theorem.

## Comparison On Shared Operations

The theoretical row uses the Alstrup--Holm backend. The shipped Myers row is separated precisely
because it does not realize the headline improvement.

| Structure | Both-end updates | Reverse | Index | Slice / split | Enumerate | Capability absent from bilateral deque |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Bilateral + optimal incremental LA | `O(1)` worst case | `O(1)` | `O(1)` worst case | `O(1)` worst case | `Theta(n)` | unrelated concat, point/middle edits |
| C# bilateral + Myers | add amortized `O(1)`; remove `O(log M)` worst | `O(1)` | `O(log M)` | `O(log M)` | `Theta(n)` | same |
| repository `ReversibleDeque<T>` | `O(1)` amortized, `O(log n)` worst | `O(1)` | `O(log n)` | derived from `SplitAt` in `O(log n)` | `Theta(n)` | concat and point/middle edits |
| persistent real-time deque | `O(1)` worst case | representation-dependent | generally linear | generally linear | `Theta(n)` | varies |
| Myers random-access stack | one-end `O(1)` | linear | `O(log n)` | suffix-oriented | `Theta(n)` | a second deque end |
| skew-binary random-access list | front `O(1)` | linear | `O(log n)` | logarithmic decomposition | `Theta(n)` | back deque operations |
| RRB vector | logarithmic-height end edits | linear or view-dependent | `O(log n)` | `O(log n)` | `Theta(n)` | concat and point/middle edits |

This matrix compares operation time. The shipped C# enumerator chooses zero ancestor queries and
`Theta(n)` temporary value slots so Myers-backed enumeration remains `Theta(n)` time;
`ReversibleDeque<T>` uses only a logarithmic traversal stack. With the optimal constant-query
backend, the reduction could instead use a query-per-output enumerator with `Theta(n)` time and
O(1) iterator state; the current C# enumerator always buffers. The headline no-worse statement is
about the optimal instantiation and shared-operation time, not every resource bound of the Myers
reference.

Hinze and Paterson's finger trees establish the relevant general-sequence baseline:
[DOI 10.1017/S0956796805005769](https://doi.org/10.1017/S0956796805005769). Okasaki's
[thesis](https://www.cs.cmu.edu/~rwh/students/okasaki.pdf) covers random-access lists and functional
queues/deques. The repository's strict `ReversibleDeque<T>` documents its own linear-use
amortization boundary; branching can replay work from a retained pre-reorganization version. The
[R Journal discussion of persistent deques](https://journal.r-project.org/articles/RJ-2015-009/)
gives a concrete account of why naive two-stack amortization is not preserved automatically by
full persistence.

Fully persistent and catenable lists/deques already achieve strong end/concatenation bounds; they
do not establish this exact constant-time index-and-slice surface. See Driscoll, Sleator, and
Tarjan's [“Fully Persistent Lists with Catenation”](https://www.cs.cmu.edu/~sleator/papers/fully-persistent-lists.htm)
and the recent verified treatment
[“Purely Functional Catenable Deques”](https://arxiv.org/abs/2505.07681).

The improvement is therefore scoped but strict: on the intersection of APIs, optimal bilateral
handles retain the best endpoint/reverse/enumeration time and improve the cited alternatives'
`O(log n)` index and slice/split bounds to `O(1)`. The
price is a manager retaining all historical pushes and the omitted richer edits.

## Novelty Audit And Claim Boundary

Targeted searches included exact and synonymous combinations of “persistent deque,” “two stacks,”
“level ancestor,” “random access,” “slice,” “ancestry interval,” “fully persistent,” and
“functional deque slicing.” The results included:

- persistent real-time and catenable deques;
- Myers and Okasaki random-access stacks/lists;
- finger trees, RRB vectors, and random-access zippers;
- queue-as-ancestor-path tutorials and contest constructions; and
- static, incremental, colored, and weighted ancestor structures.

No examined source exposed a deque as **two oppositely oriented, independently branchable ancestor
intervals closed under arbitrary slice and reverse**, with each scalar operation reduced to at
most two incremental-tree calls. That is positive evidence for a useful research candidate, not a
proof that no equivalent construction exists under another name.

A skeptical reduction is also accurate: this is two bottom-truncated persistent random-access
stacks arranged in the classic two-stack deque order, with a global incremental-ancestor index
eliminating transfers. The asymptotic theorem is inherited from that index. Accordingly, acceptable
wording is:

> An experimental persistent-deque synthesis whose two oriented ancestral intervals are closed
> under reverse and arbitrary slicing, yielding constant-call reductions for deque ends, indexing,
> slicing, and splitting.

Unacceptable wording includes “the first persistent deque,” “a new level-ancestor algorithm,”
“constant-time general sequence,” or an unqualified priority claim. A publication-quality novelty
claim would require a broader systematic review and expert peer review.

## Validation Obligations

The reference is not accepted merely because example sequences print correctly. Its executable
gate covers:

- empty, singleton, nullable, and exception contracts;
- all four end transitions, especially removal across an empty arm;
- retained versions and branching from every kind of result;
- reverse involution and interaction with pushes, pops, slices, and splits;
- every slice and split boundary of mixed-arm values;
- exhaustive small construction/orientation histories;
- long randomized retained-version histories against an array model;
- exact maximum ancestor-query counts per operation;
- direct Myers arenas on irregular trees against a naive parent oracle;
- square block-allocation seams and logarithmic hop envelopes;
- enumeration without ancestor queries;
- concurrent branches and reads through the locked reference arena; and
- Debug/Release focused, project, and full-solution builds/tests with zero warnings.

The implementation is
[`BilateralAncestralDeque.cs`](../../src/CSharp/src/Durable7.FingerTree/BilateralAncestralDeque.cs).
The focused tests are
[`BilateralAncestralDequeTests.cs`](../../src/CSharp/tests/Durable7.FingerTree.Tests/BilateralAncestralDequeTests.cs).
Recorded validation totals belong in the FingerTree validation guide only after the commands have
actually completed.

The 2026-07-25 gate completed: 15/15 focused tests, 739/739 complete FingerTree tests, and
1,545/1,545 full C# solution tests passed in both Debug and Release with zero build warnings or
errors. The full totals are 319 Numerics + 354 HAMT + 739 FingerTree + 81 Ordered + 52 Tungsten.
No benchmark result is part of this evidence.

## Exit Criteria

Keep the experiment only if all of these remain true after implementation and review:

1. the two-arm invariant is closed under every public producing operation;
2. no scalar operation makes more than two level-ancestor queries;
3. exhaustive and randomized retained-version models pass;
4. theoretical and shipped bounds remain visibly separate;
5. missing concat/middle-edit capabilities are explicit in every comparison;
6. manager retention and lock serialization are documented; and
7. novelty is stated as a scoped synthesis rather than a priority fact.

If any condition fails, reject or narrow the candidate rather than weakening the evidence language.
