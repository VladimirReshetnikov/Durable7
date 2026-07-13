# HAMT API Specification

- Status: Implemented
- Created (UTC): 2026-07-02T05:02:24Z
- Repository HEAD: 3c639e02d05377685676923a13b30a3d22fd4994
- Audience: Maintainers and reviewers of `Tools.DataStructures.Hamt`
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
collections expose public struct enumerators that keep their entire traversal state inline: obtaining
and draining an enumerator allocates nothing, and a copied enumerator advances independently of the
original.

## Map Contract

`PersistentHashMap<TKey, TValue>` implements `IReadOnlyDictionary<TKey, TValue>`.

- `Empty` returns the shared empty map using `EqualityComparer<TKey>.Default`.
- `Create(comparer)` returns an empty map using a supplied comparer.
- `CreateRange(items, comparer)` adds entries in enumeration order with last-wins semantics.
- `SetItem(key, value)` adds or replaces a key.
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

`Add` and `TryAdd` hash the key once and walk the trie once; a rejected duplicate allocates nothing.
The try-pattern `out` values (`TryGetValue`, `TryRemove`) carry `[MaybeNullWhen(false)]`, matching
the `IReadOnlyDictionary<TKey, TValue>` annotation.

No-op updates preserve instance identity throughout: replacing a value that compares equal under
`EqualityComparer<TValue>.Default`, removing an absent key, and clearing an empty map all return the
current instance. Consequently, "last-wins" value semantics hold up to default value equality: when
an incoming value compares equal to the stored value, the stored value object is retained. When a
present key is replaced or re-set, the originally stored key object is likewise retained; use
`TryGetKey` to observe it.

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

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes. Lookups allocate nothing.
- Enumeration: O(n) time. The enumerator holds at most seven inline frames (one per trie level) and
  performs no heap allocation.
- Map `CreateRange` / set `CreateRange`: O(n (w + c)) through hash-bucket staging followed by one
  canonical freeze; unlike repeated persistent updates, the build does not clone every traversed path.
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
  contents, and snapshots retain the trie's comparer.
- `SnapshotView.ToPersistentHashMap` copies a captured generation into canonical CHAMP form in O(n).

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
