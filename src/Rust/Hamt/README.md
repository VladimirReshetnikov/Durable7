# Rust HAMT

- Created (UTC): 2026-07-03T00:00:00Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and reviewers of the Rust HAMT port
- Scope: Public crate shape, semantic parity notes, and validation entry point

`durable7-hamt` ports the repository HAMT, integer Patricia, and canonical Merkle
search-tree families to safe Rust. It exposes `PersistentHashMap<K, V, S = RandomState>`,
`PersistentHashSet<T, S = RandomState>`, their one-way `TransientHashMap` / `TransientHashSet`
editing sessions, `PersistentHashBag<T, S = RandomState>`,
`PersistentBiMap<K, V, SK = RandomState, SV = RandomState>`,
`PersistentHashMultimap<K, V, SK = RandomState, SV = RandomState>`, and
`PersistentRelation<L, R, SL = RandomState, SR = RandomState>`, plus strict
`PersistentMapPatch<K, V, S>`, `PersistentDirectedGraph<T, S>`, and
`PersistentIndexedMap<K, V, I, F, SK, SI>`, plus
`BulkBuilder<K, V, S = RandomState>`, the independent one-pass scratch constructor (mutable
unpublished nodes frozen into detached persistent nodes; used by map/set `FromIterator`, set
intersection, set-relation probes). The map additionally
provides one-descent `get_or_add` / `add_or_update` factories; the bag is deliberately persistent
only and exposes neither a transient nor a public builder.

The trie follows the existing ports:

- 32-way logical branching over 32 truncated hash bits;
- canonical CHAMP branch nodes with separate data/node maps, inline payload arrays, and compact
  child-only arrays;
- immutable same-hash collision buckets;
- `Arc`-shared nodes across persistent versions;
- no-op replacement and absent removal reuse the existing root;
- `get_or_add` and `add_or_update` hash once, descend once, invoke exactly one selected closure,
  retain stored key/value representatives on hits and equal-value updates, and return the actual
  selected value beside the successor in `MapUpdateResult`;
- same-policy map/set union, intersection, difference, and symmetric difference combine CHAMP
  slots directly, prune `Arc`-identical subtries, and cache subtree cardinalities; independently
  created hash-policy states retain the receiver-policy semantic fallback;
- map bulk construction uses last-wins semantics.
- `PersistentHashBag` stores positive `i32` multiplicities in the CHAMP map and caches the expanded
  count as `i64`; updates are checked and failure-atomic, default iteration is expanded, and
  distinct/entry iteration avoids expansion. Bag union/intersection/difference/sum use max/min/
  saturated subtraction/checked addition after eagerly rebuilding an independently created
  argument under the receiver's `BuildHasher` policy identity;
- `PersistentBiMap` stores a strict bijection in forward and inverse CHAMP maps, independently
  retains the two `BuildHasher` states, preserves the first `Eq` representative in each domain,
  rejects occupied keys or values, never displaces another key during replacement, removes through
  either domain, and inverts in O(1) by swapping `Arc`-shared roots;
- `PersistentHashMultimap` stores nonempty persistent value sets in a persistent outer map,
  retains independent key/value hash builders, preserves first representatives in both domains,
  distinguishes key and pair counts, contracts the outer key after the last value removal, and
  shares every unaffected inner and outer CHAMP path;
- `PersistentRelation` stores mutually inverse multimaps, normalizes one global representative per
  represented class, supports symmetric pair and whole-domain removal, and creates an inverse by
  O(1) cloning/swapping of existing roots rather than pair traversal;
- `PersistentMapPatch` stores presence-safe before/after states, validates all expectations before
  application, distinguishes absence from a present `None` through nested `Option`, and supports
  inversion and composition with typed conflict results;
- `PersistentDirectedGraph` composes an explicit vertex set and relation, automatically adds edge
  endpoints, permits self-loops but not parallel edges, removes incident edges with a vertex, and
  reverses by O(1) root swapping;
- `PersistentIndexedMap` composes a primary CHAMP and nonunique secondary multimap. Its retained
  `Arc<F>` selector runs only for new or genuinely value-changing rows; removals use the exact
  stored index key and never invoke the selector;
- `PersistentAncestralConnectionForest` stores sparse union-by-size parent cells in a CHAMP map, so
  an absent cell denotes a singleton root and construction is O(1) for any vertex universe. Every
  `link` publishes a distinct `AncestralConnectionVersion` token — a redundant one shares the whole
  connectivity index — and a successful union labels its single new root edge with that token.
  `first_connected` reports the earliest ancestor version connecting a pair in O(w log n) from the
  two current parent paths, without searching the version history;
- map equality and diff traverse same-policy CHAMP nodes in lockstep, use stored hashes, and prune
  every `Arc`-identical descendant before key or value comparison. Diff returns owned typed
  additions, removals, and changes. Across distinct `BuildHasher` policy identities both operations
  retain the semantic lookup fallback: Rust key equivalence is always `Eq`, and each fallback
  lookup hashes with the probed map's own builder. This differs from C# comparer-object identity
  without weakening Rust's `Eq`/`Hash` contract.

The crate also exports `PersistentIntMap`/`PersistentIntSet` and
`PersistentLongMap`/`PersistentLongSet`. Their big-endian Patricia core sign-flips keys for
ascending signed iteration, path-compresses on the highest differing bit, shares immutable `Arc`
subtrees, caches subtree cardinalities, and implements prefix-aware union, intersection, and
difference. Map union and intersection also expose key/left/right combining forms without falling
back to per-entry insertion.

`MerkleSearchTree<K, V>` is the exact paper-style B=16 wide-tree port. A
`MerkleSearchTreePolicy<K, V>` binds comparer semantics and explicitly versioned canonical codecs
into the `mst-sha256-b16-v2` SHA-256 domain. Leading zero key-hash nibbles select levels;
consecutive same-level separators share a wide immutable block. Consequently independently built
maps with equal canonical content have the same shape, exact `MST2` block bytes, and root address.
Updates copy one block path through `Arc`, preserve first-equivalent-key/last-value semantics, and
require neither keys nor values to implement `Clone`. Built-in codecs cover big-endian `i32`/`i64`,
nullable UTF-8, nullable bytes, and RFC-4122/network-order GUID bytes. See the dedicated
[Merkle search tree notes](docs/merkle-search-tree.md) for the hash framing and wire manifest.

The same surface owns full content-addressed persistence: immutable `MerkleBlock` values, a
thread-safe `InMemoryMerkleBlockStore`, complete and partial `MerkleBlockPack` transfer, bounded
strict load/import, `MSP2` point/range proofs, closure-pruned synchronization, and typed three-way
merge. Import verifies the complete reachable closure and preflights every destination conflict
before the first write. All seven verification limits are finite by default, and proof queries are
budgeted before any codec or block decode callback. Merge distinguishes deletion from a present
`None` and never exposes a partial tree while conflicts remain.

Rust-specific shape:

- key equality is Rust's `Eq`; hash policy is supplied through `BuildHasher`;
- updates return new values, while `shares_root_with` exposes structural sharing for validation;
- `into_transient` moves a map/set into a one-way edit session, `to_transient` shares its root, and
  `into_persistent` consumes the session. Active sessions support reads, iteration, point edits,
  removal, clear, and receiver-policy set relations. This first semantic port intentionally
  delegates changed edits to ordinary persistent path copying; it makes no in-place-edit or
  performance claim;
- duplicate inserts return `DuplicateKey` instead of throwing;
- checked bag operations return `HashBagError`; zero-copy updates return before hashing, algebra
  preserves receiver representatives for shared classes, and argument representatives are adopted
  only for classes absent from the receiver;
- iteration is stable for an unchanged map but remains trie-order, not insertion or sorted order.

See [API notes](docs/api-notes.md), [validation](docs/validation.md), and the
[test map](tests/README.md) for the local contract and evidence entry points.

Validate from `src/Rust`:

```powershell
.\test.ps1 -Workspace Hamt
```
