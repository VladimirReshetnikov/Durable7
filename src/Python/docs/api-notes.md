# Python API and semantic notes

- Created (UTC): 2026-07-15T00:31:34Z
- Repository HEAD: fa29fbb535a231b166e75ea873d56f170a609a87

This document records Python-specific mappings for the repository's shared semantic contracts.
The package source and executable tests are authoritative where an API detail is not repeated here.

## Runtime mappings

- Persistent collections are immutable values. Updates return a new snapshot; identity-preserving
  no-ops return the original object where the contract makes that observable.
- Hash collections accept a runtime `HashPolicy`. The default follows Python equality and hash
  semantics, with an identity fallback for values that do not provide a hash.
- CHAMP edit sessions are explicitly one-way and version-bound. They provide the shared transient
  lifecycle but make no in-place-update performance claim.
- `ConcurrentHashTrie` is a thread-safe, lock-coordinated Python facade with constant-time immutable
  snapshots. It is not represented as the lock-free GCAS Ctrie of the managed reference workspace.
- Python `int` supplies the storage substrate for Patricia long keys, fixed-width integers, and
  sparse integers. Public boundaries still enforce signed 32-bit keys and fixed-width wrapping,
  checked arithmetic, byte order, and two's-complement behavior.
- Strings are indexed by Python Unicode code point. Consequently `TextRope` positions differ from
  UTF-16-code-unit workspaces for non-BMP characters; all internal Python text APIs use one
  consistent coordinate system.

## Hash collections

`PersistentHashMap` and `PersistentHashSet` use immutable 32-way CHAMP bitmap nodes and full-hash
collision buckets. Point edits path-copy only the affected spine, comparer-equivalent replacement
keeps the stored key representative, and semantic no-ops keep object identity. Algebra between maps
requires the same policy object where structural compatibility matters; set algebra accepts any
iterable and applies the receiver's policy.

The shared default policy follows coherent Python hash/equality behavior. Hashable values use
`hash` and `==`; the identical-object fast path recovers non-reflexive values such as a retained
`NaN`. Unhashable objects use process-local identity hashing and are equivalent only to themselves;
use `create_hash_policy` for structural unhashable keys. Hashes are normalized to signed 32 bits.

Transient sessions retain the cross-language one-way lifecycle, clean-publication identity, and
version-invalidated iterators. Their changed edits remain persistent path copies; Python makes no
owner-token in-place-update claim.

Patricia int maps/sets enforce signed 32-bit or signed 64-bit boundaries and traverse in ascending
signed-key order. Negative keys are masked before sign-bit biasing so Python's infinite-width
negative integers cannot leak into trie prefixes.

## Measured and frontier structures

The general sequence substrate is a persistent implicit AVL tree caching caller-supplied ordered
monoid measures. Deques, sorted collections, stable priority queues, max-high interval trees, ropes,
and immutable gap cursors share its path-copied nodes.

Extremal measures use the explicit `OptionalValue` carrier rather than reserving `None` as their
identity. Consequently, `MaxMeasure` and `MinMeasure` can measure nullable elements without losing
the distinction between an empty tree and a tree whose extremum is `None`; interval summaries use
the same presence-preserving representation for nullable endpoints.

Sequence, measured-tree, and measured-rope splits and cursor searches return frozen named
dataclasses, so callers can use semantic fields such as `left`, `right`, `item`, `cursor`, and
`found`. A measured-rope concatenation requires the exact same measure-policy object even when one
operand is empty. `RopeCursor.peek_previous()` and `peek_next()` remain convenient nullable reads;
the corresponding `peek_previous_entry()` and `peek_next_entry()` methods wrap a present value in
`RopeCursorPeek`, preserving the distinction between a stored `None` and a cursor boundary.

The frontier structures keep their actual sibling algorithms rather than flattening to tuples or
dicts: 32-way regular/relaxed RRB nodes, HMAC-SHA256 `ZZT2` zip-zip ranks, bootstrapped skew-binomial
Brodal-Okasaki heaps, winner-cached AVL priority-search queues, and the six-cursor chunked DABA Lite
schedule. Canonical built-in ranks use stable integer/UTF-8/byte hashes, never Python's salted
string hash. Custom objects require an equivalence-coherent `rank_hash` policy.

DABA Lite is mutable and intended for single-threaded mutation. Insert and evict stage all monoid
callbacks before publication, so callback failure leaves the queue unchanged. Clearing remains
O(1) in queue-structure work by publishing a fresh one-block chain after obtaining the identity.
The detached bidirectional chunk chain may be reclaimed later by Python's cyclic collector, so the
operation does not provide the prompt deterministic O(n+c) destruction contract of the native ports.

## Tungsten and numerics

Tungsten collections remain an application-specific dependency leaf. `PersistentAssociation`
uses a CHAMP key index plus measured order sequence with sparse `2^20` stamps, midpoint insertion,
and deterministic relabeling. Position-preserving `set_item` keeps the first stored key
representative; append/prepend/insert move an existing key using the incoming representative.

The six fixed-width integer classes wrap construction and ordinary operators modulo their width,
while `parse` and `checked_*` methods reject out-of-range results. Signed division truncates toward
zero and remainder has the dividend's sign, including explicit minimum-value divided by `-1`
overflow. Exact-width byte conversion is deterministic in either byte order. Python `int` is also
the direct arbitrary-precision substrate for non-negative `SparseInteger`.

## Merkle interoperability

`MerkleSearchTree` uses policy identity `mst-sha256-b16-v2`, SHA-256, strict canonical codecs, the
byte-exact `MST2` block format, seven independent verification budgets, and canonical `MSP2`
membership, absence, and inclusive-range proof envelopes. Persistence and synchronization publish
authenticated closures only; merge values distinguish absence from a present `None`.

The bundled `InMemoryMerkleBlockStore` provides synchronized, atomic multi-block publication.
Third-party protocol stores receive complete verification and conflict preflight before sequential
publication, but cannot acquire transactional guarantees the protocol does not expose.
