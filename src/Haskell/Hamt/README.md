# Haskell HAMT

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell persistent HAMT port
- Scope: `durable7-hamt` package

This package ports the repository's persistent map cores to Haskell. It provides persistent
`HashMap`, `HashSet`, `HashBag`, `HashMultimap`, and `Relation` values with a canonical 32-way CHAMP trie, strict split data/node maps,
inline payload runs, immutable equal-hash collision buckets, structural sharing between versions,
and optional runtime `HashPolicy` values for custom hash/equality behavior. Same-policy maps expose
lockstep node-based `mapEquals` and typed `MapDifference` classification; cross-policy maps retain
semantic lookup comparison. Right-valued union, left-valued intersection, difference, and
symmetric difference are implemented by direct CHAMP-slot combination.

`HashMultimap k v` stores only nonempty `HashSet v` groups under independent runtime key and value
policies, with distinct-key and checked pair counts, first representatives, pair/group edits, and
receiver-policy algebra. `Relation a b` maintains exact forward and reverse multimaps; every pure
edit constructs both successors together, and `validStructure` verifies complete inverse parity.

`PersistentMapPatch k v` stores strict presence-safe before/after states and supports preflight
apply, inversion, and compatible composition. `PersistentDirectedGraph v` combines explicit
vertices with a bidirectional relation, and `PersistentIndexedMap k v i` combines primary rows with
one selector-maintained nonunique secondary index. The modules preserve runtime hash policies and
stored representatives through idiomatic pure `Maybe`/`Either` results.

## One-Descent Map Factories

`Durable7.Hamt.HashMap` exposes the following persistent point combinators:

- `getOrAdd key addFactory map`;
- `addOrUpdate key addFactory updateFactory map`;
- `getOrAddEither` and `addOrUpdateEither`, whose selected factories may return `Left`.

Each successful operation returns `(successorMap, selectedValue)`. The key hash is evaluated once,
one CHAMP route determines presence and builds the successor, and only the selected factory is
evaluated. `getOrAdd` evaluates no factory on a hit. `addOrUpdate` evaluates the add factory on a
miss or the update factory on a hit, never both; the update factory receives the caller's lookup key
and stored value. The fallible variants are the Haskell-local analogue of a throwing callback and
let checked clients compose failure without partial functions.

An absent class stores the caller key. A present class always retains its first stored key
representative. `addOrUpdate` requires `Eq` only for values: an identical or equal update retains the
stored value representative, returns that stored value, and returns the exact source root. The
unconstrained `getOrAdd` does not compare values. Hash-policy exceptions, selected pure-factory
exceptions, value-equality exceptions, and selected `Left` results expose no successor; all source
versions remain immutable and usable. Selected factory results are evaluated to weak head normal
form as part of producing the result pair, matching the existing strict `adjust`/`mapValues`
checkpoint rather than deferring a selected callback behind a stored thunk. A `getOrAdd` hit does
not force the pre-existing stored value merely to return the result pair or successor root. These
are semantic operation-count guarantees, not timing or benchmark claims.

## Persistent Hash Bag

`Durable7.Hamt.HashBag` is an immutable unordered multiset backed by
`HashMap k Int32`. It stores one positive multiplicity and one retained representative per
`HashPolicy` equivalence class. `distinctCount :: Int` reports the number of classes and
`totalCount :: Int64` reports expanded occurrences; the module deliberately exports no ambiguous
`size`/`count`, public builder, or transient bag. `HashBagError` distinguishes a negative copy
request, per-class `Int32` overflow, and defensive `Int64` total overflow. Every operation that can
encounter one of those conditions returns `Either HashBagError`; a `Left` contains no partial bag.

The primary surface is:

```haskell
data HashBagError
  = NegativeCopies Int32
  | MultiplicityOverflow
  | TotalCountOverflow

empty        :: (Eq k, Hashable k) => HashBag k
emptyWith    :: HashPolicy k -> HashBag k
fromList     :: (Eq k, Hashable k) => [k] -> Either HashBagError (HashBag k)
fromListWith :: HashPolicy k -> [k] -> Either HashBagError (HashBag k)

distinctCount :: HashBag k -> Int
totalCount    :: HashBag k -> Int64
member        :: k -> HashBag k -> Bool
countOf       :: k -> HashBag k -> Int32
actualValue   :: k -> HashBag k -> Maybe k

add          :: k -> HashBag k -> Either HashBagError (HashBag k)
addCopies    :: k -> Int32 -> HashBag k -> Either HashBagError (HashBag k)
remove       :: k -> HashBag k -> HashBag k
removeCopies :: k -> Int32 -> HashBag k -> Either HashBagError (HashBag k)
removeAll    :: k -> HashBag k -> HashBag k
clear        :: HashBag k -> HashBag k

union, intersection, difference, sum
  :: HashBag k -> HashBag k -> Either HashBagError (HashBag k)
distinctItems :: HashBag k -> [k]
entries       :: HashBag k -> [(k, Int32)]
toList        :: HashBag k -> [k]
```

`empty`/`fromList` use the package `Eq`/`Hashable` policy, while `emptyWith`/`fromListWith` retain an
explicit policy value. Construction follows input order and retains the first equivalent item.
`member`, `countOf`, and `actualValue` expose membership, multiplicity, and the retained
representative. `add`/`addCopies`, `remove`/`removeCopies`, and `removeAll` preserve old versions.
Negative and zero copy requests are decided before hashing; zero, missing removal, and other
logical no-ops retain the receiver root. Addition uses the fallible one-descent map factory.
Removal saturates at zero and removes zero-multiplicity classes, so stored counts remain in
`1 .. maxBound :: Int32`.

`toList` is expanded, repeating each retained representative contiguously. `distinctItems` yields
one representative per class, and `entries` pairs those same representatives with multiplicities
in identical stable-for-one-version, otherwise unspecified CHAMP order. `validStructure` first
checks the underlying canonical CHAMP and then verifies positive multiplicities and their checked
sum against `totalCount`.

Bag algebra uses the receiver policy and conventional multiset operations:

- `union` takes maximum multiplicities;
- `intersection` takes minimum multiplicities;
- `difference` performs saturated receiver-minus-argument subtraction; and
- `sum` performs checked addition.

When the retained hash/equality function closures are not positively pointer-identical, the
argument is eagerly normalized under the receiver policy before every operation-specific shortcut.
Classes that collapse contribute a checked sum; the first argument representative in that
version's distinct CHAMP order represents the normalized class. Surviving receiver
representatives always win, while an argument representative can enter only for a receiver-absent
class. Thus normalization errors remain observable even for an empty intersection. Union and
intersection with self retain the receiver root, difference with self yields a receiver-policy
empty bag, and sum with self genuinely doubles multiplicities and may fail with
`MultiplicityOverflow`. Algebra is intentionally element-wise and makes no structural-combiner or
performance claim.

## Persistent Bidirectional Map

`Durable7.Hamt.BiMap` composes forward `HashMap k v` and inverse `HashMap v k` values into a
strict immutable bijection. `emptyWith` retains independent `HashPolicy` values for the key and
value domains. `add` returns `Either BiMapConflict`, while `tryAdd` returns the exact two-root source
on conflict and gives `KeyConflict` precedence when both classes are occupied. `set` adds a missing
free pair, retains both representatives for a value-policy-equivalent no-op, replaces a present
key only when the new value is free, and never displaces another key.

Lookup, stored-representative recovery, deletion, and presence-safe `tryRemove` results are
symmetric. A `Maybe (Maybe a)` opposite result distinguishes removing a stored `Nothing` from a
miss. `inverse` is O(1), merely swapping the two immutable map facades; therefore double inversion
shares both source roots without requiring an object-identity concept for pure values. `clear`
retains both policies, and `validStructure` verifies both CHAMPs and every cross-direction entry.
The strict facade fields force both successor map headers before publication, while immutable
sources remain untouched if hashing, equality, or allocation raises an exception. The bimap
deliberately has no algebra, transient, builder, or displacing force-put mode and stores roughly
two map entries per logical pair.

`Durable7.Hamt.Transient` adds one-way `MapTransient` and `SetTransient` editing sessions in
`IO`. Creating a session adopts an immutable source by reference, and `persistMap` / `persistSet`
publish the current value by reference and consume the session. Clean and logical-no-op sessions
retain the exact source root; successful edits preserve the hash policy, first equivalent key/item
representative, old snapshots, and canonical trie shape. Callback and path construction finish
before a masked `IORef` commit, so synchronous or asynchronous failure cannot partially publish an
edit. Sessions are deliberately unsynchronized and support one logical owner.

This first Haskell port is a semantic lifecycle checkpoint, not an owner-token optimization. Point
edits call the existing persistent path-copying operations and retain their complexity and
allocation behavior; only adoption, clear, and terminal publication are O(1). No benchmark or
speedup claim is attached to the API. A future internal mutable-node engine may optimize the same
surface after separately reviewed evidence without changing its observable contract.

`Durable7.Hamt.MerkleEncoding`, `Durable7.Hamt.MerkleSearchTree`, and
`Durable7.Hamt.MerklePersistence` provide the policy-bound canonical Merkle search tree.
The pure SHA-256 implementation, strict versioned codecs, domain/key framing, empty digest,
complete `MST2` blocks, and `MSP2` proof queries match C#, Rust, and Kotlin exactly. The immutable
wide tree supports stable first-key/last-value bulk construction, path-copy updates, ordered
lookup/range enumeration, digest-pruned diff, exact block/shape inspection, shared-content
diagnostics, and deep re-encoding validation. Its pure persistence tier adds immutable block-store
snapshots, complete and partial packs, seven-limit bounded verification, atomic-result save/import,
membership/nonmembership/range proofs, closure-pruned and iterative synchronization, and typed
present/absent-safe three-way merge. See the dedicated
[Merkle search-tree guide](docs/merkle-search-tree.md).

The HAMT default factories use the package-local `Hashable` class plus `Eq`, keeping the public
shape close to Haskell's `containers` style.
`HashMap.validStructure` provides a key/value-agnostic diagnostic for cached branch/collision
cardinality and
canonical node shape, including child-only node runs, bitmap cardinality, singleton payload
promotion, and collision-bucket demotion after deletion.

```powershell
cd src\Haskell
.\test.ps1 -Workspace Hamt
```

The current serialized GHC 9.12.4 gate passes the complete `hamt-test` suite with one Cabal job and
`-Werror`. Bimap coverage includes strict conflicts, independent policies, representatives,
replacement, symmetric nullable removal, O(1) inversion, clear, a 2,000-step two-map model,
retained snapshots, failure atomicity, and concurrent readers. Benchmarks remain postponed until
an isolated run.

The local [test README](test/README.md) lists the deterministic coverage areas.

Enumeration follows trie bitmap order and collision-bucket order: stable for an unchanged
version, but neither insertion order nor sorted order (matching the C# reference's documented
contract). Structural algebra uses GHC's one-way pointer-identity primitive to prune identical
immutable roots, subtries, and policy values without hashing. A negative pointer comparison never
affects semantics: the right operand is normalized under the receiver policy before combination.
When both maps retain the exact same hash and equality function closures, `mapEquals` and `diff`
traverse the two canonical node graphs in lockstep, pruning pointer-identical descendants before key
or value equality callbacks and using stored hashes rather than rehashing keys. Independently
supplied policies use the semantic lookup path, so compatible equality functions may retain
distinct coherent hash functions. They must still define compatible key equivalence. This is a
documented caller precondition because Haskell functions have no semantic equality operation for
functions; pointer identity is used only as a safe positive optimization, not as a compatibility
verdict.

`Durable7.Hamt.Patricia` adds `IntMap32`/`IntSet32` and `IntMap64`/`IntSet64`. The shared
strict big-endian Patricia core sign-flips keys for ascending signed traversal, compresses common
prefixes at the highest differing bit, and implements prefix-aware right-biased union,
left-valued intersection, and difference. Every branch caches its subtree cardinality, so a
structurally pruned algebra operation can publish the result count without traversing the retained
subtrees. `unionWith`/`unionWithKey` and `intersectionWith`/`intersectionWithKey` receive left and
right values in argument order; the keyed forms additionally receive the shared integer key.
`validStructure` verifies both the root count and every cached branch count. The unconstrained
`insert` deliberately does not require `Eq` for values, so replacement rebuilds the affected leaf
and path even when the new value is extensionally equal; callers that need equality-gated no-op
identity must compare first.

## Patricia Ordered Cursors

`Durable7.Hamt.Patricia` also exposes an immutable ordered gap cursor per family:
`PatriciaCursor k v` over `IntMap32`/`IntMap64` and `PatriciaSetCursor k` over `IntSet32`/`IntSet64`.
Both are opaque snapshot-plus-rank values. A cursor retains one exact map or set version plus a
validated rank in `0 .. size` and denotes the gap between the entries before and at that rank. The
navigation axis is the module's ascending signed-key order, so the sign-flipping key encoding,
compressed common prefixes, and branching masks all stay private.

Map factories are `cursor` (gap zero), `cursorAt` (a validated rank, `Nothing` outside `0 .. size`),
`cursorAtEnd`, `lowerBoundCursor`, `upperBoundCursor`, and `cursorAtKey`. The last returns a
`PatriciaCursorSearch k v`, whose `cursorSearchFound` reports whether the next entry is the exact key
and whose `cursorSearchCursor` is the lower-bound gap; a miss is therefore a usable insertion gap
rather than an invalid cursor. The set spellings are `setCursor`, `setCursorAt`, `setCursorAtEnd`,
`setLowerBoundCursor`, `setUpperBoundCursor`, and `setCursorAtItem`, which returns a plain
`(Bool, PatriciaSetCursor k)` pair rather than a named record.

`cursorCount`, `cursorPosition`, `cursorIsAtStart`, and `cursorIsAtEnd` query a cursor without
touching the trie. `cursorPeekPrevious` and `cursorPeekNext` return the whole neighbouring `(k, v)`
entry — or `k` for a set — wrapped in `Maybe`, so a stored `Nothing` payload stays distinguishable
from a boundary. `cursorMovePrevious`, `cursorMoveNext`, and `cursorSeek` return a new cursor over
the same logical version, and a same-rank `cursorSeek` returns the receiver unchanged.
`cursorSnapshot` returns the retained map and never consumes the cursor, so every retained ancestor
remains valid and branchable.

Edits keep these gap conventions:

- `cursorInsert` strictly adds a missing key and returns the gap after the new entry;
- `cursorPut` updates the exact next entry with the gap fixed, or inserts at a missing lower-bound
  gap and returns the gap after the new entry;
- `cursorSetNextValue` replaces the next value, retains its stored key, and keeps the gap fixed;
- `cursorDeletePrevious` removes the preceding entry and moves the gap left; and
- `cursorDeleteNext` removes the next entry and keeps the gap fixed.

`setCursorInsert`, `setCursorDeletePrevious`, and `setCursorDeleteNext` are the set analogues. Every
edit delegates to the ordinary `insert`/`delete` operation, so cached branch counts, prefix
compression, and unary-parent collapse behave exactly as they do for a direct call. Because these
repairs never reorder surviving keys, gap continuity across an edit is semantic rather than
incidental.

### Haskell-specific cursor behavior

Three properties below follow from immutability and from this module's deliberate constraint set,
not from an implementation shortfall.

Cursors are opaque pure values with hidden constructors. No uninitialized, moved-from, or disposed
state is representable, so the invalid-default contract that the C, C++, C#, and Rust ports must
enforce at run time is discharged here by the type system: every value a caller can obtain is
already a valid cursor over a valid version.

The Patricia core deliberately carries no `Eq` constraint on values. `insert` therefore rebuilds the
affected leaf and path even when the replacement is extensionally equal to the stored value, and
`cursorPut` and `cursorSetNextValue` inherit exactly that — a present-key replacement always
publishes a new version and never returns the receiver. This is the cost of leaving the value type
unconstrained; callers needing equality-gated identity must compare before calling.

`cursorInsert` collapses two distinct rejections into one `Nothing`. It refuses both a key that is
already present and a key whose lower-bound rank is not the current gap, and the `Maybe` result
cannot tell the two apart; callers needing the distinction should consult `cursorAtKey` first. The
set spelling separates them instead, because a duplicate there has a meaningful successful answer:
`setCursorInsert` returns `Just` the unchanged receiver for a duplicate at its own lower-bound gap
and `Nothing` only for a wrong gap. Ports that carry a typed error report both cases distinctly, as
does this package's own Merkle cursor, whose `MerkleCursorEditError` names `MerkleCursorDuplicateKey`
and `MerkleCursorWrongGap` separately.

### Cursor complexity

These are Profile R snapshot-plus-rank checkpoints in the sense of the repository-wide persistent
cursor design: a cursor is exactly `(root, rank)`, it retains no path frames, and every edit
delegates to the ordinary persistent operation. They inherit none of the C# rope tier's focused
representation, memoized snapshot, callback ceiling, allocation bound, or amortized-locality claims.

With key width `W` of 32 or 64:

- `cursorCount`, `cursorPosition`, `cursorIsAtStart`, `cursorIsAtEnd`, and `cursorSnapshot` are O(1);
- `cursorMovePrevious`, `cursorMoveNext`, and `cursorSeek` are O(1), because they rewrite only an
  integer;
- `cursorPeekPrevious` and `cursorPeekNext` are O(W): each is an unconditional root descent through
  the cached branch counts, not a step along a retained path;
- `lowerBoundCursor`, `upperBoundCursor`, `cursorAtKey`, and every edit are O(W); and
- a complete in-order traversal by repeated move-plus-peek is therefore O(n · W), not O(n).

Cursor context is O(1) in space. Editing through a cursor invalidates neither the source cursor nor
any earlier snapshot, and branching costs nothing beyond retaining the two roots.
