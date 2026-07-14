# Rust HAMT API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust HAMT port
- Scope: Rust naming, contracts, and intentional differences from the C# and C++ workspaces

The public crate is `tools-data-structures-hamt`, with library name
`tools_data_structures_hamt`.

Primary entry points:

- `PersistentHashMap<K, V, S = RandomState>`;
- `PersistentHashSet<T, S = RandomState>`;
- `TransientHashMap<K, V, S = RandomState>`;
- `TransientHashSet<T, S = RandomState>`;
- `BulkBuilder<K, V, S = RandomState>`;
- `DuplicateKey`;
- `MapDifference<K, V>`;
- `PersistentIntMap<V>` / `PersistentIntSet` and `PersistentLongMap<V>` / `PersistentLongSet`.
- `MerkleSearchTree<K, V>`, `MerkleSearchTreePolicy<K, V>`, `MerkleEntry<K, V>`, and
  `MerkleMapDifference<K, V>`;
- `MerkleCodec<T>`, `MerkleDigest`, the strict built-in codecs, and `Rfc4122Guid`.

The port follows the repository HAMT semantics:

- updates return new persistent values and keep old versions usable;
- nodes are immutable and shared through `Arc`;
- the trie uses 32-way CHAMP branching over 32 truncated hash bits, with independent data/node maps,
  compact inline `(hash, key, value)` payload runs, and child-only node runs;
- deletion promotes a singleton child payload back into its parent to restore canonical shape;
- equal full-hash collisions are kept in immutable collision buckets;
- no-op value replacement and absent removal preserve the existing root;
- duplicate `add`/`try_add` calls reject the key without changing the root;
- replacing an existing key retains the originally stored key object;
- bulk map construction is last-wins.
- set algebra includes union, intersection, difference, symmetric difference, subset/superset,
  proper subset/superset, overlap, and equality checks. Same-type operations whose maps descend
  from one hash-policy identity use cached-cardinality CHAMP combination and prune `Arc`-identical
  subtries without rehashing. Independently created policy identities use the existing semantic
  receiver-policy path, even when their `BuildHasher` values happen to share a type.
- map equality and typed diff likewise align the canonical logical CHAMP slots for one policy
  identity, prune every `Arc`-identical descendant, and navigate only with stored hashes. Equality
  is collision-order independent. Diff preserves receiver representatives for removals and changed
  keys, and target representatives for additions and replacement values. Independently created
  policy identities retain semantic lookup-based equality and diff.

Iterable set difference removes each probe element from the receiver, and iterable symmetric
difference toggles the distinct probe elements on the receiver. The `*_set` same-type variants use
structural CHAMP algebra when policies are compatible. Both surfaces preserve untouched subtries
and the receiver root for applicable no-op cases.

## One-way edit sessions

`PersistentHashMap::into_transient` and `PersistentHashSet::into_transient` move a persistent value
into a single-owner session. Their `to_transient` counterparts clone only the value wrapper and
share the exact current root and internal hash-policy identity. `TransientHashMap::new` /
`with_hasher` and the corresponding set factories create empty sessions directly.

The active map session exposes length, hash-policy access, lookup, stored-key recovery, key/value/
entry iteration, replacement-style `insert`, duplicate-rejecting `try_add` / `add`, `remove` /
`remove_entry`, and `clear`. The set session is a thin facade with length, policy access, membership,
stored-representative recovery, iteration, `insert`, `remove`, `clear`, and all six set relations.
Relation arguments are deduplicated with the session's retained hasher/equality policy whenever
cardinality matters. The borrow checker keeps an active iterator from overlapping a mutation.

Publication is deliberately one-way and ownership-native: `into_persistent(self)` consumes the
session. There is no reusable snapshot method, no session `Clone`, and no runtime inactive state.
Consequently a read, edit, iterator request, or second publication after successful publication is
a compile-time ownership error rather than the runtime disposed-state check required by C# aliases.

Moving adoption through `into_transient` and consuming publication are O(1). Borrowing adoption
through `to_transient` performs O(1) trie work plus the cost of `S::clone`. This first Rust port is a
semantic and lifecycle surface, not an owner-token performance kernel: each logically changed point
edit calls the ordinary persistent CHAMP operation, path-copies O(w + c) nodes/entries along the
affected route, and installs the fully constructed successor only after that operation returns. A
panic in hashing, equality, value equality, cloning, or allocation therefore cannot publish a
partial session state. Duplicate adds, absent removals, equal-value replacements, and clearing an
empty session do not replace the current root. A session containing only such no-ops publishes the
exact adopted root and policy identity. Existing source values and all published results remain
immutable and isolated.

`BulkBuilder` is a separate scratch-construction mechanism. It mirrors the C# reference's internal
bulk builder (commit `c092016`): unpublished
leaf, collision, and split-map CHAMP branch nodes are mutated in place and frozen into detached persistent nodes, so
one-pass construction costs O(n (w + c)) node mutations — bounded trie depth plus the applicable
equal-hash collision scan — with no persistent path copies between successive entries. `set_item`
follows the map's duplicate rule (first stored key instance, last supplied value, earlier stored
value retained on an equal replacement), `to_immutable` freezes a detached snapshot and leaves the
builder usable, and `into_immutable` consumes the builder by moving nodes without cloning. Map and
set `FromIterator`, set intersection, and the receiver-policy probe sets built by the binary set
relations route through the builder; incremental updates on existing collections keep their
structural-sharing paths. The Tungsten association's relabel/sort/reverse and small-side
`get_range` index rebuilds consume the builder as well.

Rust-specific differences:

- key equivalence is Rust `Eq`; the hash policy is supplied through `BuildHasher`;
- duplicate insertion returns `Result<_, DuplicateKey>` rather than throwing; `DuplicateKey`
  implements `Display` and `std::error::Error`;
- lookups return references, and removal returns owned cloned values; `try_remove_entry` also
  surfaces the stored key;
- transient edit sessions follow the same bound split: active reads require the ordinary lookup
  bounds; changed point edits require the persistent update `Clone` / `PartialEq` bounds;
- `shares_root_with` exposes root sharing for tests and diagnostics;
- iteration streams trie order through an explicit traversal stack rather than materializing all
  entries up front; map iteration yields `Iter` and set iteration yields `SetIter`, both `Clone` +
  `ExactSizeIterator` + `FusedIterator`, and `&map` / `&set` implement `IntoIterator`;
- trait bounds are per-operation: construction, length, sharing probes, and iteration require no
  bounds; lookups require `K: Eq + Hash, S: BuildHasher`; removal additionally requires
  `K: Clone, V: Clone, S: Clone`; only the insert family requires `V: PartialEq` (for the
  no-op value check). The C# reference imposes no compile-time constraints, so relaxed bounds are
  the closest Rust analogue;
- both collections implement content-based `PartialEq`/`Eq` (the C# reference uses reference
  equality and makes no value-equality claim), `Debug`, and `Default` for any `S: Default`;
  `FromIterator` is available for any default-constructible hasher policy; the map implements
  `Index<&K>` which panics on a missing key.

The hash contract is the standard Rust hash-map contract: keys that compare equal must hash equally under the
chosen `BuildHasher`. The implementation truncates `Hasher::finish()` to 32 bits to match the repository HAMT
shape.

The integer-specialized facades do not hash. They sign-flip `i32`/`i64` keys into unsigned order
and store compressed common prefixes plus the highest differing branch bit. Iteration is ascending
signed order. `union` is right-biased for duplicate map keys, `intersect` retains left values, and
`except` removes right-side keys. `union_with` and `intersect_with` additionally accept an
`FnMut(key, left, right) -> value` combiner that is invoked once for every shared key; the left and
right arguments always correspond to the receiver and the other map, respectively. All algebra
aligns Patricia prefixes and reuses whole subtrees. When a combiner returns receiver-equal values
and no key-set change is needed, the receiver root is preserved.

Every Patricia branch caches its subtree cardinality. Updates maintain that invariant while
rebuilding the affected path, and structural algebra reads the result count from the root in O(1)
instead of traversing the merged tree after the operation.

## Merkle search tree

The Merkle family deliberately uses its own `MerkleKeyComparer<K>` and `MerkleCodec<T>` policy
instead of Rust hashing. `MerkleSearchTreePolicy` retains those objects in `Arc`, hashes the
algorithm ID, application policy ID, and both codec IDs into a domain digest, and rejects codec IDs
without an explicit terminal `-v<digits>` suffix. Key codecs must be injective over comparer
equivalence classes; value codecs must be canonical. Built-in decoders consume exactly one value
and reject wrong widths, invalid nullable tags, null trailing data, and malformed UTF-8.

Entries retain key/value objects and their encodings behind shared handles. `set_item`, `remove`,
`clear`, canonical bulk construction, in-order/range iteration, map equality, semantic diff,
shape/block diagnostics, and full invariant validation impose no `Clone` bound on `K` or `V`.
Equivalent replacement keys preserve the first stored representative; the final value wins.
Equal encoded replacements and absent removals preserve root identity.

The complete format and shape contract is in [Merkle search tree](merkle-search-tree.md).

`MerkleBlockStore` uses shared-reference mutation so a store can be used concurrently and through a
trait object. `InMemoryMerkleBlockStore` protects immutable blocks with `RwLock`; a same-address,
same-byte write is idempotent, while a same-address byte conflict is classified and rejected.
`save` and destination-backed `import` inspect every supplied address before their first write.
`MerkleBlockPack` preserves deterministic transfer order, rejects duplicate addresses, and may be
complete or partial when a destination store supplies the rest of the closure.

`load_with_budget`, `import_with_budget`, and `verify_proof_with_budget` use
`MerkleVerificationBudget` to bound distinct blocks, cumulative bytes, one-block bytes, reference
depth, cumulative decoded entries, child references per block, and proof-query bytes. `new` and
`with_six_limits` validate positivity and the two byte-limit relationships. The fields intentionally
remain public for trusted local policy derivation, so mutating a `default` value bypasses those
constructor checks; untrusted configuration must go through a checked constructor. The decoder
authenticates each digest, domain, entry codec round trip, key-derived level, comparer order, exact
trailing-free `MST2` bytes, child interval, subtree count, and final reconstructed root. Failures
return `MerkleVerificationError` with `MerkleVerificationFailureKind` and an offending digest when
known. Query byte limits run before envelope, codec, hash, or block work.

Point and inclusive-range proofs carry an opaque canonical `MSP2` query plus uniquely addressed
`MerkleProofStep` blocks. Each step declares exactly the child intervals expanded by the proof;
opaque child hashes remain authenticated boundaries. `create_sync_pack` prunes a receiver's known
verified closures, while `plan_sync` requests the first absent block on every target path so a
partial receiver can be repaired iteratively.

`merge` and `merge_by` operate over shared entry records, so they require no `Clone` bound. A
`MerkleMergeValue::Present(Arc<V>)` is distinct from `Absent`, including when `V` itself is
`Option<T>` and the present value is `None`. Resolvers may select a side, base, deletion, or a new
value. Any unresolved conflict produces all typed conflicts and no partial tree.
