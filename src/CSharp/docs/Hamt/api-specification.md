# HAMT API Specification

- Status: Implemented
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers and reviewers of `Durable7.Hamt`
- Scope: Public API, persistence semantics, and complexity guarantees

For practical namespace, comparer, persistent update, and set-algebra examples, start with the
[usage guide](usage.md).

## Overview

`PersistentHashMap<TKey, TValue>` is an immutable unordered dictionary backed by a hash-array mapped
trie. Each update returns a new map version and preserves the old version unchanged. Untouched
subtrees are shared by reference, so retaining many versions costs only the changed search paths plus
new or replaced leaves.

`PersistentHashSet<T>` is a value set built on the same HAMT core. It preserves the same comparer and
structural-sharing semantics as the map. Both persistent CHAMP types expose a C#-only, one-way
`Transient` session for many single-owner point edits followed by one O(1) publication.

`PersistentHashBag<T>` is the immutable unordered multiset in the same project. It stores one
positive multiplicity per comparer equivalence class in `PersistentHashMap<T, int>` and tracks the
expanded occurrence count separately as a `long`. It has no mutable builder or transient surface.

`PersistentHashMultimap<TKey, TValue>` is the immutable set-valued multimap in the same project. It
stores nonempty persistent value sets in a persistent map and applies independent equality policies
to the key and value domains.

`PersistentRelation<TLeft, TRight>` is the immutable many-to-many bidirectional relation. It stores
every pair in forward and reverse multimaps and caches an O(1) inverse facade.

`PersistentBiMap<TKey, TValue>` is the immutable bijection facade in the same project. It stores
every pair in forward and inverse CHAMP maps, retains independent key and value comparers, and
exposes an O(1) cached inverse facade without rebuilding either trie.

`PersistentMapPatch<TKey, TValue>`, `PersistentDirectedGraph<TVertex>`, and
`PersistentIndexedMap<TKey, TValue, TIndexKey>` are repository-general derived CHAMP facades. Their
normative presence, adjacency, indexing, policy, failure, and complexity contracts are defined by
[Derived Persistent HAMT Structures](derived-persistent-structures.md).

`ConcurrentHashTrie<TKey, TValue>` is a lock-free mutable map built from bitmap C-nodes, singleton
leaves, collision nodes, and generation-stamped indirection nodes. Its public mutation surface is
linearizable, and `Snapshot()` captures the current Ctrie generation in O(1).

## Hash Trie Shape

The implementation uses CHAMP's 32-way logical branching with 5 hash bits consumed per level. Sparse
branch nodes carry separate 32-bit data and node bitmaps, an inline key/value payload array, and a
compact child-node array; each array index is the population count below the selected bit. Canonical
deletion pulls singleton leaf children into their parent payload run. Unequal keys with identical full hash codes are stored in immutable
collision buckets and compared linearly with the configured equality comparer.

Enumeration order is an implementation detail of the trie shape and comparer hash codes. It is stable
for an unchanged version, but callers must not treat it as insertion order or sorted order. Both
map, set, and bag expose public struct enumerators that keep their entire traversal state inline:
obtaining and draining a concrete enumerator allocates nothing, and a copied enumerator advances
independently of the original.

## Map Contract

`PersistentHashMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`.

- `Empty` returns the shared empty map using `EqualityComparer<TKey>.Default`.
- `Create(comparer)` returns an empty map using a supplied comparer.
- `CreateRange(items, comparer)` adds entries in enumeration order with last-wins semantics.
- `SetItem(key, value)` adds or replaces a key.
- `GetOrAdd(key, addFactory, out value)` returns the current map and stored value on a hit. On a
  miss, it invokes `addFactory` exactly once with the caller's key and returns the successor map and
  selected value.
- `AddOrUpdate(key, addFactory, updateFactory, out value)` invokes exactly one selected factory
  exactly once. The update factory receives the caller's lookup key and stored value while the map
  retains the originally stored key representative.
- `SetItems(items)` adds or replaces entries in enumeration order with last-wins semantics and
  throws `ArgumentNullException` for a null sequence.
- `Add(key, value)` adds a key and throws `ArgumentException` when the key already exists. This is
  stricter than `ImmutableDictionary<TKey, TValue>.Add`, which tolerates re-adding an equal value;
  here any existing equivalent key throws.
- `TryAdd(key, value, out result)` reports duplicate keys without throwing.
- `Remove(key)` removes a key if present and returns the original map when absent.
- `TryRemove(key, out result, out value)` reports whether a key existed and returns the removed value.
- `TryGetKey(equalKey, out actualKey)` recovers the originally stored key object for an equivalent
  key, or echoes `equalKey` back when absent.
- `Clear()` returns an empty map preserving the current comparer, and returns the current instance
  when the map is already empty.
- `MapEquals(other, valueComparer)` uses canonical trie topology for lockstep equality with
  reference-equal subtree pruning. The two maps must retain the same comparer object;
  collision-bucket order is ignored.
- `Diff(other, valueComparer)` structurally aligns logical bitmap slots, skips reference-equal
  subtries, and returns materialized `MapDifference<TKey, TValue>` values classified as `Added`,
  `Removed`, or `Changed`. Null and comparer-mismatch failures occur eagerly. Removed/changed results
  expose the source's stored key representative; added results expose the target's. Result order is
  deterministic for unchanged operands but remains an implementation detail.
- `Union(other)`, `Intersect(other)`, `Except(other)`, and `SymmetricExcept(other)` combine compatible
  maps by logical CHAMP slot. Union is right-value-biased while retaining the receiver's stored key
  representative; intersection retains receiver entries. All four prune reference-equal roots and
  descendants and require the same comparer object.

For maps derived from a shared version, equality and diff visit only non-shared trie regions (plus
reported output). Independently built equal maps still require O(n) comparison because canonical
topology does not make separately allocated nodes reference-equal. Equal-hash collision runs are
matched without regard to order and can require O(c²) key comparisons for a bucket of size c.

`Add`, `TryAdd`, `GetOrAdd`, and `AddOrUpdate` hash the key once and walk the trie once. Persistent
factory updates have no retry loop. `GetOrAdd` invokes no factory on a hit; `AddOrUpdate` invokes
exactly one of its two factories. All factory arguments are validated before hashing, even when the
selected branch would not use one of them. A rejected duplicate, `GetOrAdd` hit, or equal-value
`AddOrUpdate` no-op allocates nothing.
The try-pattern `out` values (`TryGetValue`, `TryRemove`) carry `[MaybeNullWhen(false)]`, matching
the `IReadOnlyDictionary<TKey, TValue>` annotation.

No-op updates preserve instance identity throughout: a `GetOrAdd` hit, an `AddOrUpdate` replacement
that compares equal under `EqualityComparer<TValue>.Default`, replacing an equal value through
`SetItem`, removing an absent key, and clearing an empty map all return the current instance. An
equal factory result retains and reports the stored value object. When a present key is replaced or
re-set, the originally stored key object is likewise retained; use `TryGetKey` to observe it.

Factory, key-comparer, or value-equality failure publishes no successor and leaves the source map
unchanged. A present null value selects the hit/update branch and is never conflated with absence.

The configured comparer defines hash and equality semantics, including any behavior for null keys.
The comparer must honor the usual hash contract: equivalent keys must produce equal hash codes.
Changing comparer behavior after a map has been built has the same undefined practical effect as
mutating a comparer used by `Dictionary<TKey, TValue>`.

## Set Contract

`PersistentHashSet<T>` implements `IReadOnlySet<T>` (and therefore `IReadOnlyCollection<T>`).

- `Empty`, `Create`, and `CreateRange` mirror the map factories.
- `Add`, `TryAdd`, `Remove`, `TryRemove`, `Contains`, and `Clear` mirror map behavior, including
  no-op instance identity.
- `TryGetValue(equalValue, out actualValue)` recovers the originally stored item object for an
  equivalent item, or echoes `equalValue` back when absent.
- `Union`, `Intersect`, `Except`, and `SymmetricExcept` return new persistent sets.
- `IsSubsetOf`, `IsProperSubsetOf`, `IsSupersetOf`, `IsProperSupersetOf`, `Overlaps`, and
  `SetEquals` interpret equality through the set's comparer.

Each algebra and relation has a same-type `PersistentHashSet<T>` overload. Algebra requires the
identical comparer object. Same-comparer overloads operate structurally over stored hashes without
rehashing entries and prune reference-equal roots/subtrees; cross-comparer relations fall back to
the established receiver-comparer `IEnumerable<T>` semantics. Self union/intersection and unchanged
receiver results preserve instance identity; self difference/symmetric difference return the
comparer-preserving empty set. The `IEnumerable<T>` overloads remain the arbitrary-sequence path.

`SetItems` on the map and `Union`/`Except` on the set are the sanctioned bulk updates; there are no
separate `AddRange`/`RemoveRange` members.

`Intersect`, `SymmetricExcept`, `IsSubsetOf`, `IsProperSubsetOf`, `IsProperSupersetOf`, and
`SetEquals` materialize their argument into a probe `HashSet<T>` using the set's comparer — an O(m)
time and space cost documented on each member. `IsSupersetOf` and `Overlaps` stream their argument
and exit early.

## Hash Bag Contract

`PersistentHashBag<T>` is sealed, immutable, and implements `IEnumerable<T>`. Its exact public
surface is:

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

There is intentionally no `Count`, `IReadOnlyCollection<T>`, enumerable algebra overload, public
mutable builder, transient facade, or content-equality override. Expanded occurrence counts may
exceed `int.MaxValue`; exposing them through an `int Count` would be lossy and would disagree with
the meaning of default enumeration.

### Representation, Construction, And Counts

The bag contains an internal `PersistentHashMap<T, int>` from stored representative to
multiplicity plus a cached `long TotalCount`. The following invariants hold for every published
instance:

- the map has exactly one entry per comparer equivalence class;
- every stored multiplicity is in `1 .. int.MaxValue`;
- `DistinctCount` is the map count;
- `TotalCount` is the checked sum of all stored multiplicities; and
- `IsEmpty`, `DistinctCount == 0`, and `TotalCount == 0` are equivalent.

The mathematical maximum valid total is
`int.MaxValue * (long)int.MaxValue`, which is below `long.MaxValue`; internal total arithmetic is
still checked as an invariant guard. The publicly reachable overflow boundary is therefore the
per-class `int` multiplicity, not a valid bag's `long` total.

`Empty`, `Create()`, `Create(null)`, and `Create(EqualityComparer<T>.Default)` return the shared
default-comparer empty bag. This canonicalization is based on comparer reference identity, not
semantic comparer equivalence. An empty bag carrying any other comparer object retains that exact
object through construction, `Clear`, algebra, and every result that becomes empty.

`CreateRange(items, comparer)` throws `ArgumentNullException` before enumerating a null source. It
processes occurrences in input order, retains the first equivalent item as the class
representative, and checks each multiplicity increment. Internally it aggregates through one
single-owner unpublished CHAMP bulk builder and freezes once. The builder's combining insertion
validates its internal delegate before hashing, hashes each item once, scans one full-hash bucket,
invokes the combiner exactly once only on a hit, and commits only after hashing, key equality,
checked increment, and value equality succeed. This is internal construction machinery; no mutable
staging object is part of the bag API or shared with a published bag.

`Contains` tests whether a class exists. `CountOf` returns its positive multiplicity or zero when
absent. `TryGetValue` returns the stored representative on a hit and assigns the caller's
`equalValue` to `actualValue` on a miss, matching the set convention. Comparer behavior determines
whether null is supported; equivalent items must have equal hashes and comparer behavior must
remain stable across the lifetime of all derived versions.

### Point Updates, Identity, And Failure

`Add`/`Remove` change one occurrence. `AddCopies` adds the requested count; `RemoveCopies` performs
saturated subtraction and removes the class rather than storing zero. `RemoveAll` removes the whole
class in one map descent and obtains the removed multiplicity from that operation. An absent
addition stores the caller's item, while an update of an existing class retains the stored
representative.

Negative copy counts are rejected with `ArgumentOutOfRangeException` before any hash or equality
callback. Zero-copy additions/removals return the exact receiver without hashing. Removing a
missing class, clearing an empty bag, and any complete algebra result logically equal to the
receiver are also identity-preserving no-ops. Positive `AddCopies` uses the map's one-descent
`AddOrUpdate`; positive `RemoveCopies` may first look up the multiplicity and then perform one map
update, but rebuilds at most one changed path.

Adding beyond `int.MaxValue` copies in one class throws `OverflowException` before a successor is
returned. Hash, equality, source-enumerator, or checked-arithmetic failure likewise exposes no
partially published bag and leaves every input version unchanged. Untouched CHAMP subtrees are
shared normally by successful changed updates.

### Algebra And Comparer Normalization

The four same-type operations combine class multiplicities as follows:

| Operation | Result count for one receiver-policy class |
| --- | --- |
| `Union` | `max(receiverCount, argumentCount)` |
| `Intersect` | `min(receiverCount, argumentCount)` |
| `Except` | `max(0, receiverCount - argumentCount)` |
| `Sum` | `checked(receiverCount + argumentCount)` |

The receiver's comparer always governs the result. A surviving receiver class keeps its receiver
representative. `Union` and `Sum` introduce an argument representative only when that class is
absent from the receiver; `Intersect` and `Except` never introduce one.

When the comparer objects are reference-identical, the argument already contains one entry per
receiver class and no normalization is needed. Otherwise the implementation eagerly normalizes all
distinct argument entries under the receiver comparer immediately after null validation and before
operation-specific empty, self, or identity shortcuts. Argument classes that collapse under the
receiver policy contribute the checked sum of their multiplicities. The first representative
encountered in the particular argument version's stable-but-unspecified distinct trie order becomes
the normalized representative. Two logically equivalent versions may therefore choose different
normalized representatives if their observed trie orders differ; map order itself is not semantic.

Eager normalization makes its failures observable even when, for example, an empty receiver makes
the mathematical intersection obvious. Comparer hashing/equality and checked-collapse exceptions
leave both operands unchanged. Algebra then derives immutable intermediate CHAMP versions, so a
later per-class `Sum` overflow is likewise failure-atomic. This initial implementation combines
distinct entries element by element and retains shared subtrees where ordinary map updates permit;
it does not claim the map's structural lockstep algebra bound.

Logical no-op results return the receiver instance, including `Union(this)` and `Intersect(this)`.
`Except(this)` returns the receiver-comparer empty bag. `Sum(this)` performs checked doubling and
can overflow; it is not an identity case.

### Enumeration, Materialization, And Debugging

Default enumeration is expanded: each representative occurs exactly its multiplicity times, its
copies are contiguous, and the relative order of the first occurrence of every class matches
`DistinctItems` and `Entries`. `DistinctItems` yields one stored representative per class;
`Entries` yields `KeyValuePair<T, int>` pairs. All three orders are stable for an unchanged version
but otherwise unspecified CHAMP order, not insertion or sorted order. The two distinct views are
immutable and version-bound; enumerating either interface view allocates its iterator.

The public expanded `Enumerator` is a mutable struct containing the map enumerator, current
representative, and remaining-copy count. Concrete acquisition and draining allocate nothing, and a
copied value advances independently. `Current` is `default` before the first successful
`MoveNext` and after exhaustion, `Dispose` is a no-op, and explicit `IEnumerator.Reset` throws
`NotSupportedException`. Enumerating through an interface can box the struct.

`ToArray` returns exactly the expanded enumerator order. It checks `TotalCount > Array.MaxLength`
and throws `OverflowException` before allocating; checking only `int.MaxValue` would be insufficient
on the CLR. An empty bag returns `Array.Empty<T>()`. The debugger proxy exposes a distinct
`KeyValuePair<T, int>[]`, never expanded enumeration, so debugger materialization is bounded by
`DistinctCount` even when one multiplicity or the total is very large.

## Persistent Hash Multimap Contract

`PersistentHashMultimap<TKey, TValue>` is sealed, immutable, and implements
`IEnumerable<KeyValuePair<TKey, TValue>>`. Its public surface intentionally distinguishes key-group
count from total distinct-pair count:

```csharp
public sealed class PersistentHashMultimap<TKey, TValue>
    : IEnumerable<KeyValuePair<TKey, TValue>>
{
    public static PersistentHashMultimap<TKey, TValue> Empty { get; }
    public static PersistentHashMultimap<TKey, TValue> Create(
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null);
    public static PersistentHashMultimap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> pairs,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null);

    public int KeyCount { get; }
    public long PairCount { get; }
    public bool IsEmpty { get; }
    public IEqualityComparer<TKey> KeyComparer { get; }
    public IEqualityComparer<TValue> ValueComparer { get; }
    public IEnumerable<TKey> Keys { get; }
    public IEnumerable<KeyValuePair<TKey, PersistentHashSet<TValue>>> Groups { get; }

    public bool ContainsKey(TKey key);
    public bool Contains(TKey key, TValue value);
    public int CountValues(TKey key);
    public PersistentHashSet<TValue> GetValues(TKey key);
    public bool TryGetValues(TKey key, out PersistentHashSet<TValue> values);
    public bool TryGetKey(TKey equalKey, out TKey actualKey);
    public bool TryGetValue(TKey key, TValue equalValue, out TValue actualValue);
    public PersistentHashMultimap<TKey, TValue> Add(TKey key, TValue value);
    public bool TryAdd(TKey key, TValue value, out PersistentHashMultimap<TKey, TValue> result);
    public PersistentHashMultimap<TKey, TValue> Remove(TKey key, TValue value);
    public bool TryRemove(TKey key, TValue value, out PersistentHashMultimap<TKey, TValue> result);
    public PersistentHashMultimap<TKey, TValue> RemoveKey(TKey key);
    public bool TryRemoveKey(
        TKey key,
        out PersistentHashMultimap<TKey, TValue> result,
        out PersistentHashSet<TValue> values);
    public PersistentHashMultimap<TKey, TValue> Clear();
    public KeyValuePair<TKey, TValue>[] ToArray();
}
```

The multimap represents a mathematical set of comparer-distinct pairs: adding an existing pair is
an identity-preserving no-op. `KeyCount` counts nonempty groups and `PairCount` counts distinct
pairs as a `long`; there is no ambiguous `Count`. Each group uses the exact `ValueComparer` object,
and empty groups are forbidden. Removing a group's last value therefore removes the outer key in
the same successor. Absent `GetValues`/`TryGetValues` results are empty persistent sets retaining
the value comparer.

The first key representative survives all updates to its group, and the first value representative
survives equivalent additions within that group. Enumeration visits outer groups in the CHAMP
map's stable-for-one-version order and each group's values in its CHAMP set order. Neither level is
insertion ordered or sorted. A point update copies only the affected outer and inner trie paths and
shares all other nodes. The type deliberately exposes no bag multiplicity, duplicate-pair mode,
algebra, transient, or mutable builder.

## Persistent Relation Contract

`PersistentRelation<TLeft, TRight>` represents a mathematical set of pairs through mutually
inverse `PersistentHashMultimap` indexes. `LeftCount` and `RightCount` report represented domain
classes, while checked `long PairCount` reports the same distinct-pair count in either direction.
`LeftComparer` and `RightComparer` are retained independently and swap roles through `Inverse`.

`Add` is idempotent and `TryAdd` reports whether a new pair was published. Before editing either
index, addition recovers any existing outer left and right representatives and inserts those exact
objects into both adjacency sets. This normalization is necessary because an ordinary nested
multimap retains representatives per group; the relation instead promises one first representative
globally for each represented domain class. A logical duplicate returns the receiver.

`Contains`, `ContainsLeft`, and `ContainsRight` query membership. `GetRights`/`GetLefts` return
persistent comparer-compatible adjacency sets, including comparer-preserving empties for absent
classes. `TryGetRights`/`TryGetLefts`, `CountRights`/`CountLefts`, and
`TryGetLeft`/`TryGetRight` expose presence, degree, and stored representatives without rebuilding a
set. Forward enumeration and `Groups` use stable-for-one-version nested CHAMP order.

`Remove` deletes one pair and automatically contracts any now-empty groups in both indexes.
`RemoveLeft` and `RemoveRight` delete an entire adjacency class; their try forms return the removed
persistent adjacency set. Whole-class removal is O(d log n) for degree d because it removes the
corresponding pair from each inverse group. Failures during construction of either successor remain
local: no partially updated relation is published and every retained source version is unchanged.

`Inverse` swaps the already-existing multimap roots in O(1), performs no pair traversal, is safely
cached for concurrent readers, and satisfies `ReferenceEquals(relation,
relation.Inverse.Inverse)`. The honest space cost is approximately twice a forward multimap because
every pair and adjacency membership occurs in both indexes. The relation exposes no displacement,
bag multiplicity, algebra, transient, or mutable builder.

## Persistent Bimap Contract

`PersistentBiMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>` and maintains two
maps whose entries are exact inverses. `Count`, forward enumeration, `Keys`, `Values`, the indexer,
`ContainsKey`, and `TryGetValue` observe the forward map. `ContainsValue` and `TryGetKey` observe the
inverse map. Enumeration is stable for an unchanged version but otherwise follows unspecified
forward-CHAMP order.

- `Empty` and `Create(keyComparer, valueComparer)` create empty bijections. Key and value comparer
  objects are retained independently by every successor, including empty results.
- `CreateRange` enumerates once and strictly rejects any repeated key class or value class.
- `Add(key, value)` throws when either equivalence class already exists, even if the complete pair
  is equivalent and checks the key domain first. `TryAdd` returns `false` and the exact receiver
  for either conflict; unlike result-rich sibling APIs, it does not identify the conflicting domain.
- `SetItem(key, value)` adds when the key is absent. For an existing key it preserves the stored key
  representative, returns the exact receiver when the configured value comparer considers the old
  and new values equivalent, replaces an unclaimed value, and throws before publication when the
  new value belongs to another key class. Replacement deliberately removes and re-adds both sides:
  calling the map's ordinary `SetItem` would apply `EqualityComparer<TValue>.Default` rather than
  the bimap's configured value policy.
- `RemoveKey`/`TryRemoveKey` and `RemoveValue`/`TryRemoveValue` remove the same pair through either
  domain. An absent removal returns the exact receiver. Try-removal exposes the opposite stored
  representative.
- `Clear` preserves both comparer objects and returns the receiver when already empty.
- `Inverse` swaps the two existing map roots and comparer roles in O(1), performs no enumeration,
  is cached safely for concurrent readers, and satisfies `ReferenceEquals(map,
  map.Inverse.Inverse)`.

The invariant is bidirectional: both maps have the same count; every forward `(key, value)` finds an
equivalent inverse `(value, key)` under the retained policies; and every inverse entry finds the
corresponding forward entry. A public operation constructs the facade only after both successor
maps have been produced. Comparer or allocation failure therefore publishes neither half and leaves
the source and any cached inverse facade unchanged. Retained versions support concurrent reads when
the supplied comparers do.

Point lookup is O(w + c) in its selected direction. Addition and removal perform one bounded HAMT
operation per side after conflict/representative probes; replacement performs remove-plus-add on
both sides to honor independent policies. Space is honestly approximately twice a single map:
every association is stored in both tries. Concrete enumeration wraps the allocation-free forward
map enumerator.

There is intentionally no algebra, builder, transient, displacement mode, or mutable inverse view.
Future algebra would first need a receiver-policy conflict matrix for collisions on both domains.

## CHAMP Transient Contract

The map exposes the following deliberately closed lifecycle surface:

```csharp
public static PersistentHashMap<TKey, TValue>.Transient CreateTransient(
    IEqualityComparer<TKey>? comparer = null);

public PersistentHashMap<TKey, TValue>.Transient ToTransient();

public sealed class PersistentHashMap<TKey, TValue>.Transient
    : IReadOnlyDictionary<TKey, TValue>
{
    public int Count { get; }
    public IEqualityComparer<TKey> Comparer { get; }
    public TValue this[TKey key] { get; }
    public IEnumerable<TKey> Keys { get; }
    public IEnumerable<TValue> Values { get; }

    public bool ContainsKey(TKey key);
    public bool TryGetValue(TKey key, out TValue value);
    public bool TryGetKey(TKey equalKey, out TKey actualKey);
    public void Add(TKey key, TValue value);
    public bool TryAdd(TKey key, TValue value);
    public void SetItem(TKey key, TValue value);
    public bool Remove(TKey key);
    public void Clear();
    public Enumerator GetEnumerator();
    public PersistentHashMap<TKey, TValue> Persist();
}
```

The set exposes `CreateTransient(comparer)`, `ToTransient()`, and a sealed
`PersistentHashSet<T>.Transient : IReadOnlySet<T>` with `Count`, `Comparer`, `Contains`,
`TryGetValue`, bool-returning `Add` and `Remove`, `Clear`, the six `IReadOnlySet<T>` relation methods,
`GetEnumerator`, and `Persist`. Neither surface has range edits, repeated snapshots, `ToImmutable`,
or mutable algebra methods. A reusable builder, if ever added, is a separate type with a different
lifecycle.

`CreateTransient` starts empty under the supplied comparer, or `EqualityComparer<T>.Default` when
the argument is null. `ToTransient` adopts the persistent root without visiting or copying a node.
The map's public nested type is the selected direct separate-node edit engine itself, so adoption
does not pay an additional public facade allocation. The set session is a thin facade over the map
session.

The following lifecycle rules are normative:

- The transient is active until its first successful `Persist()` call. Publication atomically makes
  it inactive and increments its version as part of invalidation.
- After publication, every property read, lookup, mutation, relation query, enumeration request,
  and second publication attempt through any direct or interface alias throws
  `ObjectDisposedException`.
- Map `Keys`/`Values` views and map/set enumerators capture their owner and version. A successful
  changed edit invalidates previously obtained aliases with `InvalidOperationException`; successful
  publication invalidates them with `ObjectDisposedException`. They cannot drain the newly
  persistent graph.
- A logical no-op does not advance the version and does not allocate or copy a path. No-ops include
  duplicate `TryAdd`/set `Add`, absent removal, clearing an empty session, and setting a value equal
  under `EqualityComparer<TValue>.Default`.
- A clean `source.ToTransient().Persist()` returns `source` by reference, including after any number
  of logical no-ops. A clean factory session returns its comparer-preserving empty; the default-
  comparer result is the canonical `Empty` object.
- The transient is unsynchronized and has one logical owner. Sequential transfer between threads is
  permitted only with caller-provided synchronization. Concurrent access is unsupported. The
  retained persistent base remains immutable and concurrently readable.

Transient point edits preserve all persistent CHAMP semantics: comparer identity, comparer-defined
null behavior, the first equivalent key/item representative, equal-value object retention, stable
trie-order enumeration, collision-bucket ordering, recursive counts, branch contraction, and
default/custom empty canonicalization. `Clear()` retains comparer identity. Map and set concrete
enumerators are allocation-free copy-safe structs; interface enumeration may box them. Set relation
queries interpret their argument through the receiver's comparer and have the same probe-
materialization/streaming behavior as the persistent set.

Every point edit has the strong exception guarantee. All potentially throwing hash/equality/value
callbacks and replacement allocations complete before the operation performs an in-place write;
commit consists only of non-throwing reference/field assignments. On failure, contents, root
identity, ownership resources, version, and captured-enumerator validity are unchanged. Publication
likewise prepares its immutable map wrapper and, for a set, its set wrapper before its non-throwing
consume step. A preparation failure leaves the transient active and retryable.

The selected representation keeps ordinary persistent leaf, collision, and bitmap-node layouts free
of owner fields. The first changed edit is performed as an ordinary persistent edit; a later changed
edit promotes only reusable branch/collision paths into exact-type transient-editable nodes. Nodes
and arrays may be changed in place only under the active edit token and explicit array-ownership
bits. Published edited nodes retain a sealed token reference and separate-node metadata; publication
does not traverse the graph to clear tags. A later transient uses a different token and therefore
copies before writing those paths.

## Complexity

Let `w` be the hash width (32 bits), `b` be the branch factor (32), and `c` be the length of an
equal-hash collision bucket.

- Lookup, insert, replace, remove, `GetOrAdd`, and `AddOrUpdate`: O(w / log2(b) + c), effectively
  bounded by seven trie levels plus collision-bucket scan for 32-bit hashes. Lookups and unchanged
  single-pass factory updates allocate nothing.
- Enumeration: O(n) time. The enumerator holds at most seven inline frames (one per trie level) and
  performs no heap allocation.
- Map `CreateRange` / set `CreateRange`: O(n (w + c)) through hash-bucket staging followed by one
  canonical freeze; unlike repeated persistent updates, the build does not clone every traversed path.
- Bag `Contains`, `CountOf`, and `TryGetValue`: O(w / log2(b) + c), allocation-free.
- Bag `Add`, positive `AddCopies`, and `RemoveAll`: O(w / log2(b) + c). A changed update rebuilds
  only its search path. Positive `RemoveCopies` has the same asymptotic bound through at most two
  bounded searches and one rebuilt path; zero-copy calls return in O(1) before hashing.
- Bag `CreateRange`: O(n (w / log2(b) + c)) through checked internal combining insertion followed by
  one canonical freeze.
- Bag expanded enumeration and `ToArray`: O(`TotalCount`) time; `ToArray` also uses
  O(`TotalCount`) result space when the array-length precondition holds. `DistinctItems`, `Entries`,
  and debugger projection are O(`DistinctCount`).
- Bag algebra: O((n + m) (w / log2(b) + c)) element-wise distinct-entry work in the general case,
  where n and m are operand distinct counts. Comparer-mismatched arguments first incur the same
  normalization bound under the receiver policy. Unchanged map paths may be shared, but no
  structural bag-algebra bound is claimed.
- `MapEquals`: O(divergent canonical nodes + collision comparisons), with reference-equal subtrees
  skipped in O(1).
- `Diff`: O(n + m) in the current public implementation, with an O(1) shared-root fast path.
- Same-type CHAMP map/set algebra: O(divergent nodes + result nodes) for shared ancestry and O(n + m)
  for independent operands; reference-equal subtrees are skipped in O(1). Arbitrary `IEnumerable<T>`
  set algebra retains its element-wise/probe-materialization costs.
- Persistent map/set to transient: O(1), with no graph walk. The map allocates its session object;
  the set allocates its thin facade and map session.
- Transient lookup: O(w / log2(b) + c), matching persistent lookup depth and collision work.
- First changed edit of a shared path: O(w / log2(b) + c), copying only that path. A later edit of an
  already owned path has the same time bound and may reuse owned nodes/arrays in place.
- Transient publication: O(1), with no graph walk or ownership-tag clearing. A dirty non-default
  result prepares the required immutable wrapper allocation before consumption.

Update allocation is O(b * depth + c) array storage — O(depth + c) allocated node objects — for the
changed path and any touched collision bucket. Unchanged subtrees remain shared and are safe for
concurrent readers because all node arrays are privately created before publication and never
mutated afterward.

Transient publication trades that O(1) bound for retained edit metadata in graphs that reused
paths. In the pinned T1 100,000-entry/512-edit End lane, the published graph retained 8,488 extra
bytes (0.1971%); Every64 retained 8,544 extra bytes (0.1984%). The sparse one-edit guard retained no
extra graph metadata but cost 88 additional allocated bytes and higher latency. Those figures are
workload evidence, not universal constants: direct persistent updates remain preferable for sparse
one-off edits, and callers retaining many transient-produced versions should account for sealed
metadata rather than assuming publication removes it.

## Concurrent Hash Trie Contract

`ConcurrentHashTrie<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>` while exposing a
mutable, thread-safe update surface:

- `SetItem` and the indexer setter atomically add or replace; equal-value no-ops publish no
  generation, same-reference values bypass user equality, and replacements retain the first
  equivalent stored key object. `AddOrUpdate` and `TryUpdate` apply the same reference-first rule.
- `TryAdd`, `TryUpdate`, and `TryRemove` are single-key conditional atomic operations.
- `GetOrAdd` and `AddOrUpdate` use retryable factories. A factory may run more than once when a CAS
  loses contention, matching the repeatability requirement of `ConcurrentDictionary` factories.
- `Clear` atomically publishes an empty root.
- `Snapshot` and `GetEnumerator` capture one immutable generation. Later writes cannot change their
  contents, and snapshots retain the trie's comparer. Snapshot enumeration matches canonical CHAMP
  order: each bitmap level emits logical singleton payloads in bitmap order before recursively
  emitting multi-entry child nodes. A frozen singleton tomb participates in the payload run at its
  parent, while an equal-hash collision bucket retains its entry order.
- `SnapshotView.ToPersistentHashMap` enumerates the captured generation once and copies it into
  canonical CHAMP form in O(n). The result retains the exact comparer object, enumeration sequence,
  and stored key/value representatives; later live-trie writes affect neither the conversion nor
  its result.

The managed implementation installs GCAS descriptors on the indirection node owning a change, and
readers help complete encountered descriptors. `Snapshot` uses a specialized root/main RDCSS
descriptor: it publishes the next root only if both the root identity and previously read root main
remain current. A competing node GCAS has priority, so RDCSS aborts rather than entering a recursive
helping cycle. This closes the otherwise legal race in which a plain root CAS could copy stale main
state after a writer had already committed.

Root-generation identity decides whether node GCAS commits, preventing a write that raced with
`Snapshot` from entering the frozen generation. The new root initially shares the old C-node graph;
a later writer renews old-generation child indirection nodes only along paths it modifies. Removal
publishes empty/singleton tomb nodes and helps promote them through parents until the live path is
compact again. Equal-hash L-nodes split back into C-node/I-node prefix structure when a later unequal
hash reaches the bucket. Reads and snapshots take no locks. A successful write copies one compact
C-node array per changed level and performs node-local GCAS; collision work is linear in an actual
equal-hash bucket. Contended operations can retry, so progress is lock-free, not wait-free.

`Generation` counts completed content-changing calls for diagnostics. It is not an atomic version
paired with an arbitrary concurrent read; content linearizes at descriptor commit before the owning
call increments the counter.

## Integer Patricia Map And Set Contract

The integer-specialized family consists of:

- `PersistentIntMap<TValue>` and `PersistentIntSet` for `int` keys/items;
- `PersistentLongMap<TValue>` and `PersistentLongSet` for `long` keys/items.

The concrete facades share a static-policy big-endian Patricia engine; no generic-math or virtual
key conversion occurs in the hot path. A sign-bit transform maps signed order to unsigned trie
order, so enumeration is ascending signed order including the minimum/maximum boundary. Keys are
their own identities and hashes, so collision buckets and comparer policies do not exist.

Map `SetItem`, `Add`/`TryAdd`, `Remove`/`TryRemove`, `Clear`, and no-op instance identity mirror the
persistent CHAMP vocabulary. `Union` is right-biased by default; `Union` and `Intersect` overloads
accept `(key, left, right)` combining functions. Default `Union`, `Intersect`, and `Except` recurse
over prefixes, prune reference-equal subtrees, and reuse untouched nodes. Set facades expose the
same three structural operations and implement `IReadOnlySet<T>`.

Lookup and update visit at most W path-compressed branches, where W is 32 or 64. Structural algebra
is proportional to the prefixes where the inputs overlap or diverge and returns immediately for
reference-equal roots. Combining overloads currently enumerate one side and therefore cost
O(m * W). Enumeration is O(n) in ascending order and currently allocates an iterator plus a traversal
stack; unlike the CHAMP enumerator, it is not an allocation-free struct enumerator.

### Patricia Ordered Cursors

The four Patricia facades ship ascending signed-key gap cursors under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md):
`PersistentIntMapCursor<TValue>`, `PersistentLongMapCursor<TValue>`, `PersistentIntSetCursor`, and
`PersistentLongSetCursor`. Each is a `public readonly struct` holding exactly a source reference and
an integer rank — a **Profile R snapshot-plus-rank checkpoint**, not a retained-frame zipper. CHAMP
maps and sets deliberately have no cursor; the Patricia families qualify because ascending signed-key
order is public semantics rather than private trie topology.

A cursor position is a gap in `0 .. Count`, with `entries < boundary` before it and the candidate
entry after it. The factories are:

```csharp
public PersistentIntMapCursor<TValue> GetCursor(int position = 0);
public PersistentIntMapCursor<TValue> GetCursorAtEnd();
public PersistentIntMapCursor<TValue> GetLowerBoundCursor(int key);
public PersistentIntMapCursor<TValue> GetUpperBoundCursor(int key);
public PersistentIntMapCursor<TValue> GetCursorAtKey(int key, out bool found);
```

The `long` map, and both set facades, mirror this exactly; the set key-seek factory is spelled
`GetCursorAtItem(item, out bool found)` rather than `GetCursorAtKey`. There is no
`GetCursorAtStart` — `GetCursor()` is the start gap through its default argument. `GetCursorAtKey`
and `GetCursorAtItem` publish the hit discriminator as an `out bool` and **always return a usable
lower-bound cursor**, so a miss is an insertion gap rather than an invalid value.

Navigation is `MovePrevious`, `MoveNext`, and `Seek(int position)` — spelled `Seek`, not `SeekRank`.
`Count`, `Position`, `IsAtStart`, and `IsAtEnd` are O(1) reads over cached subtree counts. Map cursors
peek `KeyValuePair<TKey, TValue>` and edit through `Insert(key, value)`, `SetItem(key, value)`,
`SetNextValue(value)`, `DeletePrevious()`, and `DeleteNext()`. Set cursors peek the bare item and
expose only `Add(item)` plus the two deletions: there is no value to replace, and `Add` on a present
item is an identity no-op returning the receiver cursor rather than an error.

Every edit delegates to the ordinary published operation — `Add`/`SetItem`/`Remove` on the owning
facade — so path compression, unary-parent collapse, no-op instance identity, and structural sharing
are exactly the collection's. `Snapshot()` is O(1) and returns the **exact source instance** for a
clean cursor; a no-op edit forwards the collection's own preserved reference.

The error channel is:

- `ArgumentOutOfRangeException` for a position outside `0 .. Count`, validated in `GetCursor` and
  `Seek`;
- `InvalidOperationException` for a boundary violation — moving past an end, or deleting or updating
  across an absent neighbor — and for a strict insert whose key belongs at a different gap than the
  current one;
- `ArgumentException` with `paramName` `"key"` for a strict `Insert` of an already present key,
  checked **before** the gap check, so a duplicate at the wrong gap reports the duplicate; and
- `InvalidOperationException` from every member of the invalid `default` value.

The default struct value is explicitly invalid. `Position`, `IsAtStart`, `IsAtEnd`, and the
`Seek(Position)` identity shortcut all read a private guarded accessor before returning, so none of
them can silently report gap zero on `default(PersistentIntSetCursor)`. Inherited `ValueType` members
(`Equals`, `GetHashCode`, `ToString`) are not overridden and do not throw.

Honest complexity: creation, `Seek`, and movement are O(1) — a move only rewrites an integer — while
**every peek is an unconditional O(W) order-statistic descent from the root**, because the cursor
retains no path. A complete in-order traversal by move-plus-peek is therefore O(n · W), not O(n).
Bound and exact seeks are O(W). Edits are the owning operation's O(W) plus an O(1) rank adjustment.
Context space is O(1). These cursors inherit none of the C# rope tier's focused representation, memo
cell, callback ceiling, allocation bound, or amortized-locality claims.

## Merkle Search Tree Contract

`MerkleSearchTree<TKey, TValue>` is an immutable ordered content-addressed map using the
`mst-sha256-b16-v2` wide-block format. The comparer orders keys and defines key equivalence. The
codecs define the exact bytes that are hashed and persisted. Given the same semantic policy and
canonical entries, construction, incremental updates, and deletion histories converge on the same
block graph and root digest.

### Policy And Canonical Codecs

There is deliberately no unsafe default policy. `MerkleSearchTreePolicy<TKey, TValue>` requires:

- a stable application `PolicyId` naming comparer semantics and their version;
- an `IComparer<TKey>` defining key equivalence/order;
- an injective `IMerkleCodec<TKey>` whose bytes identify comparer-equivalence classes; and
- a canonical `IMerkleCodec<TValue>` value encoding.

`IMerkleCodec<T>.EncodingId` must be nonempty and end in `-v` followed by decimal digits.
`Encode` returns a newly owned canonical byte array. `Decode` consumes exactly one complete
encoding and must reject malformed, non-canonical, or trailing input with `FormatException`.
Verified loading additionally decodes and re-encodes every field and requires byte-for-byte
identity. A custom key codec must encode equivalent keys identically and non-equivalent keys
differently; neither the library nor a digest can repair a comparer/codec disagreement.

The built-in codecs are:

| API | Encoding id | Canonical bytes |
| --- | --- | --- |
| `MerkleCodecs.Int32` | `i32-be-v1` | exactly four signed big-endian bytes |
| `MerkleCodecs.Int64` | `i64-be-v1` | exactly eight signed big-endian bytes |
| `MerkleCodecs.Utf8String` | `nullable-utf8-v1` | `00` for null; otherwise `01` plus strict UTF-8 |
| `MerkleCodecs.Bytes` | `nullable-bytes-v1` | `00` for null; otherwise `01` plus the payload |
| `MerkleCodecs.Guid` | `guid-rfc4122-v1` | exactly 16 RFC-4122/network-order bytes |

`MerkleDigest` is a 32-byte SHA-256 value with exact binary and 64-character hexadecimal
parse/format APIs. Hexadecimal output is lower case; parsing accepts either case.

### B=16 Shape And Block Wire Format

The policy domain is SHA-256 over byte tag `0x50`, followed by the algorithm id, application policy
id, key-codec id, and value-codec id, each prefixed by its signed 32-bit big-endian byte length. A
key's policy-bound digest is SHA-256 over tag `0x4B`, a length-prefixed 32-byte domain digest, and the
length-prefixed canonical key bytes. The key's layer is the number of leading zero nibbles in that
256-bit digest, from 0 through 64.

Within any key interval, all entries at its highest layer become separators in one wide block.
The lower-layer intervals before, between, and after those separators become its children. Thus a
block with `e` entries always carries `e + 1` child addresses. Because a hexadecimal digit is zero
with probability 1/16 for a uniform digest, the expected block occupancy is wide and expected depth
is logarithmic base 16. Hash outcomes can still produce a degenerate shape; these are expected, not
adversarial worst-case guarantees.

Every nonempty block is the following exact byte sequence. All integers and lengths are signed
32-bit big-endian values and all digests are 32 bytes:

| Field | Bytes | Contract |
| --- | ---: | --- |
| magic | 4 | ASCII `MST2` |
| tag | 1 | `01` for a node block |
| domain | 32 | the policy `DomainDigest` |
| layer | 1 | the common hash-derived entry layer, 0 through 64 |
| subtree count | 4 | positive count of entries in the complete block closure |
| entry count | 4 | positive number of entries in this block |
| entries | variable | repeated key length/key bytes/value length/value bytes |
| child digests | `32 * (entry count + 1)` | interval addresses; the policy's empty digest denotes no child |

The block address is SHA-256 of that complete sequence, with no bytes omitted. The empty-tree digest
is SHA-256 of ASCII `MST2`, a zero tag byte, and the domain digest. Empty children use that same
domain-specific digest. Persisted blocks therefore commit to algorithm version, policy and codec
versions, layer, cached subtree count, encoded entries, child order, and every descendant address.

### Ordered Map Surface

- `Create(policy)` creates the domain-specific empty map.
- `CreateRange(entries, policy)` sorts by the policy comparer, retains the first equivalent key
  representative, and applies last-value semantics.
- `SetItem` adds or replaces while retaining an existing equivalent key representative. An exact
  canonical-value-byte no-op returns the current instance.
- `Remove` and `Clear` preserve instance identity when they make no change.
- `Count`, `Height`, `BlockCount`, and `RootHash` expose cached representation metadata.
- lookup, dictionary enumeration, and `EnumerateRange(minimum, maximum)` use comparer order; range
  bounds are inclusive.
- `ValidateStructure` rechecks in-memory ordering, layers, child intervals, counts, block bytes, and
  digests and returns `MerkleSearchTreeStatistics`.
- `ContentEquals` compares domain and root digests in O(1), under the SHA-256 collision-resistance
  assumption.
- `MapEquals` verifies semantic key/value equality. Root and reference equality are pruning aids,
  not permission to skip semantic traversal.
- `Diff` reports comparer-ordered `Added`, `Removed`, and `Changed` values. Equal block digests are
  pruned; a separator change may require a merge scan of the whole divergent region.

### Ordered Persistent Cursor

`MerkleSearchTreeCursor<TKey, TValue>` is a `public readonly struct` holding one retained tree
version plus a comparer-order rank gap. It is a **specialized Profile R snapshot-plus-rank
checkpoint** over an already trusted in-memory tree, never a mutable editor for stored blocks.

Factories mirror the Patricia family: `GetCursor(int position = 0)`, `GetCursorAtEnd()`,
`GetLowerBoundCursor(TKey key)`, `GetUpperBoundCursor(TKey key)`, and
`GetCursorAtKey(TKey key, out bool found)`. `GetCursorAtKey` publishes its hit discriminator as an
`out bool` and always returns a usable lower-bound gap, so a miss is the insertion location.

Navigation is `MovePrevious`, `MoveNext`, and `Seek(int position)`, with O(1) `Count`, `Position`,
`IsAtStart`, and `IsAtEnd`. Peeks return `KeyValuePair<TKey, TValue>`. The edit vocabulary is
`Insert(key, value)`, `SetItem(key, value)`, `SetNextValue(value)`, `DeletePrevious()`, and
`DeleteNext()`.

**Every cursor edit calls the canonical ordinary operation.** The tree exposes no `Add`, so even the
strict `Insert` performs its own duplicate and gap precondition checks and then delegates to
`SetItem`; `SetNextValue` resolves the focused entry and calls `SetItem` with the stored key;
both deletions resolve the neighbor and call `Remove`. The cursor never constructs a node. Canonical
encoding, key-layer derivation, block splitting and merging, subtree counts, `MST2` bytes, block
digests, and the root digest are therefore byte-identical to the equivalent direct edit, and
`SetItem`'s exact-canonical-value-byte no-op and `Remove`'s unchanged-result identity both propagate
through the cursor unchanged.

`Snapshot()` is O(1), returns the exact source instance for a clean cursor, retains the exact policy
object, and never writes an `IMerkleBlockStore`. Cursor state is local navigation state: it is never
part of `MST2`, `MSP2`, a pack, a proof, or a store, and it does not weaken codec round-trip checks
or verification budgets. Create a cursor only from a tree built normally or obtained through a fully
verified `Load`/`Import`.

The error channel matches the Patricia cursors: `ArgumentOutOfRangeException` for a position outside
`0 .. Count`; `ArgumentException` on `paramName` `"key"` for a strict `Insert` of a present key;
`InvalidOperationException` for boundary violations, for a strict insert at the wrong gap, and from
every member of the invalid `default` value.

Honest complexity, with `h` the block height and `e_i` the entry occupancy of visited block `i`.
Moving the gap is O(1) because it only rewrites an integer, but the cursor retains no frames, so
**every peek re-descends from the root**. Because blocks cache each child's total subtree count
rather than cumulative child-prefix ranks, the rank descent scans a block's entries linearly:
a rank peek, a rank seek, and the initial `Position` accumulation each cost O(Σ (e_i + 1)). A key
seek costs O(Σ log(e_i + 1)) comparisons for the in-block binary search plus the same linear rank
accumulation. A complete traversal by move-plus-peek is therefore **O(n · Σ (e_i + 1))**, not O(n).
`Count` and `Position` are O(1) reads. An edit plus its snapshot is the delegated canonical
operation's cost — expected O(16 log16 n + S) for `S` changed encoded bytes under uniform layers, and
O(n + S) worst case for a degenerate block shape. Context space is O(1).

The frame-based tier described in the repository design — retaining original trusted node identity,
entries, and complete in-memory child subtrees on both sides, and claiming O(1) within-block movement
with an O(n) traversal — is **specified but implemented in no port, including this one**. Do not cite
those bounds for this cursor.

### Blocks, Stores, Packs, And Verified Loading

`MerkleBlock` owns a copy of canonical block bytes and a claimed digest. Its constructor deliberately
does not verify that pair. `IMerkleBlockStore` stores immutable blocks by digest: repeated identical
content is an idempotent no-op, while different bytes under an existing digest must raise
`MerkleVerificationException` with `ConflictingBlock`. `InMemoryMerkleBlockStore` is thread-safe and
ephemeral; it is not a durable transaction, eviction, or trust boundary.

`Save(store)` exports the complete closure, preflights known address conflicts, and returns the
number of newly stored blocks. `ExportPack()` returns that closure in deterministic preorder.
`ExportPack(digests)` returns unique explicitly requested blocks in request order. A
`MerkleBlockPack` carries the algorithm id, domain digest, target root, and unique blocks; it may be
complete or partial, so `ContainsRootBlock` is diagnostic rather than an invariant.

`Load(rootHash, policy, store, budget)` and
`Import(pack, policy, destinationStore, budget)` do not trust stored or transported bytes. They:

1. enforce the expected algorithm and policy domain where an envelope is present;
2. recompute each block digest and parse exact lengths, magic, tag, layer, and counts;
3. decode and byte-for-byte re-encode every key and value;
4. recompute every entry layer and require strict comparer order within a block;
5. reject trailing bytes and require exact canonical block reserialization;
6. follow the closure while detecting missing blocks and cycles;
7. validate child layers and separator intervals, subtree counts, and the requested root; and
8. revalidate the reconstructed in-memory structure.

`Import` may combine a partial pack with blocks already in `destinationStore`. It verifies the pack
and complete root closure before preflighting and writing the supplied blocks. This prevents a
single-threaded failed import from partially committing to the supplied store, but
`IMerkleBlockStore` does not define multi-writer transaction isolation.

Format, reference, root, and resource failures are classified by
`MerkleVerificationFailureKind` and surfaced as `MerkleVerificationException`; proof verification
reports the same classifications without throwing. Exceptions raised by an application comparer or
codec outside its documented format contract may propagate directly. The finite default
`MerkleVerificationBudget` is:

| Limit | Default |
| --- | ---: |
| distinct decoded blocks | 1,000,000 |
| cumulative proof-query and serialized-block bytes | 1 GiB |
| bytes in one block | 16 MiB |
| bytes in one proof query | 16 MiB |
| reference depth | 256 |
| cumulative decoded entries | 100,000,000 |
| child references in one block | 65,536 |

Network-facing callers should normally retain or tighten these limits. The six-argument budget
constructor sets `MaxProofQueryByteCount` to `MaxBlockByteCount`; the seven-argument overload can
tighten it independently. Proof verification charges query bytes before envelope, codec, or block
decoding and includes them in the cumulative limit. A budget limits parser and closure work; it does
not authenticate the root, policy id, comparer implementation, or peer.

### Proofs

`CreateProof(key)` emits a canonical membership proof carrying the authenticated canonical value,
or a non-membership proof ending at the authenticated empty interval. `CreateRangeProof(minimum,
maximum)` expands every child interval intersecting the inclusive range, authenticating its complete
contents while leaving disjoint child digests opaque. `MerkleProofStep.ExpandedChildIndexes` states
which child blocks are supplied; verification requires exactly the canonical expansion and rejects
missing, extra, repeated, malformed, or tampered steps.

The opaque query descriptor begins with ASCII `MSP2` and a one-byte `MerkleProofKind`. Point queries
then contain a length-prefixed canonical key and, for membership only, a length-prefixed canonical
value. Range queries contain length-prefixed canonical minimum and maximum keys. All query lengths
are signed 32-bit big-endian values, and verification rejects trailing bytes or non-canonical codec
round trips.

`VerifyProof(proof, policy, budget)` returns `MerkleProofVerificationResult`. On success it reports
the computed root, verified block count, and exact query-plus-block byte count (including an empty-
root proof's query). On failure it reports a typed failure and diagnostic without publishing decoded
tree state. The result establishes that the proof's canonical query is consistent with its declared
root and domain. The caller must obtain that root from a trusted channel; a self-consistent proof is
neither a signature nor evidence of who produced the data.

### Block Synchronization

`CreateSyncPack(receiverStore)` performs one-shot synchronization: it exports missing target blocks
and stops descending whenever the receiver already contains a block digest. This optimization
requires the receiver to treat presence as evidence that the block's verified descendant closure is
also present. Use `Import` to verify and commit the resulting complete closure.

`PlanSync(localTree, receiverStore)` supports a partial store. It compares compatible roots and, if
they differ, requests the first absent block on each target path reachable through already present
blocks. The peer answers with `ExportPack(plan.RequestedBlocks)`, the receiver stages those immutable
blocks, and the parties repeat until `RequiresBlocks` is false. The receiver must then call `Load`
or `Import` to verify the complete closure before publishing it. `RootsMatch` means the supplied
local tree already has the target root; `RequiresBlocks == false` alone can instead mean that the
store has completed a still-unpublished target closure.

`ExaminedBlockCount` and `ExaminedByteCount` describe planning work. Planning prunes at absent
frontier blocks. The current explicit-digest `ExportPack` indexes the target's complete in-memory
block graph before selecting requested blocks, so its local CPU cost is O(all target blocks) even
when transfer size is small.

### Typed Three-Way Merge

`Merge(baseTree, left, right, resolver, valueComparer)` requires one policy domain. Root-digest fast
paths return an unchanged descendant or identical descendants directly. Otherwise it comparer-merges
all three ordered maps:

- a change made by only one descendant is accepted;
- equal changes made by both descendants are accepted;
- differing changes made by both descendants create a
  `MerkleThreeWayMergeConflict<TKey, TValue>`; and
- the optional resolver may choose `UseBase`, `UseLeft`, `UseRight`, `SetValue`, `Delete`, or
  `Unresolved`.

`MerkleMergeValue<TValue>` distinguishes absence from a present nullable value. Without a resolver,
true conflicts remain unresolved. If any conflict remains, the result exposes all unresolved
conflicts and deliberately no partial tree. A successful result exposes one complete canonical
`MergedTree`.

### Complexity, Allocation, And Security Boundary

Let `h` be block height, `e` the entries in a visited block, `S` serialized bytes processed, and `k`
the number of reported range/diff entries.

- lookup performs O(sum log(e + 1)) comparisons along one path; under uniform SHA-256 layers this
  is expected O(log n), with expected `h = O(log16 n)`;
- `SetItem` and `Remove` copy one path and re-encode copied blocks, expected O(16 log16 n) entry work
  plus encoded bytes; a hash-degenerate block can make one update O(n + S);
- `EnumerateRange` is expected O(log16 n + k) for uniform layers but can scan O(n) entries in a
  degenerate wide block; full enumeration is O(n);
- `CreateRange` sorts in O(n log n), then constructs the recursive partitions in expected
  O(n log16 n + S); adversarial layers can make that construction O(n^2 + S);
- `ContentEquals` is O(1); `MapEquals` is O(n) in the general case; `Diff` is proportional to visited
  divergent regions plus output and is O(n + m) worst-case;
- full save/export/load/import is O(block count + S), with storage proportional to the closure;
- point-proof size and verification are proportional to one root-to-terminal block path; range
  proof work is proportional to expanded intersecting blocks and their bytes;
- one-shot sync traversal is proportional to missing regions plus known-subtree boundaries, while
  transferred bytes are exactly the selected block payloads; and
- a non-fast-path three-way merge materializes three ordered sequences and rebuilds through
  `CreateRange`, requiring expected O(N log N + S) time, O(N^2 + S) worst-case time under
  adversarial layers, and O(N) auxiliary storage for total input/output size N.

All expected shape bounds assume SHA-256 behaves uniformly for policy-bound key encodings. All
content-equality, pruning, proof, and synchronization claims assume SHA-256 collision resistance.
The format provides integrity relative to a trusted root, not confidentiality, availability,
signatures, key management, replay protection, or peer authentication. Keys, values, comparer
semantics, codec behavior, and policy identifiers must remain semantically immutable for the
lifetime of every published root. The tree copies encoded arrays, so later mutation of a codec's
returned buffer cannot rewrite an existing address.

## Persistent Ancestral Connection Forest Contract

`PersistentAncestralConnectionForest` is a fully branching
persistent insertion-only connectivity forest over the fixed vertex universe
`0 .. VertexCount - 1`. Parent cells live sparsely in a `PersistentHashMap<int, Cell>`; an absent
cell denotes a singleton root, so `Create(vertexCount)` is O(1) for any universe size and rejects a
negative count. `AncestralConnectionVersion` is an immutable reference-identity token carrying
`Parent`, `long Depth`, and O(1) `Root`; equal depths on sibling branches are distinct versions.

`Link(left, right)` returns a distinct child version. A successful union applies union by size
without path compression — on a size tie the first endpoint's root remains the representative —
and labels the one new root-parent edge with the child version token. A redundant link still
creates a child history version but shares the complete connectivity index, adding O(1) space.
`Find` and `Connected` resolve current union-by-size representatives. `ComponentCount` is O(1).
`GetComponentSize` returns the number of vertices in a vertex's component by reading the size the
union-by-size root cell already caches — one root walk plus a cached read, at the same cost as
`Find`, with no path compression. An isolated vertex reports 1.

`FirstConnected(left, right)` returns the earliest version on this version's root-to-current
history at which the two vertices were connected, or `null` when they are currently disconnected;
a vertex is connected to itself at the history root. The answer is computed from the two current
parent paths — the latest union-edge version strictly below their forest LCA — not by searching
the version history. `TryGetFirstConnected` is the nonthrowing tuple form.

With `n = VertexCount`, `w` the bounded CHAMP path cost, and union-by-size limiting every parent
path to O(log n) cells: `Find`, `Connected`, and `Link` are O(w log n) worst-case; `Link` allocates
O(w) new CHAMP nodes on success and O(1) on redundancy; `FirstConnected` is O(w log n) time and
O(log n) transient path space, independent of history depth. Version tokens retain their parent
chain, so history depth contributes O(depth) live token space reachable from any retained version.
Deletion, path compression, confluent merging, and retroactive updates are deliberately outside
the contract; the complete invariant and design study are normative in the
[research proposal](../../../../docs/proposals/persistent-ancestral-connection-forest-2026-07-29.md).
