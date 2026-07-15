# Haskell HAMT

- Created (UTC): 2026-07-03T04:37:54Z
- Repository HEAD: 3f49d1a1ba71390af95f5a9389b99d2e334c8beb
- Audience: Maintainers and AI agents reviewing the Haskell persistent HAMT port
- Scope: `tools-data-structures-hamt` package

This package ports the repository's persistent map cores to Haskell. It provides persistent
`HashMap`, `HashSet`, and `HashBag` values with a canonical 32-way CHAMP trie, strict split data/node maps,
inline payload runs, immutable equal-hash collision buckets, structural sharing between versions,
and optional runtime `HashPolicy` values for custom hash/equality behavior. Same-policy maps expose
lockstep node-based `mapEquals` and typed `MapDifference` classification; cross-policy maps retain
semantic lookup comparison. Right-valued union, left-valued intersection, difference, and
symmetric difference are implemented by direct CHAMP-slot combination.

## One-Descent Map Factories

`Data.Structures.Hamt.HashMap` exposes the following persistent point combinators:

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

`Data.Structures.Hamt.HashBag` is an immutable unordered multiset backed by
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

`Data.Structures.Hamt.BiMap` composes forward `HashMap k v` and inverse `HashMap v k` values into a
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

`Data.Structures.Hamt.Transient` adds one-way `MapTransient` and `SetTransient` editing sessions in
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

`Data.Structures.Hamt.MerkleEncoding`, `Data.Structures.Hamt.MerkleSearchTree`, and
`Data.Structures.Hamt.MerklePersistence` provide the policy-bound canonical Merkle search tree.
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

`Data.Structures.Hamt.Patricia` adds `IntMap32`/`IntSet32` and `IntMap64`/`IntSet64`. The shared
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
