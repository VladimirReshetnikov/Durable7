# Repository-Wide Persistent Cursor Design

- Created (UTC): 2026-07-18T00:59:25Z
- Repository implementation baseline: 7a3924c116b941cb1e43a902b63f94876bfd251a
- Status: Implemented semantic contract; all applicable public cursor families ship in all nine
  language ports, with focused-representation optimizations remaining family-local
- Audience: Maintainers and port authors maintaining persistent navigation and localized editing
- Scope: Applicability, semantics, representations, APIs, complexity, validation, and porting of
  cursors across every repository-owned persistent data-structure family

## Decision

Adopt one repository-wide **cursor** vocabulary, but do **not** add one undifferentiated generic
cursor to every immutable type.

1. Public cursors are immutable and version-bound over a stable semantic navigation axis.
   Positional and insertion-ordered collections use a gap in `0 .. Count`; key- or value-sorted
   collections use an ordered search location with before-first and after-last sentinels; nested
   ordered collections expose both applicable axes.
2. Recursive implementation trees may use private focused edit paths whose breadcrumbs retain the
   information needed to rebuild valid ancestors. A private path is not automatically a new public
   collection feature.
3. A hash-trie or heap topology is not a public navigation order. CHAMP, dual-hash-index facades,
   concurrent tries, and meldable heaps therefore do not expose public cursors merely because their
   implementations are recursive.
4. Every cursor is itself a persistent working version. Navigation and edits return new cursor
   values, retained ancestors remain usable, and materializing the collection never consumes the
   cursor.
5. `Cursor` is the sole normative API and documentation term. The background below mentions Huet's
   historical name once to explain the concept's origin; subsequent sections use *focused cursor*
   or *edit path* for representations.
6. The existing C# focused rope cursor and the eight sibling snapshot-plus-gap rope cursors remain
   the focused-performance precedent. The broader shipment uses family-local semantic checkpoints;
   it does not claim the rope representation or performance for another family or port.
7. Tungsten collections do not need cursors. They remain application-leaf consumers of ordinary
   repository-general collections and are explicitly excluded from the cursor surface.

This document is both the repository-wide design and the normative applicability record for the
shipped semantic checkpoint tier. A row marked **shipped cursor** below means that all nine language
ports expose the family-local equivalent. Representation and complexity remain local: most new
surfaces retain a canonical persistent snapshot plus a validated gap or rank, while C# rope keeps
its separately proved focused implementation. A future representation change must preserve this
observable contract and update the affected language documentation before making stronger claims.

## Background And Repository Context

Huet called the underlying focused-path technique a *zipper*: a focused subtree is paired with a
reversed path containing the siblings and constructor information needed to rebuild the root.
Moving the focus changes that decomposition; replacing it is local; closing the cursor reconstructs
the complete tree. The same idea gives a sequence location as
`left context + focus or gap + right context`. This document uses **cursor** from this point onward.

The user-provided [historical overview](https://en.wikipedia.org/wiki/Zipper_(data_structure)) is a
useful orientation. The primary source is
[Gérard Huet's 1997 paper](https://www.st.cs.uni-saarland.de/edu/seminare/2005/advanced-fp/docs/huet-zipper.pdf)
([DOI](https://doi.org/10.1017/S0956796897002864)). Repository designs add constraints that the
minimal tree example does not have: cached monoidal measures, balancing, canonical ranks, lazy tags,
content digests, comparer and ownership policies, multiple indexes that must publish atomically,
and cross-language failure models.

The repository's focused-performance precedent is:

- C# `RopeCursor<T>` and `MeasuredRopeCursor<T, TMeasure, TMeasureOps>` use a persistent
  focused-cursor-as-version representation with a bounded 16-element focus, bounded carries, and a
  memoized canonical snapshot. Their exact representation, proof boundary, and performance evidence
  remain owned by
  the [positional decision](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md),
  [measured decision](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md), and
  [FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md#positional-edit-cursor).
- C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python expose equivalent version-bound rope
  cursor semantics through snapshot-plus-gap checkpoints. They deliberately make no C# focused
  representation, memoization, allocation, callback-count, or amortized-locality claim. The shared
  current contract is in [semantic contracts](../reference/semantic-contracts.md#ropes-and-text).

This design preserves that distinction. Observable parity does not imply representation or
complexity parity.

## Goals

- Decide, explicitly and exhaustively, which persistent families have a meaningful cursor.
- Give applicable families a coherent focus model, navigation vocabulary, editing rules,
  reconstruction invariant, and honest complexity target.
- Preserve every collection's equality, ordering, measure, representative, ownership, balancing,
  content-addressing, and multi-index publication contracts.
- Make branching histories, failure atomicity, concurrency, and materialization behavior explicit.
- Provide a C#-shaped reference API while allowing idiomatic C, C++, Haskell, Kotlin, OCaml, Rust,
  TypeScript, and Python spellings.
- Separate semantic baseline requirements from optional focused-representation optimizations.
- Record explicitly that Tungsten collections need no cursor surface.

## Non-Goals

- No representation rewrite or stronger performance claim is authorized merely by this document.
- No universal reflection- or continuation-based generic cursor is part of the design.
- No raw private node, digit, sparse label, owner token, hash fragment, lazy suspension, or block
  layout becomes public API.
- No cursor silently rebases onto an unrelated collection version.
- No cursor is a mutable iterator, builder, CHAMP transient, transaction, bookmark, lens, or
  concurrent live view.
- No arbitrary element editing is added to a semantic heap merely because its private forest can be
  traversed.
- No worst-case or amortized bound is inherited from a different substrate or language port.
- No benchmark conclusion is asserted. Stronger performance claims require family-local evidence.

## Terminology

| Term | Meaning in this design |
| --- | --- |
| **focus** | The current element, subtree, search result, or gap from whose perspective the rest of the value is represented. |
| **gap** | A boundary `p` between `[0, p)` and `[p, n)`; valid even for empty, start, and end positions. |
| **context / breadcrumb** | Immutable data sufficient to reconstruct one parent around the focused child: constructor tag, direction or slot, siblings, parent payload, and required policy/cache data. |
| **location** | Focus plus its context path. A public location is called a cursor. |
| **close / materialize / snapshot** | Reconstruct or obtain the canonical persistent collection represented by the cursor. Public APIs use the family-local established term, normally `Snapshot()`. |
| **navigation version** | Several cursor positions over the same logical collection version. They may share one materialized-root memo. |
| **edit version** | A new logical collection version created by a cursor edit. It owns independent context and snapshot state. |
| **semantic cursor** | Public focus over a promised position, order, key, interval, bit rank, or measure axis. |
| **structural cursor / edit path** | Private focus over implementation nodes. Its shape may change without a public API change. |
| **snapshot-plus-position checkpoint** | A correct cursor retaining a canonical root plus position and implementing edits through ordinary persistent operations; it does not claim focused locality. |
| **focused cursor** | A representation retaining decomposed context so nearby navigation or editing can reuse the open path. |

## Applicability Test

A public cursor is applicable only when all of the following are true:

1. **Stable semantic axis.** Consumers can name an order, position, measure boundary, ordered key
   location, or recursive child relation without observing private topology.
2. **Meaningful focus.** Moving to a neighbor or parent has domain meaning. A bitmap slot selected by
   five private hash bits is not sufficient.
3. **Invariant-preserving closure.** Breadcrumbs can rebuild a valid public value while recomputing
   every affected cache and preserving policy objects.
4. **Localized workload.** Retaining the open path can plausibly benefit navigation or clustered
   edits beyond the collection's existing direct operations.
5. **Persistent ownership.** A cursor can own or retain everything needed for branching and safe
   materialization in each language.
6. **Honest abstraction.** The API does not turn an unordered collection into an accidentally
   ordered one, a priority queue into an arbitrary list editor, or a content-addressed tree into an
   unverified mutable block graph.

When only conditions 3–5 hold for private nodes, an internal focused path may be useful but no
public cursor is exposed. When a retained root plus a key or index already provides the complete
operation with no useful navigation context, the design prefers that simpler value.

## Repository-Wide Applicability Matrix

The disposition terms are:

- **shipped cursor** — the public semantic checkpoint exists in all nine language ports;
- **specialized shipped cursor** — the public surface exists only on the named semantic axis and
  does not expose raw structure;
- **internal edit path** — useful as a private algorithmic path, not a public abstraction; and
- **not applicable** — no cursor should be added under the current public contract.

| Family | Disposition | Semantic focus or reason |
| --- | --- | --- |
| `UInt256`/`Int256`, 512/1024-bit siblings | not applicable | Fixed-size numeric values have bit/limb operations, not a persistent recursive navigation axis; a bit cursor would add state without structural sharing. |
| `SparseInteger` | not applicable | Its public identity is a number. Internal sparse storage is representation, not a stable child topology. |
| CHAMP persistent hash map and set | internal edit path | A search/edit breadcrumb stack can retain bitmap nodes and collision context, but hash-trie traversal order and node shape are not public semantics. |
| Persistent hash bag | internal edit path | Its private adapter uses the CHAMP path, but the public unit is an equality class plus multiplicity; expanded enumeration is not a navigable identity. |
| Persistent bimap | internal edit path | Paired private paths can publish both indexes, but neither hash enumeration order is semantic; use direct key/value lookup publicly. |
| Set-valued hash multimap and relation | internal edit path | Nested/paired private paths can publish atomic results, but both public axes are hashed and unordered and have no semantic neighbor. |
| Persistent map patch | internal edit path | A private CHAMP path can maintain one change, but patch enumeration is otherwise unspecified; lookup/composition remain the public operations. |
| Persistent directed graph | not applicable | Cycles and multiple parents prevent one unique reconstructing path. A future traversal state is a graph navigator with visited/frontier state, not this cursor contract. |
| Persistent indexed map | internal edit path | Paired private paths can publish primary and derived indexes, but there is no single canonical public neighbor axis. |
| Concurrent hash trie and immutable snapshot view | not applicable | The live structure is mutable/concurrent; its snapshot reduces to the unordered CHAMP decision. A cursor must never imply editable access to a captured generation. |
| Persistent integer Patricia maps and sets | shipped cursor | Ascending signed-key order is public; the shipped tier retains a snapshot plus rank while preserving compressed-tree semantics. |
| Merkle search tree | specialized shipped cursor | Ordered key navigation and persistent editing ship without exposing block topology; ordinary tree operations retain canonical codecs, digests, trust boundaries, and block-persistence rules. |
| General measured finger tree / measured sequence | shipped cursor | Gap plus ordered before/after measures; positional or monotone-measure seek; private digit/node contexts remain hidden. |
| Finger-tree deque | shipped cursor | Positional gap with neighbor movement and local insertion/deletion over a retained persistent snapshot. |
| Reversible deque | shipped cursor | Same positional gap in logical orientation; reversing maps `p` to `Count - p` and swaps directional operations. |
| RRB vector | shipped cursor | Positional gap and persistent indexed edits; relaxed-size tables and leaf paths remain private. |
| Range-update sequence | shipped cursor | Positional gap, ordered measures, and range/tag operations retain the owning collection's lazy-tag semantics. |
| Rope, measured rope, text rope | shipped cursor | Preserve existing positional, measured, text-unit, branching, snapshot, and port-specific complexity contracts. |
| Sorted bag, set, and map | shipped cursor | Comparator-order search location with lower/upper-bound seek, predecessor/successor navigation, and invariant-checked edits. |
| Canonical zip-zip sorted set | shipped cursor | Same logical ordered-set cursor; ordinary persistent edits maintain deterministic ranks and rotations. |
| Measured priority queue | internal edit path | Its stored sequence is not a portable public order and arbitrary element editing is not queue semantics. A measured-tree path may remain private; no public cursor ships without a separate occurrence-identity design. |
| Brodal–Okasaki heap | not applicable | The forest topology is private and unstable under meld/delete-min; minimum access is already the semantic focus. |
| Priority-search queue | shipped cursor | Keys define a stable sorted axis; edits may replace priority/value while ordinary operations rebuild winner caches. Priority order is a query, not a second cursor order. |
| Interval tree | shipped cursor | Nondecreasing low-endpoint order with duplicate-occurrence positions; ordinary edits preserve overlap summaries. |
| Persistent interval map | shipped cursor | Unique complete interval key in deterministic interval order; exact-key and augmented-search state publish together. |
| Persistent chunked bit set | shipped cursor | Population-rank gap whose next entry is an existing set bit; seek by bit index, rank, or select. Chunk boundaries stay private. |
| Persistent ordered set and map | shipped cursor | Insertion/explicit-position gap; edits update sequence and hash index atomically without exposing sparse labels. |
| Persistent ordered multimap | shipped cursor, reduced scope | Designed as nested outer key-group and inner value-order gaps. **All nine ports currently ship a flattened grouped-pair rank instead**; see the [ordered multimap](#persistent-ordered-multimap) deviation note. |
| Tungsten `PersistentList` and `PersistentAssociation` | not applicable | These application-leaf collections have no cursor requirement; their existing persistent operations remain the complete surface. |
| Builders, one-way edit sessions, block stores, proofs, packs, and DABA Lite | not applicable | These are mutable lifecycles, persistence support values, authenticated artifacts, or a mutable window—not persistent aggregate values needing cursors. |

## Implementation Status And Shipment Boundary

The semantic checkpoint tier ships in C#, C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and
Python: every applicable cursor can retain one immutable version, navigate its documented semantic
axis, perform the family-appropriate persistent edits, return a snapshot, and preserve the source and
every retained branch. That does not mean all ports use the same node topology or have the same
asymptotic constants.

Three surfaces specified below are **not yet shipped in any port**, so the tier is complete by family
but not by operation:

- `TrySeekValue` on the ordered set and `TrySeekKey` on the ordered map. Every port offers the
  equality search only as a collection factory returning a usable end cursor on a miss, which the
  [shared ordered context](#shared-ordered-context) permits; the instance form does not exist.
- `InsertRange` on the neutral ordered set and on the range-update sequence. In most ports the
  owning collection lacks the corresponding ordinary operation, so this needs a contract decision
  before implementation.
- The ordered multimap's nested group/value navigation. See the
  [ordered multimap](#persistent-ordered-multimap) deviation note.

The [2026-07-19 cross-language review](../reviews/persistent-cursor-cross-language-review-2026-07-19__3f7c1a9e4d02.md)
records the current per-port state, including the operations that are honestly slower than this
document's targets.

| Shipment group | Public cursor capability | Implementation boundary |
| --- | --- | --- |
| Patricia maps and sets | Start/end/rank and signed-key bounds, exact-key result, adjacent peeks and movement, strict insert or put/add, adjacent deletion, snapshot | Ascending signed-key order; 32- and 64-bit maps and sets; no exposure of compressed bit paths. |
| Measured sequence, deque, reversible deque, RRB vector, Range sequence | Validated positional or measured gaps, adjacent peeks/movement, family-local insert/replace/delete, measure/tag operations where owned, snapshot | Snapshot-plus-gap checkpoints over the existing substrate. Reversal, relaxed sizes, and lazy tags remain collection invariants rather than cursor state visible to callers. |
| Rope, measured rope, text rope | Positional and measure-aware edit cursors, text-facade preservation, branching snapshots | Existing mature cursor tier; C# retains its focused implementation and siblings retain their documented checkpoints. |
| Sorted bag/set/map, canonical set, priority-search queue, interval tree/map, chunked bit set | Comparator/order/rank factories, ordered neighbors, duplicate-aware or key-preserving edits, snapshot | The cursor delegates canonical ranks, winner caches, interval summaries, and sparse-word contraction to ordinary persistent operations. |
| Neutral Ordered set/map/multimap | Explicit-position gaps, atomic persistent edits, snapshot; the multimap uses a flattened grouped-pair rank rather than the designed group/value navigation | Sequence and CHAMP indexes publish as one facade; private sparse labels never enter the cursor contract. `TrySeekValue`, `TrySeekKey`, and `InsertRange` are specified below but not yet shipped in any port. |
| Merkle search tree | Rank, lower/upper/exact-key factories, ordered neighbors, strict insert/put/value update/delete, authenticated snapshot | Specialized snapshot-plus-rank cursor. Every edit uses the canonical tree operation, so `MST2` bytes, root digest, representative, callback, and failure rules remain unchanged. |

The language surfaces are idiomatic rather than mechanically identical:

| Language | Carrier and ownership model | Primary implementation areas |
| --- | --- | --- |
| C# | Immutable cursor classes/readonly values; family-named factories and result carriers | `Tools.DataStructures.Hamt`, `Tools.DataStructures.FingerTree`, and `Tools.DataStructures.Ordered` partial cursor sources. |
| C | Explicit owned structs/handles; `copy`/`destroy` or family-local `dispose`; producing calls support exact source/result aliasing and failure-atomic publication | HAMT public headers/sources, FingerTree cursor declarations/cores, and `Ordered/ordered_cursor`. |
| C++ | Immutable value cursors in public headers, with exceptions following owning operations | HAMT headers, FingerTree sequence/ordered-search cursor headers, and Ordered cursor header. |
| Haskell | Pure opaque cursor values and explicit search-result records; policy construction remains where already required | HAMT modules, FingerTree cursor modules/facades, and `Data.Structures.Ordered.Cursor`. |
| Kotlin | Immutable cursor classes/data carriers over retained snapshots | HAMT Merkle/Patricia sources, FingerTree sequence/ordered-search sources, and Ordered cursors. |
| OCaml | Abstract/module-qualified cursor values with `option`/`result` following the local API | `lib/hamt`, `lib/finger_tree`, and `lib/ordered` cursor modules or owning family modules. |
| Rust | Owned immutable cursor structs over shared persistent storage; moves are enforced by the type system | HAMT modules, FingerTree/Range cursor implementations, and the Ordered `cursors` module. |
| TypeScript | Immutable exported cursor classes with strict ESM typing | `src/hamt`, `src/finger-tree`, and `src/ordered`. |
| Python | Typed immutable cursor objects and search result dataclasses | `data_structures.hamt`, `data_structures.finger_tree`, and `data_structures.ordered`. |

The intentionally cursor-free boundary is equally normative. CHAMP and its bag/bimap/multimap/
relation/patch/indexed-map composites keep lookup paths private because hash enumeration has no
semantic neighbor. The concurrent trie, graph, measured priority queue, Brodal–Okasaki heap,
builders, sessions, stores, proofs, numerics, DABA Lite, and all Tungsten collections do not expose
a cursor. Adding one of those surfaces requires a new applicability decision rather than borrowing
the APIs shipped here.

## Shared Public Cursor Contract

### A Cursor Owns One Logical Version

An initialized cursor contains or retains one complete persistent collection version even when that
version is physically decomposed. It is never a detachable integer position that callers can apply
to another root.

- Navigation returns a new location over the same logical version.
- Editing returns a location over one new logical version.
- Every retained cursor remains valid and branchable.
- `Snapshot()` returns the collection represented by that cursor; it does not consume or invalidate
  the cursor.
- A cursor never observes later changes through a builder, transient, live Ctrie, or mutable payload.
  The repository's existing caveats for caller-mutated payload objects still apply.
- Automatic rebasing is absent. A later change-record API may rebase only when it explicitly proves
  source-version compatibility and defines insertion affinity.

This is the same *cursor-as-version* semantic choice already made by the rope cursor. It prevents the
most dangerous cursor error: applying a path captured from version A to version B, where balancing,
hash policy, ordering, counts, tags, or digests differ.

### Three Focus Models

#### Positional gap

For a logical sequence of length `n`, position `p` is always in `0 .. n`:

```text
[0, p) | [p, n)
         ^ gap
```

`TryPeekPrevious` addresses `p - 1`; `TryPeekNext` addresses `p`. Insertion at the gap returns the
gap after inserted values. Backspace deletes `p - 1` and returns `p - 1`. Forward deletion and
replacement address `p` and keep the gap fixed. Empty, start, and end gaps are ordinary valid
states.

This model applies to deques, measured sequences, RRB vectors, range-update sequences, ropes, and
ordered insertion-position collections.

#### Ordered search location

Sparse and key-ordered cursors always denote a gap:

```text
entries < boundary | entries >= boundary
                     ^ next entry is the candidate
```

`SeekLowerBound(key)` returns the gap immediately before the first entry not less than `key` and an
exact seek adds a separate hit discriminator. Start and end are gaps zero and `Count`, not different
focus variants. Replacement and deletion address the next entry after an exact hit. `SeekUpperBound`,
`MovePrevious`, and `MoveNext` operate in the collection's promised order. A miss is not an invalid
cursor. Insertion validates the new key against both neighbor bounds and the collection's duplicate
policy. Replacing a key or interval that changes ordering is modeled as atomic remove-plus-reinsert
and returns the cursor at the new ordered location.

This model applies to Patricia, sorted, canonical sorted, priority-search, interval, Merkle, and
sparse-bit-set families.

#### Internal subtree focus

A structural cursor is:

```text
Location<Node> = FocusedSubtree<Node> + Stack<Frame<Node>>
```

Each frame stores the parent constructor or node kind, the focused child slot, immutable siblings,
the parent payload if any, and only the metadata or policy needed to recompute the parent. Raw
frames are private. Public navigation must project them into a semantic position or order.

### Core Operations

Names vary idiomatically, but public cursor families should provide the applicable subset of this
conceptual surface:

```csharp
public readonly struct SequenceCursor<TCollection, T>
{
    public int Count { get; }
    public int Position { get; }
    public bool IsAtStart { get; }
    public bool IsAtEnd { get; }

    public bool TryPeekPrevious(out T value);
    public bool TryPeekNext(out T value);
    public SequenceCursor<TCollection, T> MovePrevious();
    public SequenceCursor<TCollection, T> MoveNext();
    public SequenceCursor<TCollection, T> Seek(int position);

    public SequenceCursor<TCollection, T> Insert(T value);
    public SequenceCursor<TCollection, T> InsertRange(IEnumerable<T> values);
    public SequenceCursor<TCollection, T> DeletePrevious();
    public SequenceCursor<TCollection, T> DeleteNext();
    public SequenceCursor<TCollection, T> ReplaceNext(T value);

    public TCollection Snapshot();
}

public readonly struct OrderedCursor<TCollection, TKey, TEntry>
{
    public OrderedCursorState State { get; }
    public bool TryGetCurrent(out TEntry entry);
    public OrderedCursor<TCollection, TKey, TEntry> MovePrevious();
    public OrderedCursor<TCollection, TKey, TEntry> MoveNext();
    public OrderedCursor<TCollection, TKey, TEntry> SeekKey(TKey key);
    public OrderedCursor<TCollection, TKey, TEntry> SeekLowerBound(TKey key);
    public OrderedCursor<TCollection, TKey, TEntry> SeekUpperBound(TKey key);
    public TCollection Snapshot();
}
```

These are protocol sketches, not proposed shared .NET interfaces. Static policy types, by-reference
returns, nullable payloads, C ownership, and language-specific result types make a universal binary
interface more restrictive than useful. Concrete cursors should use family names such as
`RrbVectorCursor<T>` or `PersistentIntMapCursor<TValue>`.

### Policies And Representatives

A cursor retains the exact policies of its source version: equality/hash policy, ordering comparer,
measure/tag algebra, rank policy, key/value codecs, allocator and ownership callbacks, and any
application-leaf policy. `Snapshot()` preserves them exactly under the language's identity model.

- Lookup-equivalent input never silently replaces a stored representative unless the collection's
  existing operation does so.
- A cursor operation delegates logical duplicate, no-op, and replacement semantics to the owning
  collection contract.
- Navigation invokes no equality, hash, comparison, measure, codec, selector, or user callback
  unless that callback is inherently required to locate or materialize the requested boundary.
- Context frames cache only values whose validity follows from immutable focused state. They do not
  assume user policies are pure beyond the collection's existing law requirements.
- Multi-index collections publish a cursor edit only after every index candidate and count update
  succeeds.

### Measures And Cached Metadata

For an ordered monoidal sequence cursor:

```text
Combine(MeasureBefore, MeasureAfter) == Snapshot().Measure
```

in that order. No inverse, commutativity, default-value identity, or element equality is assumed.
Cached count, height, balance, population, max-high, minimum-priority, winner, sparse-label,
subtree-count, lazy-tag, and digest metadata must be recomputed exactly as the owning collection
requires when closing a frame.

A frame may retain an already computed sibling measure. It must never combine measures in traversal
order different from logical source order. Failed callbacks publish no partially initialized cache
or cursor version.

### Identity And No-Op Rules

- `Seek(currentPosition)`, a zero-distance move, and empty range insertion preserve the logical
  version and context. Reference identity is promised only by a family whose chosen carrier supports
  it; value-type ports instead test shared version identity.
- Boundary `Try` operations return a presence discriminator and the unchanged cursor.
- Non-`Try` boundary movement or edit fails through the language-local established error channel.
- Replacement follows the owning collection. Sequence cursors that have no element-equality policy,
  including rope, replace unconditionally. Map cursors may preserve identity for a configured
  value-equivalent replacement if the map contract already does.
- Moving away and back does not promise the same cursor object, only the same logical version and
  position.
- A clean cursor created from a collection should return that exact collection root/instance from
  `Snapshot()` where the language exposes identity. A dirty cursor may memoize one canonical
  snapshot, but memoization is an optimization and must be stated per implementation.

### Failure Atomicity

Every operation validates its cheap structural arguments before callbacks in the same precedence as
the owning collection. It constructs all new focus, context, index, cache, tag, encoded block, and
count state off to the side. If validation, allocation, comparison, hashing, equality, measure,
selector, codec, cloning, ownership, checked arithmetic, or persistence fails:

- the receiver and all retained cursors/snapshots remain valid;
- no partial cursor version is returned or installed;
- no half-updated secondary index is observable;
- no snapshot memo records a failed candidate; and
- retry follows the language-local ordinary persistent-operation contract.

C functions additionally leave an existing output unchanged on failure unless a consuming operation
explicitly documents another rule. A newly initialized output must be either wholly valid or wholly
uninitialized and safe to dispose.

### Concurrency

Initialized immutable cursors are safe for concurrent read-only use to the same extent as their
source collections and reachable policy/callback objects. Editing returns new values and ordinarily
needs no cross-thread coordination, subject to the owning collection's lifetime rules. In
particular, related C HAMT/Patricia versions with non-atomic intrusive reference counts must be
derived, copied, and destroyed serially or under an external lock. The
only permitted internal mutation is publication of a semantically invisible cache such as a
canonical snapshot or prepared measure table; it must be thread-safe, winner-returning, and
failure-atomic.

A cursor over a concurrent structure is always bound to one immutable snapshot. It is never a live
cursor whose next move can observe a different generation.

### Default, Moved-From, And Disposed States

- C# struct cursors use an explicitly invalid default value unless a family can make the default a
  policy-correct initialized empty cursor. Every member of an invalid default throws the same
  documented exception.
- C zeroed, moved-from, or explicitly disposed handles are invalid but safely destructible according
  to local conventions. C++ default and moved-from behavior follows the concrete value type and
  must be documented rather than inferred from C's handle model.
- Rust ownership should make use-after-move unrepresentable; an empty cursor is still an initialized
  value.
- Haskell, Kotlin, OCaml, TypeScript, and Python constructors do not expose an uninitialized cursor.

## Representation Profiles

### Profile S: Focused Sequence Gap Cursor

The logical invariant is:

```text
sequence = left-tree · left-carry · active[0 .. gap) · active[gap ..] · right-carry · right-tree
position = size(left-tree) + size(left-carry) + gap
count    = size(left-tree) + size(left-carry) + size(active)
         + size(right-carry) + size(right-tree)
```

Not every substrate needs both carries or a bounded active window. The simplest correct checkpoint
is `(canonicalRoot, position)`. A true focused implementation may specialize the middle into a leaf,
digit, chunk, or small bounded buffer and retain left/right contexts. It must state its own packing,
underflow, overflow, and branch-amortization proof.

### Profile T: Focused Ordered Tree Search Cursor

For a binary search-like tree, each frame is conceptually:

```text
WentLeft  (parentEntry, rightSibling, cachedParentMetadata)
WentRight (leftSibling, parentEntry, cachedParentMetadata)
```

For a wide node it is:

```text
(nodeKind, focusedSlot, completeLeftRun, completeRightRun, parentMetadata)
```

The context must retain original immutable siblings, not flatten and rebuild them. Closing one frame
recomputes the parent from children and policy. Rotations, split/merge, path compression, canonical
ranks, and digest changes may rewrite more than the immediate frame; the specialized family section
defines those repairs.

`MoveNext` descends to the leftmost entry in a right subtree or climbs until it exits a left edge;
`MovePrevious` is symmetric. Either is O(height) worst case and O(1) amortized over a complete
in-order traversal. That amortization is per traversal lineage; branching immediately before a long
climb can repeat the climb independently.

### Profile C: Composite Semantic Cursor

A facade with multiple indexes exposes the semantic axis but keeps all index roots in one immutable
cursor version:

```text
CompositeVersion = (orderedCursor, membershipRoot, secondaryRoots..., policies, counts)
```

An edit first prepares the ordered successor and every lookup/index successor, validates their
correspondence, then returns one cursor version. `Snapshot()` can only publish the complete facade.
Raw child cursors are not independently publishable when doing so could break a cross-index
invariant.

### Profile R: Root-Plus-Position Semantic Checkpoint

Every public cursor may first ship as:

```text
(canonical persistent root, validated position/search state, exact policies)
```

Creation and navigation that only change an integer/key may be O(1); peeks, edits, measure reads, or
search may call existing O(log n) operations. This profile establishes semantics and model tests
without making a locality claim. A later focused implementation may replace it without changing the
public contract if identity, callback count, and complexity documentation are respected.

## Baseline Complexity Vocabulary

Let `n` be logical size, `h` tree height, `m` inserted range size, `k` traversed results, `w` bounded
CHAMP depth, and `c` an equal-hash collision scan.

| Operation | Semantic checkpoint | Focused target, only after proof/evidence |
| --- | --- | --- |
| Create at start/end | O(1) when the root exposes ends; otherwise family bound | O(1) or O(h), family-specific |
| Seek by index/key/measure | Existing collection search, normally O(log n), Patricia O(width) | Same asymptotic bound, producing path frames |
| Peek neighbor/current | Existing lookup, normally O(log n) | O(1) while focus/path already exposes it |
| Unit next/previous | May be O(log n) | O(1) amortized over one linear traversal, O(h) worst |
| Point edit | Existing persistent operation | Local bounded work plus ancestor repair; often O(h) worst |
| Insert range | Existing split/concat bound plus Omega(m) capture | O(m + h) when substrate supports packed splicing |
| Dirty snapshot | Already O(1) for root checkpoints | O(h) or bounded packing plus O(h); memoization may make repeats O(1) |
| Traverse k neighbors after seek | O(k log n) in the conservative checkpoint | O(h + k) on one lineage |

These are vocabulary and design targets, not blanket guarantees. Every "focused target", "may
target", "targets O(1) amortized", or similar phrase in the family sections below describes the
**right-hand column** — the unshipped focused representation — and no currently shipped port has
cleared the evidence gate that would turn it into a delivered bound. All shipped cursors are the
left-hand Profile R semantic checkpoints; read every amortized-locality target as aspirational until
a named port publishes the proof required by the
[future focused-representation promotion gates](#future-focused-representation-promotion-gates). A
family implementation must publish the effect of balancing, relabeling, lazy-tag pushes, collision
scans, codec bytes, allocation/copying, callbacks, and version-DAG fan-out. Work deferred in one
branch cannot be paid for by a sibling branch.

## Sequence-Family Designs

### General Measured Finger Tree

Applicable types are C# `FingerTree<TElement, TMeasure, TMeasureOps>` and the language-local raw or
facade equivalents identified in the [catalog](../reference/data-structure-catalog.md#finger-tree-core-and-deque).
The raw general tree does not necessarily cache an element count: a maximum, interval, or arbitrary
application monoid cannot be interpreted as an index. Its cursor therefore uses **measure and
neighbor semantics**, not a fabricated `Count` or integer `Position`.

Conceptual C# surface:

```csharp
public sealed partial class FingerTree<TElement, TMeasure, TMeasureOps>
{
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> GetCursorAtStart();
    public FingerTreeCursor<TElement, TMeasure, TMeasureOps> GetCursorAtEnd();
    public bool TryGetCursor<TPredicate>(
        TPredicate predicate,
        out FingerTreeCursor<TElement, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure>;
}

public readonly struct FingerTreeCursor<TElement, TMeasure, TMeasureOps>
{
    public bool IsAtStart { get; }
    public bool IsAtEnd { get; }
    public TMeasure MeasureBefore { get; }
    public TMeasure MeasureAfter { get; }
    public bool TryPeekPrevious(out TElement value);
    public bool TryPeekNext(out TElement value);
    public FingerTreeCursor<...> MovePrevious();
    public FingerTreeCursor<...> MoveNext();
    public FingerTreeCursor<...> SeekByMeasure<TPredicate>(TPredicate predicate)
        where TPredicate : struct, IMeasurePredicate<TMeasure>;
    public FingerTreeCursor<...> Insert(TElement value);
    public FingerTreeCursor<...> DeletePrevious();
    public FingerTreeCursor<...> DeleteNext();
    public FingerTreeCursor<...> ReplaceNext(TElement value);
    public FingerTree<TElement, TMeasure, TMeasureOps> Snapshot();
}
```

The constrained predicate overload preserves the existing zero-boxing value-predicate path; a
delegate convenience may be added separately where idiomatic. The predicate has the existing
monotone-prefix precondition. A successful seek returns the gap immediately before the first element
whose inclusive prefix satisfies it. A miss returns `false` with a usable end cursor; a predicate
true for the identity selects the start on a nonempty tree. Size-measured uses may add `Count`,
`Position`, and positional `Seek` through extension methods, a dedicated size-cursor wrapper, or
universal independently cached counts. They cannot conditionally add instance members to one closed
C# generic instantiation merely because its policy happens to be a size measure.

#### Structural representation

The private derivative follows the finger-tree constructors rather than flattening the sequence:

- `Single` has a focus and top context;
- `Deep(prefix, middle, suffix)` contributes a frame recording which digit contains the focus, the
  opposite digit, the complete portions before and after the focus, and the middle tree;
- descent through the recursively measured middle tree adds a `Node2` or `Node3` frame containing
  the focused child slot and its one or two siblings; and
- lazy middle suspensions remain shared. Navigation may force them using the same thread-safe
  memoization contract as the owning tree, but it never publishes a second logical result.

The logical context caches `MeasureBefore` and `MeasureAfter` in source order. Closing a frame calls
the ordinary smart constructors so digit bounds, 2/3-node arity, measures, and laziness remain
valid. The cursor must not expose whether one port uses a Hinze–Paterson finger tree, a measured AVL
checkpoint, or another representation.

An initial Profile R implementation can retain `(tree, boundary descriptor)` and use `Split`,
`TryViewLeft`, `TryViewRight`, and `Concat`. A later focused cursor must show that it improves a named
localized history without weakening the raw tree's current fully persistent amortization claims.
It must not infer the C# rope's bounded-focus proof: the raw tree has elements rather than rope
chunks and a different lazy-spine potential.

#### Measure and callback rules

- Combining before and after measures in that order equals the snapshot measure.
- Inserting or replacing measures the new element exactly as the ordinary operation does. A generic
  cursor has no element-equality shortcut.
- Moving across an already measured immutable node reuses its cached measure. A navigation-only
  operation does not remeasure elements.
- A callback failure leaves the cursor at its old location with its old materialized snapshot.
- A dirty snapshot closes frames bottom-up and may force or publish ordinary tree suspensions; no
  cursor-specific cache is visible to callers.

Creation at an endpoint is targeted at O(1). Monotone seek retains the tree's existing split bound.
One neighbor step may target O(1) amortized and O(log n) worst, and a complete post-seek traversal
O(n), only for a focused implementation whose actual lazy-spine or balanced-tree representation has
the corresponding proof and evidence. Root-plus-position checkpoints retain their ordinary local
bounds. Point editing may be focus-local but must be documented as O(log n) worst until its
digit/node repair proof is complete. Closing a dirty arbitrary-depth context is O(log n) worst in
the reference focused design; a memoized repeat may be O(1).

### Finger-Tree Deque

`FingerTreeDeque<T>` and each language-local persistent-deque sibling receive a positional
`FingerTreeDequeCursor<T>` using Profile S. The public surface mirrors `RopeCursor<T>` where the deque
already supports the operation:

```text
GetCursor(position = 0)
Count, Position, IsAtStart, IsAtEnd
TryPeekPrevious / TryPeekNext
MovePrevious / MoveNext / Seek
Insert / InsertRange
DeletePrevious / DeleteNext / ReplaceNext
Snapshot
```

The cursor is over the deque's ordinary element order. `InsertRange` captures its input once before
publishing a cursor version, checks `int`/native count growth before construction where the local API
can know it, and returns the unchanged cursor for an empty range. Replacement is unconditional
because the deque has no equality policy. `Snapshot()` produces an ordinary deque, not a cursor-only
root variant visible through `FingerTreeDeque<T>`.

The focused representation reuses the deque's leaf-count/signpost tree. A path frame records digit
or node siblings plus cached size, rightmost-leaf signpost, and `HasLast` data. Those search caches
remain private even though reconstruction must preserve them. Pulling across a focus boundary must
use ordinary smart constructors so the strict-language suspended middle spine retains its
concurrency and fully persistent memoization behavior.

The baseline Profile R checkpoint may implement an edit with `InsertAt`, `RemoveAt`, `SetItem`, and
`SplitAt`. A focused implementation may target O(1)-amortized unit traversal and local edit only on
the precisely proved representation/history class, with O(log n) worst forced repair and dirty
closure. Endpoint creation may be O(1), arbitrary seek retains the local collection's current bound,
and no C# lazy-finger-tree, rope focus, or flush claim is imported by balanced-tree checkpoints.

The deque's optional `SortedLowerBound` helpers do not turn this into a sorted cursor. Callers that
maintain a deque in sorted order can seek by the same comparer through an explicit convenience, but
every cursor edit remains positional and can invalidate that caller-maintained precondition. The
sorted collection facades below own invariant-preserving ordered cursors.

### Reversible Deque Adapter

`ReversibleDeque<T>` uses the same public positional contract, but its focused representation must
follow the owning port's orientation-aware core. The conceptual adapter stores logical orientation
plus a deque cursor. A port may realize that with per-node mirror bits, another orientation-aware
tree, or an explicitly chosen ordinary-deque adapter; this design does not overwrite the shipped
core topology. For the last representation, if logical position is `p` and count is `n`:

```text
forward orientation: underlying position = p
reversed orientation: underlying position = n - p
```

In reversed orientation:

- logical previous/next map to underlying next/previous;
- previous/next peeks swap;
- backspace/forward-delete swap;
- inserting one value uses the mapped physical gap;
- inserting logical range `[x0, ..., xm-1]` inserts the reversed range physically; and
- `Reverse()` creates a new logical version while sharing storage, maps the gap to `n - p`, and
  swaps the logical sides:

  ```text
  old:       left | right
  reversed:  Reverse(right) | Reverse(left)
  ```

  It receives independent dirty/snapshot cache state; the source's clean snapshot cannot be reused
  as the reversed logical value.

`Snapshot()` returns a `ReversibleDeque<T>` retaining the cursor's logical orientation. A separate
`SnapshotUnderlying()` is unnecessary and would couple consumers to representation. Cursor
navigation has the chosen local core's bounds; logical reversal should remain O(1) where the owning
deque already promises it. Tests must cover the involution at every gap, especially empty/start/end
states and non-palindromic range insertion.

### Relaxed Radix-Balanced Vector

`RrbVector<T>` and sibling RRB vectors ship `RrbVectorCursor<T>`, a positional gap cursor. RRB is
a particularly good focused-cursor target because one open radix path can serve several nearby indexed
reads or edits without repeating root descent.

The radix representation below applies only to ports that actually ship packed/relaxed RRB nodes.
OCaml's current `Rrb_vector` reuses a balanced persistent sequence and expressly makes no relaxed-
radix topology claim; it uses Profile R or a focused cursor over its own tree with that implementation's
bounds and invariants.

#### Representation

The focused state is:

```text
left context + active leaf[0 .. gap) | active leaf[gap ..] + right context
```

Each radix frame records:

- node height and focused child index;
- immutable children before and after that index;
- whether the source branch was packed or relaxed; and
- cumulative sizes, or enough child counts to reconstruct them without rescanning descendants.

The active leaf holds at most the substrate's branch factor (currently 32 in the C# reference). A
move within it changes only the local offset. Crossing its edge climbs to the nearest frame with a
sibling and descends the opposite boundary. `Seek` uses cumulative sizes for a relaxed node and
radix arithmetic for a packed node.

Closing a frame enforces equal child height, maximum arity, exact counts, and strictly increasing
cumulative sizes. It omits the size table when the reconstructed branch again satisfies the packed
radix rule. It preserves the repository's actual RRB invariant—boundary rebalancing without an
invented global minimum occupancy rule—and collapses a unary root exactly as the current vector
does.

#### Edits

- `ReplaceNext` applies the vector's existing element-equality no-op rule; otherwise it path-copies
  the active leaf and dirty ancestors and keeps the gap fixed.
- `Insert` and `InsertRange` split an overfull leaf, propagate at most one boundary run through the
  open spine, and use the existing concat/rebalance algorithm when a range is already an RRB vector.
- deletion removes from the active leaf, borrows or merges only according to current RRB seam rules,
  and removes/collapses empty branches;
- `DeletePrevious` moves the logical gap left; `DeleteNext` does not; and
- clean snapshot returns the exact source vector, while dirty snapshot rebuilds the open path and
  may memoize the winner.

The reference surface should also offer `InsertRange(RrbVector<T>)` so an existing vector can be
spliced with structural sharing, plus ordinary enumerable/span capture overloads where idiomatic.
The cursor is independent of `RrbVector.Builder`: the builder is append-oriented mutable staging,
whereas the cursor is branchable local editing over an adopted persistent version.

The radix-frame representation above is a **target, not a shipped one**: every current port,
including C#, ships the Profile R checkpoint of `(root, position)` and delegates edits to the
vector's ordinary indexed operations. Its bounds are the owning vector's, and a linear scan after
one seek costs one lookup per step rather than O(k + log32 n).

In a full RRB port with the frames described above, seek is O(log32 n). In-leaf movement and
replacement copy at most one bounded leaf plus changed cursor state; boundary crossing and
rebalancing are O(log32 n) worst. A linear scan after one seek targets O(k + log32 n). Insert-range
work is at least Omega(m) for uncaptured input and otherwise follows the vector's concat/rebalance
bound. Checkpoint ports retain their local balanced-sequence costs. The design does not claim a
dedicated tail, transient RRB nodes, or constant-amortized arbitrary version-DAG editing, and no
port may claim the frame bounds without clearing the evidence gate in
[future focused-representation promotion gates](#future-focused-representation-promotion-gates).

### Range-Update Sequence

`RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` and its sibling ports receive a positional and
measured `RangeUpdateSequenceCursor`. Ports whose collection is the path-copied implicit AVL can use
the focused design below; pending lazy tags make its context more than an ordinary binary-tree
derivative. A port with another representation uses Profile R or a topology-local cursor and keeps
its documented bounds. In particular, OCaml's immutable-array checkpoint claims neither the AVL
frames nor logarithmic edit/measure bounds described for the reference core.

#### Logical state and tag invariant

Each context frame records:

```text
direction
parent stored logical element (already including the parent's own pending tag)
complete sibling subtree
parent height/count/measure
optional parent pending tag
optional inherited tag from ancestors
```

The existing invariant remains authoritative: a node's stored logical element and cached measure
already include its own pending tag; its children do not. The value exposed at a focus is that stored
logical element after applying only the carried ancestor tag—never the node tag a second time. A
read-only cursor descent therefore **carries** correctly composed inherited tags without mutating or
path-copying nodes. Merely navigating away and back must keep the clean source snapshot
reference-identical.

The first edit through a tagged path prepares an immutable normalized edit spine:

1. compose inherited and node tags in the algebra's documented newer/older order;
2. push the effective action to the old logical children before structural rearrangement;
3. apply inherited tags to an old focused value before exposing it as an edit operand;
4. do **not** apply old tags to a newly inserted or replacement element; and
5. rebuild AVL height, count, logical measure, and optional pending-tag state after rotations.

The cursor never uses `default(TTag)` as an absence marker. It preserves the separate presence bit
and clears a composed tag only through `IsIdentity` under the existing law-gated contract.

#### Surface

In addition to the ordinary positional operations, expose:

```text
MeasureBefore, MeasureAfter
MeasurePrevious(count), MeasureNext(count)
ApplyPrevious(count, tag), ApplyNext(count, tag)
Seek(position)
Snapshot()
```

`ApplyPrevious(k, tag)` targets `[Position - k, Position)` and keeps the gap fixed;
`ApplyNext(k, tag)` targets `[Position, Position + k)` and keeps it fixed. Both validate the complete
range before `IsIdentity` or any tag/measure action, matching `ApplyRange`. Zero length returns the
same cursor without callbacks. `MeasurePrevious` and `MeasureNext` use the same subtraction-safe
range validation; zero length returns the monoid identity without element or tag callbacks, and a
nonempty result combines elements in snapshot order. Their bounds are the owning implementation's
`MeasureRange` bound plus any cursor-close/open work, never an assumed constant-time inverse.

A whole-version tag through a clean root-plus-gap checkpoint can retain the owning substrate's O(1)
root update when that substrate promises it. A dirty focused cursor cannot inherit that result
automatically: it must first close, retain a separately specified global overlay tag across both
sides, or use another proved representation. The implementation must document which path it takes
and its cache/failure behavior. Absolute range operations remain available on `Snapshot()` or
through cursor methods taking an absolute index if consumer evidence justifies them.

`MeasureBefore` and `MeasureAfter` must reflect all carried tags and combine in logical order to the
whole measure. They may use cached annotated sibling subtrees plus O(log n) composed-frame work; no
O(1) promise is made until a chosen representation stores failure-atomically prepared aggregates.

Seek, point edit, proper range update, and dirty close are O(log n) worst for the implicit-AVL
reference design. A whole-sequence nonidentity tag is O(1) only in the clean/overlay cases just
specified; closing a dirty path first can add O(log n). Unit navigation may be O(1) amortized over a
linear traversal but O(log n) worst at a spine crossing in a proved focused tree implementation.
Every port otherwise publishes its local array/tree bound. A focused implementation must count tag
composition/application and measure callbacks separately from node allocations; no rope callback
ceiling or finger-tree amortization applies.

### Rope, Measured Rope, And Text

No new rope API is proposed. The shipped cursor contract already embodies Profile S and is the
observable template for positional semantics:

- `RopeCursor` is a gap cursor with previous/next peeks, movement, seek, insertion, deletion,
  replacement, branching versions, and snapshot;
- measured cursors add ordered before/after measures and absolute monotone-prefix seek; and
- text cursors preserve each language's existing text-unit and line/column rules rather than
  pretending that UTF-16 code units, bytes, Haskell `Char`, Unicode scalars/code points, and grapheme
  clusters are interchangeable.

C# keeps its selected 16-element focus, 256-element carry flush, prepared-measure fragments, and
winner-returning snapshot memo. Sibling ports remain correct root-plus-gap checkpoints unless their
own design, proof, measurements, and documentation select a focused representation. This design
does not rename current methods, change default-state behavior, add bookmarks/rebase, or widen the
proven linear-lineage complexity scope.

### Sequence-Like Non-Candidates

#### Measured priority queue

The queue's underlying measured tree has an insertion/meld sequence, but its stable semantic focus
is the minimum-priority entry. An arbitrary occurrence has no key or handle by which a cursor could
survive domain operations, and exposing `ReplaceNext` would turn the queue into a list editor with a
priority cache. Keep a measured-tree structural cursor private for `TryDequeue` or traversal
experiments. A future read-only occurrence cursor requires a separate occurrence-identity design and
consumer evidence; it is not part of the public cursor contract.

#### Brodal–Okasaki heap

The shipped heap's bootstrapped skew-binomial structure, fused primitive child/embedded forest, and
skew-rank invariants are private and may change drastically after `Meld` or `DeleteMinimum`. There
is no comparer-ordered neighbor traversal, and an arbitrary focused node cannot be replaced without
restoring global heap invariants. `Minimum`, `Insert`, `Meld`, and `DeleteMinimum` already express
its semantic locations. Do not add a public cursor.

#### DABA Lite and builders

`DabaLite` is a mutable FIFO aggregate that overwrites/detaches raw-value storage and does not expose
persistent snapshots; native ports destroy detached values promptly, while tracing-GC ports leave
reclamation to their runtimes. RRB/rope/sorted builders are mutable staging lifecycles. A persistent
cursor would neither describe their ownership nor improve their intended operations. They remain
out of scope. Immutable snapshots produced by a builder may create an ordinary family cursor after
publication.

## Ordered And Search-Family Designs

### Common Ordered-Gap Protocol

Every countable ordered family uses a gap even when its factories are key based:

```text
entries < boundary | entries >= boundary
                     ^ Position; next entry is the search candidate
```

`Position` is the number of entries before the gap. An exact search is a lower-bound seek plus a
`Found` discriminator comparing the next entry with the query. Before-first is position zero;
after-last is position `Count`. This is equivalent to the ordered search-location states in the
shared contract while avoiding a separate invalid state for a miss. Sparse bit sets use a wide
population-rank position; a raw measured tree without a count retains its measure-only protocol.

Applicable ordered cursors recognize:

```text
AtRank / SeekRank
LowerBound / UpperBound
TrySeekExact -> (Found, usable cursor)
TryPeekPrevious / TryPeekNext
MovePrevious / MoveNext
Snapshot
```

The *next* entry is the focused entry for value update or forward delete. Predecessor and successor
navigation are in the collection's documented logical order. A factory preserves the exact comparer
or policy even on an empty result. Comparing a key may throw; the source cursor remains usable.

The portable checkpoint stores `(canonical root, rank or search key, policy)`. A focused ordered-tree
cursor uses Profile T. Finger-tree-backed ordered facades may use Profile S with order-statistic
measures. The public contract does not reveal which one a port selected.

### Sorted Bag

`SortedBag<T>` and its language-local multiset siblings ship `SortedBagCursor<T>`.

- `AtRank`, `LowerBound`, `UpperBound`, and exact-range factories preserve the runtime comparer.
- `Add(item)` always finds the **upper bound** and inserts after all existing comparer-equal
  occurrences, preserving the collection's stable equal-item insertion rule. The method returns the
  gap after the new occurrence.
- `DeleteNext` removes the exact stored occurrence at the gap. `DeletePrevious` removes the exact
  predecessor and moves the gap left.
- Do not expose unconstrained `InsertHere`: inserting inside an equal run would create behavior not
  obtainable from the ordinary bag API.
- Do not expose arbitrary replacement. Changing an occurrence can change its sort position; the
  unambiguous operation is delete followed by `Add`, which returns the new upper-bound location.

An element-per-leaf finger-tree port can use an ordered measured-gap cursor. A bucketed port, such
as one storing a distinct key with a persistent duplicate sequence, uses a nested `(key path,
occurrence offset)` focus or the root-plus-rank checkpoint. It must preserve the same observable
stable order without claiming the other representation.

Seek, add, and delete retain the local sorted collection's O(log n) target. Movement within an open
measured context targets O(1) amortized and O(log n) worst; a bucket boundary may add the bucket's
local persistent-sequence cost. A complete post-seek traversal targets O(n).

### Sorted Set

`SortedSet<T>` and sibling sorted sets ship `SortedSetCursor<T>`.

- Exact search is lower bound plus comparer equivalence.
- `Add(item)` at a miss inserts at that lower-bound gap. At a hit it is an identity-preserving no-op
  and retains the stored representative.
- `DeleteNext` is allowed only when a next entry exists and removes that exact comparer class.
- There is no `ReplaceNext`: representative replacement could collide with another class and would
  violate the ordinary set's representative policy.
- `Floor`, `Ceiling`, `Lower`, and `Higher` are projections of lower/upper-bound cursor factories;
  they do not need independent traversal machinery.

The comparer is the complete ordering/equivalence policy and is retained by every cursor and empty
snapshot. Cursor insertion never accepts an arbitrary position capable of breaking sorted order.

### Sorted Map Or Dictionary

`SortedDictionary<TKey, TValue>`, `SortedMap`, and language-local siblings receive a key-ordered
cursor.

- Exact/lower/upper/rank search returns a gap whose next entry is the candidate.
- strict `Insert(key, value)` succeeds only at a missing lower-bound location;
- `SetItem(key, value)` follows the owning port's stored-key and value-equivalence contract, either
  updating the next entry or inserting at the missing gap;
- `SetNextValue(value)` is the focus-local form: it preserves the stored key representative and
  position and keeps the gap fixed;
- `DeleteNext` removes the focused key; and
- no key-rename operation ships in v1. Rename is atomic delete-plus-insert with an explicitly
  returned new location if later consumer evidence requires it.

Closing a finger-tree implementation recomputes count and last-key order-statistic measures in
source order. An AVL/B-tree implementation instead repairs balance and key bounds. A configured
value-equivalent update preserves the current cursor version only where the ordinary map promises
that no-op; the cursor does not invent a cross-port equality policy.

### Canonical Zip-Zip Sorted Set

`CanonicalSortedSet<T>` uses the sorted-set public cursor, backed in full implementations by a
Cartesian-tree cursor. A frame is:

```text
WentLeft  (ancestorItem, ancestorRank, untouchedRight)
WentRight (untouchedLeft, ancestorItem, ancestorRank)
```

The cursor retains the exact `ZipTreeRankPolicy<T>` object. A missing-key insertion derives the new
rank once, then may climb through several frames until heap-priority order permits attachment.
Removal merges the focused node's left and right subtrees, then rebuilds the remaining path. Both
operations use the same tie-breaking and split/merge rules as ordinary add/remove.

Every dirty close preserves:

- strict comparator order and one representative per equivalence class;
- the policy-derived rank for every item and the Cartesian heap relation;
- cached count and height;
- reference sharing of untouched subtrees; and
- content-hash semantics: unchanged nodes retain valid lazy digests, while new path nodes begin with
  no stale cached digest.

The result topology must equal the topology produced by the canonical ordinary operation for the
same policy and contents. Costs are O(h), not unconditionally O(log n). Expected logarithmic height
depends on the documented coherent pseudorandom rank assumptions; a degenerate collision policy can
make `h = n`. A checkpoint port that represents only sorted contents exposes semantic cursor parity
without claiming canonical node topology or zip-tree bounds.

### Priority-Search Queue

`PrioritySearchQueue<TKey, TPriority, TValue>` ships a **key-order** cursor. Priority is cached
augmentation, not a second navigation order.

Factories include exact/lower/upper key, minimum key, end, and optionally `AtMinimumPriority()`. The
latter reads the root's cached winner and performs an ordinary key seek; it does not walk a
priority-ordered sequence that does not exist.

An AVL-backed frame stores direction, ancestor entry, untouched sibling, height, count, and enough
winner information to rebuild through the ordinary balancing constructors. Dirty closure recomputes
the winner under the exact priority-then-key tie rule at every changed ancestor.

- `SetNext(priority, value)` retains the stored key representative and applies the queue's existing
  priority/value no-op rule.
- `SetItem(key, priority, value)` inserts at a miss or updates the exact hit.
- strict insertion rejects an equivalent key.
- `DeleteNext` removes the focused key and balances the path.
- predecessor/successor movement follows key order.

Updating one priority can change every ancestor winner, so it remains O(h). AVL ports target O(log n)
worst for seek/edit/close. A complete key-order traversal after one seek is O(n) on a linear cursor
lineage, with O(h) worst for one step; retained branches can repeat climbs. A sorted-array or other
checkpoint port keeps its local costs. `EnumerateAtMost` remains a winner-pruned query iterator—a
naive cursor scan must not replace it or claim its output-sensitive pruning bound.

### Interval Tree

`IntervalTree<T>` ships a low-endpoint-ordered cursor. The C# reference promises nondecreasing
`Low`, not full lexicographic `(Low, High)` order. Equal-low occurrences keep the facade's defined
placement order; in the current C# implementation a newly inserted equal-low interval precedes the
older run. Other ports follow their documented local ordering while preserving their shared
interval-query semantics.

Factories:

- rank and lower-bound by low endpoint;
- exact stored interval under the local two-endpoint matching rule;
- first overlap with a closed query interval;
- first interval containing a point; and
- start/end.

`SeekNextOverlap(query)` searches strictly after the currently focused occurrence, then advances
through the suffix using cached `MaxHigh` and stops after low endpoints exceed `query.High`. This
exclusive continuation rule prevents a factory's gap-before-hit result from rediscovering the same
occurrence indefinitely. It preserves inclusive endpoints and returns a usable end cursor on a
miss. A focused implementation retains count, last-low, and max-high context summaries; a portable
one may delegate each continuation to the current augmented search.

Insertion uses the facade's defined low-bound placement, never an arbitrary gap. `DeleteNext`
removes the exact occurrence represented by the cursor, which avoids ambiguity among duplicate
intervals. Replacing endpoints is not a local edit because it can move the interval; express it as
remove-plus-insert and return the newly located cursor. `Coalesce` remains a collection-wide
operation producing a new cursor only after materializing its result.

Interval validity is delegated to the owning API. A cursor must not silently normalize or reject an
interval differently from the collection on which it is built. Seek and augmented queries retain
the local logarithmic/output-sensitive bounds; repeated overlap continuation and duplicate-low
scans must state any additional run cost honestly.

### Persistent Interval Map

`PersistentIntervalMap<TEndpoint, TValue>` ships a cursor ordered by the unique complete interval
key `(Low, High)` under the endpoint policy.

- exact/lower/upper/rank factories use lexicographic interval order;
- first-overlap/containing factories and strictly-after-current `SeekNextOverlap` reuse max-high
  augmentation;
- strict insert succeeds only at a missing complete key;
- `SetNextValue` retains the stored interval representative and applies the value-comparer no-op
  rule; and
- `DeleteNext` removes the focused complete key.

Context measures preserve count, rightmost complete interval key, and max-high. Whether a port uses
one augmented tree for exact and overlap queries or composes multiple physical indexes is private;
every cursor edit publishes the exact-key and augmented-search state together or publishes nothing.
No endpoint replacement or cursor-level `Coalesce` is proposed because both require application-
specific payload decisions.

### Persistent Chunked Bit Set

`PersistentChunkedBitSet` ships a set-bit cursor. It traverses present bit indexes, not a dense
Boolean sequence extending to `int.MaxValue`.

Conceptual state:

```text
words before focus
optional active word index + nonzero 64-bit word + bit offset
words after focus
population before active word
logical set-bit gap rank
```

The active word is absent for an empty set, the end gap, and a missing-word insertion gap until an
edit creates it. Factories are `AtOrAfter(bitIndex)`, exact search with `Found`,
`AtRank(populationGapRank)`, start, and end. Cursor rank accepts `0 .. Count`; `Count` returns the end
gap, unlike element `Select`, whose domain is `0 .. Count - 1`. `Position` uses the same wide count
type as population count. Within one word, next/previous use trailing/leading-set-bit operations;
crossing a word moves through the underlying ordered measured context. Rank is cached population
before the word plus the popcount below the active offset.

- `Add(bitIndex)` searches at-or-after. A present bit is an identity no-op; a missing bit updates or
  inserts its word and returns the gap after the new bit.
- `DeleteNext` and `DeletePrevious` clear the exact neighboring bit.
- clearing a word's last bit removes the word entry; a publishable cursor never stores a zero word;
- negative search may return start according to the nonthrowing lookup convention, while addition
  retains the collection's negative-index validation; and
- set algebra remains a sparse word-stream operation, not a cursor primitive.

Seek/edit is logarithmic in represented word count plus constant 64-bit work **in the measured-tree
ports**, where movement within a word is O(1) and a cross-word step has the underlying context's
boundary bound. A port that backs the set with a general integer set rather than a chunked word
stream does not deliver these bounds: OCaml's `Persistent_chunked_bit_set` is a `Set.Make(Int)` with
no cached population, so rank and select are O(n) and every cursor step is O(n). That port keeps the
public set-bit cursor semantics but makes no word-local complexity claim; its deviation is recorded
in [its api notes](../../src/OCaml/docs/api-notes.md). Enumeration, rank, select, count width, and
overflow remain language-local.

## Neutral Ordered Composite Designs

The independently owned Ordered family is a particularly strong public-cursor fit because insertion
and explicit-position order are semantic. It ships cursors over that
order while retaining the hashed membership/key index as an atomic auxiliary root. It never exposes
sparse stamps or depends on Tungsten.

### Shared Ordered Context

The conceptual state is:

```text
OrderedCursorVersion {
    leftOrderedSequence
    rightOrderedSequence
    completeMembershipOrKeyIndex
    exactPolicies
    logicalCount
    optionalCanonicalSnapshotMemo
}
```

Navigation transfers one retained ordered entry between left and right without hashing and without
changing the complete index. An edit prepares an ordered successor and index successor, validates
their correspondence, then publishes one cursor version. Snapshot joins the two sequences and wraps
the already complete index. Implementations may instead retain a canonical root plus gap; the
logical split does not mandate payload duplication, one stamp representation, or a finger tree in
every port.

Private labels coordinate ordered-sequence entries with membership/key-index entries and support
positional recovery; they do not order the CHAMP itself. They are not cursor positions, bookmarks,
serialized values, or rebase anchors. An ordinary insertion chooses a label between neighbors. Gap
exhaustion relabels and rebuilds one unpublished complete result; the returned cursor is
reconstructed at the operation's contractually resulting gap—after inserted values for insertion.
No relabel amortization crosses retained branches.

An equality-seek instance method on an insertion-ordered cursor returns `false` with the receiver
location unchanged when the class is absent; there is no key-sorted lower-bound insertion gap to
infer. A collection factory may instead return `false` with a documented usable end cursor. Set,
map, multimap-group, and Association APIs must choose these forms consistently.

### Persistent Ordered Set

`PersistentOrderedSet<T>` ships `PersistentOrderedSetCursor<T>`.

Surface additions to the positional protocol are `TrySeekValue(equalValue)`, which places the gap
before the stored representative, and optional `TryInsert` result forms.

- `Insert(item)` adds an absent comparer class at the gap and returns after it. An equivalent
  existing class is an exact cursor/version no-op; it neither moves nor replaces the stored
  representative.
- `InsertRange` captures once, normalizes under the receiver policy, keeps first incoming
  representatives, removes already-present and intra-range duplicates, and prepares the complete
  result before publication. If no class is inserted, it preserves the cursor state.
- `DeletePrevious`/`DeleteNext` remove the exact stored representative from order and membership
  index atomically.
- There is no `ReplaceNext`, because replacement conflicts with first-representative retention and
  may collide with another class.
- A future `MoveValueHere` must be named as movement, retain the stored representative, and define
  destination against a pre-removal gap. V1 omits it rather than overloading duplicate insertion.

Let `w <= 7` be CHAMP depth and `c` a collision scan in the reference design. Value seek costs
O(w + c + log n); ordinary insert/delete combines O(w + c) lookup/index work with the ordered
context's endpoint or path work. Relabeling costs O(n(w + c)) per produced version. A focused dirty
snapshot joins the sides in the sequence's logarithmic bound and adopts the complete index in O(1)
structural work; a root-plus-gap checkpoint retains ordinary operation costs.

### Persistent Ordered Map

`PersistentOrderedMap<TKey, TValue>` uses the same ordered gap over entries and a complete keyed
index.

- `TrySeekKey` places the gap before the stored entry and recovers the first stored key
  representative.
- strict `Insert`/`TryInsert` adds only a missing key at the gap. Duplicate insertion never moves the
  key.
- `SetNextValue` changes only the next entry's payload, preserving stored key, stamp, and position;
  it applies the exact existing value-policy no-op rule.
- `DeletePrevious`/`DeleteNext` removes the complete entry from both indexes.
- V1 has no key rename. A future explicit `MoveKeyHere` retains the stored key and uses a separately
  documented final-index rule.

Value replacement never needs relabeling. It creates the replacement entry object required by ports
whose two indexes share entry identity, then updates both sides atomically. Hash, equality, value-
equality, allocation, or sequence failure leaves the old cursor reusable.

### Persistent Ordered Multimap

`PersistentOrderedMultimap<TKey, TValue>` has two nested orders: key-group order, then distinct-value
order inside each nonempty group. Flattened enumeration is grouped; it is not one global pair-arrival
sequence. Do not invent `PairPosition` or logarithmic random pair rank when the outer structure does
not cache value-group prefix counts.

The cursor is the sum:

```text
Empty
| FocusedGroup {
      outerGroupsBefore,
      storedKeyRepresentative,
      innerValueGap,
      outerGroupsAfter,
      completeOuterKeyIndex,
      pairCount,
      keyPolicy,
      valuePolicy
  }
```

Navigation enters a group at its start/end, moves to previous/next group, seeks a group by index or
key, and moves within the focused value order. An optional `MoveNextPair` walks inside a group then
crosses to the next group; it is sequential convenience, not a random global-rank contract.

Edits:

- an empty cursor can create one singleton group;
- `InsertGroupBefore/After(key, firstValue)` is strict and atomically creates a nonempty group;
- `InsertValue(value)` adds an absent value class at the inner gap. A duplicate is an exact no-op;
- deleting a value updates the focused group while it remains nonempty;
- deleting its final value removes the whole group and reanchors to successor-at-start, otherwise
  predecessor-at-end, otherwise `Empty`; and
- deleting a group subtracts its complete value count and uses the same deterministic reanchor.

Shipment of group-before/after and arbitrary-position inner-value insertion is gated on first
adding and specifying equivalent ordinary persistent operations. If a port deliberately makes them
cursor-only instead, it needs an independent normative model and cannot claim parity with today's
append-style `Add` surface. The preferred v1 sequence is to ship the ordinary positional operations
first, then require cursor/ordinary-operation parity.

No publishable intermediate contains an empty group. Pair-count overflow is checked before
publication. A dirty snapshot closes the inner context, installs that complete group in the outer
context/index, then closes the outer context. Let `w_k`/`c_k` and `w_v`/`c_v` be the outer-key and
inner-value CHAMP depth/collision costs. Focused work includes those hash paths plus
O(log v + log k) sequence closure; inner relabeling costs O(v (w_v + c_v)) and outer relabeling
O(k (w_k + c_k)) per produced version. A flattened seek without an outer pair-count measure is
honestly O(k + log v).

#### Shipped scope deviation

The nested `Empty | FocusedGroup { … }` cursor above is the **design target**, not what ships today.
All nine ports currently expose a single flattened grouped-pair rank: the cursor is
`(collection, pairRank)` and every peek, group seek, and edit resolves the rank by walking the pair
flattening, so those operations are O(total pairs) and a linear walk is O(P²) rather than the
O(k + log v) targeted here. The sum-type state, per-group navigation, and group-index seek are not
implemented, and the empty-group reanchor rule holds only implicitly because the rank is the prefix
sum of group sizes.

This is a reduced-scope shipment, not a completed one. Promoting a port to the nested representation
first requires shipping the ordinary positional group operations (`InsertGroupBefore/After`,
arbitrary-position `InsertValue`) that the surface above is gated on, then re-establishing
cursor/ordinary-operation parity. Until then the flattened rank must not claim the nested design's
complexity, and the reanchor rule should be modeled and tested explicitly rather than left to the
encoding. The concrete per-port costs and the two OCaml/Haskell content-rescan defects that the flat
encoding induced are recorded in the
[2026-07-19 cross-language review](../reviews/persistent-cursor-cross-language-review-2026-07-19__3f7c1a9e4d02.md).

## Tungsten Exclusion

Neither Tungsten `PersistentList` nor `PersistentAssociation` receives a cursor. Their existing
application-leaf operations remain the complete surface. This exclusion does not constrain cursor
designs in repository-general HAMT, FingerTree, or Ordered substrates and does not change the
normative [application-leaf dependency boundary](../reference/tungsten-application-leaf-boundary.md).

## Numeric Exclusions

### Fixed-Width Integers

`UInt256`/`Int256`, 512-bit, and 1024-bit values are numeric scalars. Their fixed-width half/limb
representations are implementation details, not public recursive constructors. A limb cursor would
expose layout and endianness, add more state than copying a fixed 32/64/128-byte value, and provide
no asymptotic structural-sharing benefit. A bit-gap editor would actually be a shift/mask/bit-string
API; insertion and deletion do not naturally preserve fixed width. No cursor is designed.

### Sparse Integer

`SparseInteger` is also a scalar. C# uses recursive sparse-position storage internally, while
TypeScript, Python, and OCaml use their arbitrary-precision integer substrates. Publicly freezing a
tree path would make one representation a cross-language semantic authority. Moving a set-bit
exponent can also trigger numeric carries, ordering/uniqueness repair, and a canonical small/large
representation transition rather than one local subtree replacement.

Consumers needing navigable sparse set bits should use `PersistentChunkedBitSet`; wide-integer
consumers use existing arithmetic/bit operations, while sparse-integer consumers use its arithmetic
operations. `BitConverterEx`, codecs, policies, measures, predicates, result records, and split
carriers are stateless or auxiliary values rather than persistent aggregates and receive no cursor.

## HAMT And Hash-Composition Designs

### Why CHAMP Has No Public Cursor

CHAMP map/set nodes are recursive, so a private focused edit path is well defined. The public collection
does not have a meaningful focus axis:

- equality and the 32-bit hash determine every child choice; callers cannot semantically choose a
  parent, child, or sibling;
- enumeration is stable for one unchanged version but is neither sorted, insertion ordered, nor a
  wire contract;
- `Seek(key)` followed by replace/remove would be a keyed lens around operations the map already
  exposes, not a navigator;
- exposing next/previous would promote private bitmap and collision layout into API; and
- canonical deletion can promote a singleton child into its parent's inline-data run. Since inline
  payloads enumerate before child runs, that repair can move surviving entries relative to other
  survivors. “Continue after the removed entry” therefore has no stable cross-version meaning.

No `PersistentHashMapCursor`, `PersistentHashSetCursor`, or editable CHAMP snapshot cursor is
proposed. Existing enumerators remain the representation-order traversal surface. The reusable
private edit path below may improve implementation factoring without changing public semantics.

### Private CHAMP Edit Path

The private engine is shared by ordinary map/set point operations and the composite preparation
protocols below.

Focus is one of:

```text
PresentEntry(storedHash, storedKeyRepresentative, storedValue, origin)
PresentCollision(fullHash, immutableBucket, selectedIndex)
Missing(key, hash, terminalKind)
EmptyRoot
```

`origin` distinguishes an inline bitmap-node payload from a terminal leaf when the implementation
has both forms; a port may instead normalize both into this logical focus. `terminalKind`
distinguishes an empty logical slot, an unequal terminal payload, and an equal-full-hash collision
miss. A collision bucket remains shared until an actual edit requires one new bucket.

Each bitmap-node frame retains:

- original immutable parent node or an equivalent normalized view;
- current five-bit shift and selected logical bit;
- `DataMap` and `NodeMap`;
- compact payload or child index;
- route kind—inline data, child node, or absent slot;
- untouched compact-array runs or the original array plus slice indexes; and
- cached subtree count/metadata required by the local implementation.

Private navigation descends by the caller's already computed hash. An implementation may reseek a
second key by ascending to the common hash-prefix ancestor, but good hashes intentionally destroy
key locality, so no performance promise follows. Representation-order iteration may use a separate
frame mode for equality/diff algorithms; it never becomes a public position token.

Edits close through ordinary CHAMP smart constructors:

- map replacement retains the stored key representative and applies the local value no-op rule;
- insertion fills an inline slot, creates a child at the first differing route, or extends an
  equal-hash collision bucket;
- removal shrinks a collision bucket, clears the correct bitmap slot, contracts empty branches, and
  performs canonical singleton promotion;
- set uses the same engine with a unit payload and never replaces a stored representative; and
- clean closure returns the exact source root/instance under the language's identity model.

Every closed result preserves disjoint data/node bitmaps, compact array lengths equal to bitmap
popcounts, five-bit routing, collision buckets containing one full hash in their existing local
entry order except for the ordinary operation's specified insertion/removal, cached recursive
counts, first representatives, comparer/hash policy, and the local canonical empty/singleton rules.
An immutable path never mutates nodes sealed by C# transient publication or any storage reachable
from another version.

Factored one-descent map factories retain the complete ordinary contract:

- validate every required factory before hashing, compute the hash once, and descend once;
- call no factory on a hit and call exactly the selected factory exactly once on a miss/update;
- distinguish a present null-like value from absence and retain stored representatives on semantic
  no-ops;
- prepare the complete result before publication so factory, retain, allocation, or callback failure
  exposes no change; and
- never import Ctrie retry semantics into a persistent `GetOrAdd`/`AddOrUpdate` factory.

With depth `d <= 7` and collision-bucket length `c`, seek/edit/close are O(d + c), context is O(d),
and changed allocation is limited to the bucket and ancestor path plus local compact arrays. A C
operation-local path may borrow nodes while its source remains live; an owning path retains the root
or required frames. Newly rebuilt nodes retain payloads through the configured policy. A null result
from an allocating retain callback unwinds every completed retain and installs no output. Other
ports follow their clone/move/GC rules. Failure leaves the old path reusable.

### Persistent Hash Bag Adapter

The bag has no public occurrence cursor. Its expanded equal occurrences are multiplicity, not
separately stored positions. A private adapter focuses one distinct equality class:

```text
(CHAMP path, stored representative, multiplicity, language-local expanded total)
```

It changes multiplicity only within `1 .. 2^31 - 1`; a transition to zero removes the class. Every
edit updates the expanded total by the exact delta and publishes the new map and total together.
Arithmetic is checked where that total is bounded; TypeScript `bigint` and Python `int` remain
unbounded. First representative retention and receiver policy remain unchanged. Cost is one CHAMP
path plus O(1) arithmetic; overflow where applicable, hashing, equality, allocation, clone, or retain
failure publishes no bag.

### Persistent Bimap Adapter

A bimap focus would still be a key/value lens over two unordered indexes, so it remains private. Its
paired context contains:

- forward path for the active stored `(key, value)` pair or key miss;
- inverse path for the current stored value, acquired when an edit needs it; and
- a separate inverse path for a proposed new value class.

Prepare/commit order preserves current conflict precedence:

1. probe the key domain;
2. probe old and proposed value classes under the independent value policy;
3. recognize the configured-value-equivalent no-op without substituting the ordinary map's value
   equality policy;
4. prepare complete forward and inverse roots; and
5. construct the bimap only after both succeed.

Replacement removes/readds both directions and never displaces another key. Removal closes both
holes. Clean close returns the source. Changed C#, Kotlin, TypeScript, and Python facades preserve
their reciprocal cached-inverse contract. C, C++, Haskell, and Rust preserve their documented
two-root sharing analogue. OCaml currently exposes forward/inverse lookup and enumeration rather
than an inverse facade and receives no new identity promise.

### Hash Multimap Adapter

The private pair focus is nested:

```text
outer map path at stored key representative
inner set path at stored value representative
KeyCount, PairCount, exact independent policies
```

Insertion into a missing key creates a policy-compatible inner set. Duplicate pair insertion is a
root-preserving no-op. Removing the final value removes the outer group in the same successor; no
closed intermediate contains an empty group. Whole-key removal subtracts the checked group count.

Publication builds the complete inner set, installs or removes its outer entry, verifies count
deltas, then creates the facade. Pair seek/change costs one key path plus one value path. This nested
private shape must not be confused with the public ordered-multimap cursor, whose two orders are
semantic.

### Persistent Relation Adapter

The private relation context is a bidirectional nested transaction:

```text
forward: left outer path + right inner path
reverse: right outer path + left inner path
global stored left/right representatives
equal forward/reverse pair counts
```

The mirror paths may be acquired lazily so a read-only forward operation does not pay a reverse
seek. Before editing, recover the globally stored representatives, prepare both complete multimap
successors, contract empty groups in both directions, verify equal pair-count deltas, and publish one
relation. Duplicate insertion and removal miss return the exact source. Degree-wide removal remains
the existing degree-local collection operation rather than being misrepresented as one local focus
edit. `Inverse` stays the public O(1) dual view.

### Persistent Map Patch Adapter

Patch enumeration is unordered, so a public cursor would not improve the semantic workflows
`Between`, preflight `Apply`, `Invert`, and `Compose`. A private path focus stores one patch key and
its explicit present/absent before/after values.

Editing or inversion retains the stored key, uses the retained value policy, and removes the CHAMP
entry when before and after collapse to a semantic no-op. A missing presence discriminator is never
represented by a nullable sentinel. Cost is the ordinary CHAMP path bound per change.

### Persistent Indexed Map Adapter

The secondary index is derived state and must never expose an independently editable cursor. A
private row context contains:

- primary CHAMP path with stored primary key, value, and exact stored selected index key;
- lazily acquired old/new secondary-group paths;
- retained selector and independent primary/value/index policies.

For an update:

1. a configured-value-equivalent no-op returns without calling the selector;
2. a genuine change calls the selector exactly once;
3. an equivalent selected index class retains its stored index representative;
4. otherwise prepare removal from the old secondary group, contraction if empty, and insertion into
   the new group;
5. prepare the primary row; and
6. publish both indexes only after all work succeeds.

Removal uses the stored selected key and never invokes the selector. The cost is a bounded number of
CHAMP paths plus at most one selector call.

### Persistent Directed Graph: Traversal, Not A Cursor

A graph's cycles, self-loops, and multiple incoming paths prevent one unique reconstructing ancestor
context. Choosing a spanning-tree parent records traversal history, not collection structure. No
cursor is designed.

If current enumeration proves insufficient for a named consumer, add a separately named immutable
`GraphTraversal` bound to one graph snapshot, with:

```text
DFS or BFS mode
successor, predecessor, or reversed direction
current stored vertex representative
persistent frontier
persistent visited set
optional discovery parent/edge and depth
```

Advancing may return a new traversal value, but it never edits or reconstructs the graph. Graph
edits produce another graph; the traversal remains on the original snapshot and can only restart
explicitly. Cycles, isolated vertices, self-loops, and the cached reversed facade require model
tests. Expected traversal work is one visit per reached vertex/edge plus HAMT frontier/visited costs.

Neighbor discovery follows the source snapshot's stable-for-that-version but otherwise unspecified
HAMT adjacency order unless the traversal explicitly accepts a caller ordering policy; it introduces
no canonical graph order. Its stack/queue is traversal-owned or lives in an appropriate composition
package. Do not introduce a HAMT-to-FingerTree dependency solely to obtain a persistent frontier.

## Integer Patricia Cursor Design

`PersistentIntMap<TValue>`, `PersistentIntSet`, `PersistentLongMap<TValue>`, and
`PersistentLongSet`—plus their sibling names—ship true ordered gap cursors. Signed keys are
encoded with the existing sign-bit transform, so in-order trie traversal remains ascending signed
order across minimum, zero, and maximum boundaries.

Suggested C# names are `PersistentIntMapCursor<TValue>`, `PersistentIntSetCursor`,
`PersistentLongMapCursor<TValue>`, and `PersistentLongSetCursor`. Each family offers start/end,
rank, lower/upper-bound, and exact factories, plus ordered peeks/movement and `Snapshot()`.

### Context And Reconstruction

A Patricia frame records:

```text
branch prefix
branching mask (highest differing transformed bit)
hole direction
complete sibling subtree
cached subtree count
original node identity
```

The public focus is always an ordered gap; physically the next entry may be a retained leaf, or be
absent at end/empty. Search follows compressed prefixes and stops at the first mismatch or exact
leaf. `Position` is derived from cached counts of left siblings on the path.

- Map `SetNextValue` retains the integer key and follows the owning port's ordinary replacement
  rule. Ports with a value-equality policy preserve its configured no-op; Haskell deliberately has
  no `Eq` constraint on Patricia values and rebuilds a present-key replacement.
- strict insertion at a miss joins the new leaf with the encountered leaf/subtree at the highest
  differing transformed bit and may splice that branch above one or more frames.
- set duplicate insertion is an exact no-op.
- `DeleteNext` removes the focused leaf and collapses its unary parent to the sibling.
- `DeletePrevious` does the same and moves the gap left.
- closing recomputes counts only on changed ancestors and shares every untouched sibling.

Unlike CHAMP promotion, these repairs never change the ascending relative order of surviving keys;
gap continuity is semantic. Clean snapshot returns the exact source. A dirty cursor may memoize one
canonical reconstructed root per edit version.

With key width `W` equal to 32 or 64, seek, rank, edit, and first dirty snapshot are O(W) worst.
`Count` and `Position` are O(1) when the port caches subtree counts; every shipped port does, so the
rank members are honest. A port without that cache must either add it as an internal invariant or
omit/qualify rank members rather than scan silently.

All nine ports ship the snapshot-plus-rank checkpoint: the cursor is `(root, rank)` and retains no
frames. Moving the gap is O(1) because it only rewrites an integer, but **reading** the neighbour
after a move is an unconditional O(W) root descent, so a complete in-order traversal by
move-plus-peek is O(n · W) and context is O(1). The O(W)-worst/O(1)-amortized move and the O(W)
context described earlier in this section belong to the retained-frame representation, which no port
implements; promoting a port to it requires the evidence gate in
[future focused-representation promotion gates](#future-focused-representation-promotion-gates).

Port-specific ownership remains explicit: C retains/releases the path; C++ and Rust frames retain
shared nodes and introduce no payload-copy requirement beyond the owning Patricia implementation
(current C++ edits copy from `const T&`; Rust edit bounds retain their `Clone + PartialEq`
requirements); TypeScript uses its documented 64-bit key representation; Python validates fixed-
width key ranges; OCaml retains its module-local key facades. No cursor token is portable across
widths, policy instances, or languages.

## Merkle Search Tree Cursor Design

`MerkleSearchTree<TKey, TValue>` receives the specialized
`MerkleSearchTreeCursor<TKey, TValue>`. It is a comparer-ordered persistent gap cursor over an
already trusted in-memory tree, never a mutable editor for raw stored blocks.

### Focus And Wide-Block Context

A block with `e` separators is traversed in this exact order:

```text
child[0], entry[0], child[1], entry[1], ..., entry[e - 1], child[e]
```

A frame retains:

- original trusted node/block identity and hash-derived layer;
- selected child interval or separator position;
- entries and complete in-memory child subtrees on both sides, preferably as original arrays plus
  indexes; child digests are cached metadata and cannot replace a subtree needed for closure;
- lower and upper separator bounds for the selected interval;
- cached subtree count, height/block metadata, and original digest; and
- ancestor context.

Factories support start/end, rank, lower/upper bound, and exact key. Within-block next/previous is
constant work; crossing a child/block boundary climbs and descends the context. `Position` uses
validated cached subtree counts structurally committed by the source blocks; their authentication
still depends on a trusted root. Bounded range objects remain deferred.

### Canonical Editing And Closure

- `SetNextValue` after an exact hit retains the stored key representative.
- Exact canonical value bytes recognize the ordinary `SetItem` no-op.
- Insertion computes the policy-bound SHA-256 key layer once after required validation.
- `DeleteNext` after an exact hit removes the candidate key and keeps the public gap convention.
- Every changed block is canonically encoded and rehashed; subtree count, height, block count,
  `MST2` bytes, block digests, and root digest are recomputed.

Merkle closure cannot naively plug one child into its old parent. A higher-layer inserted key can
become an ancestor; an equal-layer key can join a wide block; removal can expose/promote child
separators. Dirty closure therefore invokes the existing canonical layer-partition/split/merge logic
on the minimal affected interval while reference-sharing unaffected original subtrees; equal
digests alone neither require nor authorize object reuse. The closed tree's topology, bytes, and root
hash must equal ordinary `SetItem`/`Remove` for the same policy and logical contents.

`Snapshot()` publishes only a complete canonical in-memory tree and retains the exact policy/domain
object. It does not write an `IMerkleBlockStore`. `Save`, export, proof creation, synchronization,
and merge operate on the closed snapshot.

### Trust Boundary

- Create a cursor only from an in-memory tree constructed normally or obtained through completely
  verified `Load`/`Import`.
- Cursor operations do not weaken codec canonical-round-trip checks or verification budgets.
- `MerkleProof` is authenticated partial evidence, not a cursor: opaque child digests cannot
  reconstruct omitted subtrees.
- `MerkleSyncPlan` is a transfer frontier, not a location.
- Neither `MerkleBlock` nor store content becomes editable through the cursor.
- Root trust, authentication, confidentiality, replay, and peer identity remain outside the cursor
  exactly as they are outside the tree.

Let `h` be block height, `e_i` the occupancy of visited block `i`, and `S` changed encoded bytes.

**Shipped snapshot-plus-rank checkpoint (all nine ports).** Every port stores `(tree, position)` and
retains no frames, so each peek re-descends from the root. Key seek costs O(sum log(e_i + 1))
comparisons; rank seek, initial `Position` accumulation, **and every peek** cost O(sum (e_i + 1))
because existing nodes cache each child's total count rather than cumulative child-prefix ranks.
Moving the gap is O(1) because it only rewrites an integer, but a complete traversal by
move-plus-peek is therefore O(n · sum (e_i + 1)), not O(n). `Count` and `Position` are O(1) reads.
Edit plus first dirty snapshot is expected O(16 log16 n + S) under uniform layers and O(n + S) worst
for a degenerate block, because the edit delegates to the ordinary canonical operation. Clean and
memoized repeated snapshots are O(1). Context space is O(1).

**Focused frame-based tier (specified above, implemented nowhere).** Only an implementation whose
frames actually retain the original nodes plus indexes described earlier may claim O(1) within-block
movement, O(h) boundary movement, an O(n) complete traversal, or O(h) context; copying left/right
entry or child runs instead costs O(sum e_i) space. A validated cumulative-rank table would
additionally be required before claiming better than O(sum (e_i + 1)) rank seek. Promoting a port to
this tier requires the evidence gate in
[future focused-representation promotion gates](#future-focused-representation-promotion-gates).

These are the existing tree assumptions, not new cryptographic or adversarial guarantees.

Cross-language golden tests must apply the same cursor edit histories and require byte-identical root
hashes and `MST2` block closures under the shared golden policy, comparer semantics, codecs, and
canonical inputs, including adversarial layer patterns, start/end/min/max gaps, present-null values,
retained branches, and codec failures.

## Concurrent Facades, Builders, And Transients

### Concurrent Hash Tries

Reject a live editable cursor for C#/Kotlin lock-free Ctries and the TypeScript, Python, and OCaml
snapshot facades. A focus cannot remain attached to one structural generation while concurrent
writes renew paths, and write-back would require a new compare/exchange, conflict, factory-retry,
and linearization contract that the ports do not share.

An optional read-only `SnapshotTraversal` may capture one immutable generation and traverse its
documented snapshot order. It is explicitly outside the reconstructing-cursor contract. Editing
follows:

1. capture a snapshot;
2. convert to a detached persistent CHAMP where needed;
3. use ordinary persistent operations/private edit paths; and
4. return the detached map without implicit write-back.

C#/Kotlin traversal follows their documented canonical CHAMP-conversion order. TypeScript, Python,
and OCaml follow their captured persistent-root iteration contracts. Root-backed facades retain
their local sharing/copy costs. There is no shared cross-port entry sequence beyond those local
documents, and no lock-free, cross-worker, write-back, or live-rebasing claim transfers between
them.

### Construction Builders

Builders contain unpublished mutable nodes or staging state and may freeze repeatedly into detached
snapshots. Retaining a branchable persistent breadcrumb path across later builder mutation would
either alias mutable storage or force eager detachment and defeat the builder. No builder cursor is
designed. A frozen persistent snapshot can create its ordinary family cursor.

### One-Way Editing Sessions

Transients are single-owner and publication consumes the logical session; persistent cursors branch
freely and materialization does not consume them. A “transient cursor” would conflict with owner-
token uniqueness, version invalidation, alias consumption, and language ownership rules. Do not add
one under this name.

The private CHAMP edit-path engine may be used during one transient operation, but the path cannot
outlive that operation or bypass its prepare/commit boundary, version increment, iterator
invalidation, owner token, terminal publication, or language-local publication/failure contract.
Factoring preserves C# owner-token O(1) adoption/publication and retryable preparation failure;
sibling path-copy semantics without a transient-performance claim; C alias-wide consumption; C++
throwing-policy-move terminal invalidation/no-retry behavior; Haskell masked commit; Kotlin/Python
version invalidation; and Rust consuming publication.

## Cross-Language API And Ownership Mapping

Semantic parity means the same focus, movement, edit, branching, policy, and failure results. It
does not require identical spelling, carrier representation, allocation profile, or borrowed-value
rules.

| Language | Shipped idiomatic shape | Ownership and result rules |
| --- | --- | --- |
| C# | Concrete `XCursor<...>` returned by `GetCursor`/search factories; readonly struct over immutable references when justified, otherwise sealed value-like class; `Snapshot()` | Default struct is explicitly invalid unless a policy-correct empty can be represented. Presence-safe `Try` methods support nullable payloads. Thread-safe memo cells may be used but are not globally required. |
| C | Opaque or type-erased `x_cursor` owned handle with factory/init, the family's established clone/destroy vocabulary, navigation/edit status functions, and snapshot output; consuming move is optional rather than universal | Callback contexts and policies outlive every related cursor. Exact source/result alias support follows the owning workspace. Output is installed only on success. Peeks may borrow from the owning cursor snapshot under the existing lookup lifetime rule; an owning-copy result is added only when the family needs one. |
| C++ | Immutable `x_cursor` value retaining shared nodes/context; free/member factories and `snapshot()` | Copy/move follows the collection's policy-object rules. Borrowed peek references are lvalue-only where a temporary cursor would dangle. Context frames add no payload copies beyond the owning API and do not imply move-only support for copy-requiring families. |
| Haskell | Opaque pure `XCursor` algebraic value with `cursorAt`, movement/edit functions, and `snapshot` | Outer `Maybe`/result distinguishes a missing neighbor from a stored `Nothing`. Pure exceptions or explicit results preserve every old value. Runtime/function policy caveats remain local. |
| Kotlin | Opaque immutable `XCursor` class/value with `cursorAt` and `snapshot` | Non-null presence wrappers distinguish stored null from boundary. Runtime policy objects are retained exactly. No C# struct, memo, or allocation claim is inferred. |
| Rust | Opaque owned `XCursor` with `cursor_at`, borrowing peeks, persistent edits, and `snapshot` | Prefer `Arc`-retained context. Navigation/search/snapshot add no `Clone` bound; only edits that must duplicate affected payload storage inherit the substrate's bound. Use-after-move is statically unavailable. |
| OCaml | Abstract `X_cursor.t` module with `cursor_at`, `option`/`result` movement and edits, and `snapshot` | Preserve local policies and checkpoint representations. Do not infer native tree topology or another port's asymptotics. |
| TypeScript | Immutable `XCursor<T>` class/value with camel-case factories and `snapshot()` | Entry-shaped results distinguish stored `undefined` from a miss. Runtime hash/measure/comparison policies and isolate-local constraints remain exact. |
| Python | Typed immutable-style `XCursor` with snake-case factories, presence result objects, and `snapshot()` | Stored `None` is distinct from boundary. Python object mutability caveats remain; cursor persistence protects structure, not caller-mutated payload state. |

Current source families and the nine-language availability map are indexed by the catalog's
[persistent cursor section](../reference/data-structure-catalog.md#persistent-cursor-availability)
and family-specific tables. Numerics remain excluded even in workspaces that implement them.

`Snapshot` is the conceptual verb because the shipped rope uses it. A language whose collection
already standardizes on `to_persistent`, `close`, or another unambiguous term may retain that term,
provided it is explicitly non-consuming. C output functions may consume a moved handle as an
optimization only when a non-consuming copy form also expresses the shared semantic contract.

### No Universal Runtime Cursor Interface

Do not introduce one inheritance/interface hierarchy spanning sequences, ordered sets, measured
trees, and composite collections. Their position widths, measure/tag policies, key/value borrowing,
static type-class constraints, and legal edits differ materially. Reuse occurs in private generic
kernels and shared test laws. Public concrete types remain discoverable and honest.

Recommended naming pattern:

| Family | C#-shaped name |
| --- | --- |
| measured tree | `FingerTreeCursor<TElement, TMeasure, TMeasureOps>` |
| deque / reversible deque | `FingerTreeDequeCursor<T>`, `ReversibleDequeCursor<T>` |
| RRB / Range | `RrbVectorCursor<T>`, `RangeUpdateSequenceCursor<...>` |
| sorted/canonical/priority-search/interval | owning type name plus `Cursor` |
| chunked bit set | `PersistentChunkedBitSetCursor` |
| neutral Ordered | `PersistentOrderedSetCursor<T>`, map and multimap counterparts |
| Patricia | `PersistentIntMapCursor<TValue>` and width/set counterparts |
| Merkle | `MerkleSearchTreeCursor<TKey, TValue>` |

The C, C++, Rust, OCaml, and scripting-language ports adapt casing and module conventions. Cursor
state is never serialized and is never portable between language ports or policy instances.

## Internal Architecture Guidance

Implementation reuse should follow semantic boundaries rather than forcing every family through one
node type.

1. **Gap protocol helpers** own boundary validation, movement results, edit anchoring, version
   identity, and command-model tests. They do not own tree representation.
2. **Measured sequence context** owns ordered before/after measures and element-remeasurement rules.
   Deque, raw measured tree, sorted measured facades, interval facades, and bit-set word traversal can
   adapt it where their actual substrate permits.
3. **Binary/wide ordered path kernel** owns predecessor/successor ascent/descents and clean/dirty
   path identity. Patricia, AVL priority-search, canonical zip-zip, and Merkle each provide distinct
   frame closure and rebalancing/canonicalization policies.
4. **Private CHAMP path kernel** remains in the HAMT package and provides preparation to hash-derived
   composites. It does not leak into public cursor contracts.
5. **Composite version coordinator** stages ordered and auxiliary-index results and publishes one
   facade. It is parameterized by collection-owned validation and commit callbacks.

Avoid sharing a `CursorVersionState` across unrelated collections merely because both can memoize a
snapshot. Version state is family-specific: a Merkle state owns policy domain and dirty encoded
regions; a Range state owns tags and measure algebra; an Ordered state owns multiple indexes.

## Validation Design

### Shared Command Model

Every public positional or ordered cursor gets a deterministic model suite that stores a plain
logical sequence/map plus a gap. Commands include:

```text
create at every legal gap
seek absolute / lower bound / upper bound / exact
peek previous / next
move previous / next
insert one / insert range where legal
delete previous / next
replace or value-update where legal
snapshot
retain named ancestor
branch from any retained cursor
resume and snapshot every retained branch
```

After every command, assert position/focus, count, ordered enumeration, peeks, policies, stored
representatives, and snapshot equivalence with the ordinary persistent operation. Generate null-like
payloads, duplicate/equivalent representatives, custom policies, empty/singleton collections, and
both endpoints.

Core laws:

1. `Snapshot(GetCursor(source, p))` is the source's exact logical value for every legal gap.
2. On an interior gap, next then previous and previous then next restore the same logical version and
   gap.
3. Navigation never changes the snapshot or policy identity.
4. Cursor edits equal the corresponding ordinary collection edits.
5. Retained ancestors and sibling branches never change.
6. Same-position seek, zero move, empty insertion, duplicate/no-op edits, and configured-equal value
   updates preserve identity exactly where promised.
7. A failed `Try` boundary operation preserves the cursor and distinguishes absence from a stored
   null-like value.
8. A dirty snapshot can be repeated; where memoization is promised, all callers receive the one
   winning canonical instance.
9. Default, disposed, moved-from, and invalid handles follow the language-local contract.
10. Cursor values over initialized snapshots are safe for concurrent reads under the collection's
    existing thread-safety boundary.

### Measure And Tag Laws

- Test a noncommutative monoid, not only count/sum, so before/after combination order is observable.
- Moving through retained measured nodes invokes no element-measure callback.
- Insert and unconditional replace invoke the exact local callback count; failure publishes no edit
  or prepared cache.
- Absolute measure seek covers predicate-true-at-identity, first/middle/last hit, miss, and empty.
- Range cursors generate noncommuting tags and verify `Compose(newer, older)` through nested,
  overlapping, whole-root, and zero-length histories.
- Range rotations and deletion/insertion through tagged paths preserve the rule that old tags never
  transform a newly supplied current value.

### Structure-Specific Invariant Gates

| Structure | Required invariant coverage |
| --- | --- |
| Finger tree/deque | Empty/single/deep constructors; digit and Node2/Node3 bounds where applicable; lazy middle sharing; cached measures/counts/rightmost signposts/`HasLast`; endpoint and forced-spine histories. |
| Reversible deque | Every orientation and gap; reversal involution; previous/next swap; non-palindromic range insertion; mixed-orientation snapshot. |
| RRB vector | Full RRB ports: packed and relaxed branches, cumulative sizes, height equality, full/partial leaves, split/concat seams, unary-root collapse, leaf-boundary fan-out. Checkpoints: representation-local sequence invariants. |
| Sorted families | Lower/upper bounds, stable equal-bag order, port-specific stored-key representative replacement/retention, strict duplicate behavior, comparer failures, neighbor parity. |
| Canonical set | Exact policy ranks, Cartesian heap order, canonical topology equal to ordinary edits, content hashes, degenerate-rank stack safety. |
| Priority-search queue | BST order, AVL balance, count/height, winner priority/key tie-break, priority update on every ancestor, pruned-query parity. |
| Interval structures | Inclusive endpoints, equal-low runs, complete-key lexicographic map order, min/max endpoints, max-high summaries, overlap continuation and validity rules. |
| Chunked bit set | Negative and `int.MaxValue` boundaries, 63/64 seams, set-bit ranks/select, zero-word contraction, wide count, retained algebra results. |
| Ordered set/map | Sequence/index count equality, exact entry identity where required, private stamps, duplicate no-op, relabel at the focus, first representatives. |
| Ordered multimap | Nested key/value order, no empty group, final-value reanchor, checked pair count, independent policies, inner/outer relabel. |
| CHAMP map/set private path | Bitmap/array popcounts, deepest routes, full-hash collisions and bucket order, singleton promotion, representative/no-op rules, one-hash/one-descent factories with exact callback cardinality, and unchanged algebra/diff work bounds. |
| Hash bag adapter | Positive per-class multiplicities; distinct and expanded totals; checked bounded totals versus unbounded language totals; first representatives and receiver policy. |
| Bimap adapter | Independent key/value policies, key-first conflict precedence, equal root counts, reciprocal mapping, and port-specific inverse-view identity. |
| Hash multimap adapter | No empty groups, exact key/pair counts, independent policies, duplicate no-op, and final-value contraction. |
| Relation adapter | Exact forward/reverse pair equivalence and counts, global stored representatives, inverse parity, and degree-wide removal. |
| Map patch adapter | Explicit presence rather than null sentinels, no stored semantic no-op, preflight conflict atomicity, inversion, and composition. |
| Indexed map adapter | Selector exactly once only on genuine change, stored selected key on removal, one secondary membership per primary row, and atomic roots/counts. |
| Graph traversal | Snapshot binding, cycles/self-loops/isolated vertices, local neighbor order, frontier/visited invariants, and no write-back or canonical-order claim. |
| Patricia | Signed min/-1/0/max, highest-differing-bit join, prefix mismatch, branch collapse, cached rank/count, 32/64-bit parity. |
| Merkle | Canonical layers/blocks/counts, exact `MST2` bytes and root hashes, codec exceptions, degenerate blocks, retained dirty branches, no block-store write before explicit save. |
| Ctrie snapshot traversal | Exactly one generation, local conversion/iteration sequence, representatives and policy retention, and no write-back. |
| Transient regression | Every local consumption, alias, iterator/version invalidation, failure-retry/terminal, publication, and performance-boundary contract survives private-path factoring. |

Every implementation invokes the collection's recursive validator, when one exists, after each
generated edit in a focused invariant lane. Model equality alone cannot detect a stale measure,
winner, digest, size table, auxiliary index, or owner-policy reference.

### Failure And Ownership Injection

Managed/native policy tests inject exceptions or failures at each hash, equality, comparison,
measure, tag, selector, codec, clone/retain, allocation, checked-count, and snapshot-close step that
is fallible under the owning API. Infallible-by-signature callbacks still receive cardinality and
ordering tests. For example, C HAMT hash/equality callbacks are infallible while retain callbacks and
factories may fail; C Merkle exposes a broader fallible callback boundary. For each available
failpoint verify:

- no edited cursor or half facade is observable;
- source, ancestors, and sibling branches remain reusable;
- no snapshot cache contains a failed candidate;
- no C allocation/retain leaks and every partially prepared value is destroyed exactly once;
- multi-index roots and counts remain mutually consistent; and
- retry produces the same value as a failure-free ordinary edit.

For C, run exhaustive allocator and fallible-callback failpoints through creation, movement that
allocates ownership context, editing, closing, copying, moving, exact source/result aliasing where
supported, and disposal. C++ covers throwing policy copy/move and move-only payloads only where the
owning shipped API supports them; copy-requiring FingerTree facades keep that constraint. Rust
compile tests ensure read-only cursor APIs do not add unnecessary `Clone`. Haskell evaluates enough
of each result to force policy failures in the claimed phase.

### Concurrency Tests

- Race read-only movement/peeks/snapshots on one initialized cursor.
- If snapshot memoization is promised, race first dirty snapshot; every successful caller returns
  the winner and a failed candidate installs nothing.
- Where the owning lifetime/policy contract permits it, race independent edits from a shared
  ancestor and verify isolated branches; otherwise derive fully independent roots or test the
  serialized lineage rule.
- Never present these as live-Ctrie write-back tests. A snapshot traversal observes exactly one
  generation.
- Require every comparer, measure/tag, hash/equality, allocator, codec, ownership, and other callback
  reachable in the raced operations to support concurrent calls; immutable structure does not make
  caller policy state thread-safe.
- C handle lineage construction/destruction follows the owning family's retention contract. Do not
  race independent edits/copies/destruction for C HAMT/Patricia lineages with non-atomic
  references; C FingerTree families with atomic immutable representation references may use their
  documented read/share boundary. Only already retained values enter a concurrent test.

### Complexity And Allocation Evidence

Semantic tests prove no asymptotic claim. A focused implementation that wants stronger bounds adds
untimed counters for node/frame visits, path allocations, rotations, suspension forcing, size-table
rebuilds, label relabeling, tag actions, element measurements, codec bytes, hash blocks, active-buffer
copies, and dirty-snapshot closures.

Required adversarial histories include:

- repeated movement across one boundary;
- edit oscillation on both sides of a focus;
- a linear local history and fan-out from the same highest-potential boundary;
- seeks alternating between far ends;
- duplicate/collision runs;
- Range operations through maximally tagged spines;
- RRB packed/relaxed seam cascades;
- canonical-rank and Merkle-layer degeneration; and
- Ordered relabel fan-out.

Counters establish work shape; isolated benchmarks decide constant-factor value. Neither can replace
an amortized proof. Do not run benchmarks as part of routine semantic shipment, and do not advertise
a focused tier unless it beats or otherwise justifies itself for a named consumer history.

## Cross-Language Parity, Shipment, And Evolution

### Shipment Units

The implementation was delivered one family at a time. Each public-cursor shipment unit contains:

1. locked shared focus/edit/result contract;
2. C# reference API/source/XML docs or a documented reason another existing family is authoritative;
3. deterministic model, invariant, failure, branching, and concurrency coverage;
4. honest complexity and allocation documentation for that implementation;
5. workspace usage/API/validation updates;
6. repository catalog, semantic-contract, frontier, navigation, and test-map updates; and
7. an explicit per-port decision: focused representation, semantic checkpoint, or deferred with
   reason.

The completed checkpoint tier does not inherit the C# rope representation, memo cell, callback
ceiling, allocation bound, amortization, benchmark result, or node topology. Language-local public
indexes may name the concrete cursors now; they must keep focused-performance statements scoped to
the implementations that actually prove them.

### Implementation Ledger

The branch history is deliberately granular. Every row is a nine-language semantic shipment, in
the same C#, TypeScript, Python, Rust, Kotlin, Haskell, OCaml, C++, C port order unless the family
needed additional C core commits.

| Phase | Commit span | Result |
| --- | --- | --- |
| Design and terminology | `0478c51` through `ee75704` | Exhaustive family audit, shared version/gap/search contract, public term `cursor`, navigation indexes, and explicit Tungsten exclusion. |
| Patricia | `484eeed` through `8d17039` | Signed 32/64-bit map and set cursors with bound/exact search, persistent edits, rank models, and native ownership rules. |
| Sequence families | `8f96b2c` through `7001618` | Measured sequence, deque, reversible deque, RRB vector, Range sequence, and retained rope/text cursor alignment. |
| Ordered-search families | `a057b3e` through `2fdcfbb` | Sorted bag/set/map, canonical set, priority-search queue, interval tree/map, and chunked-bit-set cursors; C used separate core commits for its erased families. |
| Neutral Ordered | `ae22290` through `e65580f` | Set/map positional cursors and nested multimap group/value cursors with atomic facade publication. |
| Authenticated Merkle | `222aa39` through `7a3924c` | Specialized ordered snapshot-plus-rank cursors preserving canonical tree edits, exact wire/digest behavior, and each port's failure/ownership channel. |

The private CHAMP/edit-path decision required no new public type: existing persistent point updates
already retain and rebuild their path internally. Graph and Ctrie traversal objects remain separate
future designs with different state and terminology; neither is an incomplete cursor shipment.

### Validation Evidence

Each phase added deterministic boundary and edit examples plus sorted-sequence or plain-model
comparisons in every port. Applicable suites cover every gap, lower/upper/exact misses and hits,
duplicate and wrong-gap edits, retained sources and branches, null-like payloads where supported,
policy/comparer retention, and recursive invariants. Native C additionally exercises explicit
ownership, exact source/result aliasing, failure-atomic outputs, strict-warning compilers, and
sanitizers. Merkle tests verify that insert-then-delete restores the source digest and run alongside
the existing `MST2`/`MSP2` wire, proof, persistence, merge, and failure suites.

The final Merkle gates passed as follows:

| Language | Evidence on this branch |
| --- | --- |
| C# | Eight selected Merkle core/cursor tests. |
| TypeScript | Strict type checking and ten Merkle tests. |
| Python | Ruff, strict Mypy, and fourteen Merkle tests. |
| Rust | Seventeen Merkle tests, rustfmt, and library Clippy; the unsuppressed all-target Clippy lane remains blocked by unrelated pre-existing warnings. |
| Kotlin | Complete HAMT executable. |
| Haskell | Complete clean Cabal HAMT suite; only the pre-existing no-op `Typeable` warning remains. |
| OCaml | Dune `@check`, `@fmt`, and all eighteen HAMT tests. |
| C++ | Strict Clang Merkle executable with twenty-two tests plus the public-header consumer; the documented libstdc++ 12 `stable_sort` deprecation warning is suppressed for this Clang 21 lane. |
| C | Strict GCC and Clang ASan/UBSan builds with all twenty-four Merkle core/wire/persistence/cursor tests. |

### Future Focused-Representation Promotion Gates

The shipped semantic checkpoints already satisfy the public behavior gate:

- the focus position survives empty, boundary, duplicate, removal, and branching histories without
  ambiguity;
- ordinary-operation model parity and recursive invariants both pass;
- policies, representatives, null/presence, error precedence, and failure atomicity are locked;
- public docs separate semantic checkpoint and focused implementation costs;
- C ownership and all language-local lifetime constraints are complete;
- cross-language golden artifacts pass where bytes/hashes are shared.

Replacing a checkpoint with an optimized focused representation additionally requires a
proof-scoped complexity statement,
operation counters, retained-memory analysis, and isolated benchmark evidence for a named history.
Failure to clear that gate leaves the correct root-plus-position cursor in place; it is not a reason
to weaken semantics.

## Locked Decisions And Deliberate Deferrals

| Topic | Decision |
| --- | --- |
| Public and design terminology | `Cursor`. |
| Version relationship | Cursor owns one immutable logical version; edits branch; snapshot is non-consuming. |
| Sequence focus | Gap, including empty/start/end. |
| Search-ordered focus | Key-/value-sorted gap whose next entry is the exact/lower-bound candidate. |
| Insertion-ordered focus | Positional gap in explicit collection order; equality-seek misses do not infer an insertion position. |
| Raw measured position | Measure/neighbor based; no fabricated count unless the measure/substrate provides one. |
| Snapshot memo | Required only where a family ships it; otherwise optional invisible optimization. |
| Public CHAMP cursor | Rejected; private edit path only. |
| Heap cursors | Rejected for measured priority queue and Brodal–Okasaki heap. |
| Graph | Separate snapshot traversal, not a cursor. |
| Live concurrent cursor | Rejected; snapshot-bound read traversal only. |
| Generic runtime interface | Rejected; concrete cursor types plus shared laws. |
| Bookmarks and rebase | Deferred; no implicit cross-version application. |
| Selection/range objects | Deferred; ranges are operations relative to one gap where specified. |
| Mutable/transient cursor | Outside persistent cursor scope and terminology. |
| Cross-language representation parity | Not required; observable semantics and honest local docs are required. |
| Tungsten collections | Excluded; no cursor is shipped or required. |

## Coverage Audit

This design and shipment audit covers every persistent family in the current
[data-structure catalog](../reference/data-structure-catalog.md):

- numerics are explicitly excluded as scalars;
- every CHAMP, derived hash-composition, Patricia, Merkle, and concurrent-snapshot family has a
  public/private/no-cursor decision;
- every FingerTree, sequence, sorted, priority, interval, RRB, Range, rope, bit-set, canonical,
  DABA, and builder surface has a design or exclusion;
- every neutral Ordered set/map/multimap has a shipped atomic semantic cursor; and
- Tungsten application collections are explicitly excluded; their repository-general substrates
  retain their own cursor decisions.

The audit treats result carriers, codecs, measures, policies, proofs, packs, stores, builders,
transients, and mutable DABA state as supporting mechanisms rather than silently counting them as
unreviewed persistent aggregates.

## Primary References

- Gérard Huet, [The Zipper](https://www.st.cs.uni-saarland.de/edu/seminare/2005/advanced-fp/docs/huet-zipper.pdf),
  *Journal of Functional Programming* 7(5), 1997
  ([DOI 10.1017/S0956796897002864](https://doi.org/10.1017/S0956796897002864)).
- [Historical concept overview](https://en.wikipedia.org/wiki/Zipper_(data_structure)), the
  user-provided orientation reference.
- [Data-structure catalog](../reference/data-structure-catalog.md) for current public families and
  language entry points.
- [Semantic contracts](../reference/semantic-contracts.md) for current persistence, policy,
  ordering, ownership, and cursor obligations.
- [Axis 2 lifecycle and sequence-cursor plan](axis2-lifecycle-and-sequence-cursors.md) and the
  [C# rope C0 decision](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md) for the shipped
  cursor-as-version precedent and its proof boundary.
- [Porting and semantic parity guide](../guides/porting-and-semantic-parity.md) for implementation and
  documentation workflow.
- [Tungsten application-leaf dependency boundary](../reference/tungsten-application-leaf-boundary.md)
  for the mandatory one-way dependency and authority rules.
