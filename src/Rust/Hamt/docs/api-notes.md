# Rust HAMT API Notes

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers implementing and reviewing the Rust HAMT port
- Scope: Rust naming, contracts, and intentional differences from the C# and C++ workspaces

The public crate is `durable7-hamt`, with library name
`durable7_hamt`.

Primary entry points:

- `PersistentHashMap<K, V, S = RandomState>`;
- `PersistentHashSet<T, S = RandomState>`;
- `TransientHashMap<K, V, S = RandomState>`;
- `TransientHashSet<T, S = RandomState>`;
- `BulkBuilder<K, V, S = RandomState>`;
- `DuplicateKey`;
- `MapUpdateResult<K, V, S = RandomState>`;
- `MapDifference<K, V>`;
- `PersistentHashBag<T, S = RandomState>`, `HashBagEntry<T>`, `BagIter<T>`, and `HashBagError`;
- `PersistentBiMap<K, V, SK = RandomState, SV = RandomState>`, `BiMapConflict`,
  `BiMapAddResult`, and `BiMapRemoveResult`;
- `PersistentHashMultimap<K, V, SK = RandomState, SV = RandomState>`, its flattened iterator, and
  invariant result types;
- `PersistentRelation<L, R, SL = RandomState, SR = RandomState>` and its invariant result types;
- `PersistentMapPatch<K, V, S = RandomState>`, `MapPatchEntry`, and typed apply/compose conflicts;
- `PersistentDirectedGraph<T, S = RandomState>` and its invariant result types;
- `PersistentIndexedMap<K, V, I, F, SK = RandomState, SI = RandomState>` and its invariant result types;
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
- one-descent factory updates preserve the existing root on hits and equal-value updates;
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

## One-descent map factories

`PersistentHashMap::get_or_add(key, add_factory)` and
`PersistentHashMap::add_or_update(key, add_factory, update_factory)` return
`MapUpdateResult { map, value }`; `into_parts` is the ownership-friendly destructuring helper. The
result value is the value actually selected for storage, not merely an equal candidate. In
particular, `Arc<V>` callers observe the exact stored allocation on a hit or equal-value update.

Both methods compute the truncated hash exactly once and carry one selector down one CHAMP route,
including inline payloads, child nodes, and same-hash collision buckets. `get_or_add` never calls
its factory on a hit and calls it once on a miss. `add_or_update` calls only the add closure on a
miss and only the update closure on a hit. The update closure receives `(&caller_key,
&stored_value)`, so it can distinguish the lookup representative from the retained stored key.
The first stored key always survives an update.

An update candidate equal under `V: PartialEq` is discarded: the successor shares the receiver's
root, the prior stored value remains authoritative, and `value` is a clone of that stored value.
`get_or_add` therefore needs only `V: Clone`; `add_or_update` adds `V: PartialEq`. A changed
factory-produced value becomes the stored representative and is cloned once for the result record;
for shared handles such as `Arc<V>`, both positions retain the exact same allocation. Hashing,
equality, either selected closure, comparison, or cloning may panic, but all work is over immutable
source nodes and no partial successor is observable.

## Persistent hash bag

`PersistentHashBag<T, S>` is an immutable unordered multiset over
`PersistentHashMap<T, i32, S>`. It retains the first representative of every `Eq` class, requires
every stored multiplicity to be positive and no larger than `i32::MAX`, and caches an expanded
`i64` `total_count` separately from the `usize` `distinct_count`. This separation permits totals
larger than `i32::MAX` without widening every CHAMP payload.

Construction and query surface:

- `new` / `with_hasher` create empty bags, and `try_from_items` /
  `try_from_items_with_hasher` aggregate occurrences in input order;
- standard `FromIterator<T>` is also available and panics with a descriptive message if checked
  bag aggregation overflows, while the `try_*` factories return `HashBagError`;
- `contains`, `count_of`, `get` / `get_stored`, and `get_entry` expose membership, multiplicity, and
  retained representatives;
- `iter` and `IntoIterator for &PersistentHashBag` are expanded: each representative is repeated
  contiguously by its count. `distinct_items` emits one representative per class and `entries`
  emits `HashBagEntry<&T>` without expansion. All three follow stable-for-one-version, otherwise
  unspecified CHAMP order;
- `to_vec` checks conversion to `usize` and `Vec` capacity before cloning expanded items.

`add` / `add_copies` return `Result`. Negative copy counts produce
`HashBagError::NegativeCopies`; zero returns a root-sharing clone before hashing. Positive additions
use the one-descent map factory, check both the per-class `i32` sum and cached `i64` total, and retain
the existing representative. A multiplicity overflow deliberately selects an equal no-op map value
internally and returns the error without publishing that local result. `remove` / `remove_copies`
perform saturated subtraction, delete zero-count classes, and return before hashing for zero;
`remove_all` obtains the removed multiplicity from the map removal result. `clear` preserves the
receiver's `BuildHasher` policy identity.

Bag algebra is receiver-policy algebra:

- `union` takes the larger count for every class;
- `intersect` takes the smaller count;
- `except` performs saturated subtraction;
- `sum` performs checked per-class and total addition.

Before any empty/self shortcut, an argument from an independently created map policy identity is
eagerly rebuilt under the receiver's retained hasher and policy identity. This ensures every later
lookup uses receiver hashes and establishes deterministic representative precedence: a receiver
representative wins for an overlapping class, while the normalized argument representative is
adopted only for an absent class. Normalization and `sum` can return multiplicity or total overflow;
all algebra is failure-atomic because only local persistent successors exist before `Ok` is
returned. No structural-algebra or benchmark claim is made for the bag tranche.

There is intentionally no `PersistentHashBag` transient, edit session, or public bulk builder.
`BulkBuilder` remains the existing construction-only map facility; its public surface was not
expanded for bag mutation.

## Persistent bidirectional map

`PersistentBiMap<K, V, SK, SV>` composes a forward `PersistentHashMap<K, V, SK>` and inverse
`PersistentHashMap<V, K, SV>` into a strict immutable bijection. Rust supplies equivalence through
the types' lawful `Eq`/`Hash` implementations; `SK` and `SV` are independent retained hash-builder
states. `add` returns `BiMapConflict::Key` or `Value`, while `try_add` returns a root-sharing
receiver on conflict and gives key conflict precedence when both classes are represented.

`set` adds a missing pair only when the value is free. For a present key, an equal value is an exact
root-sharing no-op; a distinct free value replaces the old value while retaining the stored key
representative; an occupied value returns `BiMapConflict::Value` and never displaces its owner.
Both maps are updated only in local immutable successors, so a panic in hashing, equality, cloning,
or allocation cannot publish a half-bijection. `try_remove_key` and `try_remove_value` return the
opposite stored representative in `Option<T>` and a root-sharing receiver on a miss; nested
`Option` therefore distinguishes a removed `None` from absence.

`inverse` is O(1) and enumerates no pairs: it clones two small map facades and swaps their
`Arc`-shared roots and hash-builder roles. Rust collections are values, so the observable contract
is `inverse().inverse().shares_roots_with(source)`, not object-reference identity. The bimap has no
algebra, transient, builder, or displacing force-put surface, and honestly stores approximately two
map entries per logical pair.
## Persistent hash multimap

`PersistentHashMultimap<K, V, SK, SV>` is a set-valued multimap composed from the public CHAMP map
and set. Rust `Eq`/`Hash` define both equivalence domains, while `SK` and `SV` retain independent
`BuildHasher` policies. `key_count` reports nonempty groups and `pair_count` reports distinct pairs;
duplicate insertion returns a clone sharing the outer root.

Every stored group is nonempty and uses a clone of the retained value hasher. Removing its final
value removes the outer key in the same successor. `get_key` and `get_value` recover first stored
representatives, `groups` exposes immutable adjacency sets, and `iter` flattens them in stable-for-
one-version nested CHAMP order. `try_remove_key` returns the stored key and persistent adjacency set.
The type has no multiplicity, algebra, transient, or mutable builder.

## Persistent relation

`PersistentRelation<L, R, SL, SR>` maintains mutually inverse persistent multimaps. Addition first
normalizes through existing outer representatives so one first representative is reused globally
across all adjacency groups. Pair removal contracts both indexes; `try_remove_left` and
`try_remove_right` remove all incident pairs and return the stored representative plus immutable
adjacency set. Each whole-domain removal costs O(d log n) for degree d.

`inverse` clones and swaps the two existing root pairs in O(1) and performs no traversal. Rust
facades are ordinary values, so the port does not build a cyclic identity cache: applying `inverse`
twice yields a facade sharing both original roots, which is the ownership-native counterpart of the
C# inverse-identity contract. The honest space cost remains two indexes.

## Derived persistent structures

`PersistentMapPatch<K, V, S>` stores strict `Option<V>` before/after states in a persistent map.
The outer `Option` represents presence, so `V = Option<T>` distinguishes absence from a present
`None`. `between` consumes structural `diff` results when maps share policy lineage and inherits its
semantic fallback otherwise. `apply` validates every expectation before building a successor and
returns `MapPatchConflict<K>` without changing the source on mismatch. `invert` swaps states;
`compose` verifies shared intermediate states and removes net no-ops. Rust `PartialEq` is the value
policy, and same-type hash builders are semantically compatible even when independently created.

`PersistentDirectedGraph<T, S>` stores explicit vertices in a persistent set and unique directed
edges in a relation using clones of the same hash builder. Adding an edge adds missing endpoints and
normalizes them through the vertex set's first representatives. Self-loops are allowed; parallel
edges collapse. Edge removal retains vertices, while vertex removal deletes incoming, outgoing,
and self-loop edges. `reversed` clones/swaps relation roots in O(1), and reversing twice shares all
original roots. Missing adjacency queries return `None`, following the Rust multimap API.

`PersistentIndexedMap<K, V, I, F, SK, SI>` stores primary entries with their exact selected index
representative and maintains `PersistentHashMultimap<I, K, SI, SK>` in reverse. The selector is
retained as `Arc<F>` and runs once for a new row or a `V: PartialEq`-distinct update. Duplicate add,
equal-value update, removal, lookup, enumeration, and validation never invoke it. Changed rows move
groups only when the selected `I: Eq` class changes. A selector panic or hashing/equality panic can
abandon only unpublished persistent successors; every source root remains valid.

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
  the closest Rust analogue. Bag metadata and iteration likewise require no bounds, lookup requires
  only `T: Eq + Hash, S: BuildHasher`, expanded materialization requires `T: Clone`, and persistent
  edits/algebra add the cloning bounds needed for path copies and retained policies;
- the map, set, and bag implement content-based `PartialEq`/`Eq` (the C# reference uses reference
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

## Persistent cursors

The crate ships two public cursor families: the ordered Patricia cursors described below and
`MerkleSearchTreeCursor<K, V>`, specified in the
[Merkle ordered persistent cursor](merkle-search-tree.md#ordered-persistent-cursor) section.
CHAMP maps, sets, bags, bimaps, multimaps, relations, patches, indexed maps, and directed graphs
deliberately have no public cursor, because hash-trie enumeration has no semantic neighbor; edit
sessions and bulk builders are mutable lifecycles rather than persistent aggregates.

Both families are **Profile R root-plus-rank semantic checkpoints**. A cursor is a retained
collection value plus a validated `usize` gap, every edit delegates to the ordinary persistent
operation, and no focused representation, breadcrumb path, memoized snapshot, callback ceiling,
allocation bound, or amortized-locality claim from the C# rope tier is inherited.

Rust ownership supplies the invalid-default contract the C# struct cursors need. A cursor has no
`Default`, no moved-from state is observable, and use-after-move is a compile-time error rather than
a documented exception, so every value a caller can name is fully initialized. An empty cursor is an
ordinary initialized value over an empty collection with `len() == 0` and `position() == 0`, not a
degenerate state; `is_at_start` and `is_at_end` are both true for it.

### Ordered Patricia cursors

`PersistentIntMapCursor<V>`, `PersistentLongMapCursor<V>`, `PersistentIntSetCursor`, and
`PersistentLongSetCursor` are generated by the private `map_cursor_type!` and `set_cursor_type!`
macros in `patricia.rs`, so the four types have identical bodies over `i32`/`i64` keys. They are
re-exported from the crate root alongside `PatriciaCursorEditError`. The semantic axis is the same
ascending signed-key order the collections already promise: the sign-bit transform keeps in-order
trie traversal ascending across the minimum, zero, and maximum boundaries.

Factories are complete on both the map and the set. Maps offer `cursor()`, `cursor_at(position)`,
`cursor_at_end()`, `lower_bound_cursor(key)`, `upper_bound_cursor(key)`, and `cursor_at_key(key)`;
sets offer the same five plus `cursor_at_item(value)`. `cursor_at` returns `Option` and rejects a
position above `len`; every other factory is total. The exact-search factories return a bare
`($Cursor, bool)` tuple rather than a named search record, so a miss still yields the usable
lower-bound insertion gap. This is deliberately different from the FingerTree and Ordered crates,
which use named `OrderedCursorSearch { found, cursor }` carriers.

Navigation is `len`, `is_empty`, `position`, `is_at_start`, `is_at_end`, borrowed `peek_previous` /
`peek_next`, `move_previous` / `move_next`, `seek(position)`, and `snapshot()`. Boundary movement
and an out-of-range seek return `None`; the receiver is never consumed or invalidated. `snapshot`
returns an owned root-sharing collection clone, not a borrow. Navigation, peeking, and snapshotting
impose no `V: Clone` bound.

Edits are the ordinary Patricia operations under a gap precondition. Map `insert(key, value)`
returns `Result<Self, PatriciaCursorEditError>` and rejects an existing key with `DuplicateKey`, or
a key whose lower-bound rank is not the current gap with `WrongGap { expected, actual }`. `put`
performs the same gap check, then updates the exact next entry or inserts at a missing lower-bound
gap. Set `insert(value)` performs only the gap check: an exact duplicate is a root-sharing no-op
that returns the receiver cursor rather than an error, matching the ordinary set's duplicate rule.
`set_next_value(value)`, `delete_previous()`, and `delete_next()` return `Option<Self>` and report
absence of a neighbor as `None`. Map edits require `V: Clone + PartialEq`; set edits require no
element bounds beyond the fixed-width key.

Gap conventions after each edit are:

| Operation | Resulting gap |
| --- | --- |
| map `insert`, set `insert` on an absent key | `position + 1`, immediately after the new entry |
| map `put` on a miss | `position + 1` |
| map `put` on an exact hit | `position`, unchanged |
| set `insert` on an exact duplicate | `position`, unchanged, receiver root retained |
| `set_next_value` | `position`, unchanged; the stored integer key is retained |
| `delete_previous` | `position - 1` |
| `delete_next` | `position`, unchanged |

`put` and `set_next_value` recognize the map's ordinary equal-value no-op through
`shares_root_with` and return the receiver cursor unchanged, so a semantic no-op neither allocates a
new version nor moves the gap. Keys are `Copy` scalars and sets store no representative, so there is
no stored-representative substitution question in this family; the integer key of a replaced entry
is reused verbatim.

Let `W` be the key width, 32 or 64. `len` and `position` are O(1) reads of cached branch
cardinalities. Creating a cursor at start, end, or a rank is O(1); a bound or exact factory is O(W).
Moving the gap is O(1) because it only rewrites an integer and clones an `Arc`, but **reading** the
neighbour afterwards is an unconditional O(W) root descent through `entry_at`, so a complete
in-order traversal by move-plus-peek costs O(n · W) and not O(n). Edits are O(W) for the gap check
plus O(W) for the delegated insert or remove. Snapshot is O(1) in both the clean and the dirty case
because the cursor already retains the canonical root; there is no snapshot memo to describe.
Context space is O(1) — no frames are retained.

Cursors are `Send + Sync` on exactly the same terms as their source collections and payloads. A
comparison-free integer key means navigation invokes no user callback at all; only `V: PartialEq`
and `V: Clone` run during edits, and a panic in either leaves the receiver and every retained cursor
usable because the successor is assembled before publication.

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

`MerkleSearchTreeCursor<K, V>` adds comparer-ordered gap navigation and persistent editing over one
retained trusted tree. Its factories, gap conventions, `MerkleCursorEditError` channel, trust
boundary, and honest per-operation bounds are specified in
[Merkle search tree](merkle-search-tree.md#ordered-persistent-cursor); the complete format and shape
contract is in the same document.

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
