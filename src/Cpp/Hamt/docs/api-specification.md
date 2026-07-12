# HAMT C++ API Specification

- Status: Current API specification
- Created (UTC): 2026-07-02T17:58:46Z
- Repository HEAD: 9bba9109d24a3a104e05212e3828f12783fe8aaa
- Audience: Maintainers and reviewers of `tools::data_structures::hamt`
- Scope: Public C++ API, immutable-version semantics, wire contracts, and complexity guarantees

For practical include and value-semantics examples, start with the [usage guide](usage.md). The
[Merkle search-tree specification](merkle-search-tree.md) is the normative local reference for the
`mst-sha256-b16-v2` policy and exact `MST2` bytes.

## Overview

`persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>` is an immutable unordered map backed by a
hash-array mapped trie. Every update returns a new map value and leaves the source value unchanged.
Untouched trie nodes are shared by `std::shared_ptr<const node>`; update operations clone only the
search path and any touched equal-hash collision bucket.

`persistent_hash_set<T, Hash, KeyEqual>` is a value-set wrapper over
`persistent_hash_map<T, unit, Hash, KeyEqual>`. It preserves the same hash/equality policy and
structural-sharing behavior as the map.

The port intentionally follows C++ value semantics rather than C# reference identity. No-op updates
return a value that shares the same root node as the source; `shares_root_with` and the debug root
inspection helpers expose that property for tests.

`merkle_search_tree<K, V>` is a third, independent immutable map core. It orders keys through an
explicit policy, derives canonical geometric levels from policy-bound SHA-256 key hashes, and emits
exact `MST2` content-addressed blocks. Its C++ milestone covers the in-memory core and wire format.
It does not yet expose block-store persistence/loading, membership or range proofs,
synchronization, or three-way merge.

## Patricia Integer Maps And Sets

`persistent_int_map<T>` / `persistent_long_map<T>` and `persistent_int_set` /
`persistent_long_set` are explicit-width ordered collections backed by one big-endian Patricia
template. Keys are transformed by flipping the sign bit before branching, so `to_vector()` is in
ascending signed order across the full 32- or 64-bit domain. Branches store a compressed prefix,
the highest differing bit, two immutable children, and cached subtree cardinality.

`set_item` and `remove` path-copy only the compressed search path and preserve the root for equal
replacement or absent removal. `union_with`, `intersect_with`, and `except_with` align prefixes,
reuse disjoint/shared subtrees, and preserve the receiver root when the semantic result is unchanged.
Fixed union is right-biased for unequal overlapping values and fixed intersection keeps the left
value. Resolver overloads accept `(key, left, right)` and structurally combine only overlaps.

Patricia point operations are O(W), where W is fixed at 32 or 64 but unary-path compression normally
visits far fewer nodes. Structural algebra is O(v) for the prefix structure actually visited after
shared-root and disjoint-prefix pruning, with O(n + m) worst case. Result size is read in O(1) from
cached node metadata.

## Merkle Search Tree

The public encoding layer in `merkle_encoding.hpp` provides:

- `merkle_bytes`, an owned `std::vector<std::byte>`;
- `merkle_codec<T>`, the polymorphic interface for an injective, explicitly versioned canonical
  `encode`/`decode` pair;
- `int32_merkle_codec`, `int64_merkle_codec`, `nullable_utf8_merkle_codec`,
  `nullable_bytes_merkle_codec`, and `rfc4122_guid_merkle_codec`;
- `rfc4122_guid`, whose stored bytes are already in RFC-4122/network order;
- `merkle_digest`, an immutable comparable 32-byte SHA-256 digest with exact byte/hex parsing,
  lowercase formatting, hashing, and all-or-nothing destination writes;
- `merkle_key_comparer<K>` and `natural_merkle_key_comparer<K, Less>`; and
- `merkle_search_tree_policy<K, V>`, which owns shared comparer/codec objects and binds them to a
  semantic policy ID and the `mst-sha256-b16-v2` algorithm domain.

Codec IDs must be canonical UTF-8, have no Unicode `White_Space` code point at either edge, and end
in `-v` plus one or more ASCII digits. Policy IDs must be nonempty canonical UTF-8 and not consist
entirely of Unicode `White_Space` code points. Built-in fixed-width decoders require exact lengths;
nullable codecs distinguish
null (`00`) from a present empty payload (`01`) and reject missing/unknown tags and trailing bytes
after a null tag. The UTF-8 codec rejects overlong encodings, surrogates, truncated sequences, and
values above U+10FFFF. `merkle_codec_error` identifies malformed or noncanonical encoded values.

`merkle_search_tree_policy<K,V>::create` accepts explicit shared comparer and codec objects;
`natural` creates the comparer from a strict weak ordering. Policy compatibility is equality of
domain digests, while `shares_identity_with` tests whether two policy values own the same policy
state. `domain_digest`, `empty_digest`, `hash_key`, `hash_key_bytes`, `hash_bytes`, and the static
`level` function expose deterministic policy diagnostics. A key codec must be injective over the
comparator's key-equivalence classes; violating that requirement makes the caller's policy
inconsistent with the map contract.

The tree surface in `merkle_search_tree.hpp` includes:

- construction through the explicit-policy constructor, `create`, and `create_range`;
- `size`/`count`, `empty`, `height`, `block_count`, `root_hash`, and `policy`;
- `get_entry`, `try_get`, `try_get_key`, `contains_key`, and throwing `at` lookup;
- persistent `set_item`, `remove`, and `clear` updates;
- in-order forward iteration and materialized inclusive `enumerate_range`;
- `content_equals`, policy-compatible `map_equals`, and typed added/removed/changed `diff`;
- `shares_root_with`, `shares_policy_with`, and `shared_block_count` identity diagnostics; and
- `shape`, `blocks_preorder`, and `validate_structure` canonical-structure diagnostics.

`create_range` consumes a vector by value, stable-sorts it with the policy comparator, retains the
first representative among equivalent keys, and takes the last supplied value. `set_item` also
retains the first stored key representative. If the replacement value's canonical bytes equal the
stored bytes, or if `remove` misses, the returned tree shares its root with the source. Real updates
path-copy affected blocks and reuse untouched `std::shared_ptr<const node>` subtrees. The by-value
construction/update surface and shared entry state allow move-only `K` and `V` types; operations
such as default `map_equals` are available when their invoked comparison objects support those
types.

`merkle_search_tree_entry<K,V>` provides references, owning `key_handle`/`value_handle` shared
pointers, immutable canonical byte snapshots, the key-derived level, and identity comparison.
Entry values copied out of iteration or the materialized range vector retain their representatives
independently. Iterators and raw pointers returned by tree lookup members hold/point into raw nodes;
they remain valid only while a tree snapshot retaining the containing node remains alive. Published
nodes and byte snapshots are immutable and may be read concurrently under ordinary C++ lifetime
rules.

`content_equals` is the O(1) content-address relation: it compares the policy domain and root hash.
`map_equals` additionally checks values through a caller-supplied relation and uses shared-node and
equal-digest fast paths. `diff` requires compatible policy domains, prunes shared/equal-digest
blocks, and returns ordered `merkle_map_difference` records whose shared handles make nullable and
move-only values unambiguous. Incompatible diffs throw `merkle_policy_mismatch`; an inverted range
throws `merkle_range_error`.

The canonical topology chooses, in each comparator interval, every key at that interval's maximum
hash-derived level as the ordered separators in one wide block, then recursively builds the lower-
level child intervals. Levels are leading zero hexadecimal digits in a policy-bound key digest and
range from 0 through 64. Consequently, the final policy and comparator-ordered encoded contents—not
insertion history—determine the root, block topology, and bytes. See the
[Merkle specification](merkle-search-tree.md#exact-mst2-blocks) for the exact block layout.

`validate_structure` re-encodes retained keys and values, recomputes key levels, checks strict
separator order, child intervals, arity, bounds and cached counts/heights/block counts, reconstructs
every block, and verifies every SHA-256 digest. It returns `merkle_search_tree_statistics` only
after the complete tree validates and throws `merkle_tree_invariant_error` on a disagreement.

## Hash Trie Shape

The trie uses 32-way logical branching and consumes five hash bits per level. CHAMP branch nodes
store separate 32-bit data and node maps, a compact inline `(hash,key,value)` payload vector, and a
compact child-only vector; each slot is the population count below the selected bit. Removal
promotes a child that shrinks to one leaf back into its parent payload vector, restoring canonical
shape. Unequal keys with identical 32-bit truncated hash codes are stored in immutable collision
buckets and compared linearly with `KeyEqual`.

`Hash` may return any integral-like hash value accepted by `static_cast<std::uint32_t>`; the port
uses the low 32 bits to match the C# implementation's hash width. Equivalent keys must produce equal
32-bit truncated hash values.

Enumeration order follows trie bitmap order and collision-bucket order. It is stable for an
unchanged version but is not insertion order or sorted order. Iterators keep at most seven inline
branch frames, retain the trie root (so an iterator obtained from a temporary map value stays
valid), yield references to the stored entries without copying keys or values, and do not allocate
heap storage while traversing; copied iterators advance independently. Pointers returned by
`try_get`/`try_get_key` do not retain the trie: they stay valid only while some map value holding
the containing version is alive.

## Map Contract

- `empty()` returns an empty default-policy map.
- `create(hash, equal, values_equal)` returns an empty map using supplied policy objects.
- `create_range(items, hash, equal, values_equal)` adds entries in enumeration order with
  last-wins values. It builds through the bulk builder, so no intermediate persistent versions
  are allocated.
- `create_bulk_builder(hash, equal, values_equal)` returns a move-only `bulk_builder` for one-pass
  bulk construction. `set_item` mutates unpublished nodes in place with the map's duplicate rules
  (first stored key retained; value writes skipped when the incoming value compares equal under
  `ValueEqual`, so the earlier stored value is retained and the last distinct value wins).
  `to_immutable()` freezes the current contents into a detached persistent map by copying every
  reachable node; the builder never shares mutable storage with frozen maps and stays usable for
  further `set_item`/`to_immutable` rounds afterwards.
- `set_item(key, value)` adds or replaces a key.
- `set_items(items)` adds or replaces entries in enumeration order.
- `add(key, value)` adds a key and throws `std::invalid_argument` when an equivalent key already
  exists.
- `try_add(key, value)` returns `{map, added}` and rejects duplicate keys without throwing.
- `remove(key)` removes a key if present and returns a root-sharing value when absent.
- `try_remove(key)` returns `{map, removed, value}`.
- `try_get(key)` returns a pointer to the stored value, or `nullptr` when absent.
- `try_get_key(equal_key)` returns a pointer to the stored equivalent key, or `nullptr` when absent.
- `at(key)` returns the stored value reference or throws `std::out_of_range`.
- `clear()` returns an empty map preserving the current policy objects.
- `map_equals(other)` performs content equality with a shared-root fast path.
- `diff(other)` returns owned `map_difference<Key,T>` records classified as `added`, `removed`, or
  `changed`, also returning immediately for a shared root.

When replacing an existing key, the originally stored key object inside the trie is retained. When
the existing value compares equal under `ValueEqual`, the root is reused and the stored value object
is retained.

## Set Contract

- `empty()`, `create`, and `create_range` mirror the map factories.
- `add`, `try_add`, `remove`, `try_remove`, `contains`, `try_get_value`, and `clear` mirror map
  behavior.
- `union_with`, `intersect_with`, `except_with`, and `symmetric_except_with` return new persistent
  sets.
- `is_subset_of`, `is_proper_subset_of`, `is_superset_of`, `is_proper_superset_of`, `overlaps`, and
  `set_equals` interpret equality through the set's `Hash` and `KeyEqual` policy objects.

`intersect_with`, `symmetric_except_with`, `is_subset_of`, `is_proper_subset_of`,
`is_proper_superset_of`, and `set_equals` materialize their argument into `std::unordered_set` using
the set's policy objects. `is_superset_of` and `overlaps` stream their argument and exit early.
Set `create_range` and `intersect_with` assemble their result through the map bulk builder and
freeze it once.

## Complexity

Let `w` be the hash width used by the port (32 bits), `b` be the branch factor (32), and `c` be the
length of an equal-hash collision bucket.

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes.
- Enumeration: O(n) time with at most seven inline branch frames.
- Map `create_range` / set `create_range` / set `intersect_with`: O(n * (w / log2(b) + c)) through
  the bulk builder. A mutable unpublished trie is updated in place and frozen once, avoiding
  persistent path copies between successive input entries.
- Bulk builder `set_item`: O(w / log2(b) + c) with in-place mutation of unpublished nodes;
  `to_immutable`: O(n) node copies producing a detached persistent trie.
- Set algebra implemented from public operations: O((n + m) * update-cost) unless the operation
  only probes membership.

For a Merkle tree, let `h` be its height (at most 65), `e` the number of separators in a visited
wide block, and `r` the number of reported entries. Lookup performs binary search in each visited
block, O(h log e) when expressed with a representative block width. A point update additionally
copies the entry/child vectors and re-encodes each changed block; its cost is proportional to the
sum of changed block widths and block bytes. `create_range` is O(n log n) for stable sorting plus a
linear canonical build after encoding. Full iteration, `shape`, block enumeration, and validation
are O(n + block bytes); inclusive range enumeration is output-sensitive under interval pruning but
has O(n) worst case. Size, height, block count, policy digest, and root hash are O(1).

Update allocation is O(b * depth + c) vector storage and O(depth + c) allocated node objects for the
changed path and any touched collision bucket. Published nodes are immutable and safe for concurrent
readers as long as ordinary C++ object lifetime rules are respected. Merkle updates likewise
allocate only changed blocks and their replacement byte vectors while retaining every untouched
subtree by shared ownership.
