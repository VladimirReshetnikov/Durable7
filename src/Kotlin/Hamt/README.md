# Kotlin HAMT

- Created (UTC): 2026-07-03T18:26:53Z
- Repository HEAD: 315d9f19500953c69c2b60ccb430e779f1c4226d
- Audience: Maintainers and AI agents reviewing the Kotlin persistent HAMT port
- Scope: `tools.datastructures.hamt` package

This workspace ports the repository HAMT map and set family to Kotlin/JVM. It provides persistent
`PersistentHashMap<K, V>` and `PersistentHashSet<T>` values with a canonical 32-way CHAMP trie,
separate data/node bitmaps, inline payload runs, immutable equal-hash collision buckets, structural
sharing between versions, and optional runtime `HashPolicy<K>` values for custom hash/equality
behavior. Maps expose policy-compatible semantic equality and typed added/removed/changed diff.
Maps and sets also expose same-type structural union, intersection, difference, and symmetric
difference. CHAMP nodes cache subtree cardinality, align logical bitmap slots without rehashing,
and let map equality, typed diff, algebra, and same-policy set relations prune
JVM-reference-identical roots and descendants; cross-policy relations retain receiver-policy
semantics.

`PersistentBiMap<K, V>` composes two independently policy-bound CHAMP maps into a strict immutable
bijection. It rejects occupied classes in either domain, retains the first key and value
representatives, replaces only with a free value, supports symmetric lookup/removal, and caches a
reciprocal O(1) inverse facade whose inverse is the exact source object.

Persistent maps also provide one-descent `getOrAdd` and `addOrUpdate` point combinators. Each
operation hashes once, follows one CHAMP route, invokes only the selected factory, and returns a
`MapValueResult<K, V>` containing both the successor map and the value actually retained. Hits keep
the original key representative; an update equal to the stored value keeps that value instance and
the exact source map. `PersistentHashBag<T>` builds an immutable unordered multiset over the same
map, with explicit `distinctCount`, 64-bit `totalCount`, positive checked 32-bit multiplicities,
expanded/default iteration, distinct and entry views, and receiver-policy multiset algebra.

`PersistentHashMultimap<K, V>` adds nonempty set-valued groups under independent key/value
`HashPolicy` objects, checked `Long` pair counts, pair/group edits, receiver-normalized algebra, and
validation. `PersistentRelation<L, R>` maintains exact forward and reverse multimaps with
bidirectional lookup, inversion, endpoint-group removal, and complete inverse validation.

`PersistentMapPatch<K, V>` adds presence-safe strict before/after changes with preflight apply,
inversion, and policy-compatible composition. `PersistentDirectedGraph<V>` combines explicit
vertices with the relation for forward/reverse adjacency. `PersistentIndexedMap<K, V, I>` combines
primary rows with one selector-maintained nonunique secondary index. All retain runtime policy
objects, first representatives, immutable snapshots, and atomic composite publication.

The CHAMP map and set also expose one-way `Transient` editing sessions through `toTransient()` and
`createTransient(...)`. Adoption and `persist()` are O(1) reference transfers, clean or logically
unchanged sessions publish the exact adopted persistent object, and successful publication consumes
the session. This Kotlin tier is intentionally a lifecycle facade over the existing immutable
implementation: every point edit still path-copies an ordinary persistent CHAMP successor. It makes
no transient throughput or allocation-win claim. Policy identity, stored representatives, active
reads and receiver-policy set relations, callback-failure atomicity, and retained-source isolation
are preserved. Enumeration views capture the active snapshot and session version when acquired:
logical no-ops preserve them, while successful edits invalidate them even before iteration begins.

`ConcurrentHashTrie<K,V>` is the deliberately mutable JVM member. It uses generation-stamped
indirection nodes, helping node-local GCAS, and a root/main RDCSS transition for lock-free updates
and linearizable O(1) immutable snapshots. Empty/singleton tombs contract deletion paths instead of
retaining historical skeletons; later writers lazily renew old-generation children only along paths
they modify. Snapshots convert to the canonical persistent CHAMP representation explicitly in O(n).

The integer-specialized family provides `PersistentIntMap`/`PersistentIntSet` and
`PersistentLongMap`/`PersistentLongSet`. A shared big-endian Patricia engine path-compresses on the
highest differing bit, sign-flips keys for ascending signed iteration, and performs prefix-aware
structural union, intersection, and difference without hashing or collision buckets. Branches cache
subtree cardinality, so algebra results publish their count without a finishing traversal; map union
and intersection also provide `(key, leftValue, rightValue)` combining overloads. Rebuilds retain the
receiver and its root whenever an update or algebra operation is semantically unchanged.

The default collection factories use Kotlin `hashCode`/`equals`, keeping the public shape close to
JVM collection expectations while preserving the repository HAMT contracts: persistent updates,
duplicate-key rejection, last-wins bulk replacement, original-key retention on equivalent-key
replacement, multiset counts, and set/bag algebra.

The workspace also exposes `MerkleSearchTree<K, V>`, the exact safe-JVM core/wire port of the C#
paper-style B=16 wide Merkle search tree. `MerkleSearchTreePolicy<K, V>` binds a comparator and
explicitly versioned canonical codecs into the `mst-sha256-b16-v2` domain. SHA-256 leading-zero
nibbles select levels, consecutive same-level separators share one immutable wide block, and
independent histories converge to the same topology, exact `MST2` bytes, and root digest. Persistent
updates preserve first-equivalent-key/last-value semantics and share untouched node references.
Strict built-ins cover big-endian `Int`/`Long`, nullable UTF-8, nullable bytes, and RFC-4122 UUIDs.
The persistence tier adds immutable content-addressed blocks, a concurrent memory store, complete
and partial transfer packs, bounded verified load/import, exact `MSP2` point and range proofs,
frontier synchronization, and typed three-way merge that distinguishes deletion from a present
nullable value. Seven finite verification limits bound untrusted blocks, bytes, depth, entries,
child references, and proof queries before allocation or codec work.
See [Merkle search tree](docs/merkle-search-tree.md) for the exact framing and API contract.

Validate from `src/Kotlin`:

```powershell
.\build.ps1 -Workspace Hamt
```

The current fully serialized Kotlin 2.4.0/JVM 21 gate passes all 69 registered test groups. The
bimap group includes strict conflicts, independent policies, representatives, non-displacing
replacement, nullable values, cached inverse identity, a 2,000-step two-map model, failure
atomicity, and concurrent readers. Benchmarks remain postponed until an isolated run.

See [API notes](docs/api-notes.md), [Merkle search tree](docs/merkle-search-tree.md),
[validation](docs/validation.md), and the [test map](tests/README.md).
