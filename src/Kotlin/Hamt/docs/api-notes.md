# Kotlin HAMT API Notes

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers implementing and reviewing the Kotlin HAMT port
- Scope: Kotlin naming, contracts, and intentional differences from the C# and Rust workspaces

Primary entry points:

- `PersistentHashMap<K, V>`;
- `PersistentHashSet<T>`;
- `PersistentHashBag<T>`;
- `MapValueResult<K, V>` for persistent map factory operations;
- `PersistentHashMap.Transient<K, V>` and `PersistentHashSet.Transient<T>` one-way editing
  sessions;
- `HashPolicy<K>` for runtime hash/equality policy injection;
- `DuplicateKeyException`, `AddResult<T>`, and removal result records.
- `ConcurrentHashTrie<K,V>` and its immutable `Snapshot<K,V>`.
- `PersistentIntMap<V>` / `PersistentIntSet` and `PersistentLongMap<V>` / `PersistentLongSet`.
- `MerkleSearchTree<K, V>`, `MerkleSearchTreePolicy<K, V>`, `MerkleEntry<K, V>`, and the
  `MerkleMapDifference<K, V>` variants.
- `MerkleCodec<T>`, `MerkleDigest`, the strict built-in codecs, and `MerkleEncodedBlock`.
- `MerkleBlock`, `MerkleBlockStore`, `InMemoryMerkleBlockStore`, `MerkleBlockPack`, and
  `MerkleVerificationBudget` for verified persistence.
- `MerkleProof`, `MerkleProofStep`, `MerkleProofKind`, `MerkleSyncPlan`, and the typed
  `MerkleMergeValue` / `MerkleMergeResolution` / `MerkleThreeWayMergeResult` family.

The port follows the repository HAMT semantics:

- updates return new persistent values and keep old versions usable;
- trie nodes are immutable and shared by JVM object reference;
- the trie uses 32-way CHAMP branching over 32 hash bits, with separate payload and child bitmaps;
- ordinary key/value leaves are inlined in compact payload runs, while child runs contain only
  subtries; deletion promotes singleton child payloads to restore canonical shape;
- equal full-hash collisions are kept in immutable collision buckets;
- no-op value replacement and absent removal preserve the existing root;
- duplicate `add` / `tryAdd` calls reject the key without changing the root;
- replacing an existing key retains the originally stored key object;
- bulk map construction is last-wins;
- set algebra includes union, intersection, difference, symmetric difference, subset/superset,
  proper subset/superset, overlap, and equality checks.
- `mapEquals` requires the same `HashPolicy` object and compares canonical bitmap slots in lockstep;
  `diff` reports typed added, removed, and changed entries in deterministic structural order.
  Both use stored hashes without rehashing and return immediately at every reference-identical root,
  descendant subtrie, or leaf. Equal-hash collision buckets remain insertion-order independent and
  are matched through the retained key policy.

Same-type map/set algebra requires the same `HashPolicy` object, aligns CHAMP bitmap slots, uses
stored hashes, and returns immediately for reference-identical roots or subtrees. Nodes cache entry
counts so results do not need a finishing traversal. Shared-ancestry work is proportional to
divergent/result nodes; independent operands require O(n + m). Same-policy relations use structural
intersection; cross-policy relation overloads deliberately fall back to the established receiver-
policy iterable semantics.

Kotlin-specific differences:

- default equality is Kotlin/JVM `equals`; custom behavior is supplied through `HashPolicy<K>`;
- duplicate insertion throws `DuplicateKeyException` for `add` and returns `AddResult` for `tryAdd`;
- lookup miss paths return `null`;
- iteration is exposed as Kotlin `Sequence`/`Iterable` over stable trie order;
- `sharesRootWith` exposes root sharing for tests and diagnostics.

The hash contract is the standard hash-map contract: keys considered equivalent by the active
`HashPolicy` must produce the same hash through that policy.

## One-Descent Persistent Map Factories

`PersistentHashMap.getOrAdd(key, addFactory)` and
`PersistentHashMap.addOrUpdate(key, addFactory, updateFactory)` are ordinary immutable point
operations. Their `MapValueResult<K, V>` reports both the successor `map` and the `value` actually
stored, avoiding a second lookup. They do not adopt a map into a transient session and do not use a
lookup-then-`put` composition internally.

Both operations compute the key hash exactly once and use the existing CHAMP recursion to select a
leaf, collision entry, inline bitmap payload, or child subtree and rebuild at most that route.
`getOrAdd` invokes no factory on a hit and its add factory exactly once on a miss. `addOrUpdate`
invokes exactly one of its two factories exactly once. The add factory receives the caller's key;
the update factory receives the exact caller lookup key plus the stored value. Kotlin's non-null
function parameters are validated by the generated JVM entry checks before the method body hashes
the key, including when a factory's branch would not be selected.

A miss stores the caller's key and selected value. A hit always retains the first stored key
representative. If an update result is identical to or equal to the stored value under Kotlin's
ordinary value equality, the map retains the stored value instance, returns that instance in
`MapValueResult.value`, and returns the exact source map. A present `null` value remains distinct
from absence because trie traversal selects the branch before interpreting the value. Hash-policy,
factory, or value-equality exceptions escape without publishing a successor; every source version
therefore remains unchanged and usable.

The operation bound is O(w + c), where `w` is the bounded CHAMP depth and `c` is the scanned
equal-full-hash collision bucket. The single hash/descent and callback counts are semantic operation
contracts, not benchmark claims.

## Persistent Hash Bag

`PersistentHashBag<T>` is an immutable unordered multiset backed only by
`PersistentHashMap<T, Int>`. Its map contains one positive multiplicity per `HashPolicy`
equivalence class. `distinctCount` is the number of classes; `totalCount: Long` is the expanded
occurrence count. The type deliberately exposes neither an ambiguous `size`/`count` property nor a
public builder or transient session. `empty(policy)` and `from(items, policy)` retain the exact
policy object, and construction processes input in iteration order while keeping the first
representative of each class.

`contains` tests class membership, `countOf` returns a class multiplicity or zero, and `get` returns
the retained representative or `null`. For nullable element types, call `contains` to distinguish a
stored null representative from a miss. `add`/`remove` change one occurrence;
`addCopies`/`removeCopies` change a requested nonnegative number; `removeAll` removes a complete
class. Negative copy counts fail before hashing, zero-copy operations return the receiver without
hashing, removal saturates at zero, and missing removal is an identity-preserving no-op. Stored
multiplicities are checked `Int` values in `1..Int.MAX_VALUE`; an addition that would overflow
throws before a successor is returned. Internal `Long` total arithmetic is checked defensively.

Default `Iterable<T>` and `asSequence()` traversal are expanded: every retained representative is
yielded once per occurrence, with a class's copies contiguous. `distinctItems()` yields one
representative per class, and `entries()` yields the same representatives paired with their
positive multiplicities in identical stable-for-that-version, otherwise unspecified CHAMP order.
Kotlin's standard iterable materializers can be used when the expanded result fits the target JVM
collection.

Bag algebra is receiver-policy multiset algebra:

- `union` selects the maximum multiplicity per class;
- `intersect` selects the minimum;
- `except` computes saturating receiver-minus-argument multiplicities; and
- `sum` performs checked per-class addition.

When the policy objects are not reference-identical, the complete argument is first normalized
under the receiver's policy, before any operation-specific empty or identity shortcut. Argument
classes that collapse contribute a checked sum, and the first representative in that argument
version's distinct CHAMP order represents a normalized class. A surviving receiver class always
keeps its receiver representative; only a class absent from the receiver can introduce an argument
representative. Thus comparer/hash/equality or checked-collapse failures remain observable even
when an empty intersection could otherwise be answered immediately.

Logical no-op algebra returns the exact receiver. In particular, union or intersection with self
returns the receiver, except with self returns an empty bag retaining the receiver policy, and sum
with self genuinely doubles every multiplicity and can overflow. Algebra and point operations build
only immutable intermediate maps, so callback and arithmetic failures leave both operands intact.
The implementation is deliberately element-wise over distinct entries and claims no structural
lockstep or benchmark advantage.

## One-Way CHAMP Editing Sessions

`PersistentHashMap.toTransient()` and `PersistentHashSet.toTransient()` adopt an existing value by
reference in O(1). The companion `createTransient(...)` factories begin from an empty persistent
value under the supplied policy. A session retains that exact `HashPolicy` object and exposes active
size, emptiness, lookup, stored-representative recovery, and iteration. Map point verbs are `put`,
indexed assignment (`session[key] = value`), `add`, `tryAdd`, `remove`, `tryRemove`, and `clear`; set
sessions expose `add`, `remove`, and `clear`, plus `isSubsetOf`, `isProperSubsetOf`, `isSupersetOf`,
`isProperSupersetOf`, `overlaps`, and `setEquals` over `Iterable<T>`. Relation arguments are
interpreted with the active session's retained policy and do not replace stored representatives.
Map `put` and both set point verbs report whether the logical collection changed. Map `tryRemove`
returns an `HamtEntry` object, keeping a stored nullable key or value distinguishable from a miss.

`persist()` returns the session's current persistent object by reference in O(1) and then consumes
the session. If no successful logical change occurred—including equal-value replacement, duplicate
`tryAdd`, absent removal, or clearing an already-empty collection—the result is the exact adopted
map or set object. Every later read, edit, iteration request, or publication attempt throws
`IllegalStateException`. Map `entries()`, `keys()`, and `values()` and set `asSequence()` capture the
immutable snapshot and session version when the view is acquired. Logical no-ops leave acquired
views valid; a successful edit makes them throw `ConcurrentModificationException` even if their
iterator is created only afterward. Publication makes existing and new session-backed iteration
throw `IllegalStateException`.

This is not the C# owner-token kernel. A Kotlin session stores one reference to the current
`PersistentHashMap` or `PersistentHashSet`; each edit first computes the ordinary immutable
successor, path-copying the affected CHAMP route, and only then replaces that reference. Adoption
and publication are O(1), but edit time and allocation are exactly those of the corresponding
persistent operation. No performance advantage is claimed. If hashing, equivalence, or value
equality throws, no successor reference is installed and the active session remains unchanged and
retryable. Sessions are single-owner and unsynchronized. Reentrant mutation/publication from a
policy callback is rejected with `IllegalStateException`; externally serialize all access to one
session.

## Concurrent Ctrie

The mutable Ctrie stores bitmap C-nodes behind generation-stamped indirection nodes. Updates install
helping GCAS descriptors on the owning indirection node. `snapshot()` installs a helping root/main
RDCSS descriptor and advances the root generation only while the predecessor's raw main slot is
still the exact node it observed; a writer committed in that window therefore cannot be lost.
Every root consumer helps pending descriptors through `readRoot()`. Empty and singleton tomb nodes
are promoted through their parents after removal, preserving the invariant that the root is never a
tomb and that live lookup depth does not grow with historical churn. Later writers copy
old-generation child indirections only along paths they modify. `Snapshot.toPersistentHashMap()`
performs the explicit O(n) conversion into canonical CHAMP form. Snapshot enumeration already uses
that canonical order: at each bitmap level logical singleton payloads appear in bitmap order before
multi-entry child nodes, frozen singleton tombs participate in the parent payload run, and
equal-hash collision buckets retain their entry order. `getOrPut` and `compute` callbacks
can run repeatedly after a lost GCAS and must therefore be repeatable. `set` and `compute` treat an
identical value reference as an immediate no-op before invoking user equality. Progress is
lock-free, not wait-free.

## Integer Patricia Family

The 32- and 64-bit map/set facades share a big-endian Patricia core. A sign-bit transform maps
signed keys to unsigned trie order, so iteration is ascending signed order including minimum and
maximum boundaries. Nodes store a common prefix and highest differing bit; insertion and removal
path-copy only the compressed search spine. `union`, `intersect`, and `except` align prefixes,
reuse disjoint or identical subtrees, and preserve receiver identity for semantic no-ops. Each node
caches its subtree cardinality, allowing an algebra result to expose `size` without a separate
result-tree traversal. Map `union` and `intersect` overloads accept a
`(key, leftValue, rightValue) -> value` function that is invoked exactly for keys present in both
operands; disjoint subtrees remain structurally shared.

## Merkle Search Tree

`MerkleSearchTreePolicy<K, V>` combines comparator semantics, a caller-owned policy ID, and
explicitly versioned injective key/value codecs. Its `domainDigest` binds those identifiers to the
`mst-sha256-b16-v2` algorithm. Keys are hashed again within that domain; their count of leading zero
hexadecimal digits selects levels 0 through 64. The built-in codecs use fixed-width big-endian
integers, tagged nullable UTF-8 or bytes, and RFC-4122 network-order UUIDs. UTF-8 encoding rejects
unpaired surrogates and decoding rejects malformed byte sequences.

`MerkleSearchTree.empty` and `from` create immutable canonical trees. `from` sorts by the supplied
comparator, retains the first equivalent key object, and applies the last value. `setItem`, `remove`,
and `clear` return persistent versions; unchanged encoded values and absent removals return the same
tree object, while changed paths share untouched block objects. Lookups preserve nullable values via
`getEntry`; iteration and `enumerateRange` use explicit stacks; `diff` skips matching block digests
and returns unambiguous added/removed/changed variants. `contentEquals` compares compatible content
addresses, while `mapEquals` permits a caller-supplied value relation.

The tree retains caller key and value references. Codecs own their encoded byte snapshots, and all
public byte-returning APIs return defensive copies. `blocksPreorder`, `shape`, `sharedBlockCount`,
and `sharesRootWith` expose deterministic diagnostics without making nodes mutable.
`validateStructure` re-encodes every retained key and value, recomputes key-derived levels, checks
strict key order, child intervals, cached bounds/counts/heights, exact block bytes, and SHA-256
content addresses. It therefore rejects caller mutation that makes a retained object disagree with
the canonical bytes captured at insertion. See [Merkle search tree](merkle-search-tree.md) for the
complete policy, `MST2` block, `MSP2` proof, verified-load, synchronization, and merge contracts.

Persistence operations are exposed through instance extensions (`save`, `exportPack`,
`createSyncPack`, `planSync`, `createProof`, and `createRangeProof`) and companion members
(`load`, `importPack`, `verifyProof`, and `merge`). Kotlin uses `importPack` because `import` is a
language keyword. Load/import never publish a tree before the complete reachable closure has passed
digest, codec, canonical-byte, count, ordering, level, and child-interval validation. Import may
overlay a partial pack on an existing store, but it does not write supplied blocks until the full
closure verifies and every existing address conflict has been preflighted.
