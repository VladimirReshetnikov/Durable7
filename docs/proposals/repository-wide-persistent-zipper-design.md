# Repository-Wide Persistent Zipper Design

- Created (UTC): 2026-07-18T00:59:25Z
- Repository HEAD: 5a51fc35ce9f5f7a4333d7fc29857af25fd188d3
- Status: Design proposal; only the rope cursor surfaces identified below are currently shipped
- Audience: Maintainers and port authors designing persistent navigation and localized editing
- Scope: Applicability, semantics, representations, APIs, complexity, validation, and porting of
  zippers across every repository-owned persistent data-structure family

## Decision

Adopt one repository-wide zipper vocabulary, but do **not** add one undifferentiated generic zipper
to every immutable type.

1. Public zippers are immutable, version-bound **cursors** over a stable semantic navigation axis.
   Positional collections use a gap in `0 .. Count`; ordered collections use an ordered search
   location with before-first and after-last sentinels; nested ordered collections expose both axes.
2. Recursive implementation trees may use private structural zippers whose breadcrumbs retain the
   information needed to rebuild valid ancestors. A private edit path is not automatically a new
   public collection feature.
3. A hash-trie or heap topology is not a public navigation order. CHAMP, dual-hash-index facades,
   concurrent tries, and meldable heaps therefore do not expose public zippers merely because their
   implementations are recursive.
4. Every cursor is itself a persistent working version. Navigation and edits return new cursor
   values, retained ancestors remain usable, and materializing the collection never consumes the
   cursor.
5. Public APIs use `Cursor`; documentation and implementation notes use *zipper* for the focused
   representation. This follows the terminology already selected for the shipped rope cursors.
6. The existing C# focused rope zipper and the eight sibling snapshot-plus-gap cursor checkpoints
   remain the normative shipped surfaces. This proposal generalizes their observable contract but
   does not retroactively claim their representations or performance for another family or port.
7. Tungsten cursors, if implemented, remain application-leaf APIs. General-purpose code must never
   depend on them or adopt their behavior as its semantic baseline.

This proposal is intentionally a design, not a shipment claim. A row marked **public cursor** below
means that the abstraction is applicable and specified here; it does not mean that the named type
already exists. Each implementation still requires its own API documentation, tests, measurements
where performance is claimed, and repository catalog update.

## Background And Repository Context

Huet's original zipper represents a focused subtree together with a reversed path containing the
siblings and constructor information needed to rebuild the root. Moving the focus changes that
decomposition; replacing the focus is local; closing the zipper reconstructs the complete tree.
The same idea gives a sequence location as `left context + focus or gap + right context`.

The user-provided [zipper overview](https://en.wikipedia.org/wiki/Zipper_(data_structure)) is a useful
orientation. The primary source is Gérard Huet's 1997 paper,
[The Zipper](https://www.st.cs.uni-saarland.de/edu/seminare/2005/advanced-fp/docs/huet-zipper.pdf)
([DOI](https://doi.org/10.1017/S0956796897002864)). Repository designs add constraints that the
minimal tree example does not have: cached monoidal measures, balancing, canonical ranks, lazy tags,
content digests, comparer and ownership policies, multiple indexes that must publish atomically,
and cross-language failure models.

The repository already ships a production instance of the idea:

- C# `RopeCursor<T>` and `MeasuredRopeCursor<T, TMeasure, TMeasureOps>` use a persistent
  zipper-as-version with a bounded 16-element focus, bounded carries, and a memoized canonical
  snapshot. Their exact representation, proof boundary, and performance evidence remain owned by
  the [positional decision](../../src/CSharp/docs/FingerTree/rope-cursor-c0-decision.md),
  [measured decision](../../src/CSharp/docs/FingerTree/measured-rope-cursor-c2-decision.md), and
  [FingerTree API specification](../../src/CSharp/docs/FingerTree/api-specification.md#positional-edit-cursor).
- C, C++, Haskell, Kotlin, OCaml, Rust, TypeScript, and Python expose equivalent version-bound rope
  cursor semantics through snapshot-plus-gap checkpoints. They deliberately make no C# focused-
  zipper, memoization, allocation, callback-count, or amortized-locality claim. The shared current
  contract is in [semantic contracts](../reference/semantic-contracts.md#ropes-and-text).

This design preserves that distinction. Observable parity does not imply representation or
complexity parity.

## Goals

- Decide, explicitly and exhaustively, which persistent families have a meaningful zipper.
- Give applicable families a coherent focus model, navigation vocabulary, editing rules,
  reconstruction invariant, and honest complexity target.
- Preserve every collection's equality, ordering, measure, representative, ownership, balancing,
  content-addressing, and multi-index publication contracts.
- Make branching histories, failure atomicity, concurrency, and materialization behavior explicit.
- Provide a C#-shaped reference API while allowing idiomatic C, C++, Haskell, Kotlin, OCaml, Rust,
  TypeScript, and Python spellings.
- Separate semantic baseline requirements from optional focused-representation optimizations.
- Reuse general sequence and ordered-cursor mechanisms from Tungsten without reversing the
  repository's one-way dependency boundary.

## Non-Goals

- No implementation is authorized merely by this document.
- No universal reflection- or continuation-based generic zipper is proposed.
- No raw private node, digit, sparse label, owner token, hash fragment, lazy suspension, or block
  layout becomes public API.
- No cursor silently rebases onto an unrelated collection version.
- No cursor is a mutable iterator, builder, CHAMP transient, transaction, bookmark, lens, or
  concurrent live view.
- No arbitrary element editing is added to a semantic heap merely because its private forest can be
  traversed.
- No worst-case or amortized bound is inherited from a different substrate or language port.
- No benchmark conclusion is asserted. Performance claims require family-local evidence after an
  implementation exists.

## Terminology

| Term | Meaning in this proposal |
| --- | --- |
| **focus** | The current element, subtree, search result, or gap from whose perspective the rest of the value is represented. |
| **gap** | A boundary `p` between `[0, p)` and `[p, n)`; valid even for empty, start, and end positions. |
| **context / breadcrumb** | Immutable data sufficient to reconstruct one parent around the focused child: constructor tag, direction or slot, siblings, parent payload, and required policy/cache data. |
| **location** | Focus plus its context path. A public location is called a cursor. |
| **close / materialize / snapshot** | Reconstruct or obtain the canonical persistent collection represented by the cursor. Public APIs use the family-local established term, normally `Snapshot()`. |
| **navigation version** | Several cursor positions over the same logical collection version. They may share one materialized-root memo. |
| **edit version** | A new logical collection version created by a cursor edit. It owns independent context and snapshot state. |
| **semantic cursor** | Public focus over a promised position, order, key, interval, bit rank, or measure axis. |
| **structural zipper** | Private focus over implementation nodes. Its shape may change without a public API change. |
| **snapshot-plus-position checkpoint** | A correct cursor retaining a canonical root plus position and implementing edits through ordinary persistent operations; it does not claim focused locality. |
| **focused zipper** | A representation retaining decomposed context so nearby navigation or editing can reuse the open path. |

## Applicability Test

A public zipper is applicable only when all of the following are true:

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

When only conditions 3–5 hold for private nodes, an internal structural zipper may be useful but no
public cursor is proposed. When a retained root plus a key or index already provides the complete
operation with no useful navigation context, the design prefers that simpler value.

## Repository-Wide Applicability Matrix

The disposition terms are:

- **shipped cursor** — public semantics already exist; retain the current contract;
- **public cursor** — applicable and recommended for a future public surface;
- **specialized cursor** — applicable only on the named semantic axis or behind an evidence/API
  gate; do not expose raw structure;
- **internal zipper** — useful as a private algorithmic path, not a public abstraction; and
- **not applicable** — no zipper should be added under the current public contract.

| Family | Disposition | Semantic focus or reason |
| --- | --- | --- |
| `UInt256`/`Int256`, 512/1024-bit siblings | not applicable | Fixed-size numeric values have bit/limb operations, not a persistent recursive navigation axis; a bit cursor would add state without structural sharing. |
| `SparseInteger` | not applicable | Its public identity is a number. Internal sparse storage is representation, not a stable child topology. |
| CHAMP persistent hash map and set | internal zipper | A search/edit breadcrumb stack can retain bitmap nodes and collision context, but hash-trie traversal order and node shape are not public semantics. |
| Persistent hash bag | internal zipper through its CHAMP substrate | The public unit is an equality class plus multiplicity; direct key lookup is the meaningful operation. Expanded enumeration is not a navigable identity. |
| Persistent bimap | not applicable publicly | A focus would have to keep forward and inverse indexes synchronized; neither hash enumeration order is semantic. Use direct key/value lookup. |
| Set-valued hash multimap and relation | not applicable publicly | Both axes are hashed and unordered. A pair focus would expose one private index order and complicate atomic dual-index publication without a semantic neighbor. |
| Persistent map patch | not applicable publicly | Patch enumeration is stable only for one version and otherwise unspecified; key lookup and patch composition are the semantic operations. |
| Persistent directed graph | not a classical zipper | Cycles and multiple parents prevent one unique reconstructing path. A future traversal state would be a graph navigator with visited/frontier state, not this zipper contract. |
| Persistent indexed map | not applicable publicly | Primary and secondary indexes are hashed and atomic; there is no single canonical neighbor axis. |
| Concurrent hash trie and immutable snapshot view | not applicable publicly | The live structure is mutable/concurrent; its snapshot reduces to the unordered CHAMP decision. A cursor must never imply editable access to a captured generation. |
| Persistent integer Patricia maps and sets | public cursor | Ascending signed-key order is public and the compressed binary path provides compact reconstructing breadcrumbs. |
| Merkle search tree | specialized cursor | Ordered key navigation and block-path editing are meaningful, but digest recomputation, canonical codecs, trust boundaries, and block persistence require a dedicated design. |
| General measured finger tree / measured sequence | public cursor | Gap plus ordered before/after measures; positional or monotone-measure seek; private digit/node contexts remain hidden. |
| Finger-tree deque | public cursor | Positional gap with neighbor movement and local insertion/deletion; a semantic checkpoint can precede a focused implementation. |
| Reversible deque | public cursor adapter | Same positional gap in logical orientation; reversing maps `p` to `Count - p` and swaps directional operations. |
| RRB vector | public cursor | Positional gap plus leaf/path breadcrumbs and relaxed-size tables; especially useful for clustered indexed edits. |
| Range-update sequence | public cursor | Positional gap, ordered measures, and range/tag operations; breadcrumbs must carry inherited lazy tags and rebuild normalized caches. |
| Rope, measured rope, text rope | shipped cursor | Preserve existing positional, measured, text-unit, branching, snapshot, and port-specific complexity contracts. |
| Sorted bag, set, and map | public cursor | Comparator-order search location with lower/upper-bound seek, predecessor/successor navigation, and invariant-checked edits. |
| Canonical zip-zip sorted set | public cursor | Same logical ordered-set cursor; private breadcrumbs additionally maintain deterministic ranks and rotations. |
| Measured priority queue | specialized/read-only cursor only | Its sequence order is observable but arbitrary element editing is not queue semantics. Reuse an internal measured-tree zipper; expose a public locator only if occurrence identity is first designed. |
| Brodal–Okasaki heap | not applicable publicly | The forest topology is private and unstable under meld/delete-min; minimum access is already the semantic focus. |
| Priority-search queue | public key-order cursor | Keys define a stable sorted axis; edits may replace priority/value while winner caches are rebuilt. Priority order is a query, not a second cursor order. |
| Interval tree | public cursor | Nondecreasing low-endpoint order with duplicate-occurrence positions; overlap summaries rebuild through context. |
| Persistent interval map | public cursor | Unique complete interval key in deterministic interval order; exact and augmented indexes publish together. |
| Persistent chunked bit set | public cursor | Focus an existing set bit with before-first/after-last sentinels; seek by bit index, rank, or select. Chunk boundaries stay private. |
| Persistent ordered set and map | public cursor | Insertion/explicit-position gap; edits update sequence and hash index atomically without exposing sparse labels. |
| Persistent ordered multimap | public nested cursor | Outer key-group focus plus inner value-order gap; flattened pair movement is derived, not the sole representation. |
| Tungsten `PersistentList` | public application-leaf cursor | Positional gap adapter over the leaf's list vocabulary; may consume a general sequence cursor. |
| Tungsten `PersistentAssociation` | public application-leaf cursor | Ordered rule gap plus keyed focus, preserving kernel-driven update/move rules inside Tungsten only. |
| Builders, one-way edit sessions, block stores, proofs, packs, and DABA Lite | not applicable | These are mutable lifecycles, persistence support values, authenticated artifacts, or a mutable window—not persistent aggregate values needing zippers. |

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

This is the same *zipper-as-version* semantic choice already made by the rope cursor. It prevents the
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

This model applies to deques, measured sequences, RRB vectors, range-update sequences, ropes,
ordered insertion-position collections, and Tungsten lists/associations.

#### Ordered search location

Sparse and key-ordered structures use one of:

```text
BeforeFirst | At(entry) | InsertionGap(lower, upper) | AfterLast
```

The public API may encode hit/miss separately while retaining one usable cursor in either case.
`SeekLowerBound`, `SeekUpperBound`, `SeekKey`, `MovePrevious`, and `MoveNext` operate in the
collection's promised order. A miss is not an invalid cursor. Insertion validates the new key
against both neighbor bounds and the collection's duplicate policy. Replacing a key or interval
that changes ordering is modeled as atomic remove-plus-reinsert and returns the cursor at the new
ordered location.

This model applies to Patricia, sorted, canonical sorted, priority-search, interval, Merkle, and
sparse-bit-set families.

#### Internal subtree focus

A structural zipper is:

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
source collections. Editing returns new values and needs no cross-thread coordination. The only
permitted internal mutation is publication of a semantically invisible cache such as a canonical
snapshot or prepared measure table; it must be thread-safe, winner-returning, and failure-atomic.

A cursor over a concurrent structure is always bound to one immutable snapshot. It is never a live
cursor whose next move can observe a different generation.

### Default, Moved-From, And Disposed States

- C# struct cursors use an explicitly invalid default value unless a family can make the default a
  policy-correct initialized empty cursor. Every member of an invalid default throws the same
  documented exception.
- C and C++ moved-from/zeroed handles, and explicitly disposed C handles, are invalid but safely
  destructible according to local conventions.
- Rust ownership should make use-after-move unrepresentable; an empty cursor is still an initialized
  value.
- Haskell, Kotlin, OCaml, TypeScript, and Python constructors do not expose an uninitialized cursor.

## Representation Profiles

### Profile S: Sequence Gap Zipper

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

### Profile T: Ordered Tree Search Zipper

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
CompositeVersion = (orderedZipper, membershipRoot, secondaryRoots..., policies, counts)
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

These are vocabulary and design targets, not blanket guarantees. A family implementation must
publish the effect of balancing, relabeling, lazy-tag pushes, collision scans, codec bytes,
allocation/copying, callbacks, and version-DAG fan-out. Work deferred in one branch cannot be paid
for by a sibling branch.

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
    public bool TryGetCursor(
        IMeasurePredicate<TMeasure> predicate,
        out FingerTreeCursor<TElement, TMeasure, TMeasureOps> cursor);
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
    public FingerTreeCursor<...> SeekByMeasure(IMeasurePredicate<TMeasure> predicate);
    public FingerTreeCursor<...> Insert(TElement value);
    public FingerTreeCursor<...> DeletePrevious();
    public FingerTreeCursor<...> DeleteNext();
    public FingerTreeCursor<...> ReplaceNext(TElement value);
    public FingerTree<TElement, TMeasure, TMeasureOps> Snapshot();
}
```

The predicate has the existing monotone-prefix precondition. A successful seek returns the gap
immediately before the first element whose inclusive prefix satisfies it. A miss returns `false`
with a usable end cursor; a predicate true for the identity selects the start on a nonempty tree.
Size-measured aliases may add `Count`, `Position`, and positional `Seek` without putting those members
on the arbitrary-monoid cursor.

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
`TryViewLeft`, `TryViewRight`, and `Concat`. A later focused zipper must show that it improves a named
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
  zipper-specific cache is visible to callers.

Creation at an endpoint is targeted at O(1). Monotone seek retains the tree's existing split bound.
One neighbor step is O(1) amortized and O(log n) worst under the family-local persistent finger-tree
analysis; a complete traversal after one seek is O(n). Point editing may be focus-local but must be
documented as O(log n) worst until its digit/node repair proof is complete. Closing a dirty arbitrary
depth context is O(log n) worst; a memoized repeat may be O(1).

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
or node siblings and cached size data; it does not expose sorted-search signposts. Pulling across a
focus boundary must use ordinary smart constructors so the strict-language suspended middle spine
retains its concurrency and fully persistent memoization behavior.

The baseline Profile R checkpoint may implement an edit with `InsertAt`, `RemoveAt`, `SetItem`, and
`SplitAt`. The focused target is O(1)-amortized unit traversal and local edit on the precisely proved
history class, O(log n) worst for a forced repair, and O(log n) dirty closure. Endpoint creation may
be O(1), arbitrary seek retains the current near-end logarithmic split bound, and no rope focus or
flush constant is imported.

The deque's optional `SortedLowerBound` helpers do not turn this into a sorted cursor. Callers that
maintain a deque in sorted order can seek by the same comparer through an explicit convenience, but
every cursor edit remains positional and can invalidate that caller-maintained precondition. The
sorted collection facades below own invariant-preserving ordered cursors.

### Reversible Deque Adapter

`ReversibleDeque<T>` does not need a second structural engine. Its cursor stores the logical
orientation plus a cursor over the underlying deque. If logical position is `p` and count is `n`:

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
- `Reverse()` keeps the same logical version and maps the gap to `n - p` while toggling orientation.

`Snapshot()` returns a `ReversibleDeque<T>` retaining the cursor's logical orientation. A separate
`SnapshotUnderlying()` is unnecessary and would couple consumers to representation. Cursor
navigation has the underlying cursor's bounds; toggling orientation is O(1). Tests must cover the
involution at every gap, especially empty/start/end states and non-palindromic range insertion.

### Relaxed Radix-Balanced Vector

`RrbVector<T>` and sibling RRB vectors receive `RrbVectorCursor<T>`, a positional gap cursor. RRB is
a particularly good zipper target because one open radix path can serve several nearby indexed
reads or edits without repeating root descent.

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

Seek is O(log32 n). In-leaf movement and replacement copy at most one bounded leaf plus changed
cursor state; boundary crossing and rebalancing are O(log32 n) worst. A linear scan after one seek
targets O(k + log32 n). Insert-range work is at least Omega(m) for uncaptured input and otherwise
follows the vector's concat/rebalance bound. The design does not claim a dedicated tail, transient
RRB nodes, or constant-amortized arbitrary version-DAG editing.

### Range-Update Sequence

`RangeUpdateSequence<TElement, TMeasure, TTag, TOps>` and its sibling ports receive a positional and
measured `RangeUpdateSequenceCursor`. It uses an implicit-AVL path zipper, but pending lazy tags make
the context more than an ordinary binary-tree derivative.

#### Logical state and tag invariant

Each context frame records:

```text
direction
parent logical element
complete sibling subtree
parent height/count/measure
optional parent pending tag
optional inherited tag from ancestors
```

The existing invariant remains authoritative: a node's own logical element and cached measure
already include its pending tag; its children do not. A read-only cursor descent therefore **carries**
the correctly composed inherited tag without mutating or path-copying nodes. It returns a logical
peek by applying that carried action. Merely navigating away and back must keep the clean source
snapshot reference-identical.

The first edit through a tagged path prepares an immutable normalized edit spine:

1. compose inherited and node tags in the algebra's documented newer/older order;
2. push the effective action to the old logical children before structural rearrangement;
3. apply inherited tags to an old focused value before exposing it as an edit operand;
4. do **not** apply old tags to a newly inserted or replacement element; and
5. rebuild AVL height, count, logical measure, and optional pending-tag state after rotations.

The zipper never uses `default(TTag)` as an absence marker. It preserves the separate presence bit
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
same cursor without callbacks. A whole-version tag may remain an O(1) tagged root even when invoked
through a cursor; the implementation must not open every path simply to preserve focus. Absolute
range operations remain available on `Snapshot()` or through cursor methods taking an absolute
index if consumer evidence justifies them.

`MeasureBefore` and `MeasureAfter` must reflect all carried tags and combine in logical order to the
whole measure. They may use cached annotated sibling subtrees plus O(log n) composed-frame work; no
O(1) promise is made until a chosen representation stores failure-atomically prepared aggregates.

Seek, point edit, proper range update, and dirty close are O(log n) worst under the existing implicit-
AVL contract. Whole-sequence nonidentity tagging remains O(1). Unit navigation may be O(1)
amortized over a linear traversal but O(log n) worst at a spine crossing. A focused implementation
must count tag composition/application and measure callbacks separately from node allocations; no
rope callback ceiling or finger-tree amortization applies.

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
own design, proof, measurements, and documentation select a focused representation. This proposal
does not rename current methods, change default-state behavior, add bookmarks/rebase, or widen the
proven linear-lineage complexity scope.

### Sequence-Like Non-Candidates

#### Measured priority queue

The queue's underlying measured tree has an insertion/meld sequence, but its stable semantic focus
is the minimum-priority entry. An arbitrary occurrence has no key or handle by which a cursor could
survive domain operations, and exposing `ReplaceNext` would turn the queue into a list editor with a
priority cache. Keep a measured-tree structural zipper private for `TryDequeue` or traversal
experiments. A future read-only occurrence cursor requires a separate occurrence-identity design and
consumer evidence; it is not part of the first public tranche.

#### Brodal–Okasaki heap

The heap's bootstrapped forest, violations, and scheduling structure are private and may change
drastically after `Meld` or `DeleteMinimum`. There is no comparer-ordered neighbor traversal, and an
arbitrary focused node cannot be replaced without restoring global heap invariants. `Minimum`,
`Insert`, `Meld`, and `DeleteMinimum` already express its semantic locations. Do not add a public
zipper.

#### DABA Lite and builders

`DabaLite` is a mutable FIFO aggregate with deterministic reclamation; RRB/rope/sorted builders are
mutable staging lifecycles. A persistent zipper would neither describe their ownership nor improve
their intended operations. They remain out of scope. Immutable snapshots produced by a builder may
create an ordinary family cursor after publication.
