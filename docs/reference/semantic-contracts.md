# Semantic Contracts Reference

- Created (UTC): 2026-07-03T23:50:37Z
- Repository HEAD: 96a766f45fa42b5bd14c5ae3173956300cbff21b
- Audience: Maintainers and AI agents preserving cross-workspace behavior
- Scope: Shared contracts for repository-owned numerics, HAMT, FingerTree-family structures, ownership models, and documentation obligations

This reference summarizes the behavioral contracts that should stay recognizable across the repository's
language workspaces. It is not a replacement for the workspace API specifications, public headers, XML
documentation, or source tests. Use it as a checklist when reviewing whether a change preserves the
intended semantics across C#, C, C++, Haskell, Kotlin, and Rust.

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

Use the [data-structure catalog](data-structure-catalog.md) for the inventory of public entry points.
Use the [porting guide](../guides/porting-and-semantic-parity.md) for the workflow that carries a
behavior change through sibling workspaces.

## Contract Vocabulary

| Term | Meaning in this repository |
| --- | --- |
| Persistent value | An operation that looks like an update returns a new value or handle while preserving every retained old version. |
| Structural sharing | Versions reuse immutable substructure where possible; no-op updates often preserve the same root or instance when the local API exposes that diagnostic. |
| Reference-first equality | When two stored/incoming references are provably identical, implementations may accept equality without invoking user equality. A non-identical reference never proves semantic inequality. |
| Policy preservation | Hash, equality, comparison, measure, allocator, ownership, and callback policies flow into derived versions unless a local API explicitly creates a new policy. |
| Stable but unspecified order | Enumeration order is deterministic for unchanged structure versions, but callers must not treat the exact trie/tree traversal order as a sorted or insertion order unless the API says so. |
| Measure | A monoidal summary cached on a finger-tree node, chunk, or facade element and used for split, locate, rank, priority, interval, rope, or text navigation. |
| Version-bound cursor | An immutable working value that owns one persistent sequence version and a position within it; navigation and editing never redirect it to a different version implicitly. |
| Checkpoint port | A port that preserves observable API semantics and tests while documenting a remaining representation or asymptotic parity boundary. |
| Facade | A public collection built on a shared core engine, such as sorted sets on measured trees or text ropes on measured ropes. |

## Fixed-Width Integer Numerics

`Tools.Numerics` currently lives in the C# workspace. Its contract covers fixed-width signed and
unsigned integers plus sparse integer helpers.

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

## HAMT Map And Set

The HAMT workspaces implement persistent hash-array mapped trie maps and sets.

Public surfaces:

| Language | Map | Set | Policy shape |
| --- | --- | --- | --- |
| C# | `PersistentHashMap<TKey, TValue>` | `PersistentHashSet<T>` | `IEqualityComparer<T>` |
| C | `tds_hamt_map` | `tds_hamt_set` | callback tables and context pointers |
| C++ | `persistent_hash_map<Key, T, Hash, KeyEqual, ValueEqual>` | `persistent_hash_set<T, Hash, KeyEqual>` | template policy objects |
| Haskell | `HashMap k v` | `HashSet a` | `Hashable`, `Eq`, optional `HashPolicy` |
| Kotlin | `PersistentHashMap<K, V>` | `PersistentHashSet<T>` | runtime `HashPolicy<K>` |
| Rust | `PersistentHashMap<K, V, S>` | `PersistentHashSet<T, S>` | `Eq` plus `BuildHasher` |

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
- Iteration order is trie order: stable for unchanged versions, not sorted, not insertion ordered, and
  not a serialization contract.
- Diagnostic root-sharing APIs such as `shares_root_with` or debug root kind are test aids, not
  general semantic dependencies for callers.

Language-specific obligations:

| Language | Additional contract |
| --- | --- |
| C# | Public XML docs must state comparer preservation, exception behavior, no-op identity where promised, and IReadOnly collection semantics. |
| C | Every function must define ownership of input keys/values, retained outputs, status codes, and cleanup on partial failure. Callback contexts must outlive collections that use them. |
| C++ | Value objects should remain cheap to copy through shared immutable nodes; template policies must stay part of the value's semantic identity. |
| Haskell | `HashPolicy` and package-local `Hashable` shape are part of the port, avoiding third-party dependencies while preserving persistent HAMT behavior. |
| Kotlin | Miss paths and duplicate results should use idiomatic Kotlin null/result/exception shapes documented in API notes. |
| Rust | Keys that compare equal under `Eq` must hash equally under the chosen `BuildHasher`; removal returns owned cloned values where exposed. |

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
- Text rope offsets are element offsets in the local text representation. In C#, Kotlin, Rust, and
  Haskell docs, be explicit about `char`, UTF-16, Unicode scalar, byte, or `Text` semantics when it matters.
- Newline navigation must define line numbering, column numbering, trailing newline behavior, and
  invalid offset behavior.
- Builders must document mutation, snapshot publication, and whether later builder changes can affect
  previously produced immutable ropes.

C# additionally ships the positional `RopeCursor<T>`; no measured-rope, text, deque, RRB, raw-
finger-tree, or Tungsten cursor is implied by that surface. Its current obligations are:

- A cursor position is a gap in `0 .. Count`: previous operations address `p - 1`, next operations
  address `p`, insertion returns the gap after the inserted values, backspace moves the gap left,
  and forward deletion or replacement keeps it fixed. Empty, start, and end gaps are valid states.
- The initialized cursor is an immutable value over one logical version and navigation context.
  Navigation retains the version and its snapshot cache; every edit creates an independent version.
  Retained cursors and snapshots stay valid, and editing any retained cursor creates a branch without
  changing its ancestor or siblings.
- `Seek(Position)` and an empty `InsertRange` preserve the exact shared version and context state.
  `ReplaceNext` always creates a new logical version and does not consult element equality.
- A cursor created from a rope has that source object as its clean snapshot. A dirty first
  `Snapshot()` publishes one canonical rope through a thread-safe winner-returning memo cell;
  repeated clean snapshots from any navigation context over the version are O(1) and return the same
  rope reference. A failed construction publishes nothing and leaves the version reusable.
- Initialized cursor values are safe for concurrent reads, including racing first snapshots. The
  default `RopeCursor<T>` value is deliberately invalid; the initialized empty state comes from
  `Rope<T>.Empty.GetCursor()`.
- The proven complexity scope is linear-lineage only: local movement and single-element edits are
  O(1) amortized along one lineage and O(log n) worst-case, while dirty snapshot materialization is
  bounded focus/carry packing plus an O(log n) tree join. Editing `b` independently retained cursors
  at a boundary has the conservative O(b log n) aggregate bound; there is no unqualified
  arbitrary-version-DAG O(1)-amortized claim.

The normative C# details and evidence are in the
[FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md),
[usage guide](../../src/CSharp/docs/FingerTree/usage.md),
[validation guide](../../src/CSharp/docs/FingerTree/validation.md), and
[C0 decision record](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md).

## Tungsten Collections

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
| C# | Garbage-collected references and immutable public collection values | Exceptions, nullable/miss behavior, XML documentation, structural sharing, builder publication |
| C | Opaque handles/value structs with explicit copy/destroy and callback policies | Status codes, allocation failure, retained versus borrowed values, cleanup obligations, callback lifetime |
| C++ | RAII values over shared immutable nodes | Move/copy cost, exception behavior, policy object lifetime, iterator/materialization behavior |
| Haskell | Pure immutable values | Total versus `Maybe` operations, package-local type classes, dependency-light design, strictness where relevant |
| Kotlin | Immutable JVM values plus runtime policies | Null/result/exception shapes, JVM comparator/hash policy behavior, structural sharing, and documented engine complexity |
| Rust | Owned values, borrows, `Arc` sharing, traits, and `Option`/`Result` | Clone requirements, borrowed lookup results, panic boundaries, safe Rust guarantees, Send/Sync claims only when proven |

## What A New Public Surface Must Document

When adding a new collection, numeric type, helper, builder, or facade, add docs that answer:

- What is the public entry point and which namespace, module, header, package, or crate exports it?
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
