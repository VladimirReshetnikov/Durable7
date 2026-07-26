# Checkpoint-Differential Ordered Map: the `PersistentDeltaMap` C# Prototype

- Status: Research design note with a C# reference prototype and focused tests; novelty candidate,
  not a priority claim
- Created: 2026-07-25
- Repository HEAD at drafting: `67a67f9f553a9194db9d8dd3cf9c7bd670f9b981`
- Prototype: [`PersistentDeltaMap.cs`](../../src/CSharp/src/Durable7.FingerTree/PersistentDeltaMap.cs)
- Focused tests:
  [`PersistentDeltaMapTests.cs`](../../src/CSharp/tests/Durable7.FingerTree.Tests/PersistentDeltaMapTests.cs)
- Shipment boundary: C# reference prototype only; this note is **not** a nine-language shipment or
  parity commitment

## Abstract

`PersistentDeltaMap<TKey, TValue>` is the C# reference implementation of a fully persistent
checkpoint-differential ordered map specialized for a common, narrow question:

> Which key classes differ now from the last checkpoint, and what were their checkpoint and current
> values?

A version stores three immutable roots: checkpoint state `B`, current state `S`, and an ordered
change index `D`. `D` contains exactly one presence-safe `(before, after)` record for each
comparator-equivalent key class on which `B` and `S` differ. The first effective write to a class
captures its checkpoint-relative `before`; later writes replace only `after`; returning to the
checkpoint value removes the record. Checkpoint and rollback are root changes, not scans.

Let `N` be the number of key classes in `dom(B) ∪ dom(S)`, with a floor of two for logarithms, and
let `k = |D|`. The C# path-copy prototype has the same asymptotic point-map bounds as its underlying
persistent ordered map: lookup and effective point edit are `O(log N)`, snapshots and branching are
`O(1)`, and retaining an edited successor adds `O(log N)` nodes. In return it consumes its complete,
sorted, exact checkpoint-relative change stream in `Θ(k + 1)` time. That is output-optimal and is an
asymptotic improvement over reconstructing the same sparse change set from state-only path-copied
trees: an evenly spaced `k`-leaf adversary exposes
`Θ(k log(N / k + 1))` unshared frontier nodes, including `Θ(log N)` work for one change. The extra
delta-tree edit is `O(log(k + 1)) ⊆ O(log N)`, so it changes constants but not the point-update
asymptotic.

This is an online, explicit-checkpoint trade: work and information are maintained as edits happen.
It is not a faster arbitrary-version differ, a transaction log, a confluent merge, or a bulk-update
structure. The public mutation surface is intentionally point-only. In particular, an eager exact
`Clear` would create up to `Θ(N)` removal records and is omitted rather than hiding that cost.

The underlying ideas have close precedents: persistent trees, undo-record elision, net-change CDC,
before/after change capture, comparator-keyed diff buffering, ordered write-batch overlays, and even
the exact phrase “Persistent Delta Map” are all prior art. The scoped research claim is only that the
searched primary sources did not expose and analyze this exact combination as a fully persistent
comparison-ordered **map** API: explicit `O(1)` checkpoint and rollback, exact presence-safe
first-before/final-after records, deterministic sorted `Θ(k + 1)` enumeration, and unchanged
asymptotic point-map operations. This is a falsifiable literature-search result, not a patentability
or historical-priority assertion.

## Why this structure is useful

The target workloads already maintain a meaningful synchronization boundary and repeatedly need a
small, deterministic patch:

- incremental view or secondary-index maintenance;
- UI dirty-state and undo/preview branches;
- build-system invalidation and cache write-back;
- state replication at explicit commit boundaries;
- audit preparation where a consumer needs old and new values, but not every intermediate write.

A conventional persistent map makes versions cheap, but a root pointer alone does not name the
changed keys. A write log names operations, but repeated writes, cancellations, and ordering must be
resolved later. `PersistentDeltaMap` maintains the exact net answer online.

## Semantic model and notation

The key comparer `C_K` defines both order and identity:

`x ≡K y` iff `C_K.Compare(x, y) == 0`.

The value comparer `C_V` defines semantic value equality and cancellation:

`a ≡V b` iff `C_V.Equals(a, b)`.

Both policies must be stable and coherent. In particular, `C_K` must define a total preorder whose
equivalence classes form a total order, and `≡V` must behave as an equivalence relation. Comparer
calls are assumed `O(1)` in the tables below; a nonconstant comparison cost is additive.

An endpoint is presence-safe:

```text
Endpoint<V> ::= Absent | Present(V)
```

Presence is separate from the value, so `Present(null)` is not `Absent`. Endpoint equivalence first
compares presence and, when both are present, uses `≡V`.

For one version:

- `B` is its immutable checkpoint ordered map;
- `S` is its immutable current ordered map;
- `D` is its immutable ordered map from key class to `(before, after)`;
- `s = |dom(S)|`;
- `N = max(2, |dom(B) ∪ dom(S)|)`, counting `≡K` classes rather than object identities;
- `k = |D|`.

For a point operation, `N` means the larger of this quantity before and after the operation. Thus an
insertion into an empty version still has a well-defined logarithmic bound. Always `0 ≤ k ≤ N`.

“Exact” throughout this note means extensional equality under `≡K` and `≡V`, not reference identity.
A supplied value that compares equal may be treated as a no-op, and comparator-equivalent key
objects are one logical key class.

## C# reference API

The prototype defines three supporting public types:

```csharp
public readonly struct DeltaMapValue<T>
{
    public static DeltaMapValue<T> Absent { get; }
    public bool HasValue { get; }
    public T Value { get; }
    public static DeltaMapValue<T> Present(T value);
}

public enum PersistentMapChangeKind { Added, Removed, Updated }

public readonly record struct PersistentMapChange<TKey, TValue>(
    TKey Key,
    DeltaMapValue<TValue> Before,
    DeltaMapValue<TValue> After)
{
    public PersistentMapChangeKind Kind { get; }
}
```

`PersistentDeltaMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`. Its surface is:

| Group | Members |
| --- | --- |
| Construct | `Empty`, `Create(keyComparer, valueComparer)`, `CreateRange(entries, ...)` |
| Current state | indexer, `Count`, `IsEmpty`, `Keys`, `Values`, `ContainsKey`, `TryGetValue`, enumeration |
| Point mutation | `SetItem(key, value)`, `Remove(key)` |
| Epoch control | `Checkpoint()`, `Rollback()` |
| Change query | `ChangeCount`, `HasChanges`, `TryGetChange`, `GetChanges()` |
| Ordered query | `MinEntry`, `MaxEntry`, `EntryAt`, `IndexOfKey`, floor/ceiling/lower/higher, `GetRange` |
| Snapshot escape | `CurrentSnapshot`, `CheckpointSnapshot`, `ToArray` |
| Policy | `Comparer`, `ValueComparer` |

There is deliberately no mutating `Clear`, range update, union, merge, arbitrary rebase, or
`Diff(otherVersion)`. `GetRange` is a read that returns a plain current-state snapshot; it does not
create a delta-map epoch for the range.

## Representation invariant

The concrete representation is:

```text
Version = (current: SortedDictionary<K,V>,
           checkpoint: SortedDictionary<K,V>,
           changes: SortedDictionary<K,DeltaRecord<V>>,
           valueComparer: IEqualityComparer<V>)

DeltaRecord<V> = (before: Endpoint<V>, after: Endpoint<V>)
```

All three ordered maps use the same key comparer. A valid version satisfies:

1. **Exact support.** `D` has an entry for a key class `q` iff the optional values `B(q)` and `S(q)`
   are not endpoint-equivalent.
2. **Exact endpoints.** For every `q ∈ dom(D)`, `D[q].before` is endpoint-equivalent to `B(q)` and
   `D[q].after` is endpoint-equivalent to `S(q)`.
3. **No zero records.** No record has endpoint-equivalent `before` and `after`; in particular,
   `(Absent, Absent)` is impossible.
4. **One sorted record per class.** `D` is unique by `≡K`, and in-order traversal yields the change
   records in `C_K` order.
5. **Representative stability.** If a changed class existed in `B`, its checkpoint key
   representative is retained through updates and delete/re-add operations. If it was absent in
   `B`, the first insertion representative is retained while that class remains continuously
   net-changed. Cancellation removes all memory of that absent class; a later insertion starts a
   new change episode and may choose a new representative.
6. **Clean-root canonicalization.** When `D` becomes empty, the prototype reuses the checkpoint
   root for `S`. Thus a wholly cancelled epoch is reference-clean as well as extensionally clean.
7. **Persistence.** Every referenced map is immutable. An operation returns either the same version
   for a semantic no-op or a new wrapper; it never mutates any retained version.

The internal `ValidateInvariants()` method checks the three constituent map invariants, every
record's endpoints, both directions of exact support, and cardinality. It is diagnostic validation,
not part of the public API or operation cost.

The distinction between values and their comparer classes is load-bearing. Suppose a checkpoint
stores value object `v0`, the value comparer says `v0 ≡V v1`, and another key remains dirty. A
cancelled update may leave `v1` as the current representative while omitting that class from `D`.
That is correct under the declared semantic policy. It would not be correct under object identity.

## Algorithms

The pseudocode uses presence-preserving lookup, including the stored key representative.

### Set one item

```text
SetItem(q, v):
    current? = lookupEntry(S, q)
    if current? is Present((storedKey, old)) and old ≡V v:
        return this

    delta? = lookupEntry(D, q)
    representative =
        if current? then current?.storedKey
        else if delta? then delta?.storedKey
        else q

    S' = S.set(representative, v)
    before =
        if delta? then delta?.record.before
        else if current? then Present(current?.old)
        else Absent
    after = Present(v)

    D' =
        if before ≡Endpoint after then D.remove(representative)
        else D.set(representative, (before, after))

    if D'.isEmpty:
        S' = B
    return Version(S', B, D', C_V)
```

The first effective write captures `before`; the `delta?` branch preserves it thereafter. Every
effective write replaces `after`. Comparing the endpoints implements cancellation.

### Remove one item

```text
Remove(q):
    current? = lookupEntry(S, q)
    if current? is Absent:
        return this

    delta? = lookupEntry(D, q)
    representative =
        if delta? then delta?.storedKey
        else current?.storedKey

    S' = S.remove(current?.storedKey)
    before =
        if delta? then delta?.record.before
        else Present(current?.old)
    after = Absent

    D' =
        if before ≡Endpoint after then D.remove(representative)
        else D.set(representative, (before, after))

    if D'.isEmpty:
        S' = B
    return Version(S', B, D', C_V)
```

### Checkpoint, rollback, and change enumeration

```text
Checkpoint():  return Version(S, S, empty(C_K), C_V)
Rollback():    return Version(B, B, empty(C_K), C_V)
GetChanges():  in-order traverse D and expose (key, before, after)
```

The clean case returns the same wrapper. Otherwise checkpoint and rollback allocate only a constant
size wrapper and empty-map root; all state storage is shared. A checkpoint successor does not alter
the epoch of its parent or sibling branches.

## Correctness proof sketches

### Exactness by induction

The empty constructor and `CreateRange` establish `B = S` and `D = ∅`, so all invariants hold.
Assume they hold before a point operation on class `q`.

For every class other than `q`, neither state endpoint nor its delta record changes. For `q`, there
are two cases:

- If `D` already contains `q`, the induction hypothesis says its `before` is equivalent to `B(q)`.
  The algorithm retains that endpoint and replaces `after` with the new `S'(q)`.
- If `D` does not contain `q`, exact support says `S(q)` is endpoint-equivalent to `B(q)`. Capturing
  `S(q)` therefore captures a valid checkpoint-relative `before`, even if it is a different but
  `≡V`-equivalent object representative.

The final endpoint comparison inserts the record exactly when `B(q)` and `S'(q)` differ, and removes
it otherwise. Therefore exact support, exact endpoints, and the absence of zero records are
preserved. Ordered-map uniqueness preserves one record per `≡K` class and sorted output.

### First-before/final-after coalescing

Once a class enters `D`, all later operations take `before` from the existing record. No later write
can replace it. Every effective operation sets `after` to the new state endpoint. By induction over
the writes in an epoch, a surviving record has the first checkpoint-relative endpoint and the final
current endpoint, independent of the number of intermediate writes. If the endpoints become
equivalent, the record disappears, so a complete round trip produces no false change.

### Change classification

The no-zero invariant leaves exactly three endpoint shapes:

- `Absent → Present(v)`: `Added`;
- `Present(v) → Absent`: `Removed`;
- `Present(v0) → Present(v1)` with `v0 ≢V v1`: `Updated`.

Both-absent is impossible, and both-present equivalent values are cancelled.

### Full persistence and failure atomicity

The three roots and wrapper fields are immutable. Any retained version can therefore be updated,
including a version with children that were already derived from it; the result is a new branch in
the version tree. This is full persistence, not confluent persistence: the structure does not merge
two branch heads.

All comparer calls and persistent-map operations finish before a successor is returned. If a key or
value comparer throws, or allocation fails, no source object has been modified. Work allocated before
the exception may become garbage, but every previously reachable version remains valid. This is the
prototype's failure-atomicity argument; comparers with changing answers can still violate the
semantic preconditions.

### Output optimality

The result contains exactly `k` records. Any API that materializes or visits every record has an
information-theoretic `Ω(k)` output cost, plus constant call/enumerator overhead. An in-order walk of
`D` takes `Θ(k + 1)`, hence is output-optimal.

## Exact complexity

The table assumes an ordered-map substrate with worst-case logarithmic search, point update, split,
and path copying; constant-time stable key/value comparison; and enough address space that size
overflow is not the limiting operation. `m` is the number of input entries to `CreateRange`, `r` is
the number of current entries enumerated by a consumer, and “fresh retained space” excludes the
user's returned `k` output records.

| Operation | Time | Fresh retained space in C# path-copy prototype | Notes |
| --- | ---: | ---: | --- |
| `Empty`, `Create` | `O(1)` | `O(1)` | Empty roots |
| `CreateRange(m)` | `O(m log m)` | `O(m)` | Stable sort; last comparator-equivalent entry wins |
| `Count`, `ChangeCount` | `O(1)` amortized | `O(1)` | A fresh finger-tree spine may force memoized deferred work |
| `IsEmpty`, `HasChanges` | `O(1)` | `O(1)` | Root/empty checks |
| `CurrentSnapshot`, `CheckpointSnapshot` | `O(1)` | `O(1)` | Return immutable roots |
| `MinEntry`, `MaxEntry` | `O(1)` | `O(1)` | Current state only |
| indexer, `ContainsKey`, `TryGetValue` | `O(log N)` | `O(1)` | Current state only |
| `EntryAt`, `IndexOfKey`, neighbor query | `O(log N)` | `O(1)` | Current state only |
| `GetRange(low, high)` | `O(log N)` | `O(log N)` path structure | Returned plain snapshot shares untouched structure |
| semantic no-op `SetItem` | `O(log N)` | `O(1)` | Returns the same wrapper |
| absent `Remove` | `O(log N)` | `O(1)` | Returns the same wrapper |
| effective `SetItem` or `Remove` | `O(log N + log(k + 1)) = O(log N)` | `O(log N + log(k + 1)) = O(log N)` | Updates `S` and at most one entry of `D` |
| `Checkpoint`, `Rollback` | `O(1)` | `O(1)` | Root sharing; clean input returns itself |
| `TryGetChange` | `O(log(k + 1))` | `O(1)` | Searches `D`, not `S` |
| create `GetChanges()` enumerable | `O(1)` | `O(1)` | Lazy |
| fully consume `GetChanges()` | `Θ(k + 1)` | `O(1)` persistent storage | Exactly `k` sorted records; iterator stack is substrate-local |
| enumerate `r` current entries | `Θ(r + 1)` after iterator setup | `O(1)` persistent storage | Same traversal as `SortedDictionary` |
| `ToArray` | `Θ(s)` | `Θ(s)` | Copies current entries |

At a single live version, the three roots retain `O(N + k)` logical payload/node storage in the
worst case: two `O(N)` state snapshots and a `k`-entry delta index, with structural sharing often
reducing the physical total. If a program retains every effective point successor, each successor
adds `O(log N + log(k + 1)) = O(log N)` path nodes, the same asymptotic version-retention cost as an
ordinary path-copied ordered map. The delta index can approximately double the path-copy constant
and stores another key plus two endpoint payload references per changed class.

The bound does not count the cost of executing user comparers or retaining large value graphs. `D`
stores value references/values as supplied; it does not deep-copy them.

## Why the change query is asymptotically better

### State-only merge

Merging the sorted enumerations of `B` and `S` finds an exact map difference in `Θ(N)` time even
when `k = 1`. It uses no history and works between arbitrary maps, but ignores structural sharing.

### Pointer-pruned structural diff

A sharing-aware differ skips reference-equal subtrees. Rust's `im::OrdMap::diff`, for example,
documents time proportional to the elements in nodes unique across the operands after shared nodes
are subtracted. This is often excellent, but a point edit necessarily path-copies its search spine.

For a concrete adversary, take a complete fixed-fanout path-copied search tree with `N` leaf key
classes and change `k` leaves spaced evenly through key order. The union of their copied
root-to-leaf paths contains:

```text
Θ(k)                         nodes above the point where the paths separate
+ Θ(k log(N / k + 1))       nodes in the k disjoint lower spines
= Θ(k log(N / k + 1))       unshared frontier nodes
```

A differ whose only unchanged-subtree certificate is node identity must inspect that frontier to
locate and classify the changed leaves. With `k = 1`, it follows one unshared path of
`Θ(log N)` nodes. `PersistentDeltaMap` already paid for the `D` update during `SetItem`/`Remove`, so
the later complete change walk is `Θ(k + 1)`: `Θ(1)` for one record and a
`Θ(log(N / k + 1))` factor improvement in this sparse adversarial family.

This is deliberately **not** claimed as a universal lower bound for every state representation.
Canonical hashes, per-version logs, change summaries, or another auxiliary index add information
beyond bare path-copy identity. `PersistentDeltaMap` is itself such an auxiliary-index design.

### Canonical and confluent trees

Hash-consed canonical treaps can skip extensionally equal subtrees even without shared construction
history. Liljenzin's confluent persistent maps give expected
`O(k log(N / k))` commit/refresh cost for `k` modifications in a flow and `N` total items. Blelloch
and Reid-Miller give optimal expected `O(m log(n / m))` work for treap set operations on independent
input sizes `m ≤ n`. These structures solve a broader problem—arbitrary operands and confluent
combination—and their expected bounds are not a defect.

The checkpoint-differential map solves a narrower online problem. Its explicit checkpoint lets it
replace comparison search during the later change query with a maintained `k`-entry answer. “Just
Join” and the classic set-operation results establish a `Θ(m log(n / m + 1))` comparison-model bound
for general ordered set operations, where `m` is the smaller **input size**, not this note's output
size `k`. They do not contradict `Θ(k)` here because the checkpoint-differential map stores extra
history-dependent information while edits occur.

### Cancellation case

A state-only path-copied map can end an `A → B → A` round trip with a fresh but extensionally equal
root, leaving an unshared path for a later structural equality/diff. A delta record cancels as soon
as its endpoints agree. If the whole epoch is clean, the prototype also assigns `S = B`, so both
`GetChanges()` and root-identity equality finish in `O(1)`.

## No asymptotic regression on the admitted surface

Relative to an ordinary persistent ordered map:

- state lookup, rank, neighbors, range extraction, and state enumeration delegate to the same root;
- an effective point operation adds one `O(log(k + 1))` delta-tree operation to an existing
  `O(log N)` state-tree operation, and `k ≤ N`;
- snapshot/branch, checkpoint, and rollback are root operations;
- the C# implementation's retained space per edit remains `O(log N)`;
- semantic no-ops and absent removals return the identical object.

This parity statement is only for the operations admitted above. It does not compare an operation
that this prototype omits, nor claim arbitrary-version diff parity.

## Persistence and space constructions

### C# reference prototype: path copying

The prototype composes two instances of the repository's measured-finger-tree
`SortedDictionary`: one for `S` and one for `D`; `B` is a shared state root. It is intentionally
straightforward:

- immutability gives full branching persistence;
- `Checkpoint` and `Rollback` change roots in `O(1)`;
- point edits copy paths in both affected trees;
- a live version has a constant-size wrapper;
- retained ancestors and siblings remain independently usable.

This is the implementation described by the complexity table and is the only implementation present
in this repository at drafting time.

### DSST theoretical construction

Driscoll, Sarnak, Sleator, and Tarjan (DSST) show a fully persistent red-black-tree construction with
`O(log N)` access/update time and `O(1)` worst-case space per insertion or deletion, using displaced
changes; their earlier node-splitting construction gives related amortized bounds. Instantiating
both `S` and `D` with that core construction preserves the delta-map time bounds and, because one
logical edit performs only a constant number of tree insertions/deletions, gives `O(1)` additional
tree space per effective point edit in the core ordered-map model. The wrapper and epoch operation
remain `O(1)`.

That result is a construction option, **not a property of the C# prototype**. There is also an
augmentation caveat: the prototype exposes rank/select through cached subtree counts. Updating
ordinary subtree-size fields changes every ancestor on an insertion or removal, so a direct
DSST simulation would spend `Θ(log N)` modifications and lose the `O(1)`-space refinement.
Therefore the DSST space claim applies immediately only to the core search/update/predecessor/
successor/ordered-iteration surface. Preserving `EntryAt` and `IndexOfKey` with `O(1)` update space
would require a separate augmented-tree construction and proof. The safe shipment bound for the
full current C# API remains path-copy `O(log N)` space per effective edit.

## The point-API and `Clear` boundary

For the explicit-record invariant, clearing a checkpoint containing `N` keys creates `N` net
removals. Eagerly maintaining `D` therefore costs `Θ(N)` time and space. An ordinary immutable map
can expose root-dropping `Clear` much more cheaply, so adding that method naïvely would violate the
“no asymptotic regression” premise.

The prototype consequently omits `Clear` and all bulk mutations. This is an API restriction, not a
claim that every possible bulk-aware design is impossible. A lazy “clear marker plus exceptions” or
complement delta could make some bulk histories cheap, but it would be a different representation:
it must still enumerate all removals when `k = Θ(N)`, handle reinsertions and cancellations without
scanning `B` when final `k` is small, preserve sorted output, and be proved under branching. That is
future research rather than an undocumented fast path.

`Create()` is an `O(1)` way to start an unrelated empty epoch when no old-to-new patch is required.
It is not semantically equivalent to clearing while retaining the old checkpoint.

## Related work and closest precedents

The following table links directly to papers, implementation documentation/source, vendor
documentation, or the patent text. “Difference” means only that the cited artifact does not establish
the full guarantee package studied here; it is not a criticism of a broader or differently scoped
system.

| Primary source | Relevant overlap | Difference from this proposal |
| --- | --- | --- |
| Driscoll, Sarnak, Sleator, Tarjan, [*Making Data Structures Persistent*](https://www.cs.cmu.edu/~sleator/papers/making-data-structures-persistent.pdf), JCSS 1989 | Full persistence; persistent red-black trees with logarithmic operations and constant update space constructions | Persistence substrate, not checkpoint-relative ordered before/after enumeration |
| Allain et al., [*Snapshottable Stores*](https://cs.nyu.edu/~am15509/publications/allain-clement-moine-scherer-24.pdf), PACMPL/ICFP 2024 | Explicit snapshots; record elision keeps at most one undo record per modified location between snapshots | Mutable rerooted store; restore is path/`Δ` work for persistent snapshots; no comparison order or public exact final-after map diff |
| replikativ, [`persistent-sorted-set` diff buffering](https://github.com/replikativ/persistent-sorted-set/blob/main/doc/diff-buffering.md) | Closest implementation precedent: immutable sorted B-tree, comparator-keyed `PersistentTreeMap` diffs, net latest-wins `Present`/`Absent`, durable anchors | Internal store/restore write-amplification mechanism; bounded per-child buffers may flush; set effects record final presence but not a public checkpoint-relative first-before/final-after map stream or `Rollback` API |
| RocksDB, [`WriteBatchWithIndex` wiki](https://github.com/facebook/rocksdb/wiki/Write-Batch-With-Index), [header](https://github.com/facebook/rocksdb/blob/main/include/rocksdb/utilities/write_batch_with_index.h), and [`RollbackToSavePoint` implementation](https://github.com/facebook/rocksdb/blob/main/utilities/write_batch_with_index/write_batch_with_index.cc#L1143-L1154) | Base-plus-local-write overlay, comparator-ordered index, latest entry per key with `overwrite_key`, merged and point reads, savepoints | Mutable and non-branchable; underlying batch retains operations; exposed entries lack checkpoint `Before` and extensional `A → B → A` cancellation; rollback rebuilds the index by replaying retained records |
| DFINITY, [`PageMap` source](https://github.com/dfinity/ic/blob/master/rs/replicated_state/src/page_map.rs) | Cheaply cloned checkpoint-backed state plus a persistent sorted `PageDelta`; dirty final pages enumerate in `O(k)` | Page-granularity state engine rather than a comparison-map API; records final dirty pages without checkpoint `Before` endpoints or extensional cancellation when bytes return to baseline, and does not expose this per-version checkpoint/rollback contract |
| Rust `im`, [`OrdMap::diff` source and complexity contract](https://docs.rs/im/latest/src/im/ord/map.rs.html#389-406) | Ordered add/remove/update diff; skips shared nodes | Reconstructs from two state roots; cost depends on unshared nodes rather than only output `k` |
| Liljenzin, [*Confluently Persistent Sets and Maps*](https://arxiv.org/abs/1301.3388) | Fully/confluently persistent canonical maps; equal-subtree skipping; expected `O(m log(n/m))` flow merge | Broader arbitrary-branch commit/refresh problem; randomized/hash-consed representation and expected bound |
| Blelloch and Reid-Miller, [*Fast Set Operations Using Treaps*](https://www.cs.cmu.edu/~scandal/papers/treaps-spaa98.pdf) | Fully persistent expected-optimal ordered set union/intersection/difference | General two-input set operations, not an online explicit-checkpoint before/after index |
| Blelloch et al., [*Just Join for Parallel Ordered Sets*](https://doi.org/10.1145/2935764.2935768) | `Θ(m log(n/m + 1))` comparison-model set-operation work | Establishes the general ordered-set baseline; this proposal changes the problem by maintaining history online |
| Dolt, [prolly-tree architecture](https://www.dolthub.com/docs/architecture/storage-engine/prolly-tree/) and [structural-sharing caveat](https://www.dolthub.com/blog/2024-01-19-structural-sharing-with-schema-changes/) | Content-defined chunks, structural sharing, fast version diff | Hash/content-defined chunking; documented behavior depends on retained sharing and can approach full scan when sharing is lost; not deterministic comparison-model worst-case |
| Styx, [delta maps for snapshotting](https://pure.tudelft.nl/ws/portalfiles/portal/304132791/s00778-026-00971-x.pdf), VLDB Journal 2026 | A hash-table delta map records changes between snapshot intervals | Snapshot/compaction and recovery have linear components; unordered system snapshot mechanism, not a fully branching ordered map with public exact before/after changes |
| Microsoft SQL Server, [`cdc.fn_cdc_get_net_changes`](https://learn.microsoft.com/en-us/sql/relational-databases/system-functions/cdc-fn-cdc-get-net-changes-capture-instance-transact-sql?view=sql-server-ver17) | One net row per changed source key over an LSN interval; repeated changes coalesce | Database log query with system-specific operation images, not a persistent in-memory map or asymptotic DS construction |
| Neo4j, [CDC output schema](https://neo4j.com/docs/cdc/current/procedures/output-schema/) | Before/after state and cancellation of non-changes | CDC event stream; output order is not guaranteed and it does not provide the point-map complexity package |
| [US 7,096,331, *System and method for managing data associated with copying and replication procedures in a data storage environment*](https://patents.justia.com/patent/7096331) | Explicitly defines a disk-resident “Persistent Delta Map” tracking divergent storage extents | Bitmap/storage-recovery meaning, not a generic persistent ordered map; decisive evidence that the name itself is not novel |
| [US 7,720,817 B2, official PDF](https://ptacts.uspto.gov/ptacts/public-informations/petitions/1460522/download-documents?artifactId=zgluMcMfnwsla-d8d8Uy4qA2vkmxZbah-dZdBqDbTVoCOQNuO_PpniU) | Snapshot delta maps track changes between two times, are sorted by primary address, persist with snapshots, and can be merged | Block-recovery/log index; no fully persistent functional map API, presence-safe value endpoints, or stated `N`/`k` bounds |
| [US 2002/0165724 A1, *Method and system for propagating data changes through data objects*](https://patents.justia.com/patent/20020165724) | Per-period net changes, before/after values, first/last transaction metadata, merging changes | Strong evidence that broad before/after coalescing is prior art; database propagation context, unordered net output, no fully persistent ordered-map bounds |

The persistent-sorted-set, RocksDB, and DFINITY entries are especially important. They mean this
proposal cannot honestly claim invention of “ordinary tree/base plus ordered delta,” latest-wins
keyed coalescing, ordered write overlays, or checkpoint-backed dirty-page iteration. SQL/Neo4j CDC
and US 2002/0165724 preclude a broad claim to net changes or before/after capture. Snapshottable
Stores preclude a broad record-elision claim. US 7,096,331 precludes novelty in the name
`PersistentDeltaMap`; the research label
“checkpoint-differential ordered map” is used here to avoid trading on that phrase.

## Scoped novelty statement

The candidate contribution is the **contract and bound combination**, not the type name or any
component technique:

> A checkpoint-differential, fully persistent comparison-ordered point map in which each version
> owns one explicit checkpoint, checkpoint and rollback take `O(1)`, point reads and writes retain
> ordinary `O(log N)` bounds, and exact presence-safe first-before/final-after net changes are
> available once each in deterministic key order in worst-case `Θ(k + 1)` time.

As of the targeted search on 2026-07-25, no primary source in the table was found to expose and
analyze all of those properties together. That is the strongest defensible novelty wording. It is
not a claim that no such implementation exists, not a legal opinion, and not enough by itself to
establish academic novelty. A broader database, source-code, patent, and citation search could
invalidate it. If a prior ordered map with the same public semantics and bounds is found, the
remaining contribution is only a C# reference composition, invariant/proof package, and empirical
evaluation.

The asymptotic success criterion is independent of that historical question: against the specified
state-only path-copy and general ordered-set baselines, the maintained index changes a sparse
checkpoint-relative change query from logarithmic-per-region reconstruction to output-optimal
`Θ(k + 1)` without worsening admitted point-operation Big-O bounds.

## Limitations and nonclaims

- **One explicit baseline per version.** `GetChanges()` is only from that version's checkpoint to
  its current state. It cannot answer a diff between arbitrary retained versions in `Θ(k)`.
- **No confluent merge.** Branches are independently updateable, but merging two heads needs a
  separate conflict policy and algorithm.
- **Point mutations only.** `Clear`, bulk set/remove, union, and range mutation are outside the
  parity claim.
- **Two comparer-defined semantics.** Exactness is extensional under the supplied policies.
  Mutable, incoherent, or side-effectful comparers invalidate ordinary sorted-map guarantees too.
- **No durable log.** The C# roots are in-memory objects. There is no serialization, crash recovery,
  LSN, or cross-process checkpoint identity.
- **No thread-safe mutation object.** Immutable versions support concurrent reads when comparers do;
  there is no shared atomic head or compare-and-swap API.
- **Payload retention.** A delta retains both endpoint values until checkpoint/rollback or version
  reclamation; large object graphs can dominate node costs.
- **C# only.** API names, exception behavior, allocation constants, and finger-tree details are
  reference-prototype facts, not cross-language contracts.
- **No universal diff lower bound.** The adversary applies to state-only fixed-fanout path copying
  with node identity as the skip certificate. Other maintained metadata changes the problem.
- **No peer-reviewed result yet.** Proof sketches need formalization and the claimed bound package
  needs independent review.

## Validation plan

The companion C# test file already covers the three change kinds, nullable presence, repeated-write
coalescing and cancellation, sorted output, root-sharing checkpoint/rollback, retained branches,
representative episodes, custom value equivalence, 3,000 deterministic randomized model steps,
baseline-size-independent change enumeration, and ordinal comparer-failure injection. Those tests
are reference-prototype evidence, not a proof of the complexity theorem. Recorded validation on
2026-07-25 is 15/15 focused delta-map cases, 739/739 complete FingerTree cases, and 1,545/1,545
complete serialized C# solution cases in both Debug and Release. The public library build completes
with zero warnings and zero errors. Benchmarks were not run: the asymptotic claim rests on the
representation proof and deterministic comparison-count guard, not wall time. The remaining plan
below also records stronger property, invariant, allocation, adversarial-comparison, and integration
work.

### Deterministic semantic tests

1. Distinguish `Absent` from `Present(null)` for additions, removals, and updates.
2. Verify `Added`, `Removed`, and `Updated` endpoint shapes.
3. Write the same class repeatedly and assert first `Before`, final `After`, one record.
4. Exercise `add → update → remove`, `remove → re-add`, and `A → B → A` cancellation.
5. Assert `GetChanges()` is strictly key-comparer ordered and contains no duplicate key class.
6. Assert semantic value no-ops and absent removals return the identical wrapper.
7. Assert checkpoint and rollback produce clean versions, preserve snapshots, and invoke no key or
   value comparer callbacks in the already-clean case.
8. Retain parents and sibling branches; update and checkpoint each independently.
9. Use a comparer with multiple object representatives per class and verify the representative rule,
   including the new-episode rule after an absent-baseline cancellation.
10. Throw from key and value comparers at each observable callback position; assert the source and
    all retained branches still enumerate and pass invariants.

### Randomized model checking

Maintain a simple model pair `(B_model, S_model)` under the same key and value policies. After each
random `SetItem`, `Remove`, `Checkpoint`, or `Rollback`:

- derive the expected exact diff by a full sorted merge of the model maps;
- compare every key, presence bit, before value, after value, kind, and order with `GetChanges()`;
- compare `ChangeCount`, `HasChanges`, current enumeration, rank, and neighbor queries;
- call `ValidateInvariants()`;
- occasionally fork from an old retained version and continue on both branches.

Use seed replay and shrinking. Include value types, reference types, nullable values, reverse order,
case-insensitive key order, and value comparers coarser than object equality.

### Complexity and allocation validation

- Wrap the key comparer with a counter. For powers-of-two `N`, verify lookup and effective point
  update comparison counts grow logarithmically.
- Hold `N` fixed and vary `k`; verify `TryGetChange` grows with `log(k + 1)`, not `log N`.
- Fully enumerate changes and assert exactly `k` yields with no key/value comparisons during the
  in-order walk.
- Construct evenly spaced changed keys for `k ∈ {1, 2, 4, ..., N}` and compare visited-node/
  comparison counts against a state-only structural differ and a full ordered merge.
- Measure fresh allocations for no-op, point edit, checkpoint, rollback, and cancellation. Confirm
  no-op identity and bounded checkpoint/rollback allocation; fit point-edit allocation to
  `log N + log(k + 1)`.
- Retain all versions during allocation tests so garbage collection cannot hide per-version space.

Comparison counts and allocation fits validate asymptotic trends; they do not prove them. The proof
depends on the substrate contract and representation invariant.

### Integration gates

Before treating the prototype as a supported C# feature:

- extend the focused tests with invariant checks, randomized branching, rank/neighbor delegation,
  and seed shrinking;
- run the complete C# solution tests, analyzers, XML-documentation build, and public API checks;
- document whether `GetChanges()` iterator exception/current/reset behavior follows the repository's
  collection contract;
- benchmark against full merge and a sharing-aware structural diff at sparse and dense `k`;
- perform an independent literature and proof review;
- make a separate decision about naming, namespace ownership, and whether any consumer justifies a
  supported shipment.

Cross-language ports, parity matrices, catalog entries, and release claims occur only after those
gates and an explicit shipment decision. This document does not make that decision.

## Research extensions

Three follow-ups are potentially worthwhile but are separate structures:

1. **Arbitrary-version deltas.** Add persistent epoch identifiers and compose deltas along a version
   tree, with careful cancellation and an LCA query. The challenge is keeping arbitrary pair output
   sensitive without making updates or version metadata asymptotically worse.
2. **Bulk/complement deltas.** Represent `Clear` and large ranges lazily, switching between sparse
   changed-key and sparse unchanged-key forms while preserving sorted output and branching.
3. **Space-refined core.** Implement the two ordered roots with DSST displaced-change red-black trees
   and validate `O(1)` update space on the restricted non-rank surface; study whether rank/select can
   be recovered without ancestor-size rewrites.

Each extension needs its own invariant, adversary, and related-work review. None is required for the
reference prototype's current asymptotic change-enumeration result.
