# Semantic Contracts Reference

- Created (UTC): 2026-07-03T23:50:37Z
- Repository HEAD: 96a766f45fa42b5bd14c5ae3173956300cbff21b
- Audience: Maintainers and AI agents preserving cross-workspace behavior
- Scope: Shared contracts for repository-owned numerics, HAMT, FingerTree-family structures, ownership models, and documentation obligations

This reference summarizes the behavioral contracts that should stay recognizable across the repository's
language workspaces. It is not a replacement for the workspace API specifications, public headers, XML
documentation, or source tests. Use it as a checklist when reviewing whether a change preserves the
intended semantics across C#, C, C++, Haskell, Kotlin, Rust, TypeScript, and Python.

Authoritative local documents remain:

- [C# Numerics API and behavior reference](../../src/CSharp/docs/Numerics/api-and-behavior-reference.md)
- [C# HAMT API specification](../../src/CSharp/docs/Hamt/api-specification.md)
- [C HAMT API specification](../../src/C/Hamt/docs/api-specification.md)
- [C++ HAMT API specification](../../src/Cpp/Hamt/docs/api-specification.md)
- [C# FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md)
- [C FingerTree API notes](../../src/C/FingerTree/docs/api-notes.md)
- [C++ FingerTree API notes](../../src/Cpp/FingerTree/docs/api-notes.md)
- [Kotlin FingerTree API notes](../../src/Kotlin/FingerTree/docs/api-notes.md)
- [Rust FingerTree API notes](../../src/Rust/FingerTree/docs/api-notes.md)
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
| Measure | A monoidal summary cached on a finger-tree node, chunk, or facade element and used for split, locate, rank, priority, interval, rope, or text navigation. |
| Version-bound cursor | An immutable working value that owns one persistent sequence version and a position within it; navigation and editing never redirect it to a different version implicitly. |
| Checkpoint port | A port that preserves observable API semantics and tests while documenting a remaining representation or asymptotic parity boundary. |
| Facade | A public collection built on a shared core engine, such as sorted sets on measured trees or text ropes on measured ropes. |
| Application-specific leaf | A workspace that may consume general libraries but cannot be a dependency or semantic baseline for general-purpose structures. Reusable mechanisms leave it only through an independently owned fork. Tungsten is the repository's current example. |

## Fixed-Width Integer Numerics

`Tools.Numerics` in C# is the semantic reference; TypeScript and Python expose sibling ports of its
fixed-width signed and unsigned integers plus sparse integer helpers. TypeScript uses `bigint`, and
Python uses arbitrary-precision `int`, behind explicit fixed-width normalization and validation.

Public entry points:

- `UInt256`, `Int256`, `UInt512`, `Int512`, `UInt1024`, `Int1024`
- `SparseInteger`
- `BitConverterEx`

Shared obligations:

- Fixed-width values have deterministic two's-complement binary representations.
- Signed and unsigned types must agree on bit width, limb layout, conversion behavior, parse/format
  behavior, and edge-case validation where their public surfaces overlap.
- Binary conversion APIs must define byte order, exact byte counts, signedness, and validation order
  for null, index, range, and remaining-length failures.
- Arithmetic, comparison, parsing, formatting, and conversion docs should state whether behavior is
  checked, wrapping, sign-extending, truncating, or exception-throwing.
- Declaration parity tests are part of the contract: adding a member to one wide integer family usually
  means either adding the sibling member or documenting why parity is intentionally broken.

Primary evidence:

- [Numerics validation](../../src/CSharp/docs/Numerics/validation.md)
- [Numerics tests README](../../src/CSharp/tests/Tools.Numerics.Tests/README.md)
- [Wide-integer maintainer guidance](../../src/CSharp/docs/Numerics/wide-integer-maintainer-guidance.md)
- [TypeScript API notes](../../src/TypeScript/docs/api-notes.md) and [validation](../../src/TypeScript/docs/validation.md)
- [Python API notes](../../src/Python/docs/api-notes.md) and [validation](../../src/Python/docs/validation.md)

## HAMT Map And Set

The HAMT workspaces implement persistent hash-array mapped trie maps and sets.

Public surfaces:

| Language | Map | Set | Policy shape |
| --- | --- | --- | --- |
| C# | `PersistentHashMap<TKey, TValue>` and its nested `Transient` | `PersistentHashSet<T>` and its nested `Transient` | `IEqualityComparer<T>` |
| C | `tds_hamt_map` and `tds_hamt_map_transient` | `tds_hamt_set` and `tds_hamt_set_transient` | callback tables and context pointers |
| C++ | `persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>` and nested `transient` | `persistent_hash_set<T, Hash, KeyEqual>` and nested `transient` | template policy objects |
| Haskell | `HashMap k v` and `MapTransient k v` | `HashSet a` and `SetTransient a` | `Hashable`, `Eq`, optional `HashPolicy` |
| Kotlin | `PersistentHashMap<K, V>` and nested `Transient<K, V>` | `PersistentHashSet<T>` and nested `Transient<T>` | runtime `HashPolicy<K>` |
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
| Rust | Keys that compare equal under `Eq` must hash equally under the chosen `BuildHasher`; removal returns owned cloned values where exposed. |
| TypeScript | Runtime hash policies define equivalence and representative retention; `getOrAdd`/`addOrUpdate` follow the C# one-descent selected-factory contract; transient sessions are isolate-local path-copying facades with no cross-worker progress or edit-performance claim. |
| Python | Equality and hashing are explicit retained policy; `get_or_add`/`add_or_update` preserve one-descent factory selection and presence-safe result values; mutable application objects require caller discipline, and changed or consumed sessions invalidate version-bound iterators dynamically. |

### Persistent hash bags

`PersistentHashBag` ships in C#, TypeScript, and Python as an unordered-multiset facade over the
language-local persistent HAMT. It stores exactly one representative and one positive multiplicity
in `1 .. 2^31 - 1` per policy equivalence class. Distinct-class count is separate from expanded
total count, and no ambiguous collection `Count`/`size` is exposed for expanded enumeration. C# uses
a checked `long` total, TypeScript uses `bigint`, and Python uses `int`. Construction and point
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
reference is the [C# HAMT API specification](../../src/CSharp/docs/Hamt/api-specification.md); the
[TypeScript API notes](../../src/TypeScript/docs/api-notes.md) and
[Python API notes](../../src/Python/docs/api-notes.md) document their presence-safe lookup and
wide-count mappings.

### Construction-only HAMT bulk builders

C# bulk construction uses an internal mutable unpublished CHAMP builder. C++ and Rust expose the
same construction facility publicly; TypeScript and Python now expose reusable public builders as
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

### Concurrent snapshot facades

The C# and Kotlin/JVM Ctries are the deliberate lock-free managed tier. `Snapshot` captures a
generation in O(1); later writes lazily renew paths and cannot alter the captured view. Snapshot
enumeration follows canonical CHAMP ordering: singleton/data entries precede multi-entry child/node
runs at each bitmap level, frozen empty tombs are skipped, frozen singleton tombs are promoted
logically into the data run, and equal-hash collision buckets retain their local order. Explicit
snapshot-to-CHAMP conversion is O(n) and preserves that sequence, the exact policy object, stored
key/value representatives, null/present-null semantics, and isolation from later live writes.

TypeScript and Python preserve the consumer-level mutable-map/O(1)-snapshot vocabulary without
claiming that protocol. TypeScript is isolate-local and synchronous. Python serializes operations
with an `RLock` and publishes immutable CHAMP roots. Neither facade uses GCAS/RDCSS descriptors,
generation renewal, or a lock-free progress guarantee; conversion and concurrency claims must follow
its local API notes and tests.

### One-way CHAMP editing lifecycle

All eight HAMT workspaces expose a single-owner map/set edit-then-publish lifecycle. The shared
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
| C | `tds_hamt_map_transient` / `tds_hamt_set_transient` use explicit clone/destroy handles over one ref-counted state. Clones are lifecycle aliases: one successful publication consumes them all. Operations report `TDS_HAMT_TRANSIENT_CONSUMED` or `TDS_HAMT_TRANSIENT_MODIFIED` as appropriate; failed edits and relation queries preserve session/output atomicity. |
| C++ | Nested sessions are movable but not copyable and publish only through `std::move(session).persist()`. Generation-bound iterators reject changed or consumed sessions. A throwing custom policy move terminally invalidates the source and, for assignment, the destination; publication has no retry/content-preservation guarantee after a throwing policy move. Nothrow-movable policies avoid both exceptional boundaries. |
| Haskell | `MapTransient` / `SetTransient` live in `IO`; `persistMap` / `persistSet` consume the `IORef` state. Candidate construction precedes a masked commit, so synchronous or asynchronous failure cannot partially install an edit. |
| Kotlin | Nested `Transient` sessions enforce consumption with `IllegalStateException`; acquired views capture the current persistent snapshot and session version, survive logical no-ops, and fail after content changes. Callback failure leaves the active session unchanged and retryable. |
| Rust | `TransientHashMap` / `TransientHashSet` publish with consuming `into_persistent(self)`, so the type system prevents use-after-publication. `into_transient` moves a source while `to_transient` shares its root; both retain persistent path-copy edit costs. |
| TypeScript | `TransientHashMap` / `TransientHashSet` enforce one-way publication at runtime inside one JavaScript isolate. Changed edits replace the current persistent root through ordinary path copies; the facade makes no cross-worker progress or transient-performance claim. |
| Python | `TransientHashMap` / `TransientHashSet` retain a persistent current value, consume on publication, and invalidate iterators after content changes or publication; changed edits remain persistent path copies rather than owner-token mutation. |

The local authoritative references are the [C API specification](../../src/C/Hamt/docs/api-specification.md),
[C++ API specification](../../src/Cpp/Hamt/docs/api-specification.md),
[Haskell HAMT workspace](../../src/Haskell/Hamt/README.md),
[Kotlin API notes](../../src/Kotlin/Hamt/docs/api-notes.md),
[Rust API notes](../../src/Rust/Hamt/docs/api-notes.md),
[TypeScript API notes](../../src/TypeScript/docs/api-notes.md), and
[Python API notes](../../src/Python/docs/api-notes.md).

## Insertion-Ordered Persistent Set

`PersistentOrderedSet` ships in the neutral C#, TypeScript, and Python Ordered modules. It composes
a persistent HAMT membership/stamp index with a persistent ordered sequence and must not depend on
the application-specific Tungsten family. Shared obligations are:

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
result/exception shapes and diagnostic adaptations are documented in the
[TypeScript](../../src/TypeScript/docs/api-notes.md) and
[Python](../../src/Python/docs/api-notes.md) package notes.

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

C#, C, C++, Haskell, Kotlin, Rust, TypeScript, and Python ship positional and measured/text cursors. No deque, RRB,
raw-finger-tree, reversible-deque, or Tungsten cursor is implied by those surfaces. Every shipped
cursor shares these observable obligations:

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

The C# positional zipper additionally requires a shared navigation context and snapshot cache. A
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

The C, C++, Haskell, Kotlin, Rust, TypeScript, and Python positional checkpoints store an
already-canonical retained rope plus its gap. The first five native/JVM checkpoints have the
complexity boundaries documented in their local API notes; TypeScript and Python likewise inherit
their package-local persistent checkpoint costs rather than the C# zipper bounds. None has a
default-constructed cursor or makes a zipper, memo-cell, or
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
zipper, memo-cell, allocation-ceiling, or O(1)-amortized locality claim.

The C#, C, C++, Haskell, Kotlin, Rust, TypeScript, and Python measured cursors additionally share these result semantics:

- `MeasureBefore` aggregates `[0, Position)` and `MeasureAfter` aggregates `[Position, Count)`;
  combining them in that order yields the whole version's measure without assuming an inverse,
  commutativity, element equality, or a default-value identity.
- Absolute measure seek selects the gap before the first element whose inclusive prefix satisfies a
  lawful monotone predicate. A predicate already true at the identity selects zero for a nonempty rope; misses and empty ropes
  return `false` with an end cursor whose before measure is the whole measure.
- The newline specialization uses the language's existing zero-based line/column rules. C#, Kotlin,
  and TypeScript positions are UTF-16 code units; C and C++ positions are `char`/`std::string` bytes;
  Haskell positions are `Char` elements; Rust positions are Unicode scalar values; Python positions
  are Unicode code points. None denotes a grapheme-cluster index.
  Navigation and edits preserve access to the existing text helpers.

The C# measured zipper additionally prepares element measures in the immutable cursor lineage,
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
checked `size_t` preflights before new element-measure callbacks. It claims no focused zipper,
snapshot memo, allocation ceiling, callback-count ceiling, or amortized locality.

C `ft_measured_rope_cursor` extends the explicit owned-handle checkpoint with ordered before/after partitions,
absolute monotone-prefix search, and a usable end cursor on a miss. Copy, movement, positional seek, and snapshot
perform O(1) structural work plus a self-owned policy-context allocation; partitions, copied peeks, point edits,
and search are O(log n) plus bounded chunk work, and range insertion is O(m + log n). The nominal
`ft_text_rope_cursor` preserves `ft_text_rope` snapshots and its byte-oriented LF-only line/column helpers.
Known-count measured concat/insertion and derived text line count reject `size_t` overflow before publication.
Callbacks are infallible under the existing C boundary, so no callback-retry guarantee is claimed. Neither C
cursor claims a focused zipper, snapshot memo, callback/allocation ceiling, amortized locality, or benchmark
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
first. The derived text line count is separately checked. No zipper, memo, allocation ceiling,
callback-count ceiling, or amortized-locality claim is made.

Rust's opaque `MeasuredRopeCursor<T, P>` retains the exact chunked measured-rope root plus a
validated `usize` gap. Ordered measures and absolute monotone search require no `T: Clone`; edits
retain the affected-chunk clone bound. Creation, movement, positional seek, and snapshot are O(1),
while measures, peeks, point edits, and search are O(log n) plus bounded chunk work and range
insertion is O(m + log n). `MeasuredRopeCursorSearch<T, P>` carries a usable end cursor on a miss.
The nominal `TextRopeCursor` wraps the newline policy without losing the `TextRope` facade and
reports line/column positions in Unicode scalar values under the existing LF-only measure. Checked
count preflights reject unrepresentable `usize` growth before attempted element-measure callbacks.
No focused zipper, snapshot memo, allocation ceiling, callback-count ceiling, or amortized-locality
claim is made.

TypeScript and Python retain their immutable measured-rope checkpoints plus validated gaps. Ordered
before/after measures, absolute prefix search, retained branching, unconditional replacement, and
text-facade preservation follow the shared result semantics above. TypeScript keeps JavaScript
UTF-16 indexing. Python's measured rope uses an immutable measured-AVL checkpoint and its text cursor
counts Unicode code points. Positional and measured cursors in both packages expose entry/wrapper
peeks so a stored `undefined`/`None` remains distinct from a missing neighbor; measured replacement
invokes the replacement's measure callbacks even when the object is identical. Neither package claims the C# focus/carry zipper, snapshot-memo,
allocation, callback-count, or amortized-locality bounds.

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
[validation guide](../../src/C/FingerTree/docs/validation.md). The TypeScript and Python checkpoints
are specified by their [TypeScript API notes](../../src/TypeScript/docs/api-notes.md) and
[Python API notes](../../src/Python/docs/api-notes.md), respectively.

## Tungsten Collections

This section is a contract only among the application-specific Tungsten ports. Tungsten collections
serve the Tungsten project, may change as Wolfram-kernel behavior is newly discovered or
reinterpreted, and may move out of this repository. They are leaf consumers: general collections
must not reference their packages/types, use their implementation, or inherit their semantics.
If a mechanism deserves general use, fork it under a separate owner with independent contracts and
tests; do not make the fork track later Tungsten changes automatically.
The detailed [application-leaf boundary](tungsten-application-leaf-boundary.md) controls what counts
as a dependency, an independent fork, harmless provenance, and sufficient validation.

Tungsten collections compose HAMT keyed lookup with persistent ordered storage into a sequence facade
(`PersistentList`) and an insertion-ordered map (`PersistentAssociation`) whose ordering behavior
matches the kernel-verified Tungsten Language rules.

Shared obligations:

- Indexing is zero-based; every documented Tungsten correspondence names the one-based operation it
  mirrors.
- The association's ordering rules are normative and test-locked: duplicate construction keys keep
  first position with last value; `SetItem` updates in place; `Append`/`Prepend` move an existing
  key to the end/front; `Insert` of an existing key wins position and value with the index read
  before the old occurrence is removed; `Join` keeps the receiver's positions with the argument's
  values; `Sort`/`KeySort` are stable and produce ordinary associations.
- Keyed and ordered reads are both first-class: keyed lookup must not enumerate, and ordered
  enumeration must not hash.
- Key equality is the factory-supplied comparer; derived associations preserve it, and stored-key
  retention follows the HAMT contract (in-place updates keep the stored key instance; re-adds
  store the supplied instance).
- Observably unchanged writes preserve identity or root sharing where that is part of the local
  language contract; in value-shaped ports, they must remain semantically unchanged without
  weakening persistence.
- Order-maintenance labels (stamps) are an implementation detail; ports must reproduce the honest
  cost contract — gap-exhaustion relabeling is per produced version and not amortized under
  branching persistence — without exposing labels in the API.
- Absent-key reads are exceptions or `false`/miss results, never sentinel entries; mapping absence
  to `Missing[...]`-style values is a client concern.

## Ownership And Lifetime By Language

| Language | Ownership model | Documentation focus |
| --- | --- | --- |
| C# | Garbage-collected references, immutable public collection values, and explicitly single-owner transient sessions where exposed | Exceptions, nullable/miss behavior, XML documentation, structural sharing, builder publication versus one-way transient consumption |
| C | Opaque handles/value structs with explicit copy/destroy, callback policies, and explicitly aliased transient state where exposed | Status codes, allocation failure, retained versus borrowed values, alias-wide consumption, cleanup obligations, callback lifetime |
| C++ | RAII values over shared immutable nodes plus move-only editing sessions where exposed | Move/copy cost, exception behavior including policy moves during session movement and publication, policy object lifetime, iterator/materialization behavior |
| Haskell | Pure immutable values plus explicitly scoped `IO` editing sessions where exposed | Total versus `Maybe` operations, session consumption, package-local type classes, dependency-light design, strictness where relevant |
| Kotlin | Immutable JVM values plus runtime policies and dynamically consumed editing sessions where exposed | Null/result/exception shapes, version-bound views, JVM comparator/hash policy behavior, structural sharing, and documented engine complexity |
| Rust | Owned values, borrows, `Arc` sharing, consuming editing sessions where exposed, traits, and `Option`/`Result` | Clone requirements, borrowed lookup results, ownership-enforced publication, panic boundaries, safe Rust guarantees, Send/Sync claims only when proven |
| TypeScript | Garbage-collected JavaScript objects, immutable public versions, runtime policies, and dynamically consumed sessions where exposed | ESM exports, `undefined`/miss/result shapes, stable hashing versus JavaScript identity, iterator invalidation, isolate-local concurrency, and UTF-16 indexing |
| Python | Garbage-collected Python objects, immutable public versions, runtime policies, and dynamically consumed sessions where exposed | Typing/runtime validation, `None` versus absence, equality/hash coherence, iterator invalidation, lock-coordinated facades, code-point indexing, and mutable-value caveats |

## What A New Public Surface Must Document

When adding a new collection, numeric type, helper, builder, or facade, add docs that answer:

- What is the public entry point and which namespace, module, header, package, or crate exports it?
- Which workspace owns it, and does every dependency point toward a repository-general substrate?
- If the design was inspired by Tungsten, where is the independently owned fork and what
  Tungsten-specific guarantees were retained or deliberately dropped? A new general surface may
  not use Tungsten as its implementation or semantic baseline.
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
