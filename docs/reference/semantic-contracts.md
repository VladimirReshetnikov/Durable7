# Semantic Contracts Reference

- Created (UTC): 2026-07-03T23:50:37Z
- Repository HEAD: 96a766f45fa42b5bd14c5ae3173956300cbff21b
- Audience: Maintainers and AI agents preserving cross-workspace behavior
- Scope: Shared contracts for repository-owned HAMT, Ordered, FingerTree-family structures, ownership models, and documentation obligations

This reference summarizes the behavioral contracts that should stay recognizable across the repository's
language workspaces. It is not a replacement for the workspace API specifications, public headers, XML
documentation, or source tests. Use it as a checklist when reviewing whether a change preserves the
intended semantics across C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python.

Authoritative local documents remain:

- [C# HAMT API specification](../../src/CSharp/docs/Hamt/api-specification.md)
- [C HAMT API specification](../../src/C/Hamt/docs/api-specification.md)
- [C++ HAMT API specification](../../src/Cpp/Hamt/docs/api-specification.md)
- [C# FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md)
- [C# Range-update sequence contract](../../src/CSharp/docs/FingerTree/range-update-sequence.md)
- [C# Ordered API specification](../../src/CSharp/docs/Ordered/api-specification.md)
- [C FingerTree API notes](../../src/C/FingerTree/docs/api-notes.md)
- [C++ FingerTree API notes](../../src/Cpp/FingerTree/docs/api-notes.md)
- [Kotlin FingerTree API notes](../../src/Kotlin/FingerTree/docs/api-notes.md)
- [Rust FingerTree API notes](../../src/Rust/FingerTree/docs/api-notes.md)
- [OCaml package API notes](../../src/OCaml/docs/api-notes.md)
- [TypeScript package API notes](../../src/TypeScript/docs/api-notes.md)
- [Python package API notes](../../src/Python/docs/api-notes.md)

Use the [data-structure catalog](data-structure-catalog.md) for the inventory of public entry points.
Use the [porting guide](../guides/porting-and-semantic-parity.md) for the workflow that carries a
behavior change through sibling workspaces.

## Contract Vocabulary

| Term | Meaning in this repository |
| --- | --- |
| Persistent value | An operation that looks like an update returns a new value or handle while preserving every retained old version. |
| One-way editing session | A single-owner edit-then-publish lifecycle over a persistent collection. It may be an optimized owner-token transient or a semantic facade over persistent path copying; it is unsynchronized, is consumed by publication, and is not a reusable staging builder. |
| Structural sharing | Versions reuse immutable substructure where possible; no-op updates often preserve the same root or instance when the local API exposes that diagnostic. |
| Reference-first equality | When two stored/incoming references are provably identical, implementations may accept equality without invoking user equality. A non-identical reference never proves semantic inequality. |
| Policy preservation | Hash, equality, comparison, measure, allocator, ownership, and callback policies flow into derived versions unless a local API explicitly creates a new policy. |
| Stable but unspecified order | Enumeration order is deterministic for unchanged structure versions, but callers must not treat the exact trie/tree traversal order as a sorted or insertion order unless the API says so. |
| Insertion/explicit-position order | Enumeration follows first insertion plus unmistakable positional operations; equality decides membership but does not determine order, and comparison order is not retained unless separately promised. |
| Dual-index composite | One public collection owns two persistent indexes for different query dimensions and must publish them only when their cross-index correspondence invariants hold. |
| Measure | A monoidal summary cached on a finger-tree node, chunk, or facade element and used for split, locate, rank, priority, interval, rope, or text navigation. |
| Tag action | A tag monoid acting consistently on individual elements and cached ordered measures; composition order, subtree cardinality, identity recognition, and distribution laws are part of the public policy contract. |
| Version-bound cursor | An immutable working value that owns one persistent sequence version and a position within it; navigation and editing never redirect it to a different version implicitly. |
| Checkpoint port | A port that preserves observable API semantics and tests while documenting a remaining representation or asymptotic parity boundary. |
| Facade | A public collection built on a shared core engine, such as sorted sets on measured trees or text ropes on measured ropes. |

## HAMT Map And Set

The HAMT workspaces implement persistent hash-array mapped trie maps and sets.

Public surfaces:

| Language | Map | Set | Policy shape |
| --- | --- | --- | --- |
| C# | `PersistentHashMap<TKey, TValue>` and its nested `Transient` | `PersistentHashSet<T>` and its nested `Transient` | `IEqualityComparer<T>` |
| C | `d7_hamt_map` and `d7_hamt_map_transient` | `d7_hamt_set` and `d7_hamt_set_transient` | callback tables and context pointers |
| C++ | `persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>` and nested `transient` | `persistent_hash_set<T, Hash, KeyEqual>` and nested `transient` | template policy objects |
| Haskell | `HashMap k v` and `MapTransient k v` | `HashSet a` and `SetTransient a` | `Hashable`, `Eq`, optional `HashPolicy` |
| Kotlin | `PersistentHashMap<K, V>` and nested `Transient<K, V>` | `PersistentHashSet<T>` and nested `Transient<T>` | runtime `HashPolicy<K>` |
| OCaml | `Persistent_hamt` and `Transient` | `Persistent_hash_set` and its edit session | retained `Common.Hash_policy` |
| Rust | `PersistentHashMap<K, V, S>` and `TransientHashMap<K, V, S>` | `PersistentHashSet<T, S>` and `TransientHashSet<T, S>` | `Eq` plus `BuildHasher` |
| TypeScript | `PersistentHashMap<K, V>` and `TransientHashMap<K, V>` | `PersistentHashSet<T>` and `TransientHashSet<T>` | runtime `HashPolicy<K>` |
| Python | `PersistentHashMap[K, V]` and `TransientHashMap[K, V]` | `PersistentHashSet[T]` and `TransientHashSet[T]` | retained `HashPolicy[K]` |

Shared obligations:

- Trie branching is 32-way and bitmap-indexed over 32 hash bits.
- Equal full-hash collisions are represented by immutable collision buckets.
- Update-shaped operations preserve existing versions.
- `Set` or replacement-style operations are last-wins for existing keys.
- `Add` or duplicate-rejecting operations fail without changing the logical collection when an
  equivalent key already exists.
- Lookup, remove, and key-recovery operations use the stored equality policy, not object identity,
  unless the policy itself encodes identity.
- Replacing a value for an equivalent key preserves the originally stored key object where the local
  API exposes key recovery.
- Bulk construction should document whether duplicate keys are last-wins or rejected.
- Set algebra preserves the receiver's equality/hash policy unless the local API documents another
  policy source.
- Equality, diff, no-op replacement, and structural algebra should prune reference-identical
  roots/subtries/values before invoking semantic equality when the language can do so safely. This
  is an optimization only: policy-defined equality remains authoritative for non-identical values.
- Lockstep equality/diff may interpret stored hashes structurally only when the implementation has
  a positive witness that both maps retain one hash-policy identity (or the C equivalent of exact
  callback/context compatibility). Independently created but semantically compatible policies must
  retain lookup-based comparison when their coherent hash states can differ; APIs that require
  exact policy identity may instead reject the operation explicitly.
- Iteration order is trie order: stable for unchanged versions, not sorted, not insertion ordered, and
  not a serialization contract.
- Diagnostic root-sharing APIs such as `shares_root_with` or debug root kind are test aids, not
  general semantic dependencies for callers.

Language-specific obligations:

| Language | Additional contract |
| --- | --- |
| C# | Public XML docs must state comparer preservation, exception behavior, no-op identity where promised, and IReadOnly collection semantics. Persistent `GetOrAdd`/`AddOrUpdate` validate delegates before hashing, hash and descend once, invoke only the selected factory once, retain stored key/value representatives on equal updates, and publish nothing on callback failure. |
| C | Every function must define ownership of input keys/values, retained outputs, status codes, and cleanup on partial failure. Callback contexts must outlive collections that use them. |
| C++ | Value objects should remain cheap to copy through shared immutable nodes; template policies must stay part of the value's semantic identity. |
| Haskell | `HashPolicy` and package-local `Hashable` shape are part of the port, avoiding third-party dependencies while preserving persistent HAMT behavior. |
| Kotlin | Miss paths and duplicate results should use idiomatic Kotlin null/result/exception shapes documented in API notes. |
| OCaml | Runtime hash policies define equivalence and representative retention; persistent operations use immutable successors, absence uses `option`, fallible indexed/trust-boundary operations use `result`, and one-way sessions reject access after publication. |
| Rust | Keys that compare equal under `Eq` must hash equally under the chosen `BuildHasher`; removal returns owned cloned values where exposed. |
| TypeScript | Runtime hash policies define equivalence and representative retention; `getOrAdd`/`addOrUpdate` follow the C# one-descent selected-factory contract; transient sessions are isolate-local path-copying facades with no cross-worker progress or edit-performance claim. |
| Python | Equality and hashing are explicit retained policy; `get_or_add`/`add_or_update` preserve one-descent factory selection and presence-safe result values; mutable application objects require caller discipline, and changed or consumed sessions invalidate version-bound iterators dynamically. |

### Persistent hash bags

`PersistentHashBag` ships across all nine languages as an unordered-multiset facade over the
language-local persistent HAMT. It stores exactly one representative and one positive multiplicity
in `1 .. 2^31 - 1` per policy equivalence class. Distinct-class count is separate from expanded
total count, and no ambiguous collection `Count`/`size` is exposed for expanded enumeration. C# uses
a checked `long` total; C/C++/Haskell/Kotlin/Rust use their corresponding bounded wide integer;
OCaml uses a checked native count for the current facade, TypeScript uses `bigint`, and Python uses
`int`. Construction and point
additions retain the first stored representative; expanded enumeration repeats each representative
contiguously, while distinct-item and entry views enumerate one class each in the same
stable-for-one-version, otherwise unspecified HAMT order.

Bag algebra uses maximum union, minimum intersection, saturated difference, and checked additive
sum. The receiver comparer defines equivalence and receiver representatives win every surviving
receiver class. A reference-different argument comparer is normalized eagerly under the receiver
policy before any operation-specific shortcut; collapsed argument multiplicities are checked and
use the first representative observed in that argument version's HAMT order. Zero-copy and logical
no-op updates return the receiver, and empty results preserve its policy object. Array/list
materialization validates the local runtime representation before allocation. The normative semantic
reference is the [C# HAMT API specification](../../src/CSharp/docs/Hamt/api-specification.md);
language-local API notes document ownership, failure/result, presence-safe lookup, and wide-count
mappings. The [completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md)
indexes every port.

### Persistent bidirectional maps

`PersistentBiMap` and its language-local counterparts ship across all nine languages as strict
immutable bijections over two CHAMP maps. Their shared semantic contract is:

- key and value hash/equality policies are independent retained objects; every successor,
  including empty and inverse views, preserves the corresponding policies;
- each policy equivalence class appears at most once in its domain, both maps have the same count,
  and every forward entry has exactly one equivalent inverse entry and vice versa;
- strict add rejects an occupied key class or value class even when the complete pair is equivalent
  and checks the key domain first. Nonthrowing insertion returns an unchanged/root-sharing source;
  APIs with a domain-bearing result report key conflict before value conflict, while C# deliberately
  exposes only its established boolean `TryAdd` shape;
- set adds a missing free pair, treats a configured-value-policy-equivalent update as a no-op that
  retains both representatives, replaces a present key only with a free value class, and never
  displaces another key;
- replacement removes and reinserts both directions rather than relying on the ordinary map value-
  equality shortcut, which may use a different policy;
- removal through either domain deletes the same pair and returns or reports the opposite stored
  representative with an explicit presence discriminator for null-like values;
- clear preserves both policies and is a semantic no-op on an already empty value; forward
  enumeration follows stable-for-one-version, otherwise unspecified CHAMP order;
- inverse is O(1) in pair count and enumerates no entries. Reference-semantic ports cache reciprocal
  facade objects; value-semantic ports guarantee that double inversion shares the same two roots;
- callback, comparison, cloning, or allocation failure publishes no one-sided successor and leaves
  the source valid; and
- the honest storage model is approximately two map entries per logical pair. No port exposes
  algebra, a builder, a transient/edit session, or a displacing force-put operation.

C's explicit handle model adds two ownership rules: callback contexts outlive every related bimap,
and opposite representatives returned by a non-aliased removal borrow the source snapshot. C handle
reference counts are non-atomic, so retaining/destroying a shared lineage is serialized even though
already-retained snapshots support concurrent reads. Rust uses lawful `Eq`/`Hash` plus independent
`BuildHasher` states rather than arbitrary equality callbacks. These mappings preserve the same
two-domain contract without pretending the APIs have identical identity or ownership mechanics.

The cross-language entry-point matrix is in the
[data-structure catalog](data-structure-catalog.md#persistent-bidirectional-map), and the detailed
shipment evidence is in the
[PersistentBiMap completion audit](../reviews/persistent-bimap-cross-language-completion-2026-07-15.md).

### Construction-only HAMT bulk builders

C# bulk construction uses an internal mutable unpublished CHAMP builder. C++ and Rust expose the
same construction facility publicly; TypeScript and Python expose reusable public builders as
well. Their common contract is narrower than a transient editing session:

- mutable leaf, collision, and bitmap nodes are unpublished and mutated in place during one-pass
  construction, avoiding a persistent path copy between successive input entries;
- duplicate assignment retains the first stored key representative, keeps an earlier equal value
  representative, and otherwise installs the last distinct value;
- freezing produces a detached immutable CHAMP—later builder mutation cannot affect an earlier
  snapshot—and the reusable freeze leaves the builder active; and
- map/set bulk factories and documented construction-heavy internal routes use the builder, while
  ordinary updates on an existing persistent collection retain structural-sharing path copies.

Rust additionally offers a consuming freeze that moves owned nodes. These builders do not acquire
the adoption, iterator, or one-way publication lifecycle of map/set transients.

OCaml exposes a reusable staging builder with detached freezes and the same representative rules,
but builder edits currently invoke persistent path copying. It does not claim the unpublished
mutable-node construction bound above.

### Concurrent snapshot facades

The C# and Kotlin/JVM Ctries are the deliberate lock-free managed tier. `Snapshot` captures a
generation in O(1); later writes lazily renew paths and cannot alter the captured view. Snapshot
enumeration follows canonical CHAMP ordering: singleton/data entries precede multi-entry child/node
runs at each bitmap level, frozen empty tombs are skipped, frozen singleton tombs are promoted
logically into the data run, and equal-hash collision buckets retain their local order. Explicit
snapshot-to-CHAMP conversion is O(n) and preserves that sequence, the exact policy object, stored
key/value representatives, null/present-null semantics, and isolation from later live writes.

OCaml, TypeScript, and Python preserve the consumer-level mutable-map/O(1)-snapshot vocabulary
without claiming that protocol. TypeScript is isolate-local and synchronous. OCaml and Python
serialize operations with a mutex/`RLock` and publish immutable CHAMP roots. None of these facades uses GCAS/RDCSS descriptors,
generation renewal, or a lock-free progress guarantee; conversion and concurrency claims must follow
its local API notes and tests.

### One-way CHAMP editing lifecycle

All nine HAMT workspaces expose a single-owner map/set edit-then-publish lifecycle. The shared
contract is semantic, not representational:

- Creating an empty session or adopting a persistent source and successfully publishing the
  current persistent value perform O(1) trie work and do not traverse the CHAMP graph. Costs of
  user-defined policy copying, moving, or reference retention remain language-defined.
- Sessions preserve the exact equality/hash policy, stored representatives, unchanged-version trie
  order, retained source isolation, and receiver-policy set-relation semantics.
- Publication is one-way. It consumes the logical session; the language either rejects later access
  dynamically or makes such access unavailable through ownership. A session is unsynchronized and
  has one logical owner even when a language permits explicit handle aliases or moves.
- Logical no-ops preserve the current persistent root or object and do not invalidate a
  version-bound iterator/view. A clean adopted session therefore republishes the exact source root
  or instance in the language's identity model.
- A failed edit must not install a partial successor. Failure and retry semantics for terminal
  publication follow the local ownership and exception model and must be stated explicitly.

C# is the optimized reference implementation: its owner-token kernel mutates only nodes and arrays
proved owned by the active token and path-copies shared or sealed storage. Adoption and successful
`Persist()` are O(1), publication seals the token and invalidates all aliases/views/enumerators, and
the preparation/commit boundary preserves a retryable strong exception guarantee. The exact C#
contract and its performance evidence are in the [API specification](../../src/CSharp/docs/Hamt/api-specification.md),
[usage guide](../../src/CSharp/docs/Hamt/usage.md), and
[T2 shipment decision](../../src/CSharp/docs/Hamt/transient-t2-decision.md).

The sibling ports deliberately implement the lifecycle without claiming the owner-token
optimization. Each changed point edit computes an ordinary persistent successor by path-copying the
affected CHAMP route, then replaces the session's current persistent value. Consequently their edit
time and allocation behavior remain those of persistent updates, and no transient-performance
advantage is claimed:

| Language | Lifecycle and failure shape |
| --- | --- |
| C | `d7_hamt_map_transient` / `d7_hamt_set_transient` use explicit clone/destroy handles over one ref-counted state. Clones are lifecycle aliases: one successful publication consumes them all. Operations report `D7_HAMT_TRANSIENT_CONSUMED` or `D7_HAMT_TRANSIENT_MODIFIED` as appropriate; failed edits and relation queries preserve session/output atomicity. |
| C++ | Nested sessions are movable but not copyable and publish only through `std::move(session).persist()`. Generation-bound iterators reject changed or consumed sessions. A throwing custom policy move terminally invalidates the source and, for assignment, the destination; publication has no retry/content-preservation guarantee after a throwing policy move. Nothrow-movable policies avoid both exceptional boundaries. |
| Haskell | `MapTransient` / `SetTransient` live in `IO`; `persistMap` / `persistSet` consume the `IORef` state. Candidate construction precedes a masked commit, so synchronous or asynchronous failure cannot partially install an edit. |
| Kotlin | Nested `Transient` sessions enforce consumption with `IllegalStateException`; acquired views capture the current persistent snapshot and session version, survive logical no-ops, and fail after content changes. Callback failure leaves the active session unchanged and retryable. |
| OCaml | `Persistent_hamt.Transient` retains one persistent current root, replaces it with path-copy successors, and dynamically rejects access after one-way publication. Reusable `Bulk_builder` is a separate construction facility. |
| Rust | `TransientHashMap` / `TransientHashSet` publish with consuming `into_persistent(self)`, so the type system prevents use-after-publication. `into_transient` moves a source while `to_transient` shares its root; both retain persistent path-copy edit costs. |
| TypeScript | `TransientHashMap` / `TransientHashSet` enforce one-way publication at runtime inside one JavaScript isolate. Changed edits replace the current persistent root through ordinary path copies; the facade makes no cross-worker progress or transient-performance claim. |
| Python | `TransientHashMap` / `TransientHashSet` retain a persistent current value, consume on publication, and invalidate iterators after content changes or publication; changed edits remain persistent path copies rather than owner-token mutation. |

The local authoritative references are the [C API specification](../../src/C/Hamt/docs/api-specification.md),
[C++ API specification](../../src/Cpp/Hamt/docs/api-specification.md),
[Haskell HAMT workspace](../../src/Haskell/Hamt/README.md),
[Kotlin API notes](../../src/Kotlin/Hamt/docs/api-notes.md),
[OCaml API notes](../../src/OCaml/docs/api-notes.md),
[Rust API notes](../../src/Rust/Hamt/docs/api-notes.md),
[TypeScript API notes](../../src/TypeScript/docs/api-notes.md), and
[Python API notes](../../src/Python/docs/api-notes.md).

## Set-Valued Hash Multimap And Persistent Relation

All nine languages ship a set-valued persistent hash multimap and a bidirectional persistent
relation over their local HAMT maps and sets. Shared obligations are:

- the key/left policy and value/right policy are independent, retained by every result, and define
  equality classes plus first-representative retention on their respective axes;
- a multimap stores only nonempty value sets, distinguishes distinct-key count from checked pair
  count, and removes an outer key when its last pair is removed;
- duplicate pair insertion and removal of an absent pair are identity no-ops;
- union, intersection, and difference normalize argument pairs under the receiver's two policies,
  preserve receiver representatives for surviving classes, and publish no partially normalized
  result if enumeration or policy code fails;
- a relation's forward and reverse multimaps contain exactly the same logical pairs, its pair count
  agrees with both indexes, and `Inverse`/`inverse` swaps those persistent indexes without scanning
  the relation; and
- adding or removing a relation pair updates both directions atomically, leaving every retained
  version and both current indexes mutually consistent on failure.

Language-local result values, overflow types, callback failure reporting, and ownership are allowed
to be idiomatic. The [derived-family catalog](data-structure-catalog.md#derived-persistent-maps-relations-and-sparse-bit-sets)
links every public surface; the C# [HAMT API specification](../../src/CSharp/docs/Hamt/api-specification.md)
is the detailed managed reference.

## Insertion-Ordered Persistent Set

`PersistentOrderedSet` ships in neutral Ordered modules across all nine languages. It composes
a persistent HAMT membership/stamp index with a persistent ordered sequence and must not depend on
Shared obligations are:

- equality/hash policy defines membership, duplicate collapse, stored representatives, algebra,
  and relations; the exact receiver policy is retained by every result, including empties;
- construction and duplicate additions retain the first representative and its position;
  movement is explicit, retains the stored representative, and interprets a supplied destination
  as the final result index;
- positional reads/removals/ranges, reversal, and stable one-shot sort preserve immutable prior
  versions; a sort does not remain active for later additions;
- union appends argument-only classes after receiver order; intersection and difference retain
  receiver order; symmetric difference emits receiver-only then argument-only classes;
- all algebra and all six set relations eagerly enumerate and normalize the complete argument under
  the receiver policy before applying a shortcut, so late enumeration/policy failures are visible;
- receiver representatives win surviving receiver classes, while the first normalized argument
  representative wins an argument-only class; and
- documented logical no-ops return the receiver, failures publish nothing, private sparse labels
  are not API, and a relabel rebuild leaves retained versions untouched.

The normative surface and bounds are in the
[C# Ordered API specification](../../src/CSharp/docs/Ordered/api-specification.md). Runtime-native
result/exception, ownership, and diagnostic adaptations are documented in each sibling workspace;
the [completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md)
indexes them.

## Insertion-Ordered Persistent Map

`PersistentOrderedMap` ships beside the neutral ordered set across all nine languages. It uses the
same equality-defined key classes and explicit positional order while attaching one payload to each
class. Shared obligations are:

- range construction retains the first key representative and its first position while the last
  payload for that equality class wins;
- setting an existing key changes only its payload: it neither replaces the stored key
  representative nor moves the entry; movement remains explicit;
- setting an absent key appends it, and positional reads, removals, ranges, reversal, movement, and
  stable one-shot sorting preserve all older versions;
- key lookup and ordered enumeration describe one logical entry set, counts agree across indexes,
  and no entry exists in only one index;
- equality policy controls key identity but never payload equality or sort order; and
- where a local value-equality policy is part of the surface, documented equal-payload updates are
  identity no-ops; already-positioned movement is likewise a documented no-op where exposed, while
  callback, allocation, or comparison failure publishes neither index.

The general map is independently owned by each neutral Ordered workspace.
`PersistentAssociation` supplied historical design evidence only and is neither a dependency nor a
semantic authority. See the C# [Ordered API specification](../../src/CSharp/docs/Ordered/api-specification.md)
and the [cross-language catalog](data-structure-catalog.md#derived-persistent-maps-relations-and-sparse-bit-sets).

## Composition-First Derived Structures

`PersistentOrderedMultimap`, `PersistentMapPatch`, `PersistentDirectedGraph`,
`PersistentIndexedMap`, and `PersistentChunkedBitSet` ship across all nine languages with
language-local naming, result, ownership, and policy shapes. Their shared obligations are:

- ordered multimaps retain the first key representative and key-group position, retain the first
  value representative and position within each group, enumerate in grouped order, and never store
  an empty group;
- map patches distinguish absence from every present value (including null-like values), validate
  all before-states before applying any edit, preserve source maps on conflict, invert by swapping
  states, and compose only through equal intermediate states;
- directed graphs store explicit vertices separately from unique edges, add missing endpoints when
  adding an edge, maintain forward and reverse adjacency together, and remove every incident edge
  when removing a vertex;
- indexed maps retain one selected secondary key per primary row, keep exactly one secondary pair
  per primary key, move membership atomically when the selector changes class, and preserve stored
  primary and secondary representatives; and
- chunked bit sets accept only nonnegative signed-32-bit indexes, omit zero 64-bit words, enumerate
  ascending bits, cache represented-word and population counts, implement inclusive rank and
  zero-based select by measured descent, and perform algebra over sparse word streams rather than
  scanning to the largest index.

Every composite publishes all constituent indexes together or publishes no successor. The general
families depend only on general HAMT, Ordered, and FingerTree substrates
or adopts its behavior as a semantic baseline. The detailed managed contracts are the
[derived HAMT structures](../../src/CSharp/docs/Hamt/derived-persistent-structures.md),
[ordered multimap](../../src/CSharp/docs/Ordered/persistent-ordered-multimap.md), and
[chunked bit set](../../src/CSharp/docs/FingerTree/persistent-chunked-bit-set.md) references.

## Finger-Tree Core

The FingerTree family has two core ideas:

- a tuned persistent deque for sequence operations;
- a general monoid-measured tree used directly and as the engine for derived facades.

Shared obligations:

- Empty, singleton, endpoint, concatenation, split, index, insert, remove, and range operations preserve
  immutable snapshot semantics.
- Measured trees cache a monoidal summary. Measure policies must provide an identity value, element
  measure, and associative combine operation in the language's local shape.
- Split and locate operations depend on the documented predicate boundary over prefix measures. A new
  measure or predicate helper must document the ordering assumptions it needs.
- Product and sum measures should preserve component order and component-specific predicate mapping.
- Built-in measures such as size, key, rank, minimum, maximum, priority, interval, and newline measures
  must keep the same observable interpretation across ports where exposed.
- Lazy middle publication and cached measures must not expose partially initialized data to readers.
- Checkpoint ports may use simpler internals, but their API notes must state the remaining complexity
  boundary clearly.

## Range-Update Sequence

The C# FingerTree assembly ships
`RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` as a separate path-copied implicit-AVL core.
It is an immutable indexed sequence with persistent point edits, split/concat/range extraction,
lazy range updates, and ordered whole/range measures. It does not add lazy tags to the existing
finger-tree engines. The C# reference is the semantic baseline for the shipped C, C++, Haskell,
Kotlin, Rust, TypeScript, Python, and OCaml ports. OCaml preserves the observable algebra and
persistence contract through immutable arrays and makes no implicit-AVL lazy-update bound claim.

Its `IRangeUpdateAlgebra<TElement, TMeasure, TTag>` policy extends `IMeasure` and must satisfy all
of these obligations:

- the measure has an identity and associative ordered combine;
- tags have an identity and associative composition, with `Compose(newer, older)` meaning apply
  `older` first and `newer` second;
- tag action on an element composes in that same order;
- tag action on a cached measure composes in that same order and receives the represented element
  count;
- applying a tag to a singleton measure agrees with measuring the tagged element;
- the action distributes over ordered measure combination with the left/right counts, including
  noncommutative measures;
- applying any tag to the empty measure at count zero preserves the empty measure; and
- every tag recognized by `IsIdentity`, including a value-distinct representation, obeys the full
  identity equations.

The implementation invariant is logical rather than merely physical: a node's stored value and
cached measure already include that node's pending tag, while its children do not. Descent and
rotations push pending tags before structural rearrangement. Non-mutating reads instead carry the
newer inherited tag down the path and compose it after the node's older tag, so indexing,
measurement, and enumeration never mutate shared versions. Count, AVL height/balance, ordered
measure, composition direction, and logical enumeration are executable invariants.

Public behavior must preserve these contracts:

- all retained versions remain immutable; successful updates share unaffected interiors, and an
  exception from any policy callback publishes no successor;
- range validation precedes tag callbacks; empty ranges preserve the receiver/empty measure without
  invoking the tag policy, and recognized identity updates preserve the receiver;
- whole-sequence nonidentity application is O(1) and allocates one replacement root; proper
  subrange updates and measures perform O(log n) boundary work;
- direct insert, replacement, and removal push old tags before installing a new element, so a prior
  range tag never applies accidentally to newly inserted/replaced content;
- concrete enumeration uses the public mutable struct enumerator; copied enumerators share traversal
  state and fail fast if advanced out of sync, while independently created enumerators remain safe;
  and
- concurrent reads of retained versions are safe subject to the caller's policy callback safety.

The exact API, affine assignment/addition example, invariants, and complexity live in the
[Range contract](../../src/CSharp/docs/FingerTree/range-update-sequence.md); the executable gate is
recorded in the [C# validation guide](../../src/CSharp/docs/FingerTree/validation.md#range-update-sequence-integration-gate).
At the pre-bimap Range shipment checkpoint, both full serialized C# Debug and Release builds
completed with zero warnings and zero errors, and both configurations passed 1,417/1,417 tests.
Language-local Range ports, together with the same
tranche's HAMT factories, hash bags, and neutral ordered sets, now ship in C, C++, Haskell, Kotlin,
Rust, TypeScript, Python, and OCaml. The
[cross-language completion audit](../reviews/benchmark-independent-structures-cross-language-completion-2026-07-15.md)
records their exact mappings and gates. No benchmark was run; measurements remain postponed until
an isolated session.

## Reversible Deque

Reversible deques provide logical orientation without eagerly copying the sequence.

Shared obligations:

- `reverse` or its equivalent returns a new view/value and preserves the original.
- Endpoint operations, indexing, splitting, concatenation, traversal, and materialization observe the
  logical orientation.
- Mixed-orientation concatenation must not silently reinterpret one operand in physical order.
- O(1) reversal claims belong only where the implementation uses an orientation bit, mirrored root, or
  equivalent view rather than materializing a new sequence.

Use the [reversible deque complexity audit](reversible-deque-complexity-audit.md) when changing any
orientation-aware implementation.

## Insertion-Ordered Set

`Durable7.Ordered.PersistentOrderedSet<T>` is the C# reference for a general-purpose
insertion-ordered set that also ships in the eight sibling languages, including OCaml. Its retained `IEqualityComparer<T>`
defines equality classes, membership,
lookup, duplicate collapse, and algebra. Enumeration order is a separate semantic dimension:
construction retains first-occurrence order, ordinary `Add` appends an absent class, and
`AddFirst`/`Insert` place only absent classes. Adding a comparer-equivalent value is an identity
no-op that neither moves the class nor replaces its first stored representative. Movement is
available only through explicit `MoveToFirst`, `MoveToLast`, and final-result-index `MoveTo`
operations, all of which move the stored representative rather than the lookup argument.

The order is not comparison-sorted. `Sort` is a stable one-shot transformation whose ties retain the
old order; it preserves the equality comparer but does not store the ordering comparer or change the
meaning of later additions. Ranges, reversal, sorting, movement, and removal preserve immutable old
versions, comparer identity, and stored representatives for surviving classes. Logical no-ops
return the receiver where the API specification promises identity.

Every version owns two persistent indexes:

```text
PersistentHashMap<T, long>  membership class -> private stamp
FingerTreeDeque<Entry>      private stamp + stored representative in enumeration order
```

Ordered owns their composition. Its published invariants require equal counts, strictly increasing
deque stamps, exactly one map entry per deque entry and vice versa, the same representative in both
indexes, the receiver's exact comparer object, and isolation of every retained version. Sparse stamp
selection and relabel cadence are private. If an insertion or movement exhausts a label gap, the new
version may rebuild and relabel both indexes; that O(n) work belongs to that produced branch, so no
amortization claim spans siblings derived from the same old version.

Set-producing algebra and all six set relations eagerly normalize the entire argument under the
receiver's comparer, even for another `PersistentOrderedSet<T>` with a different comparer object.
The first argument representative encountered during that normalization wins each collapsed
argument class, while receiver representatives win every surviving receiver class. Result order is
deterministic:

| Operation | Ordered result |
| --- | --- |
| `Union` | Receiver classes in receiver order, then argument-only classes in normalized argument order |
| `Intersect` | Surviving receiver classes in receiver order |
| `Except` | Surviving receiver classes in receiver order |
| `SymmetricExcept` | Receiver-only classes in receiver order, then argument-only classes in normalized argument order |

The workspace is independently owned and depends only on the public C# HAMT and FingerTree projects.
Similar sparse-order mechanics are provenance, not shared ownership. C, C++, Haskell, Kotlin,
Rust, TypeScript, Python, and OCaml ship neutral sibling implementations derived from this Ordered
contract through language-local ownership and policy models.

The [Ordered validation guide](../../src/CSharp/docs/Ordered/validation.md) and
[test map](../../src/CSharp/tests/Durable7.Ordered.Tests/README.md) define the evidence
boundary. Focused single-worker Debug and Release lanes each discover and pass 62 tests. At the
historical pre-Range Ordered shipment checkpoint, the complete serialized C# Release gate built
with zero warnings or errors and passed all 1,355 tests.
Correctness, deterministic operation-count, invariant, persistence, failure, API-shape, and
dependency gates are the current evidence. Benchmarks are not a shipment requirement and remain
postponed to an isolated, contention-free run.

## Sorted Collections

Sorted bags, sets, and maps are ordered facades over the local core engine or an idiomatic ordered
storage checkpoint.

Shared obligations:

- Comparison policy defines equality for sorted structures. If two values compare equal, the API must
  document whether duplicates are counted, rejected, replaced, or merged.
- Sorted bag enumeration includes duplicates in sorted order.
- Sorted set enumeration contains one representative per comparison-equivalent value.
- Sorted map lookup, rank, and boundary queries use key comparison, not hash equality.
- Rank and index APIs must define zero-based versus one-based indexing, miss behavior, and duplicate
  handling.
- Builders or mutable construction helpers must document when they stop sharing with immutable
  snapshots and when snapshots are published.

## Priority Queues

Priority queues select the front entry according to the local priority comparison.

Shared obligations:

- The comparison policy defines the front priority.
- Equal-priority tie behavior must be documented in the local API spec or notes. If the structure is
  stable, tests should prove it; if it is unspecified, docs should avoid promising stability.
- Peek and pop/dequeue APIs must distinguish empty-queue behavior from valid entries whose element or
  priority value can be null/default.
- Meld or concat operations, where exposed, must preserve priority policy and old versions.

## Interval Trees

Interval trees store ordered intervals and expose overlap or containment queries.

Shared obligations:

- Interval construction must define whether empty or inverted intervals are rejected, normalized, or
  accepted.
- Endpoint comparison policy defines ordering, equality, overlap, and containment.
- Query APIs must document whether endpoints are inclusive and what happens on the first matching
  overlap when multiple overlaps exist.
- Removal must state whether it removes one matching interval, all matching intervals, or a
  comparison-equivalent representative.
- Measured implementations use cached interval summaries, commonly count, last low endpoint, and max
  high endpoint, so subtree pruning remains valid.

## Persistent Interval Maps

The payload-bearing interval map ships across all nine languages as a derived companion to the
interval tree. Its complete `(low, high)` pair is the unique key; intervals that share only one
endpoint remain distinct. Shared obligations are:

- endpoint policy defines interval-key ordering, equality, validity, and inclusive overlap;
- exact lookup, insertion/replacement, and removal operate on the complete interval key;
- replacing a payload preserves the stored interval representative and does not duplicate or move
  the interval key;
- stabbing and overlap queries use the augmented interval index and return entries in the local
  deterministic interval order;
- the exact-key index and augmented search index contain the same intervals, payloads, and count;
  and
- a logical no-op returns the receiver, while allocation, comparison, or callback failure leaves
  both indexes and all retained versions unchanged.

See the C# [FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md) and
the [cross-language catalog](data-structure-catalog.md#derived-persistent-maps-relations-and-sparse-bit-sets).

## Persistent Collection Cursors

C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python ship public cursors for every
repository-owned persistent family with a stable semantic navigation axis. The complete
applicability matrix, per-family edit rules, representation profiles, complexity vocabulary,
ownership mapping, and validation design are in the
[repository-wide persistent cursor design](../proposals/repository-wide-persistent-cursor-design.md).

The shipped groups are:

- signed 32/64-bit Patricia maps and sets in ascending key order;
- measured sequences, deques, reversible deques, RRB vectors, Range sequences, and ropes/text over
  positional or measured gaps;
- sorted bags/sets/maps, canonical sorted sets, priority-search queues, interval trees/maps, and
  chunked bit sets over their documented order or population-rank axis;
- neutral insertion-/explicit-position Ordered sets, maps, and nested multimaps; and
- Merkle search trees through a specialized comparer-order authenticated cursor.

Every initialized cursor owns or retains one immutable logical version and a valid gap, rank, or
measure boundary. Navigation returns a cursor over the same version. Editing returns a cursor over
one new version and never changes the receiver, a retained ancestor, or a sibling branch. Snapshot
is non-consuming. A cursor is not a detachable position, mutable iterator, builder, transient,
bookmark, rebase operation, or live concurrent view.

Positional cursors use gaps in `0 .. Count`: previous operations address `p - 1`, next operations
address `p`, insertion returns the gap after inserted values, backspace moves the gap left, and
forward deletion or replacement keeps it fixed. Ordered cursors use the gap before the next
comparer-ordered candidate; lower-bound misses remain usable, exact search carries a separate hit
discriminator, and edits validate that a key/value belongs at the current gap. Nested Ordered
multimap cursors keep group and value positions explicit rather than inventing a private flattened
tree order.

Cursors retain the source's exact comparer, equality/hash policies, measure/tag algebra, ownership
callbacks, codecs, allocators, and stored-representative rules. Family-local ordinary persistent
operations remain authoritative for duplicate behavior, no-op identity, checked growth, lazy tags,
balancing, canonical ranks, auxiliary indexes, winner/interval caches, Merkle bytes/digests, error
precedence, and failure atomicity. Most new ports are semantic snapshot-plus-gap/rank checkpoints;
they make no C# focused-rope locality, callback, allocation, memoization, or benchmark claim.

CHAMP maps/sets and their bag, bimap, hash multimap/relation, patch, and indexed-map compositions
keep edit paths private because hash traversal has no public semantic neighbor. Ctrie live
views, graphs, measured priority queues, Brodal–Okasaki heaps, DABA Lite, builders, sessions, stores,
proofs, and packs intentionally expose no persistent cursor.

## Ropes And Text

Ropes provide persistent chunked sequences. Measured ropes add custom measure-based split and locate.
Text ropes specialize measured ropes for newline-aware text navigation.

Shared obligations:

- Rope indexing, split, insert, remove, concat, slice, and chunk enumeration must define logical
  element offsets and range validation.
- Chunking is an implementation detail unless a public API exposes chunks; exposed chunk APIs must
  state whether chunks are immutable, borrowed, copied, compacted, or normalized.
- Measured rope split and locate inherit the same measure-law requirements as measured trees.
- Text rope offsets are element offsets in the local text representation. In every language's docs,
  be explicit about byte, `char`, UTF-16, Unicode scalar/code-point, or `Text` semantics when it matters;
  Python indexes Unicode code points.
- Newline navigation must define line numbering, column numbering, trailing newline behavior, and
  invalid offset behavior.
- Builders must document mutation, snapshot publication, and whether later builder changes can affect
  previously produced immutable ropes.

C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python ship positional and measured/text
rope cursors. In addition to the repository-wide cursor contract above, every rope cursor shares
these observable obligations:

- A cursor position is a gap in `0 .. Count`: previous operations address `p - 1`, next operations
  address `p`, insertion returns the gap after the inserted values, backspace moves the gap left,
  and forward deletion or replacement keeps it fixed. Empty, start, and end gaps are valid states.
- The initialized cursor is an immutable value over one logical version and a validated gap.
  Navigation retains the version; every edit creates an independent version.
  Retained cursors and snapshots stay valid, and editing any retained cursor creates a branch without
  changing its ancestor or siblings.
- A same-position seek and an empty range insertion preserve the exact stored version state.
  `ReplaceNext` always creates a new logical version and does not consult element equality.
- A cursor created from a rope retains that source version as its clean snapshot. Snapshot creation
  must not mutate an ancestor or sibling version, and a failed edit must leave the receiver reusable.
- Initialized cursor values do not mutate shared persistent storage during reads. Each language must
  document its default-construction, invalid-operation, borrowed-value, and thread-safety conventions.

The C# positional cursor's focused representation additionally requires a shared navigation context and snapshot cache. A
dirty first `Snapshot()` publishes one canonical rope through a thread-safe winner-returning memo
cell; repeated clean snapshots from any navigation context over the version are O(1) and return the
same rope reference. Failed construction publishes nothing, initialized cursors support racing first
snapshots, the default `RopeCursor<T>` is invalid, and the initialized empty state comes from
`Rope<T>.Empty.GetCursor()`. Its proven complexity scope is linear-lineage only: local movement and
single-element edits are O(1) amortized along one lineage and O(log n) worst-case, while dirty snapshot
materialization is bounded focus/carry packing plus an O(log n) tree join. Editing `b` independently
retained cursors
  at a boundary has the conservative O(b log n) aggregate bound; there is no unqualified
  arbitrary-version-DAG O(1)-amortized claim.

The C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python positional checkpoints store an
already-canonical retained rope plus its gap. Their language-local checkpoints have the
complexity boundaries documented in their local API notes; TypeScript, Python, and OCaml likewise inherit
their package-local persistent checkpoint costs rather than the C# focused cursor bounds. None has a
default-constructed cursor or claims a focused cursor representation, memo-cell, or
O(1)-amortized local-edit claim. Haskell uses outer `Maybe` for the boundary, so a stored `Nothing`
at element type `Maybe a` is `Just Nothing`; invalid movement/edit operations also return `Nothing`.
Its pure growth failures raise a length-overflow exception before publishing a result, leaving all
retained values reusable. Kotlin uses a non-null peek wrapper to distinguish a stored null from a
missing neighbor. Rust edits retain the substrate's `T: Clone` bound; read-only cursor operations do
not.

C `ft_rope_cursor` expresses the same retained-root checkpoint as an explicit owned handle. The zeroed,
moved-from, or disposed value is invalid; initialized cursors require `copy`, consuming `move`, and `dispose`.
Cursor-producing operations support exact source/result aliasing and otherwise publish a distinct uninitialized
destination only on success. Copy, movement, seek, and snapshot perform O(1) structural work plus allocation of
one self-owned policy context and therefore return `ft_status`; failure leaves retained inputs and an existing
output unchanged. Peeks copy through the rope's value policy rather than returning borrowed storage. Peeks and
point edits are O(log n) plus bounded chunk work, while array insertion adds O(m) capture work. It makes no
focused cursor representation, memo-cell, allocation-ceiling, or O(1)-amortized locality claim.

The C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python measured cursors additionally share these result semantics:

- `MeasureBefore` aggregates `[0, Position)` and `MeasureAfter` aggregates `[Position, Count)`;
  combining them in that order yields the whole version's measure without assuming an inverse,
  commutativity, element equality, or a default-value identity.
- Absolute measure seek selects the gap before the first element whose inclusive prefix satisfies a
  lawful monotone predicate. A predicate already true at the identity selects zero for a nonempty rope; misses and empty ropes
  return `false` with an end cursor whose before measure is the whole measure.
- The newline specialization uses the language's existing zero-based line/column rules. C#, Kotlin,
  and TypeScript positions are UTF-16 code units; C and C++ positions are `char`/`std::string` bytes;
  Haskell positions are `Char` elements; OCaml and Rust positions are Unicode scalar values; Python
  positions are Unicode code points. None denotes a grapheme-cluster index.
  Navigation and edits preserve access to the existing text helpers.

The C# measured cursor's focused representation additionally prepares element measures in the immutable cursor lineage,
shares them with descendants, and publishes prefix/suffix tables failure-atomically. Failed or racing
callbacks cannot expose partially initialized state, and a dirty snapshot does not remeasure already
prepared elements. Its measure properties are O(1) cached reads and its newline specialization adds
no second text representation.

Kotlin's `MeasuredRopeCursor<T, M>` remains a snapshot-plus-gap checkpoint over the measured AVL
substrate. Nullable aggregate values remain distinct from the empty aggregate. Creation, movement,
positional seek, and snapshot are O(1); measure reads, peeks, point
edits, and absolute measure search are O(log n), and measure reads may invoke the runtime policy.
`TextRopeCursor` is a thin newline-policy facade that retains the exact `TextRope` on navigation and
wraps edited measured snapshots in O(1). Kotlin checked growth rejects `Int` overflow before policy
callbacks or publication after one-shot range capture. It claims no C# fragment cache, snapshot memo,
allocation ceiling, or O(1)-amortized local editing.

C++ `measured_rope_cursor<T, MeasurePolicy>` is the analogous root-plus-gap checkpoint over the
chunked measured rope. Its search result carries a usable end cursor on a miss; borrowed peeks are
lvalue-only, and move operations copy the shared root so the source remains valid. The
`text_rope_cursor` alias preserves the existing byte-oriented text facade. Known-count growth uses
checked `size_t` preflights before new element-measure callbacks. It claims no focused cursor representation,
snapshot memo, allocation ceiling, callback-count ceiling, or amortized locality.

C `ft_measured_rope_cursor` extends the explicit owned-handle checkpoint with ordered before/after partitions,
absolute monotone-prefix search, and a usable end cursor on a miss. Copy, movement, positional seek, and snapshot
perform O(1) structural work plus a self-owned policy-context allocation; partitions, copied peeks, point edits,
and search are O(log n) plus bounded chunk work, and range insertion is O(m + log n). The nominal
`ft_text_rope_cursor` preserves `ft_text_rope` snapshots and its byte-oriented LF-only line/column helpers.
Known-count measured concat/insertion and derived text line count reject `size_t` overflow before publication.
Callbacks are infallible under the existing C boundary, so no callback-retry guarantee is claimed. Neither C
cursor claims a focused cursor representation, snapshot memo, callback/allocation ceiling, amortized locality, or benchmark
evidence.

Haskell's opaque `MeasuredRopeCursor v a` is the analogous snapshot-plus-gap checkpoint over the
existing chunked measured finger tree. `measureBefore`/`measureAfter` preserve noncommutative order,
and `MeasuredRopeCursorSearch` distinguishes an absolute first-prefix hit from a miss while retaining
a usable end cursor. Creation, movement, positional seek, and snapshot are O(1); peeks, measure
partitions, point edits, and measure search are O(log n) plus a bounded 64-element chunk scan, and
range insertion is O(m + log n). The text cursor is a zero-wrapper type alias, so its snapshots keep
all text helpers and positions count `Char` elements. Because `TextRope` is also a
type alias, callers must construct it through `fromString`/`fromText` or preserve the same newline
measure extensionally; nominal enforcement is not claimed. Checked measured growth rejects `Int`
overflow before attempted-growth element/monoid callbacks once operand counts and the necessary
range spine are available. Pure evaluation may still force an unevaluated operand or list spine
first. The derived text line count is separately checked. No focused cursor representation, memo, allocation ceiling,
callback-count ceiling, or amortized-locality claim is made.

Rust's opaque `MeasuredRopeCursor<T, P>` retains the exact chunked measured-rope root plus a
validated `usize` gap. Ordered measures and absolute monotone search require no `T: Clone`; edits
retain the affected-chunk clone bound. Creation, movement, positional seek, and snapshot are O(1),
while measures, peeks, point edits, and search are O(log n) plus bounded chunk work and range
insertion is O(m + log n). `MeasuredRopeCursorSearch<T, P>` carries a usable end cursor on a miss.
The nominal `TextRopeCursor` wraps the newline policy without losing the `TextRope` facade and
reports line/column positions in Unicode scalar values under the existing LF-only measure. Checked
count preflights reject unrepresentable `usize` growth before attempted element-measure callbacks.
No focused cursor representation, snapshot memo, allocation ceiling, callback-count ceiling, or amortized-locality
claim is made.

TypeScript, Python, and OCaml retain their immutable measured-rope checkpoints plus validated gaps. Ordered
before/after measures, absolute prefix search, retained branching, unconditional replacement, and
text-facade preservation follow the shared result semantics above. TypeScript keeps JavaScript
UTF-16 indexing. Python's measured rope uses an immutable measured-AVL checkpoint and its text cursor
counts Unicode code points; OCaml validates UTF-8 and indexes `Uchar.t` scalar values. Positional and
measured cursors expose presence-safe results where the element domain requires them, so a stored
`undefined`/`None` remains distinct from a missing neighbor. Measured replacement invokes the
replacement's measure callbacks even when the object is identical. None of these packages claims
the C# focus/carry cursor representation, snapshot-memo, allocation, callback-count, or amortized-locality bounds.

The normative C# details and evidence are in the
[FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md),
[usage guide](../../src/CSharp/docs/FingerTree/usage.md),
[validation guide](../../src/CSharp/docs/FingerTree/validation.md), and
[C0 positional decision](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md), and
[C2 measured decision](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md). The
Kotlin checkpoint is specified in its [API notes](../../src/Kotlin/FingerTree/docs/api-notes.md) and
[validation guide](../../src/Kotlin/FingerTree/docs/validation.md). The Haskell checkpoint is
specified by its [workspace README](../../src/Haskell/FingerTree/README.md) and executable
[test map](../../src/Haskell/FingerTree/test/README.md). The Rust checkpoint is specified by its
[API notes](../../src/Rust/FingerTree/docs/api-notes.md) and
[validation guide](../../src/Rust/FingerTree/docs/validation.md). The C++ checkpoint is specified by
its [API notes](../../src/Cpp/FingerTree/docs/api-notes.md) and
[validation guide](../../src/Cpp/FingerTree/docs/validation.md). The C cursor checkpoints are
specified by its [API notes](../../src/C/FingerTree/docs/api-notes.md) and
[validation guide](../../src/C/FingerTree/docs/validation.md). The TypeScript, Python, and OCaml checkpoints
are specified by their [TypeScript API notes](../../src/TypeScript/docs/api-notes.md) and
[Python API notes](../../src/Python/docs/api-notes.md), and
[OCaml API notes](../../src/OCaml/docs/api-notes.md), respectively.

## Ownership And Lifetime By Language

| Language | Ownership model | Documentation focus |
| --- | --- | --- |
| C# | Garbage-collected references, immutable public collection values, and explicitly single-owner transient sessions where exposed | Exceptions, nullable/miss behavior, XML documentation, structural sharing, builder publication versus one-way transient consumption |
| C | Opaque handles/value structs with explicit copy/destroy, callback policies, and explicitly aliased transient state where exposed | Status codes, allocation failure, retained versus borrowed values, alias-wide consumption, cleanup obligations, callback lifetime |
| C++ | RAII values over shared immutable nodes plus move-only editing sessions where exposed | Move/copy cost, exception behavior including policy moves during session movement and publication, policy object lifetime, iterator/materialization behavior |
| Haskell | Pure immutable values plus explicitly scoped `IO` editing sessions where exposed | Total versus `Maybe` operations, session consumption, package-local type classes, dependency-light design, strictness where relevant |
| Kotlin | Immutable JVM values plus runtime policies and dynamically consumed editing sessions where exposed | Null/result/exception shapes, version-bound views, JVM comparator/hash policy behavior, structural sharing, and documented engine complexity |
| OCaml | Garbage-collected immutable algebraic values plus runtime policy records and dynamically consumed sessions where exposed | `option`/`result` boundaries, policy identity, qualified modules, strict warnings, Unicode-scalar text positions, mutex-coordinated facades, and documented checkpoint complexity |
| Rust | Owned values, borrows, `Arc` sharing, consuming editing sessions where exposed, traits, and `Option`/`Result` | Clone requirements, borrowed lookup results, ownership-enforced publication, panic boundaries, safe Rust guarantees, Send/Sync claims only when proven |
| TypeScript | Garbage-collected JavaScript objects, immutable public versions, runtime policies, and dynamically consumed sessions where exposed | ESM exports, `undefined`/miss/result shapes, stable hashing versus JavaScript identity, iterator invalidation, isolate-local concurrency, and UTF-16 indexing |
| Python | Garbage-collected Python objects, immutable public versions, runtime policies, and dynamically consumed sessions where exposed | Typing/runtime validation, `None` versus absence, equality/hash coherence, iterator invalidation, lock-coordinated facades, code-point indexing, and mutable-value caveats |

## What A New Public Surface Must Document

When adding a new collection, helper, builder, or facade, add docs that answer:

- What is the public entry point and which namespace, module, header, package, or crate exports it?
- Which workspace owns it, and does every dependency point toward a repository-general substrate?
- Which existing family is the semantic baseline?
- Which operations preserve persistence and structural sharing?
- Which policies are stored, inherited, or supplied per operation?
- What are the failure modes, and are they exceptions, status codes, `Maybe`, nullable returns,
  `Option`, or `Result`?
- What order does enumeration expose?
- Which operations have contractual complexity or allocation behavior?
- Which tests prove the contract?
- Which sibling workspaces intentionally do not expose the surface yet?

Then update:

- the workspace README;
- the workspace docs index;
- the [data-structure catalog](data-structure-catalog.md) if the public family surface changed;
- the [navigation matrix](navigation-matrix.md) if the new doc is a better first stop for a task;
- the [test suite map](test-suite-map.md) if tests, samples, benchmarks, or stress controls changed.
