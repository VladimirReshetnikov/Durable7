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

## HAMT maps, bags, builders, and sessions

`PersistentHashMap.getOrAdd(key, addFactory)` and
`addOrUpdate(key, addFactory, updateFactory)` return a `MapUpdateResult` containing the selected
value and persistent map. They hash once, descend once, validate all supplied factories before
hashing, and invoke exactly one selected factory at most once. A hit containing `undefined` remains
a hit because branch selection uses the stored entry rather than its value. Updates retain the
first equivalent key representative. SameValueZero is the TypeScript value-equivalence rule, so an
equal update retains and reports the stored value representative and returns the receiver map.
The small `{ map, value }` result carrier is necessarily allocated even on a map no-op; the no-op
claim concerns CHAMP nodes and successor maps rather than the whole method call.

`PersistentHashBag<T>` is an immutable unordered multiset with one stored representative and a
positive `number` multiplicity per policy class. `distinctCount` counts classes; exact expanded
`totalCount` is an uncapped `bigint`, deliberately avoiding an ambiguous or lossy `size`. Per-class counts
preserve the C# `1 .. 2^31 - 1` bound. Copy-count arguments must be integers in
`0 .. 2^31 - 1` and are validated before hashing; zero-copy updates, missing removals, and empty
clears preserve receiver identity. Expanded iteration repeats each representative contiguously;
`distinctItems()` and `entries()` expose the matching distinct order. `tryGetValue` uses a
presence-discriminated result so a stored `undefined` representative is unambiguous. `toArray()`
preflights its exact total against JavaScript's `2^32 - 1` maximum array length.

Bag algebra accepts another bag and is governed by the receiver's exact `HashPolicy` object:
`union` takes maximum counts, `intersect` takes minimum counts, `except` uses saturated subtraction,
and `sum` uses checked addition. Receiver representatives win surviving classes. A mismatched-policy
argument is eagerly normalized under the receiver policy before shortcuts; collapsed classes are
checked-summed and retain the first representative observed in that argument version's stable
HAMT order. The bag intentionally has no transient, builder, symmetric-difference, arbitrary-
iterable algebra, or content-equality surface.

`HashMapBulkBuilder<K, V>` is a reusable construction-only staging object, also available through
`PersistentHashMap.createBulkBuilder`. It exposes only policy/count state, `setItem`, `setItems`, and
`toImmutable`. First key representatives win, the last SameValueZero-distinct value wins, and equal
values retain the earlier value object. Every freeze copies the reachable CHAMP nodes into a
detached immutable snapshot while leaving the builder reusable; no key-policy callback runs during
that freeze. Staging uses uniquely owned mutable leaf, collision, and bitmap nodes that are never
published directly. The builder remains construction-only and is deliberately separate from the
lookup/removal/adoption lifecycle of a transient session.

`TransientHashSet` exposes the six read-only set relations in addition to lookup and mutation.
Relations use the transient's receiver policy, do not advance its mutation version, and obey the
same one-way lifecycle: every relation throws `TransientConsumedError` after publication before
enumerating its argument.

## Independent insertion-ordered set

`PersistentOrderedSet<T>` is a neutral general-purpose family exported through the `ordered`
subpath. It composes only the public CHAMP map and FingerTree families and never imports or delegates
to Tungsten. A `HashPolicy<T>` defines equality classes; the set retains the first representative,
insertion or explicitly requested order, private sparse `bigint` labels, positional lookup/removal/
ranges, explicit final-index movement, reversal, and stable one-shot sorting.

Algebra and all six relations eagerly normalize the complete argument under the receiver policy,
including another ordered set with a different policy object. Receiver order and representatives win
shared classes; first normalized argument representatives supply argument-only classes. Logical
no-ops preserve the exact receiver, and empty results preserve the policy. Stored `undefined` is
unambiguous through `tryGetValue`'s `{ found, value }` result. TypeScript iterators retain immutable
snapshots but do not emulate the C# struct-enumerator copy/fail-fast mechanics. The full local contract
and API mapping are in [the ordered-set notes](ordered.md).

## Persistence and sharing

The CHAMP, Patricia, measured AVL, lazy range-update AVL, RRB, canonical zip-zip,
Brodal–Okasaki, priority-search, interval, and Merkle cores use immutable nodes and path copying.
No-op operations return the receiver where the corresponding semantic contract defines a no-op.
Builders and transient sessions never mutate an already published persistent version.

TypeScript CHAMP transients preserve O(1) adoption, clean/no-op identity publication, single-owner
semantics, version-bound enumeration, and one-way publication. Their edits call the immutable CHAMP
kernel; they do not claim the C# T2 owner-token in-place mutation bound. Rope cursors likewise preserve
immutable branching, gap semantics, navigation/edit behavior, measures, and text line/column mapping,
but use persistent path-copying edits instead of the C# bounded-window zipper optimization.
Positional and measured cursors expose `peekPreviousEntry`/`peekNextEntry` wrappers so stored
`undefined` is distinct from a boundary. `replaceNext` is an unconditional edit: it publishes a
fresh rope even for the identical object, and measured replacement invokes the supplied element's
measure callback before publication.

`ConcurrentHashTrie` provides synchronous mutation, generation tracking, and O(1) immutable snapshots
inside one JavaScript isolate. It deliberately does not claim the multi-threaded GCAS/RDCSS progress
contract of the C# and Kotlin Ctries; JavaScript object graphs cannot be atomically shared between
workers.

## Lazy range-update sequence

`RangeUpdateSequence<Element, Measure, Tag>` is the independent implicit-AVL sequence with cached
ordered measures and algebraic lazy range tags. Each instance retains one exact
`RangeUpdateAlgebra` object. That runtime object replaces the C# static `TOps` parameter and is part
of sequence identity: canonical empties, source factory shortcuts, and concatenation all preserve or
require the exact object.

`compose(newer, older)` represents older-then-newer application. Pending absence uses a separate
boolean and never `undefined`, a default tag, or `identityTag`, so both stored `undefined` elements
and an `undefined` tag remain valid. Structural edits push tags by immutable path copying. Indexed
reads, range queries, and iteration carry inherited tags and allocate no persistent nodes. Empty
updates are callback-free, recognized identities retain the receiver, whole nonidentity updates
replace only the root wrapper, and counts/ranges retain the C# `Int32.MaxValue` boundary and
validation order.

TypeScript iterators are independent snapshot-bound JavaScript iterators rather than C# copyable
struct enumerators. The port consequently makes no struct-copy fail-fast or same-object worker-thread
claim. The complete contract and API mapping are in the
[range-update sequence notes](range-update-sequence.md).

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

The repository's frozen CHAMP tier, order-maintenance list, persistent chunked bitset, styled-text
rope, and other unshipped frontier/derived-catalog entries remain proposals or explicitly postponed
candidates. Benchmark prototypes are evidence machinery, not package API.
