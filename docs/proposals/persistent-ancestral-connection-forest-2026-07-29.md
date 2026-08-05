# Persistent ancestral connection forest

**Status:** experimental design selected for implementation on
`experimental`

**Date:** 2026-07-29

**Namespace:** `Durable7.Hamt`

**Scope:** an immutable, fully branching, insertion-only connectivity structure over a fixed vertex
universe

## Decision

Implement `PersistentAncestralConnectionForest` as a union-by-size forest stored in the
repository's canonical C# CHAMP. In addition to ordinary persistent connectivity, it answers this
history query:

> Given a version `v` and vertices `x` and `y`, which earliest version on the unique root-to-`v`
> lineage made them connected?

The proposed `FirstConnected` operation has the same asymptotic cost as one connectivity query. A
same-backend persistent disjoint-set forest that treats connectivity as a black box needs a
logarithmic number of connectivity queries to locate that version. The improvement is a factor of
`log(K + 2)`, where `K` is the number of successful unions on the queried lineage. Since
`K <= N - 1`, the gap is a full `log N` factor on histories with `Theta(N)` successful unions. The
new operation does not change the asymptotic bounds of construction, lookup, insertion,
component-count access, forking, or retained history.

The novelty claim is deliberately narrow. Timestamped union forests, persistent arrays, union by
size, and version trees are established techniques. The contribution claimed here is the exact
online, immutable, fully branching ADT and its use of branch-valid union-edge tags to answer
`FirstConnected` without a binary search over versions. This is a useful data-structure synthesis,
not a claim of a new union-find lower bound or a publication-grade literature result.

## Abstract data type

Let `V = { 0, ..., N - 1 }` be a fixed integer universe. A value denotes both a connectivity
snapshot and one node in a version tree. The root version contains every vertex and no connections.
Every `Link` creates one child version; versions are immutable and any retained version may be
extended, so the version graph is a tree.

The semantic operations are:

```text
Create(vertexCount) -> root

version.Link(x, y) -> child
version.Find(x) -> vertex
version.Connected(x, y) -> bool
version.FirstConnected(x, y) -> VersionToken?
version.TryGetFirstConnected(x, y, out token) -> bool

version.Version -> VersionToken
version.VertexCount -> int
version.ComponentCount -> int

token.Parent -> VersionToken?                // null only at the root
token.Root -> VersionToken
token.Depth -> nonnegative integer
```

`Link(x, y)` adds an undirected connection to the child's logical partition. It creates a child even
when the endpoints were already connected; such an edge changes no partition cell. Forking requires
no operation beyond retaining a version reference and is therefore O(1).

`FirstConnected(x, y)` has lineage-relative semantics:

- it returns `null` when `x` and `y` are disconnected in the queried version;
- it returns the root version when `x` and `y` are the same fixed-universe vertex;
- otherwise it returns the shallowest ancestor `a` of the queried version for which the two
  vertices are connected.

The result is never a version on another branch. Global allocation or creation order is not part of
the contract because sibling versions are incomparable.

Representatives are implementation artifacts. The API may return one for interoperability, but it
does not promise that the same component has the same representative in sibling versions.

## Exact comparison family

The comparison is intentionally same-backend and same-model. Both structures are:

- purely functional at the public boundary;
- fully persistent, with updates allowed from any retained version;
- insertion-only over the same fixed universe;
- union-by-size forests without path compression;
- stored in the same persistent random-access map;
- supplied the same O(1)-space version handle and ancestry metadata.

Define:

- `R` as the time for one lookup in a persistent-map snapshot;
- `W` as the time for one persistent-map point update;
- `S_W` as the fresh retained space allocated by one point update;
- `K` as the number of successful unions on the queried root-to-version lineage;
- `M` as the number of retained versions; and
- `U <= M` as the number of successful unions across retained history.

Union by size bounds every parent chain in every version by `floor(log2 N)`. Constants such as the
two finds and at most two map updates performed by `Link` are suppressed.

| Operation | Same-backend persistent DSU | Proposed forest |
|---|---:|---:|
| Retain/fork a version | O(1) | O(1) |
| `Create` | O(1) | O(1) |
| `Find` | O(R log N) | O(R log N) |
| `Connected` | O(R log N) | O(R log N) |
| `ComponentCount` | O(1) | O(1) |
| `Link` | O(R log N + W) | O(R log N + W) |
| `FirstConnected` | O(R log N log(K + 2)) | **O(R log N)** |
| Current semantic universe | Theta(N) | Theta(N) |
| Current explicit index | O(N) | O(N) |
| Retained physical space | O(M + U S_W) | O(M + U S_W) |

The strongest black-box comparator does not search redundant link events. It maintains an
append-only ancestry index containing only successful-union snapshots, then binary-searches the
`K` states on the queried event lineage for the first true connectivity result. The table grants it
O(1) level-ancestor selection and O(1) metadata work per link; linear-space dynamic trees with leaf
addition and constant-time ancestor queries are
[known](https://doi.org/10.4230/LIPIcs.ISAAC.2021.66), so redundant versions cannot inflate the
claimed gap. A conventional binary-lifting or persistent-sequence implementation only makes the
comparator slower. Even after this strengthening, `K` can be `N - 1`, requiring
`Theta(log N)` separate connectivity probes in the worst case.

The proposed structure adds one version-token reference to an already updated child-root record.
That is a constant-size field and does not add a map update, map traversal, or asymptotic retained
space.

### CHAMP specialization

The first implementation uses
`Durable7.Hamt.PersistentHashMap<int, Cell>`. The repository CHAMP visits at most seven trie levels
for a 32-bit hash. Distinct `int` keys have distinct full hashes under the default comparer, so this
specialization cannot form a collision bucket containing different vertices. Let `w <= 7` be the
trie depth. A lookup takes O(w), and a write allocates O(w) rebuilt trie nodes.

Substituting `R = W = S_W = O(w)` gives:

| Operation | Comparator with CHAMP | Proposed forest with CHAMP |
|---|---:|---:|
| Find/connectivity | O(w log N) | O(w log N) |
| `Link` | O(w log N) | O(w log N) |
| `FirstConnected` | O(w log N log(K + 2)) | **O(w log N)** |

Because `w <= 7`, ordinary operations and `FirstConnected` are worst-case O(log N), while black-box
history search is O(log N log(K + 2)). When `K = Theta(N)`, this is O(log N) versus O(log^2 N).
The sparse root represents every singleton implicitly, so
`Create` is O(1), a redundant `Link` allocates only its version wrapper and token, and retained
history is O(M).

A future generic-vertex facade would have to restore the CHAMP's `O(w + c)` bound, including
equal-full-hash collision scans and bucket cloning. The integer specialization is intentional: its
worst-case claim does not silently assume a well-behaved user hash function.

## Representation

Each version contains:

```text
VersionState {
    VersionToken version;             // immutable identity, parent, and depth
    int vertexCount;
    int componentCount;
    PersistentHashMap<int, Cell> cells;
}

Cell {
    int parent;
    int size;                          // meaningful only when parent == this vertex
    VersionToken? joinedAt;            // null at roots
}
```

The root CHAMP is empty. An absent cell canonically means `parent(x) = x`, `size(x) = 1`, and
`joinedAt(x) = null`. A stored root is therefore non-singleton. This sparse convention makes
construction O(1) and stores only roots and children touched by successful unions.

To add `(x, y)` to version `p`:

1. Find `rx` and `ry` using only `p.cells`.
2. Allocate the child version token `t`, with `t.Parent = p.Version` and
   `t.Depth = p.Depth + 1`.
3. If `rx == ry`, publish a child that reuses `p.cells` exactly and preserves the component count.
4. Otherwise attach the smaller root below the larger root. In the child-root record set
   `parent = winner` and `joinedAt = t`; in the winner-root record store the summed size.
5. Publish the child with the resulting CHAMP root and component count minus one. The parent
   remains unchanged.

Ties may be resolved deterministically, but tie policy is not semantic. The stored `joinedAt` token
belongs to the successful union, not to a later query or path-shortening operation.

## `FirstConnected` algorithm

All record reads below use the queried version's one immutable CHAMP root.

```text
FirstConnected(v, x, y):
    if x == y:
        return RootVersion

    px = ReadParentPath(v.cells, x)
    py = ReadParentPath(v.cells, y)

    if px.root != py.root:
        return null

    l = deepest common vertex in reverse(px), reverse(py)
    answer = latest-by-depth joinedAt tag on x..l and y..l
    return answer
```

`ReadParentPath` returns at most `floor(log2 N) + 1` vertices and the tag on every traversed parent
edge. Reversing the two short paths or walking indices backward locates their lowest common ancestor
without another persistent lookup. The implementation may scan both suffixes for clarity. Because
edge tags increase strictly toward the root, only the last edge on each nonempty suffix is a
candidate, but the simpler full scan has the same bound.

The query uses O(log N) temporary references. This ephemeral scratch is not retained by any version
and does not alter the persistent-space bound.

## Correctness argument

### Lemma 1: every version selects a forest

The root version consists of implicit singleton trees. A successful `Link` changes the parent of exactly
one root to a distinct root. It therefore joins two trees without creating a cycle. A redundant
edge changes no parent. Induction over a root-to-version lineage proves that every version's parent
relation is a forest representing exactly the connected components of its logical graph.

### Lemma 2: parent paths have logarithmic height

When a root becomes a child, the winning component has size at least as large as the losing
component. Consequently the component containing any vertex at least doubles every time that
vertex's root-path depth increases. No component exceeds `N`, so a parent path has at most
`floor(log2 N)` edges. Persistence does not change the argument: it applies independently to the
one forest selected by each queried version.

### Lemma 3: tags increase toward a selected root

Consider a live parent edge `r -> p` tagged by version `t`. Both endpoints were roots immediately
before `t`; all edges below `r` were therefore created at strict ancestors of `t`. Any parent edge
later placed above `p` must be created by a strict descendant of `t`. Thus the depths of tags on
every selected child-to-root path strictly increase.

This statement is lineage-local. It never compares tags from sibling versions, and a snapshot
cannot contain a parent edge introduced only in a sibling because persistent point updates do not
mutate the shared parent CHAMP root.

### Lemma 4: a pair's threshold is the maximum tag below its forest LCA

Let connected vertices `x` and `y` have lowest common ancestor `l` in version `v`'s union forest.
Let `P` be the union of the parent edges on `x..l` and `y..l`, excluding the shared path above `l`.
Every edge in `P` must exist before the forest paths connect `x` to `y`, so the pair cannot be
connected before the maximum-depth tag `t*` in `P`.

At `t*`, the last missing edge joins two previously distinct components. Every other edge in `P`
has an ancestor tag by Lemma 3, so immediately after that union the whole `x`-to-`y` forest path
exists. Therefore `x` and `y` are connected at `t*`, and `t*` is their first connected version.

Edges above `l` are deliberately excluded. They attach an already-connected `x`/`y` component to
some later component and must not overwrite the pair's earlier history.

### Lemma 5: sibling updates cannot contaminate a query

A query reads every parent cell from `v.cells`, never from the version stored in an edge tag.
Path copying gives sibling versions different CHAMP roots at their changed paths and shared,
immutable nodes elsewhere. Hence the path and every tag observed by a query belong to `v` or one of
its ancestors. An update made from another child of a shared ancestor is unreachable from
`v.cells`.

### Theorem

By Lemma 1, differing roots mean disconnected vertices. Otherwise the two O(log N)-length paths
have a well-defined LCA. Lemma 4 proves that the maximum suffix tag returned by the algorithm is
exactly the first connected ancestor version, and Lemma 5 extends the argument from a linear update
sequence to arbitrary retained branches. Lemma 2 and one persistent lookup per traversed record give
O(R log N) query time.

## Why path compression is excluded

Ordinary path compression preserves present connectivity but not the uncompressed merge boundary
used by `FirstConnected`.

For example:

1. At `t1`, union `a` with `b`.
2. At `t2`, union `c` with `d`.
3. At `t3`, union the two components.
4. Compress the paths of `a` and `b` directly to the final root.

The correct answer for `FirstConnected(a, b)` is `t1`. If the compressed edges merely carry the
maximum skipped timestamp, both paths can report `t3`; their original LCA boundary has vanished.
More elaborate summaries can preserve selected history queries, but they are a different design
and require a new no-regression proof.

The proposed implementation therefore performs root-only union by size and never rewrites a
non-root parent. Alternatively, a future implementation could keep a separate uncompressed witness
forest while using compression in a connectivity index, but that duplication is outside this
experiment.

## Failed shortcuts and counterexamples

- **One timestamp per component root is insufficient.** If `a` and `b` connect at `t1` and their
  component joins another at `t3`, the component's newest timestamp is `t3`, not the answer for
  `(a, b)`.
- **Taking the maximum tag all the way to the root is insufficient.** Both endpoints share later
  edges above their LCA. Including those edges overestimates an earlier pair connection.
- **A single mutable evolution-tree parent is not fully branching.** A component root retained at a
  fork can acquire one parent in the left branch and a different parent in the right branch. The
  parent relation must be selected through the queried persistent snapshot.
- **Global version sequence numbers are not semantic time.** A sibling allocated later is not a
  later state of the queried branch. Only ancestry and ancestor depth order live tags.
- **A black-box OR of partial connectivity answers is not enough.** Connectivity can arise from
  edges in several summaries, for example `x-a` in one and `a-y` in another. This is why a generic
  segment-tree-over-history composition does not remove the binary-search or merge work.

## Implementation map

The initial C# experiment should be confined to:

- `src/CSharp/src/Durable7.Hamt/PersistentAncestralConnectionForest.cs`;
- `src/CSharp/tests/Durable7.Hamt.Tests/PersistentAncestralConnectionForestTests.cs`; and
- this proposal.

Suggested public and internal pieces are:

- `PersistentAncestralConnectionForest`: immutable version over integer vertices;
- `AncestralConnectionVersion`: opaque immutable identity with parent and depth;
- private immutable `Cell` and `PathStep` values;
- iterative root finding plus a path reader used by the history query;
- `TryGetFirstConnected` as the non-nullable-control-flow companion to `FirstConnected`; and
- internal `ValidateInvariants` and exact-root-sharing diagnostics, following repository test
  patterns.

Construction rejects negative counts, and every operation eagerly rejects integer vertices outside
`0 .. VertexCount - 1`. Published versions contain no mutable collection state and may be read
concurrently.

## Test and validation map

The focused suite should include:

1. Empty-edge singleton semantics, including `FirstConnected(x, x) == root`.
2. A linear history where several pairs first connect at distinct versions.
3. A later supercomponent merge proving that a pair retains its earlier connection version.
4. Sibling branches that attach the same retained roots differently, with no cross-branch leakage.
5. Redundant and parallel edges, proving that no-op child versions do not replace an earlier tag.
6. Old-version queries after deep descendant updates.
7. Union-by-size sequences exercising the logarithmic-height boundary and cached component count.
8. Sparse-root cases that leave untouched singleton vertices absent from the CHAMP.
9. Randomized retained-branch model checking. Each model version keeps its parent and an explicit
   partition; exhaustive pair checks validate connectivity and component count, while scanning model
   ancestors validates the exact `FirstConnected` token.
10. The explicit `t1`/`t2`/`t3` pair-LCA counterexample, including one queried endpoint equal to the
    forest LCA after that component later becomes a child.
11. Negative universe sizes, invalid endpoint vertices, checked publication, and retained-source
    failure atomicity.
12. Structural validation of sparse-cell canonicality, parent membership, acyclicity, root sizes, component
    size totals, maximum height, null root tags, strict tag-depth growth, and tag ancestry relative
    to the validated version.

No allocation-count test should claim that `FirstConnected` is allocation-free: its O(log N)
scratch paths are part of the simple implementation. A useful allocation gate instead verifies that
a redundant `Link` reuses the exact parent-map root and that a successful update rebuilds only
the CHAMP paths documented by `PersistentHashMap`.

## Limitations and non-goals

- Vertices are exactly the integers `0 .. VertexCount - 1`, fixed at construction. A generic-key
  facade or persistent `MakeSet` is a possible extension but is not covered by this proof or
  comparator.
- Edges can be added but not removed. After deletion, connectivity is not monotone and there may be
  several connected intervals on a lineage.
- There is no confluent merge of sibling graph versions.
- Updates create children; they do not retroactively mutate an existing version and all of its
  already-created descendants.
- `FirstConnected` is defined only relative to one queried version's ancestor chain. It does not
  compare or reconcile sibling histories.
- The structure returns the first connectivity event, not necessarily a shortest path, an original
  edge witness, bridge information, or an explanation proof.
- No semantic guarantee is attached to representative choice.
- The simple design deliberately forgoes path compression. Its bounds are worst-case logarithmic in
  component size rather than the inverse-Ackermann amortized bound of an ephemeral linear-history
  union-find.
- The integer-only CHAMP specialization is part of the unconditional bound. A generic-key port
  would inherit equal-hash collision scans and collision-bucket cloning.
- A sealed linear history can be preprocessed into a component tree with faster O(1) LCA queries;
  this online fully branching structure does not claim superiority in that model.

## Prior-art boundary

The following are the closest primary sources found in the targeted search:

- Tarjan analyzes weighted union and path compression in the classical ephemeral problem in
  [Efficiency of a Good But Not Linear Set Union Algorithm](https://doi.org/10.1145/321879.321884).
  The proposed forest uses only the weighted-union height argument and deliberately excludes path
  compression.
- Driscoll, Sarnak, Sleator, and Tarjan establish general persistence techniques and the distinction
  between partial and full persistence in
  [Making Data Structures Persistent](https://doi.org/10.1016/0022-0000(89)90034-2).
- Dietz's [Fully Persistent Arrays](http://hdl.handle.net/1802/5695) explicitly studies branching
  version trees and gives O(log log n) expected-amortized persistent-array operations. That is a
  possible alternative backend; this proposal parameterizes its proof by `R`, `W`, and `S_W`
  instead of claiming CHAMP is the only representation.
- Gualà, Leucci, and Ziccardi maintain a linear-space tree under leaf additions with constant-time
  level-ancestor queries in
  [Resilient Level Ancestor, Bottleneck, and Lowest Common Ancestor Queries in Dynamic Trees](https://doi.org/10.4230/LIPIcs.ISAAC.2021.66).
  The comparator is therefore granted constant-time selection in its successful-union event tree.
- Gaibisso, Gambosi, and Talamo give a linear-history, partially persistent set-union structure with
  O(1) `Union` and O(log N) historical `PFind` in
  [A Partially Persistent Data Structure for the Set-Union Problem](https://www.numdam.org/article/ITA_1990__24_2_189_0.pdf).
- Kaya, Saule, Kucuktunc, and Catalyurek describe timestamped evolution forests for an offline
  evolving network. They traverse a logarithmic-height union forest to recover pair connection
  time, and describe a larger component forest that supports O(1) queries after static LCA
  preprocessing, in
  [Algorithms for Offline Tracking of Connected Components in Large Evolving Networks](https://onurkucuktunc.github.io/papers/proceedings/Kaya-DNASDM12.pdf).
- Cesario, Zakhour, Weisenburger, and Salvaneschi's 2026
  [Versioned E-Graphs](https://programming-group.com/assets/pdf/papers/2026_Versioned-EGraphs.pdf)
  is especially close in application: it labels union-find edges by nodes in a version tree and
  supports branching logical contexts. Its update semantics can add an equality at a version that
  already has descendants, so it recursively propagates that equality and in the worst case matches
  cloning. This proposal instead treats every version as immutable and creates a child for every
  new edge; it adds the distinct `FirstConnected` lineage query.

These sources establish all major ingredients around the proposal. The targeted search did not
find the exact immutable, online, fully branching ADT with a same-find-bound `FirstConnected`
operation. Absence from this search is not proof that the combination has never appeared. Any
external novelty claim should therefore remain qualified, and the repository should present this
as an experimental synthesis with an executable proof surface.

## Acceptance criteria

The experiment is successful only if all of the following are demonstrated:

- implementation and randomized retained-branch tests agree with the graph-and-ancestor model;
- validation establishes sparse-cell canonicality, current root sizes, the resulting height bound,
  tag order, and branch ancestry;
- shared operations perform the same number of asymptotic CHAMP reads and writes as the comparator;
- `FirstConnected` reads O(log N) parent cells and never probes O(log K) successful-union versions;
- documentation preserves the integer CHAMP's bounded-depth argument, notes the generic-key
  collision caveat, and preserves the offline-linear-history caveat; and
- no claim is made for deletion, confluent merge, dynamic vertices, path compression, or global
  ordering of sibling versions.
