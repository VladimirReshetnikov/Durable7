# Ancestral Slice Queue: Scoped Research And C# Reference Design

- Created: 2026-07-25
- Status: Experimental, C# reference implementation; not a cross-language shipment commitment
- Audience: Maintainers evaluating persistent sequence designs and future backend work
- Scope: Restricted append/drop/slice persistence over an incremental ancestor index
- Namespace: `Durable7.FingerTree.Experimental`

## Decision

Prototype an **Ancestral Slice Queue** (`AncestralSliceQueue<T>`) as a manager-backed persistent
sequence with a deliberately restricted edit algebra. A value denotes one contiguous interval of a
root-to-node path in an append-only tree. Appending creates one child below that interval's tail;
removing or slicing changes only the two interval boundaries. Old values remain valid and can be
used as independent branch points.

The proposal makes two different complexity statements and keeps them separate:

1. The data-structure reduction is parameterized by an incremental level-ancestor backend. If
   `AddLeaf` costs `U(M)` and `AncestorAtDepth` costs `Q(M)`, the queue inherits those costs as set
   out below.
2. Alstrup and Holm proved that both operations can take `O(1)` worst-case time with linear space
   when the only tree update is adding a leaf. With that backend, every scalar queue operation in
   this proposal, including indexed access and slice/split construction, is `O(1)` worst case.

The C# reference implementation does **not** implement the Alstrup--Holm structure. It ships a much
simpler Myers-style parent/jump-link backend. That backend has `O(1)` amortized leaf insertion and
`O(log M)` ancestor queries, where `M` is the number of element nodes ever added to its arena.
Consequently, the reference implementation's indexer and boundary-seeking operations are
`O(log M)`, not `O(1)`. The implementation demonstrates the semantics and backend boundary; it is
not empirical proof of the optimal instantiation.

The scoped contribution is therefore the combination and API boundary: a known persistent-queue
path representation, generalized to persistent subrange values and factored over an incremental
ancestor service. The path representation itself is prior art, and this document does not claim to
have invented persistent queues, ancestor-interval queues, Myers jump links, level-ancestor data
structures, or rootish allocation.

## Useful Workload And Deliberate Non-Goals

The target workload creates many related FIFO snapshots, selects ranges from them, indexes those
ranges, and continues appending from any retained result. Examples include branching event traces,
parser or search frontiers with cheap checkpoints, speculative logs, and provenance-preserving
windows into append histories.

The supported semantic algebra is:

- append at the right;
- inspect or remove either visible endpoint;
- index a visible element;
- take a prefix, drop a prefix, select a contiguous slice, or split at one boundary; and
- enumerate in visible front-to-back order.

The following are non-goals because they break the one-path-interval invariant or belong to a
strictly richer problem:

- prepend, arbitrary insertion, arbitrary deletion, or point replacement;
- concatenation of unrelated histories or confluent persistence;
- canonicalization of equal contents reached through different histories;
- a self-contained purely functional value with no shared manager;
- reclaiming individual historical nodes while arbitrary queue handles may remain alive; and
- replacing the repository's general deque, vector, rope, or finger-tree sequence APIs.

No dominance statement in this document includes an operation that the Ancestral Slice Queue does
not support. In particular, omitting general concatenation and point update is the reason its
stronger indexed and slicing bounds are possible.

## Public Semantic Surface

The C# prototype is an immutable `IReadOnlyList<T>`-shaped value. Its intended surface is:

| Member | Meaning |
| --- | --- |
| `Create(arena)` | Create an empty value owned by the supplied incremental-ancestor arena. |
| `CreateMyers()` | Create an empty value using the shipped Myers jump-link arena. |
| `CreateRange(values)` | Create a fresh Myers-backed queue by appending the values in enumeration order. |
| `Count`, `IsEmpty` | Inspect the visible interval length. |
| `First`, `Last`, indexer | Inspect an endpoint or the element at a zero-based visible index. |
| `AddLast(value)` | Add one child below this value's current anchor/tail and return the extended value. |
| `RemoveFirst()`, `RemoveLast()` | Return the interval with one visible endpoint excluded. |
| `TryRemoveFirst(out value, out result)`, `TryRemoveLast(out value, out result)` | Return the removed item and remainder without an exception-based empty case. |
| `Take(count)`, `Drop(count)` | Select a prefix or remove a prefix. |
| `Slice(start, count)` | Select one contiguous visible subrange. |
| `SplitAt(index)` | Return a result whose `Left` prefix and `Right` suffix meet at a validated boundary. |
| enumeration | Yield the visible values in FIFO order. |

All producing operations return a new handle and leave the source usable. A handle may be retained,
branched, sliced, emptied, and appended again. Values produced by different arenas are unrelated;
there is no cross-arena concatenation or node import operation.

Normal repository rules should apply to validation: negative positions/counts, overflowing ranges,
or an index equal to `Count` fail; endpoint removal and endpoint access fail on an empty value;
`TryRemove*` reports the empty case without publishing a partial result. The arena must reject depth
or node-count overflow before publishing a child.

## Representation

### Append tree

For the proof, give each arena a virtual unlabeled root `bottom` at depth `-1`. Every successful
`AddLeaf(parent, value)` returns a fresh immutable node `u` with:

```text
u.Parent = parent
u.Depth  = parent.Depth + 1
u.Value  = value
```

The C# backend uses stable integer handles and reserves handle `0` for `bottom`; its private bottom
record contains no observable `T` value. Another backend may use a different internal carrier.
Adding a leaf to any old node is permitted. Therefore all appends made through one manager form a
rooted tree rather than one linear log.

### Queue handle

A queue value is the constant-sized pair:

```text
(tail, lowDepth)
```

subject to:

```text
0 <= lowDepth <= tail.Depth + 1
```

where the virtual root permits `(bottom, 0)`. If `lowDepth <= tail.Depth`, the visible sequence is
the labels of the unique ancestors of `tail` at depths

```text
lowDepth, lowDepth + 1, ..., tail.Depth.
```

If `lowDepth == tail.Depth + 1`, the value is empty. Its `tail` is still an **anchor**. Appending to
that empty value creates a child of the anchor but exposes only the new child, not the discarded
prefix. This anchored-empty rule makes all-zero slices branchable and avoids a process-global empty
arena.

The count is therefore:

```text
Count = tail.Depth - lowDepth + 1
```

including zero for an anchored empty value. The C# object caches this derived count so `Count` and
`IsEmpty` do not call the arena; its invariant includes equality with the formula above. Handles
retain their arena as well as the logical boundaries, and the public constructors prevent callers
from combining an arena with an arbitrary tail handle.

## Formal Invariant And Preservation Proof

Let `A(v, d)` be the unique ancestor of node `v` at absolute depth `d`, allowing
`A(v, -1) = bottom`. Define the denotation of a valid handle `H = (t, l)` as:

```text
[[H]] = [ A(t, d).Value | d = l, l + 1, ..., t.Depth ]
```

when `l <= t.Depth`, and the empty sequence otherwise. The representation invariant is:

1. `t` is `bottom` or a node published by the handle's arena;
2. `0 <= l <= t.Depth + 1`;
3. the parent chain is immutable and strictly decreases depth by one; and
4. the backend returns the unique `A(t, d)` for every requested `-1 <= d <= t.Depth`; and
5. the C# count cache equals `t.Depth - l + 1`.

The operations below are equations on handles. Boundary cases may be special-cased by an
implementation to avoid a backend call, but that does not change their denotation.

| Operation on `H = (t, l)`, `n = Count` | Result handle or value |
| --- | --- |
| `AddLast(x)` | `(AddLeaf(t, x), l)` |
| `First` | `A(t, l).Value` for `n > 0` |
| `Last` | `t.Value` for `n > 0` |
| `H[i]` | `A(t, l + i).Value`, `0 <= i < n` |
| `RemoveFirst()` | `(t, l + 1)`, `n > 0` |
| `RemoveLast()` | `(t.Parent, l)`, `n > 0` |
| `Drop(k)` | `(t, l + k)`, `0 <= k <= n` |
| `Take(k)` | `(A(t, l + k - 1), l)`, `0 <= k <= n` |
| `Slice(s, k)` | `(A(t, l + s + k - 1), l + s)`, `0 <= s <= n`, `0 <= k <= n-s` |
| `SplitAt(k)` | `((A(t, l + k - 1), l), (t, l + k))`, `0 <= k <= n` |

The `k = 0` prefix/slice equations intentionally ask for the ancestor at depth `l - 1`; that node
is the empty result's anchor. When `l = 0`, it is the virtual root. `Drop(n)` and the suffix of
`SplitAt(n)` instead remain anchored at the old tail. Empty values can consequently have distinct
provenance while having the same empty sequence denotation.

### Lemma 1: each result is representable

For `AddLast`, the new tail has depth `t.Depth + 1`, while `l` is unchanged, so
`l <= newTail.Depth + 1`. For `RemoveFirst` and `Drop`, the preconditions give
`l + k <= t.Depth + 1`. For `RemoveLast`, non-emptiness gives `l <= t.Depth`, hence
`l <= t.Parent.Depth + 1`. Each ancestor-producing operation asks for a depth between `-1` and
`t.Depth`; its result has exactly that depth, and its new low boundary is at most one greater.
The C# constructors cache `Count+1`, `Count-1`, `Count-k`, `k`, or the two split lengths as dictated
by the same endpoint arithmetic, so the redundant count equality is preserved as well. Thus every
equation preserves the numeric, ownership, and cache invariant.

### Lemma 2: each operation has the stated sequence semantics

`AddLast` preserves every ancestor at depth at most `t.Depth` and labels only its new deepest child,
so its denotation is `[[H]] ++ [x]`. Raising the low boundary by one removes exactly the first
visible depth. Replacing the tail by its parent removes exactly the last visible depth. `Drop`,
`Take`, and `Slice` choose the corresponding inclusive depth interval. The two `SplitAt` intervals
are adjacent, disjoint, and cover `l .. t.Depth`; therefore concatenating their denotations yields
`[[H]]`.

### Lemma 3: old versions cannot change

Published nodes, their parent links, depths, values, and ancestor metadata are immutable. `AddLeaf`
publishes a fresh child and does not alter the selected parent. Every queue operation allocates or
returns a new constant-sized handle. Later children may branch below any retained node but cannot
change the unique root-to-node path of an existing tail. Therefore the denotation of every retained
handle is stable.

### Theorem: full persistence for the restricted algebra

By Lemmas 1 and 2, each operation returns a valid handle with the advertised value. By Lemma 3, the
source and every earlier handle remain valid after every update. Because `AddLast` accepts a handle
from any old branch, updates may be applied to all historical versions. The structure is thus fully
persistent for the declared operation set. It is not confluently persistent because it cannot merge
two unrelated versions.

## Backend Contract And Parameterized Bounds

Let:

- `M` be the number of labeled element nodes ever published by one arena;
- `n` be the current handle's visible count;
- `U(M)` be the time for `AddLeaf`;
- `Q(M)` be the time for one `AncestorAtDepth` query; and
- `S(M)` be total arena space, excluding values and retained queue handles.

All operation bounds are sequential RAM-model bounds. They exclude time waiting for the shipped
arena's lock and work performed by caller-owned payload objects.

The backend must support adding a leaf below **any** previously published node, not merely the most
recent leaf. Parent, depth, and value reads are required to be `O(1)`. It must also guarantee that
concurrent or reentrant readers never observe a partially initialized node if its implementation
supports concurrent use.

The shipped backend's integer handles are arena-relative capabilities by contract, not globally
unique identifiers. It range-checks a handle but cannot distinguish an in-range integer copied from
another arena. Direct backend callers must keep each handle with its owner. This cannot corrupt
`AncestralSliceQueue<T>` handles because their tails and constructors are private; it is a safety
tradeoff in the low-level public extension seam.

Each handle equation contains at most one `AddLeaf` or one `AncestorAtDepth` call; `SplitAt` shares
the one ancestor-seeking prefix with a suffix that only moves `lowDepth`. All other work is a fixed
number of integer, parent, value, and handle operations. This directly yields the parameterized
table rather than assuming a complexity from the queue's name.

| Queue operation | Backend-parameterized time | New arena space |
| --- | ---: | ---: |
| `Create` | `O(1)` | `O(1)` manager state |
| `AddLast` | `U(M)` | one backend node |
| `Count`, `IsEmpty`, `Last` | `O(1)` | `O(1)` |
| `First`, indexer | `Q(M)` | `O(1)` |
| `RemoveFirst`, `Drop` | `O(1)` | `O(1)` |
| `RemoveLast` | `O(1)` | `O(1)` |
| `TryRemoveFirst` | `Q(M)` because it also returns `First` | `O(1)` |
| `TryRemoveLast` | `O(1)` | `O(1)` |
| `Take`, `Slice`, `SplitAt` | `Q(M)` | `O(1)` |
| build `k` values by repeated `AddLast` | `O(k * U(M+k))` | `k` backend nodes |

`First` is not an `O(1)` operation under a logarithmic ancestor backend merely because it is an
endpoint: the front is an ancestor selected jointly by `tail` and `lowDepth`. Similarly,
`RemoveFirst` itself changes one integer in `O(1)`, while a `TryRemoveFirst` overload that also
returns the removed value must perform the front query. Caching the current front would move that
query to the next `RemoveFirst`; it cannot make both operations constant without stronger backend
support.

The reference enumerator walks parent links from the tail into a `T[n]` array and then yields that
array in forward order, giving `Theta(n)` time rather than performing `n` ancestor queries. This
requires `Theta(n)` transient element slots. A backend with `Q(M) = O(1)` can instead perform one
query per output in `Theta(n)` time and `O(1)` iterator state. Either time bound is output-optimal;
the transient-space distinction remains part of the backend contract.

Every live queue value is a constant-sized handle. Total process memory is not `O(n)` for a current
slice: it is `S(M) + O(H)`, where `H` is the number of queue handles retained by callers, plus the
payloads reachable from the `M` historical nodes.

## The Shipped Myers Jump-Link Backend

`MyersIncrementalAncestorArena<T>` is the practical C# baseline. It exposes stable integer node
handles through `IIncrementalAncestorArena<T>` and keeps each immutable node record private. A
record contains its parent handle, depth, value, and one additional jump handle/distance. The jump
coalesces suitable adjacent jumps; ancestor search repeatedly takes a jump that does not cross
above the target depth and otherwise follows the parent. This is the applicative random-access
stack technique introduced by Eugene Myers, adapted from one list to the root-to-node list of every
branch in the append tree.

The important shipped claims are asymptotic rather than a particular pointer-traversal constant:

- constructing the parent and jump links for a child takes `O(1)` pointer work;
- an ancestor query takes `O(log d)` worst-case pointer traversals for node depth `d`, hence
  `O(log M)`;
- nodes use `O(1)` links and fields each; and
- total backend space is `O(M)`.

The arena stores private node records in fixed odd-sized blocks of capacities
`1, 3, 5, 7, ...`. Before block `r` there are exactly

```text
1 + 3 + ... + (2r - 1) = r^2
```

slots, so a zero-based arena index `i` belongs to block `floor(sqrt(i))` at offset
`i - floor(sqrt(i))^2`. If `M` labeled nodes have been added, the store contains `M+1` records
including `bottom`; allocated slot capacity is `M + 1 + O(sqrt(M+1))`. The directory itself has
`O(sqrt(M+1))` entries.

Building a queue from `k` input values by repeated `AddLast` takes `O(k * U(M+k))` time and
publishes `k` backend nodes. The public `CreateRange` factory performs exactly that fold over a
fresh Myers arena, so its shipped bound is `Theta(k)` amortized total time.

This layout does not make allocation worst-case constant in the managed runtime. Allocating and
zeroing the next `2r+1` reference block, and occasionally growing the directory, costs more than a
constant on that append; summed across `M` appends it is `O(M)`. Thus shipped `AddLeaf` and
`AddLast` have **amortized**, not worst-case, constant cost per inserted node. A backend that
requires worst-case updates must deamortize or replace this allocation policy.

With `E` denoting the number of element nodes retained by the arena, the honest C# bounds are:

| C# operation | Myers/odd-arena bound |
| --- | ---: |
| `AddLast` | `O(1)` amortized |
| `Count`, `IsEmpty`, `Last` | `O(1)` worst case |
| `First`, indexer | `O(log E)` worst case |
| `RemoveFirst`, `RemoveLast`, `Drop` | `O(1)` worst case |
| `TryRemoveFirst` | `O(log E)` worst case |
| `TryRemoveLast` | `O(1)` worst case |
| `Take`, a `Slice` whose result tail moves, nontrivial `SplitAt` | `O(log E)` worst case |
| `CreateRange` with `n` values | `Theta(n)` time, `n` retained nodes |
| enumerate `n` visible items | `Theta(n)` time and `Theta(n)` transient element slots |
| arena storage after `E` enqueues | `Theta(E)` records, `E + 1 + O(sqrt(E+1))` record slots |

Boundary-specialized calls such as `Take(Count)`, `Drop(0)`, `Slice(s, Count-s)`, or a split at
`Count` can return an existing/derived tail without querying an ancestor. The table gives the
general worst case; a suffix slice that keeps the old tail is `O(1)` even on Myers.

The shipped arena places `AddLeaf`, ancestor navigation, parent/depth/value reads, statistics, and
node-count inspection under one private lock. Queue handles and already published node records are
immutable, but the implementation does not currently exploit that fact for lock-free reads. This is
a semantic thread-safety property, not a parallel-progress guarantee. Payload objects retain their
ordinary C# mutability and thread-safety caveats.

`PublishedNodeCount` excludes the bottom record; the block and slot counts include its physical
storage. The queue algorithm never consults these diagnostics. `GetStatistics()` returns one locked
snapshot with `PublishedNodeCount`, `BlockCount`, `AllocatedSlotCount`, `AddLeafCount`,
`AncestorQueryCount`, `LastAncestorHopCount`, `MaximumAncestorHopCount`, and
`TotalAncestorHopCount`. These counters support representation tests and observation; they are not
part of the queue denotation and do not by themselves prove an asymptotic bound. The two query
totals saturate at `Int64.MaxValue` rather than wrapping.

## Constant-Time Theoretical Instantiation

Alstrup and Holm's incremental level-ancestor result maintains a rooted tree under additions of
leaves with `O(1)` worst-case `AddLeaf` and `O(1)` worst-case level-ancestor queries in linear space.
The update permits adding a leaf below an existing node, exactly the branch operation required here.
Instantiating the backend contract with that result gives:

- `O(1)` worst-case `AddLast`, endpoint access/removal, index access, `Take`, `Drop`, `Slice`, and
  `SplitAt`;
- `O(1)` new words per successful enqueue and a constant-sized queue handle;
- `Theta(n)` front-to-back enumeration; and
- `O(M + H)` retained structural space across the manager and live handles.

This statement is a reduction to the published data structure, not a claim that
`MyersIncrementalAncestorArena<T>` has those bounds. It also inherits the theorem's computation and
allocation model. A production port must implement, prove, and test the full incremental algorithm
before changing the shipped complexity table. Substituting static level-ancestor preprocessing is
invalid because old handles may be extended online; substituting binary lifting gives logarithmic
update space/time and does not establish the constant bound.

The primary source is Stephen Alstrup and Jacob Holm, “Improved Algorithms for Finding Level
Ancestors in Dynamic Trees,” ICALP 2000
([DOI](https://doi.org/10.1007/3-540-45022-X_8)). Its stronger `O(1)` statement is specifically for
one tree updated by adding leaves. The paper's more general forest/edge-addition result has an
inverse-Ackermann aggregate bound and should not be substituted into the table without changing the
claim.

## Prior Art And Scoped Novelty

### What is already known

The core queue-as-path idea is not new:

- The [USP 2016 persistent-data-structure tutorial](https://linux.ime.usp.br/~yancouto/2016-WIP-Tutorial.pdf)
  explicitly stores start and end vertices for every persistent queue version in a tree of enqueued
  values. Enqueue adds a child below the end; pop advances the start using ancestor tables.
- The 2014 article [“Persistent Queue”](https://sudonull.com/post/104846-Persistent-Queue) describes a
  nonempty queue as first/last pointers where the first is an ancestor of the last, and develops
  incremental navigation for queue operations.
- The [Andrew Stankevich Contest 37 editorial](https://codeforces.com/topic/19339/en4#G), section G,
  finds a persistent queue's front with binary ancestors over a version/path tree.
- [“Top Tree Compression of Tries”](https://www.cs.haifa.ac.il/~oren/Publications/toptreetrie.pdf)
  explicitly relates extraction of root-to-node paths to fully persistent queues, providing further
  primary-source evidence that the path/queue connection is established.
- Hood and Melville's classic queue already supplies persistent FIFO endpoint operations in
  constant worst-case time; see [DOI 10.1016/0020-0190(81)90030-2](https://doi.org/10.1016/0020-0190(81)90030-2).

Myers's 1983 applicative random-access stack already combines constant-time cons/head/tail with
logarithmic suffix access using two links per cell. The primary paper is
[“An Applicative Random-Access Stack”](https://publications.mpi-cbg.de/Myers_1983_6328.pdf). Recent
work tightens the pointer-traversal constants and approaches the information-theoretic limit for
that two-pointer representation: Peters, Foo, and Adams,
[“Pushing the Information-Theoretic Limits of Random-Access Lists”](https://yongqi.foo/papers/myers.pdf).
Okasaki's [thesis](https://www.cs.cmu.edu/~rwh/students/okasaki.pdf) gives the closely related
skew-binary random-access lists used in the comparison below.

Rootish arrays, finger trees, random-access lists, and dynamic level ancestry are likewise existing
components, not inventions of this proposal.

### The narrow research claim

Among the primary papers, tutorials, contest editorials, libraries, and patents examined in the
targeted search, no source exposed this exact combination:

1. an immutable, branchable queue handle denoting any contiguous ancestral interval;
2. `Take`, `Drop`, arbitrary `Slice`, and `SplitAt` results that themselves remain appendable;
3. indexed access and every interval-boundary operation reduced to one incremental level-ancestor
   query; and
4. the resulting all-`O(1)` worst-case scalar API when paired with Alstrup--Holm.

That search result is not proof of absence or first publication. The combination follows naturally
once the two ingredients are placed side by side, and the path-interval representation appears in
contest/tutorial literature. The safe description is **an experimental scoped synthesis and
candidate API/Pareto point**, not “the first persistent queue represented by a tree,” not a new
level-ancestor algorithm, and not an unqualified novelty claim.

The candidate asymptotic improvement is equally scoped and is a time comparison, not whole-model
dominance. Compared with a persistent FIFO that has `O(1)` endpoint operations but linear indexed
access, the Alstrup--Holm instantiation improves indexed access to `O(1)` without worsening time for
the shared endpoint operations. Compared with a finger-tree sequence or random-access list, it
improves applicable index and boundary selection from logarithmic to constant while retaining
constant applicable end-update time. It obtains that result by giving up arbitrary edits, unrelated
concatenation, standalone functional ownership, and current-version-only space. The shipped Myers
reference does not realize this asymptotic index improvement over logarithmic alternatives.

### Why broader lower bounds do not contradict it

Straka's [optimal worst-case fully persistent arrays](https://ufal.mff.cuni.cz/~straka/papers/2009-perarray.pdf)
give `O(log log m)` worst-case lookup/update with constant update space, together with the relevant
cell-probe barrier under the paper's space assumptions, for arbitrary point-update arrays.
Bille and Gortz's
[persistent-string result](https://drops.dagstuhl.de/storage/00lipics/lipics-vol181-isaac2020/LIPIcs.ISAAC.2020.48/LIPIcs.ISAAC.2020.48.pdf)
establishes optimal `O(log n / log log n)` random access under a substantially richer arbitrary-edit
interface. Neither lower bound applies to a structure that only adds leaves and selects ancestral
subintervals. Conversely, this proposal gives no improvement for their operation sets.

The recent Myers lower bound concerns navigation with the specified small number of links per list
cell. Alstrup--Holm uses a global linear-space dynamic index and a different model, so an `O(1)` ASQ
query does not violate that bound.

## Comparison Matrix

The table compares only representative semantics; “not supported” is not silently treated as an
improved bound.

| Structure | Persistence/model | Applicable end update | Index | Slice/split | Important capability ASQ omits |
| --- | --- | ---: | ---: | ---: | --- |
| ASQ + Alstrup--Holm | immutable handles over mutable monotone manager | `O(1)` worst case | `O(1)` worst case | `O(1)` worst case | prepend, update, unrelated concat |
| C# ASQ + Myers | same manager model | append `O(1)` amortized; handle-only removals `O(1)` | `O(log M)` | `O(log M)` | same restrictions |
| Hood--Melville queue | purely functional persistent FIFO | `O(1)` worst case | `Theta(n)` | generally `Theta(n)` | indexed/slice API |
| Myers applicative stack | purely functional/tail sharing | cons/head/tail `O(1)` | `O(log n)` | suffix lookup `O(log n)` | queue-right append and general slice facade |
| Skew-binary random-access list | purely functional | front operations `O(1)` | `O(log n)` | typically logarithmic decomposition | FIFO-right append bounds differ |
| Finger tree / `Data.Sequence` style | purely functional persistent sequence | `O(1)` amortized at fingers | `O(log n)` | `O(log n)`; concat supported | ASQ has no general concat/prepend |
| Random Access Zipper | purely functional randomized cursor | local edit `O(1)` | relocation `O(log n)` expected/amortized | cursor-centric | arbitrary cursor edits |
| Rootish resizable array | ephemeral mutable array | `O(1)` | `O(1)` | not persistent views | persistence and branching |

Finger-tree bounds and semantics come from Hinze and Paterson,
[“Finger Trees: A Simple General-Purpose Data Structure”](https://www.cs.ox.ac.uk/ralf.hinze/publications/FingerTrees.pdf).
The Random Access Zipper comparison refers to Headley and Hammer,
[“The Random Access Zipper”](https://arxiv.org/abs/1608.06009).

Within this repository, `FingerTreeDeque<T>` remains the general persistent deque: it supports both
ends and a broader sequence algebra, with endpoint operations amortized constant, logarithmic worst
case, and logarithmic indexing/splitting. ASQ is justified only when branching append histories and
subrange/index access dominate and the missing operations are genuinely unnecessary.

## Why The Odd Arena Is Not A Persistent Rootish Array

The odd-block arena borrows rootish arithmetic only for manager storage. Its blocks contain
immutable **history-node records**, and the one manager appends to them under synchronization.
Queue handles never path-copy or own those blocks. Persistence follows from immutable nodes and
stable handles, not from persistence of the arena's directory.

A direct persistent rootish-array attempt does not preserve the desired bounds. With block sizes
near `sqrt(n)`, copying a partially filled data block for a branch costs `Theta(sqrt(n))`. Copying a
flat block directory has the same problem; recursively persistent directories add navigation levels
and no longer justify a simple `O(1)` access argument. In the symmetric square-recursive attempt,
both directory selection and within-block selection recurse on size `sqrt(n)`, giving
`T(n) = 2T(sqrt(n)) + O(1) = Theta(log n)`. Sharing or sealing full immutable blocks does not solve
branching appends to the same partially filled last block: an adversary can fork that version and
force the partial-block copy on every branch. The manager-backed arena avoids those copies by
centralizing append-only allocation, at the price of retained-history space and loss of a purely
functional ownership model.

The classic ephemeral result is Brodnik, Carlsson, Demaine, Munro, and Sedgewick,
[“Resizable Arrays in Optimal Time and Space”](https://sedgewick.io/wp-content/themes/sedgewick/papers/1999Optimal.pdf):
constant operations with `n + O(sqrt(n))` storage and a matching slack lower bound in its allocation
model. A standard pedagogical RootishArrayStack is documented by
[Open Data Structures](https://opendatastructures.org/ods-java/2_6_RootishArrayStack_Space.html).
The odd sequence `1,3,5,...` merely changes triangular boundaries into exact squares; it does not
improve the asymptotics.

More generally, Tarjan and Zwick's
[“Optimal Resizable Arrays”](https://epubs.siam.org/doi/10.1137/23M1575792) provides an existing
`N + O(N^(1/r))` resident-space / `O(r)` amortized grow-shrink tradeoff and matching lower bounds for
a broad class. Those results further counsel against presenting odd blocks as a new persistent
sequence family.

## Lifetime, Space, Equality, And Concurrency Limits

### Retained-history model

`M` counts all enqueues ever accepted by an arena, including branches no longer reachable from the
application's current queue variables. The shipped arena retains those node records and their
payloads until the arena itself becomes unreachable. Dropping a queue prefix or releasing one
handle does not decrement node ownership and does not reduce `M`. This is normal for a simple
arena-backed persistent history but can be unacceptable for unbounded services.

A future reclaiming backend would need either tracing from all live handles, explicit epochs or
leases, or a different ancestor index that tolerates deletion. None is part of this proposal, and
reclamation must not be inferred from garbage collection of a queue handle.

### Identity and equality

Two handles can enumerate equal values while having different tails, anchors, or arenas, and two
separately allocated handles may also describe the same ancestry interval. Object reference identity
is therefore neither content equality nor a complete provenance-equivalence test. The prototype
does not promise `O(1)` sequence equality, hashing, or canonical empty identity. Content comparison
and hashing, if later added, require at least examining values unless a separately proved
authenticated aggregate is maintained.

There must be no static shared `Empty<T>` backed by one immortal global arena: it would couple
unrelated workloads, retain their payloads together, and introduce global write contention.
Factories create or explicitly accept an arena so lifetime and synchronization have an owner.

### Concurrency and failure publication

Immutable queue handles and published nodes give stable concurrent semantics. The C# Myers arena
serializes both updates and backend reads with one lock; this permits several threads to branch from
old handles safely but does not promise parallel, lock-free, or wait-free progress. A node must be
completely initialized before its handle enters a returned queue. Allocation failure or depth/count
overflow must leave the source handle valid and must not publish a count referring to a missing
node.

Enumeration observes the immutable path of its captured handle. Concurrent additions below the
same or another old node cannot enter that path. This snapshot behavior follows from the invariant,
not from copying the payload objects.

## C# Implementation And Test Map

The experimental shipment is intentionally one-port and localized:

| Path | Responsibility |
| --- | --- |
| `src/CSharp/src/Durable7.FingerTree/AncestralSliceQueue.cs` | Public `IIncrementalAncestorArena<T>`, `MyersIncrementalAncestorArena<T>`, `MyersIncrementalAncestorStatistics`, and immutable `AncestralSliceQueue<T>`; private integer-addressed node records, odd-block ownership, and `IReadOnlyList<T>` enumeration. |
| `src/CSharp/tests/Durable7.FingerTree.Tests/AncestralSliceQueueTests.cs` | API semantics, persistence/branching, slices/splits, anchored empties, factory/backend routing, Myers ancestor correctness, square block boundaries, enumeration, validation, and synchronized concurrency stress. |
| `docs/proposals/ancestral-slice-queue-2026-07-25.md` | Research boundary, proof, parameterized/theoretical bounds, shipped bounds, prior art, and limitations. |

The tests should map to the proof obligations rather than only examples:

1. **Model equivalence:** randomized `AddLast`, both removals, `Take`, `Drop`, `Slice`, `SplitAt`,
   index, and enumeration against immutable array/list slices.
2. **Branch stability:** retain several ancestors, append distinct values to each, and verify all
   sources and siblings remain unchanged.
3. **Anchored empties:** exercise `Take(0)`, `Drop(Count)`, every zero-length `Slice`, both endpoint
   removals of a singleton, then append and prove that no discarded value reappears.
4. **Boundary arithmetic:** empty, singleton, full ranges, split positions `0` and `Count`, invalid
   negatives, overflow-shaped ranges, and default/uninitialized values if the public carrier allows
   them.
5. **Ancestor oracle:** compare Myers queries on deep chains and irregular branches with a naive
   parent walk, including exact depth, parent, root, and virtual-root boundaries.
6. **Odd-block transitions:** append across total counts `0, 1, 4, 9, 16, ...` and branch from nodes
   on both sides of each square boundary.
7. **Enumeration:** front-to-back order for whole histories and interior slices, repeated and
   concurrent enumeration, and no observation of later appends.
8. **Concurrency/publication:** multiple writers branching through one arena plus readers over old
   handles; validate complete values and paths rather than claiming lock-free progress.
9. **Complexity guardrails:** `GetStatistics()` makes deterministic pointer-hop and odd-block
   counters available for tests of the Myers implementation. Such tests may check the observed hop
   envelope and block-capacity arithmetic, but wall-clock benchmarks and finite counter samples must
   not be treated as proofs of asymptotic or Alstrup--Holm bounds.
10. **Parameterization and routing:** create a queue with an explicit arena, reject invalid factories,
    and use counter deltas to verify that handle-only operations publish/query no nodes, `AddLast`
    publishes exactly one, and every boundary-seeking operation makes at most one ancestor query.

The current public-API suite exercises items 1--10. Its direct arena tests compare a branched Myers
tree with a naive parent-walk oracle, check exact odd-block capacity at every square transition
through 4,096 published nodes, and assert a deliberately conservative logarithmic pointer-hop
envelope on a 32,768-node chain. Those finite deterministic checks guard the shipped implementation;
they are not a proof of its asymptotic bound and do not test an Alstrup--Holm backend.

Focused validation can be run with:

```powershell
dotnet test src/CSharp/tests/Durable7.FingerTree.Tests/Durable7.FingerTree.Tests.csproj `
  --filter FullyQualifiedName~AncestralSliceQueue
```

On 2026-07-25, the focused lane passed 15/15 tests in Debug and Release. The complete FingerTree
project passed 739/739 tests, and the serialized full C# solution passed 1,545/1,545 tests, in both
configurations with zero build warnings or errors. No benchmark result is part of this evidence.

The complete C# FingerTree suite remains the regression gate. The C# catalog and usage documents
label the API experimental; cross-language ports should wait until the API and manager-lifetime
tradeoff have a consumer and that status is deliberately changed.

## References

- Stephen Alstrup and Jacob Holm, “Improved Algorithms for Finding Level Ancestors in Dynamic
  Trees,” ICALP 2000: [DOI](https://doi.org/10.1007/3-540-45022-X_8).
- Eugene W. Myers, “An Applicative Random-Access Stack,” 1983:
  [paper](https://publications.mpi-cbg.de/Myers_1983_6328.pdf).
- David Peters, Yongqi Foo, and Stephen Adams, “Pushing the Information-Theoretic Limits of
  Random-Access Lists,” 2025: [paper](https://yongqi.foo/papers/myers.pdf).
- Chris Okasaki, “Purely Functional Data Structures”:
  [thesis](https://www.cs.cmu.edu/~rwh/students/okasaki.pdf).
- Robert Hood and Robert Melville, “Real-Time Queue Operations in Pure LISP,” 1981:
  [DOI](https://doi.org/10.1016/0020-0190(81)90030-2).
- Ralf Hinze and Ross Paterson, “Finger Trees: A Simple General-Purpose Data Structure”:
  [paper](https://www.cs.ox.ac.uk/ralf.hinze/publications/FingerTrees.pdf).
- Andrej Brodnik, Svante Carlsson, Erik D. Demaine, J. Ian Munro, and Robert Sedgewick, “Resizable
  Arrays in Optimal Time and Space”: [paper](https://sedgewick.io/wp-content/themes/sedgewick/papers/1999Optimal.pdf).
- Robert E. Tarjan and Uri Zwick, “Optimal Resizable Arrays”:
  [DOI](https://doi.org/10.1137/23M1575792), [arXiv](https://arxiv.org/abs/2211.11009).
- Jan Straka, “Optimal Worst-Case Fully Persistent Arrays”:
  [paper](https://ufal.mff.cuni.cz/~straka/papers/2009-perarray.pdf).
- Philip Bille and Inge Li Gortz, “Random Access in Persistent Strings and Segment Selection”:
  [paper](https://drops.dagstuhl.de/storage/00lipics/lipics-vol181-isaac2020/LIPIcs.ISAAC.2020.48/LIPIcs.ISAAC.2020.48.pdf).
- Daniel R. Headley and Matthew A. Hammer, “The Random Access Zipper”:
  [arXiv](https://arxiv.org/abs/1608.06009).
