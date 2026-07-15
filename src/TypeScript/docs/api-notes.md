# TypeScript API and semantic notes

- Created (UTC): 2026-07-15T00:12:55Z
- Repository HEAD: 6bf20605073b1750d871d4bd53ef75fcfe25484c
- Scope: TypeScript mappings for repository-wide semantic contracts

## Runtime mapping

TypeScript has no user-defined operators, value types, deterministic finalization, or shared-memory
object graph. Consequently, fixed-width integers expose named arithmetic methods, missing-value APIs
use entries or discriminated unions where `undefined` may be a stored value, and all immutable
collections are reference objects. Signed 64-bit keys and values use `bigint`; 32-bit keys use
`number` with range checks.

`PersistentHashMap` follows JavaScript `Map`-style default key equivalence: SameValueZero for
primitives and identity for objects. Callers can supply a `HashPolicy` for structural keys. Equivalent
replacement retains the stored key representative across the HAMT, Patricia, sorted, Merkle, and
Tungsten families.

## Persistence and sharing

The CHAMP, Patricia, measured AVL, RRB, canonical zip-zip, Brodal–Okasaki, priority-search, interval,
and Merkle cores use immutable nodes and path copying. No-op operations return the receiver where the
corresponding semantic contract defines a no-op. Builders and transient sessions never mutate an
already published persistent version.

TypeScript CHAMP transients preserve O(1) adoption, clean/no-op identity publication, single-owner
semantics, version-bound enumeration, and one-way publication. Their edits call the immutable CHAMP
kernel; they do not claim the C# T2 owner-token in-place mutation bound. Rope cursors likewise preserve
immutable branching, gap semantics, navigation/edit behavior, measures, and text line/column mapping,
but use persistent path-copying edits instead of the C# bounded-window zipper optimization.

`ConcurrentHashTrie` provides synchronous mutation, generation tracking, and O(1) immutable snapshots
inside one JavaScript isolate. It deliberately does not claim the multi-threaded GCAS/RDCSS progress
contract of the C# and Kotlin Ctries; JavaScript object graphs cannot be atomically shared between
workers.

## Exact Merkle interoperability

The Merkle policy domain, canonical codecs, base-16 key levels, wide canonical topology, empty-tree
manifest, and node blocks match `mst-sha256-b16-v2`. `MST2` blocks and `MSP2` query descriptors are
byte-identical to sibling ports. Built-in codecs cover int32, int64, nullable strict UTF-8, nullable
bytes, and RFC-4122 UUIDs. Verification authenticates hashes, domains, codec round trips, ordering,
levels, child intervals, subtree counts, exact reserialization, closure completeness, and seven
finite budgets before publication.

The store API is synchronous because Node's in-memory and common embedded stores are synchronous.
Custom remote stores should stage blocks asynchronously outside the tree, then call verified import
or load against a synchronous snapshot.

## Numerics

`UInt256` through `Int1024` store canonical wrapped `bigint` values. Arithmetic methods reproduce
fixed-width two's-complement wrapping; checked factories and operations reject overflow. Shifts,
rotates, bitwise operations, signed division corner cases, radix parsing, formatting, and endian
conversion are differential-tested against native `bigint` models. `SparseInteger` preserves the
nonnegative arbitrary-precision semantic surface using native `bigint`; unlike the C# implementation,
it does not need a recursive sparse representation because JavaScript already supplies arbitrary-
precision integers.

## Deliberate non-ports

The repository's frozen CHAMP tier, range-update sequence, order-maintenance list, persistent chunked
bitset, styled-text rope, and other frontier/derived-catalog entries are proposals or explicitly
postponed candidates rather than existing shipped data structures. They are therefore not presented
as TypeScript ports. Benchmark prototypes are evidence machinery, not package API.
