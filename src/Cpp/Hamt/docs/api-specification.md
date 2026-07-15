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

`persistent_hash_bag<T, Hash, KeyEqual>` is an immutable unordered multiset backed by
`persistent_hash_map<T, std::int32_t, Hash, KeyEqual>`. It distinguishes the number of equivalence
classes (`distinct_count`) from the expanded signed-64-bit occurrence count (`total_count`) and
retains one representative per receiver-policy class.

The port intentionally follows C++ value semantics rather than C# reference identity. No-op updates
return a value that shares the same root node as the source; `shares_root_with` and the debug root
inspection helpers expose that property for tests.

The CHAMP map and set also expose nested move-only `transient` types for an explicit edit-then-
publish lifecycle. These sessions port the C# lifecycle and representative contracts, not its
owner-token mutation kernel: each successful content-changing edit invokes the ordinary immutable
operation and path-copies the affected trie region.

`merkle_search_tree<K, V>` is a third, independent immutable map core. It orders keys through an
explicit policy, derives canonical geometric levels from policy-bound SHA-256 key hashes, and emits
exact `MST2` content-addressed blocks. `merkle_persistence.hpp` and `merkle_proofs.hpp` add bounded
verified stores/import, exact `MSP2` proofs, iterative synchronization, and typed three-way merge;
see the [persistence specification](merkle-persistence.md).

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

The persistence surface in `merkle_persistence.hpp` includes:

- immutable `merkle_block`, `merkle_block_pack`, and `merkle_sync_plan` values;
- the abstract `merkle_block_store` and shared-mutex `in_memory_merkle_block_store`;
- immutable seven-limit `merkle_verification_budget` values and classified
  `merkle_verification_error` failures;
- `export_merkle_pack`, `save_merkle_tree`, bounded `load_merkle_tree`, and complete/partial
  `import_merkle_pack`; and
- closure-pruned `create_merkle_sync_pack` plus iterative `plan_merkle_sync`.

The proof/merge surface in `merkle_proofs.hpp` includes immutable `merkle_proof` and
`merkle_proof_step`, exact point/range proof creation, bounded `verify_merkle_proof`, typed
`merkle_merge_value`/`merkle_merge_resolution`, and `merge_merkle_trees`. Public sequence-bearing
values own private vectors and expose const spans only. Load/import/proof validation reconstructs
through the core's canonical entry/node machinery, so move-only keys/values and exact entry-handle
reuse survive across the added layer.

See [Merkle persistence](merkle-persistence.md) for verification order, failure precedence,
publication atomicity, `MSP2` bytes, sync protocol, and present-null merge semantics.

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

## CHAMP Edit Sessions

`persistent_hash_map::create_transient(...)` starts an empty active map session with explicit
policy objects. `map.to_transient()` adopts an lvalue by sharing its immutable root, while
`std::move(map).to_transient()` transfers the map value. The set exposes the same factories. The
session types are movable but not copyable, have no public default constructor, and publish only
through the rvalue-qualified `std::move(session).persist()` operation.

An active map session exposes `count`, `is_empty`, policy access, lookup, `set_item`, duplicate-
rejecting `add`, reporting `try_add`, reporting `remove`, `clear`, iteration, materialization, and
canonical-structure diagnostics. The set session is a thin map-backed facade with lookup,
stored-representative recovery, reporting `add`/`remove`, `clear`, iteration, materialization, and
diagnostics. It also mirrors the persistent set's subset, proper-subset, superset, proper-superset,
overlap, and set-equality queries for initializer lists, persistent sets, and ranges. Relation
arguments are interpreted through the active session's retained hash/equality policy. The first
equivalent key/item representative and the map's `ValueEqual` no-op rule are exactly those of the
persistent receiver.

Adoption does not walk the trie. A clean session, including one that sees only duplicate adds,
absent removals, equal-value replacement, or empty clear, publishes a map/set sharing the source
root and policy identity. A real edit creates the same path-copied immutable successor that the
persistent operation would create and replaces only the session's current value. Consequently,
retained source maps/sets remain isolated and readable throughout editing. This first C++ session
does **not** mutate CHAMP nodes in place and makes no edit-throughput or allocation improvement
claim over persistent updates. It is separate from `bulk_builder`, whose unpublished mutable nodes
serve repeated construction and whose `to_immutable()` snapshots do not consume the builder.

Point-edit candidate construction completes all hashing, equality callbacks, policy copying, and
allocation before commit. Commit changes only the immutable root/count pair and generation through
non-throwing assignments. A candidate-construction failure therefore leaves session contents,
policy objects, generation, and captured-iterator validity unchanged.

Each iterator captures the session generation and retains the immutable root it traverses. A
successful content change invalidates it with `std::logic_error`; a logical no-op does not. Copied
iterators advance independently. Moving a session transfers its iterator control, so pre-move
iterators continue to describe the logical session now owned by the destination. Move assignment
invalidates iterators previously obtained from the overwritten destination. Publication invalidates
all session iterators and consumes the session. Every later read, edit, iteration request, or
publication attempt throws `std::logic_error`; operations on a moved-from session do likewise.
Destroying an active session also makes an iterator's next observation fail deterministically.
A consumed or moved-from variable remains a valid target for move assignment from a fresh session.

If a custom policy throws during session move construction, the source session and its iterators
become terminally invalid. If a custom policy throws during session move assignment, both the source
and overwritten destination sessions, plus both iterator lineages, become terminally invalid. The
partially moved policy/map subobjects remain destructible but cannot be observed through collection
operations. Successful moves retain the ordinary control-transfer behavior above; nothrow-movable
policies avoid this exceptional boundary entirely. The set facade inherits the same rule.

`persist() &&` constructs its result by moving the current map and its policy objects, then marks
the session consumed. With nothrow-movable policies this is a non-throwing publication step. If a
custom policy move constructor throws, the exception propagates before the consume flag is set, but
already-moved map or policy subobjects are not rolled back; the session has no retry/content
preservation guarantee in that exceptional case. Set publication may already have consumed its map
session before a later throwing move into the set wrapper. Callers requiring retryable publication
should provide nothrow-movable policy objects.

The session is unsynchronized and has one logical owner. A caller may move it between threads under
external synchronization, but must not overlap reads or edits on one session. Immutable source and
published values keep the ordinary concurrent-read contract.

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
  `add_or_update(key, add_value, update_factory)` is the public construction-only combine hook: it
  validates a nullable/empty callable before hashing, hashes once, scans one trie path and at most
  one equal-hash bucket, invokes the update callable only on a hit with `(stored, incoming)`, and
  retains stored key/value representatives when the selected value compares equal. Callback and
  key/value-comparer failures occur before the owning root/count commit.
  `to_immutable()` freezes the current contents into a detached persistent map by copying every
  reachable node; the builder never shares mutable storage with frozen maps and stays usable for
  further staging/freeze rounds afterwards.
- `create_transient(hash, equal, values_equal)` creates an empty move-only edit session;
  `to_transient()` adopts an existing value, and rvalue-only `persist()` publishes and consumes the
  session. Session point edits use the persistent path-copy kernel rather than the bulk builder.
- `set_item(key, value)` adds or replaces a key.
- `set_items(items)` adds or replaces entries in enumeration order.
- `add(key, value)` adds a key and throws `std::invalid_argument` when an equivalent key already
  exists.
- `try_add(key, value)` returns `{map, added}` and rejects duplicate keys without throwing.
- `get_or_add(key, add_factory)` returns `{map, selected_value}`. A hit shares the current root and
  returns the stored value without invoking the factory; a miss invokes the factory exactly once
  and stores the caller's key.
- `add_or_update(key, add_factory, update_factory)` returns `{map, selected_value}`. It invokes
  exactly one selected factory exactly once in one hashed descent. The update factory receives the
  caller's lookup key and stored value; the stored key remains in the result. A `ValueEqual` result
  retains and returns the stored value representative and shares the current root.
- `remove(key)` removes a key if present and returns a root-sharing value when absent.
- `try_remove(key)` returns `{map, removed, value}`.
- `try_get(key)` returns a pointer to the stored value, or `nullptr` when absent.
- `try_get_key(equal_key)` returns a pointer to the stored equivalent key, or `nullptr` when absent.
- `at(key)` returns the stored value reference or throws `std::out_of_range`.
- `clear()` returns an empty map preserving the current policy objects.
- `union_with`, `intersect_with`, `except_with`, and `symmetric_except_with` combine maps. Union is
  right-biased for unequal values while retaining the receiver's stored key representative;
  intersection retains receiver entries.
- `map_equals(other)` performs content equality by aligning canonical data/node bitmaps for maps
  retaining one internal policy identity and pruning every pointer-identical descendant before
  invoking key or value equality. Independently created identities retain semantic lookup
  comparison, including compatible equality objects with different coherent hash states.
- `diff(other)` uses the same-identity lockstep descendant pruning and returns owned
  `map_difference<Key,T>` records classified as `added`, `removed`, or `changed`. That structural
  path performs no key rehashing; equal-hash collision runs retain unordered key matching and its
  quadratic bucket worst case. Independently created identities use the semantic lookup fallback.

When replacing an existing key, the originally stored key object inside the trie is retained. When
the existing value compares equal under `ValueEqual`, the root is reused and the stored value object
is retained.

Both persistent factory callables are checked for a null function pointer or false-valued callable
wrapper before the key is hashed, regardless of which branch would be selected. Hashing, key/value
equality, either selected factory, copying, or allocation may throw; no successor is published and
the immutable source remains unchanged. Each operation computes the lookup hash once and descends
the trie once, including a single linear scan of an applicable full-hash collision bucket.

Each map creation owns an internal policy-identity token that is copied into persistent descendants
and builder snapshots. Algebra over one identity aligns CHAMP slots directly, uses cached subtree
cardinalities, and prunes pointer-identical subtries without invoking `Hash`. Algebra between
independently created identities uses the receiver-policy element-wise path because arbitrary C++
policy objects have no generally available equality operation.

## Set Contract

- `empty()`, `create`, and `create_range` mirror the map factories.
- `create_transient`, `to_transient`, and the nested move-only session mirror the map lifecycle;
  set `add` and `remove` report whether membership changed, and its six relation families mirror
  the persistent set's initializer-list, same-set, and range overloads.
- `add`, `try_add`, `remove`, `try_remove`, `contains`, `try_get_value`, and `clear` mirror map
  behavior.
- `union_with`, `intersect_with`, `except_with`, and `symmetric_except_with` return new persistent
  sets. Same-type operands use the map's structural path when their policy identity matches.
- `is_subset_of`, `is_proper_subset_of`, `is_superset_of`, `is_proper_superset_of`, `overlaps`, and
  `set_equals` interpret equality through the set's `Hash` and `KeyEqual` policy objects.

Range overloads of `intersect_with`, `symmetric_except_with`, `is_subset_of`,
`is_proper_subset_of`, `is_proper_superset_of`, and `set_equals` materialize their argument into
`std::unordered_set` using the set's policy objects. Same-type relations use structural
intersection for a shared policy identity and preserve receiver-policy range semantics otherwise.
Range `is_superset_of` and `overlaps` stream their argument and exit early.
Set `create_range` and `intersect_with` assemble their result through the map bulk builder and
freeze it once.

## Hash Bag Contract

- `empty`, `create`, and `create_range` construct bags. Range construction aggregates occurrences
  in enumeration order through one reusable map builder and retains the first representative of
  each equivalence class.
- `distinct_count` is the number of equivalence classes; `total_count` is the expanded occurrence
  count. There is deliberately no ambiguous `count` member.
- `contains`, `count_of`, and `try_get_value` provide membership, multiplicity, and retained-
  representative lookup.
- `add`/`add_copies` and `remove`/`remove_copies` update a class persistently. Copy counts must be
  in `[0, INT32_MAX]`; zero is an identity, removal saturates at zero, and an absent removal is an
  identity. `remove_all` removes the complete class.
- Per-class multiplicities are positive `std::int32_t` values. Addition that would exceed
  `INT32_MAX` throws `std::overflow_error`; `total_count` addition is checked against `INT64_MAX`.
  Validation or arithmetic failure leaves every source bag unchanged.
- `union_with` chooses the maximum multiplicity, `intersect_with` chooses the minimum,
  `except_with` performs saturating receiver-minus-argument subtraction, and `sum_with` performs
  checked addition.
- Algebra always uses the receiver's `Hash`/`KeyEqual` policy. An operand with a different internal
  policy identity is eagerly normalized through a receiver-policy bulk builder before algebra.
  Classes that collapse during normalization have their multiplicities checked and summed in
  operand enumeration order; the first encountered operand representative is retained.
- When a class already exists in the receiver, its representative wins. Argument representatives
  are adopted only for newly introduced classes. Semantic no-ops share the receiver root; union and
  intersection with self are identities, difference with self clears, and algebra with an empty
  argument preserves the usual identity/annihilator laws.
- Ordinary iteration expands occurrences. `distinct_items()` and `entries()` return owning views
  over retained snapshots, and `to_vector()` checks `vector::max_size()` before materializing the
  expanded sequence. `debug_validate_canonical()` checks the backing CHAMP shape, positive
  multiplicities, checked multiplicity sum, and cached total agreement.

Bag enumeration follows the backing map's stable trie order rather than insertion or sorted order.
Equivalent representatives therefore follow receiver/operand precedence, not lexical order. The
bag is an immutable value safe for concurrent reads under the same policy-object lifetime and
thread-safety assumptions as the map.

## Complexity

Let `w` be the hash width used by the port (32 bits), `b` be the branch factor (32), and `c` be the
length of an equal-hash collision bucket.

- Lookup, insert, replace, and remove: O(w / log2(b) + c), effectively bounded by seven trie levels
  plus collision-bucket scan for 32-bit hashes.
- `get_or_add` / `add_or_update`: the same O(w / log2(b) + c) bound, with one hash, one descent,
  and one selected factory invocation (or none for a `get_or_add` hit).
- Enumeration: O(n) time with at most seven inline branch frames.
- Same-identity map equality and diff: O(v + r + Σ cᵢ²), where `v` is the unmatched canonical trie
  region visited after pointer-identical descendant pruning, `r` is the number of reported
  differences, and each `cᵢ` is a visited equal-hash collision-run length. The quadratic terms come
  from unordered pairwise key matching; the overall worst case is O((n + m)²). Independently
  created policy identities use element-wise semantic lookups, each bounded by trie depth plus the
  applicable collision scan, with the same quadratic all-collision worst case.
- Map `create_range` / set `create_range` / set `intersect_with`: O(n * (w / log2(b) + c)) through
  the bulk builder. A mutable unpublished trie is updated in place and frozen once, avoiding
  persistent path copies between successive input entries.
- Bulk builder `set_item` / `add_or_update`: O(w / log2(b) + c) with in-place mutation of
  unpublished nodes;
  `to_immutable`: O(n) node copies producing a detached persistent trie.
- Edit-session adoption/publication: O(1) trie work and no trie traversal; copying or moving custom
  policy objects has the cost defined by those objects. Session lookup and point edits have the
  same bounds and allocation behavior as the corresponding persistent operations because they use
  the same path-copy kernel.
- Edit-session enumeration: O(n), with the persistent iterator's seven inline traversal frames plus
  one O(1) generation check per iterator operation.
- Set algebra implemented from public operations: O((n + m) * update-cost) unless the operation
  only probes membership.
- Bag point edits have map point-operation complexity. Same-policy bag algebra is O((n + m) *
  update-cost) in the current facade; different-policy algebra additionally performs one O(m *
  update-cost) eager normalization and O(m) total-count accounting. Expanded iteration and
  `to_vector` are O(`total_count`), while distinct/entry views are O(`distinct_count`).

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
