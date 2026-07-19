# TypeScript persistent ordered collections

- Created (UTC): 2026-07-15T02:15:19Z
- Repository HEAD: 6dbabd71db65ea2771a0b6581c119a367d96d106
- Scope: Ordered set, map, and grouped multimap APIs, contracts, runtime mapping, and validation

`PersistentOrderedSet<T>` is the strict TypeScript port of the independently owned C# insertion-
ordered set. It is exported from `@vladimir-reshetnikov/data-structures/ordered` and the package root.
“Ordered” means insertion and explicitly requested positional order, not comparison-sorted order.
`sort` is a stable one-shot reorder; subsequent additions append normally.

## Dependency boundary and representation

The implementation is a neutral general-purpose collection:

```text
ordered/PersistentOrderedSet
├── hamt/PersistentHashMap
└── finger-tree/FingerTree
```

It neither imports nor delegates to Tungsten. The CHAMP map associates each equality-policy class
with a private `bigint` order-maintenance stamp. A right-biased last-stamp measure over the persistent
FingerTree sequence supplies logarithmic stamp location and positional edits. Sparse labels use the
C# implementation's `2^20` initial spacing and signed-64-bit relabel boundary, but labels are private
and do not appear in the public API.

`PersistentOrderedMap<K, V>` owns key/value entries in its positional tree and stores only
key-to-stamp navigation in CHAMP. `PersistentOrderedMultimap<K, V>` composes that map with one
ordered value set per nonempty group; neither facade changes the dependency direction.

Every version maintains one ordered representative and one matching stamp-map entry per policy
class. Published values are immutable. Ordinary point operations path-copy affected HAMT/FingerTree
spines; relabel, reversal, changed sorting, and algebra rebuild unpublished candidates where their
contracts require it. Old versions and sibling branches remain usable. The TypeScript port makes no
C# deque-enumerator-state, allocation, or managed-thread performance claim.

## TypeScript API mapping

| Contract area | TypeScript members |
| --- | --- |
| Construction | `empty`, `create`, `from`, `createRange` |
| State | `size`, `count`, `isEmpty`, `policy`, `first`, `last` |
| Lookup | `contains`, `tryGetValue`, `getAt`, `indexOf` |
| Addition/movement | `add`, `addFirst`, `insert`, `moveToFirst`, `moveToLast`, `moveTo` |
| Removal | `remove`, `tryRemove`, `removeAt`, `removeFirst`, `removeLast`, `clear` |
| Range/order | `getRange`, `take`, `drop`, `reverse`, stable one-shot `sort` |
| Algebra | `union`, `intersect`, `except`, `symmetricExcept` |
| Relations | `isSubsetOf`, `isProperSubsetOf`, `isSupersetOf`, `isProperSupersetOf`, `overlaps`, `setEquals` |
| Enumeration | iteration, `toArray` |

`empty`/`from`, `size`, and `policy` are the package's idiomatic counterparts to C# `Create`/
`CreateRange`, `Count`, and `Comparer`; `create`, `createRange`, and `count` are also supplied as
discoverable parity spellings. `HashPolicy<T>` combines the hash and equivalence callbacks retained
as one exact runtime object.

`tryGetValue` returns `{ found, value }`, distinguishing a stored `undefined` representative from a
miss; a miss echoes the lookup value. `tryRemove` returns `{ removed, set }`, and a miss carries the
exact receiver. Invalid positions/counts throw `RangeError`, empty endpoint operations throw `Error`,
and absent explicit movement throws `OrderedSetMissingValueError`.

JavaScript iterators are ordinary immutable-snapshot iterators. Independently obtained iterators and
an iterator retained across successor construction remain independent; the C# value-enumerator copy
fail-fast rule has no JavaScript analogue.

`validateStructure()` is the TypeScript package's established public-diagnostics adaptation: it
recomputes the Ordered-owned dual-index invariants and returns the validated count. The analogous C#
hook is test-internal, where an `InternalsVisibleTo` seam is available.

The ordered map adds keyed value replacement without implicit movement. The ordered multimap adds
independent key/value policies, group and pair counts, grouped iteration, group lookup, pair/group
removal, and nested validation. Its order is first key-group insertion followed by first value
insertion within each group; it intentionally does not preserve one globally interleaved pair
arrival history. Removing a group's final pair contracts it, and re-adding that key appends a new
group.

## Equality, representatives, and order

The exact retained `HashPolicy<T>` defines class membership and argument normalization. The first
representative installed for a class remains until removal:

- construction keeps its first occurrence and position;
- `add`, `addFirst`, and `insert` are exact identity no-ops for an existing class and never move it;
- explicit movement uses the stored representative and interprets `moveTo`'s index in the final
  result;
- ranges, reversal, sorting, and algebra retain surviving receiver representatives; and
- argument-only algebra classes use the first representative observed during receiver-policy
  normalization.

Default policy semantics are JavaScript SameValueZero for primitives and identity for objects. This
includes ordinary stored `undefined`, `null`, and `NaN` classes; `-0` and `0` collapse while retaining
the first exact representative.

## Algebra, relations, identity, and failure

All algebra and all six relations eagerly consume and normalize the complete argument under the
receiver policy before applying an operation-specific shortcut. Same-type arguments with a different
policy are enumerated; their policy's membership callbacks are not delegated to. Duplicate argument
values count once, first argument representatives win collapsed argument classes, and a late iterator
or policy failure is therefore observable even when an earlier value appears decisive.

Result ordering is receiver order followed, where applicable, by first normalized argument-only
order. Receiver representatives win shared classes. Duplicate additions, current-position movement,
absent removal, empty clear, full range, trivial reverse/sort, unchanged stable sort, and logical
union/intersection/difference no-ops return the receiver exactly. Empty results retain the receiver
policy and use the shared empty singleton only for the shared default policy.

All callbacks run while building unpublished candidates. A hash, equivalence, ordering, or iterator
failure cannot mutate a source or partially publish a successor. Positional validation precedes hash
and equivalence callbacks for `insert`, `moveTo`, and range operations.

The TypeScript FingerTree facade is backed by the workspace's persistent measured AVL sequence.
Membership remains bounded-CHAMP-depth plus any equal-hash collision scan; positional lookup,
movement, and edits are logarithmic in the sequence size, and enumeration is linear. This port does
not inherit the tuned C# deque's endpoint amortization or struct-enumerator allocation claims.

## Ordered cursors

All three facades ship a public cursor: `PersistentOrderedSetCursor<T>`,
`PersistentOrderedMapCursor<K, V>`, and `PersistentOrderedMultimapCursor<K, V>`. Each is a
**Profile R root-plus-position semantic checkpoint** — an immutable exported class retaining one
exact collection version plus a validated gap, delegating every edit to the ordinary persistent
operation of that facade. None of them claims the C# rope tier's focused representation, snapshot
memo, callback ceiling, allocation bound, or amortized-locality result; the private sparse `bigint`
stamps never enter the cursor contract.

The semantic axis is the collection's own explicit order. For the set and map the gap is a boundary
in `0 .. size` over insertion/explicit-position order; for the multimap it is a boundary in
`0 .. pairCount` over the **flattened grouped** pair enumeration — key-group order, then value order
inside each group. The multimap cursor is therefore one pair-rank axis rather than the nested
outer-group plus inner-value pair of cursors described for the reference design.

| Contract area | Set | Map | Multimap |
| --- | --- | --- | --- |
| Factories | `getCursor`, `getCursorAtItem` | `getCursor`, `getCursorAtKey` | `getCursor`, `getCursorAtPair`, `getCursorAtGroup` |
| State | `snapshot`, `position`, `size`, `isAtStart`, `isAtEnd` | same, with `size` | `snapshot`, `position`, `pairCount`, `isAtStart`, `isAtEnd` |
| Navigation | `tryPeekPrevious`, `tryPeekNext`, `movePrevious`, `moveNext`, `seek` | same | same |
| Edits | `insert`, `tryInsert`, `deletePrevious`, `deleteNext` | `insert`, `tryInsert`, `setNextValue`, `deletePrevious`, `deleteNext` | `add`, `tryAdd`, `deletePrevious`, `deleteNext` |

`getCursor(position = 0)` builds a gap directly. The three equality-seek factories return
`{ found, cursor }`; on a miss they return `found: false` with a usable cursor at the append position
(`size`, or `pairCount` for the multimap) rather than an invalid state. `getCursorAtGroup` locates
the group's first pair. All three constructors are public and validate their position, so there is no
uninitialized cursor state.

`tryPeekPrevious`/`tryPeekNext` on the set return the presence-discriminated
`OrderedSetCursorPeek<T>` — `{ found: true, value }` or `{ found: false }` — so a stored `undefined`
representative is never confused with a boundary. The map and multimap return an
`OrderedMapEntry<K, V>` or `OrderedMultimapEntry<K, V>` object, or `undefined` at a boundary; the
entry object itself is always present, so no separate discriminator is needed there.

### Gap conventions and duplicate handling

Insertion returns the gap after the newly ordered element; `deletePrevious` removes the entry before
the gap and moves the gap left; `deleteNext` removes the entry after the gap and keeps the gap fixed.
Every deletion updates the ordered sequence and the CHAMP index atomically through the facade's own
`removeAt`/`remove`, so no publishable intermediate has a disagreeing pair of indexes and, for the
multimap, no publishable intermediate holds an empty group.

Duplicate handling deliberately follows each facade's ordinary contract, and the three differ:

- **Set** `insert(item)` is a silent exact no-op for an existing equivalence class. It returns the
  receiver cursor unchanged — the class is neither moved nor re-represented — and `tryInsert` reports
  that through `{ inserted: false, cursor }` with `cursor === receiver`.
- **Map** `insert(key, value)` is strict and throws `DuplicateKeyError` for an existing key.
  `tryInsert` is the nonthrowing form and reports `{ inserted: false, cursor }` with the cursor placed
  at the existing key's index.
- **Multimap** `add(key, value)` follows grouped `add` semantics: a duplicate `(key, value)` pair is
  an exact no-op returning the receiver cursor, while a new pair joins its existing group — so the
  resulting gap is inside that group, not at the end of the enumeration.

There is no `replaceNext` on the set, because replacement would conflict with first-representative
retention and could collide with another class. The map's focus-local update is `setNextValue`, which
routes through `set`: it preserves the stored key representative, the stamp, and the position, keeps
the gap fixed, and applies the map's configured `valueEquals` no-op rule, returning the receiver
cursor when the value is equivalent. No key rename, no positional movement, and no range insertion are
exposed on any of the three cursors; use the facades' `moveTo`/`moveToFirst`/`moveToLast` and
`insert`/`createRange` surfaces and take a fresh cursor.

Every cursor retains the exact `HashPolicy` objects, the map's `valueEquals` function object, and the
multimap's independent key and value policies. `snapshot` is a public readonly **property** holding
the retained collection — not a `snapshot()` method as in the finger-tree, Patricia, and Merkle
cursors — and reading it neither consumes nor invalidates the cursor. Every retained cursor and every
sibling branch stays valid.

### Errors and complexity

`RangeError` carries **both** the bad-argument and the boundary channel. An out-of-range constructor
or `seek` position raises it, and so do the boundary conditions "already at the start/end" and "has no
previous/next entry". Callers cannot tell the two apart by exception type; test `isAtStart`,
`isAtEnd`, and `size`/`pairCount` first, or use the `tryPeek*` members, which report a boundary as an
absent result instead of throwing. `DuplicateKeyError` from map `insert` and
`OrderedSetMissingValueError` from facade movement remain distinct types. A hash, equivalence, or
value-equality callback that throws leaves the receiver cursor and its snapshot untouched.

Let `n` be the collection size, `w` the bounded CHAMP depth, `c` an equal-hash collision scan, `k` the
multimap's key-group count, and `p` its pair count.

Set and map:

- Cursor creation, `size`, `position`, `isAtStart`, `isAtEnd`, `snapshot`: O(1).
- `getCursorAtItem`/`getCursorAtKey`: O(w + c) index lookup plus an O(log n) stamp locate.
- `tryPeekPrevious`/`tryPeekNext`: O(log n).
- `movePrevious`, `moveNext`, `seek`: O(1); the following peek pays a fresh O(log n) descent, so a
  full traversal by move-plus-peek is O(n log n).
- `insert`, `tryInsert`, `setNextValue`, `deletePrevious`, `deleteNext`: O(w + c) index work plus
  O(log n) sequence work, and O(n(w + c)) on a version whose insertion exhausts the sparse label gap
  and triggers a relabel.

Multimap — the flattened pair axis is **not** backed by a pair-prefix measure, and the port does not
pretend otherwise:

- Cursor creation, `pairCount`, `position`, `isAtStart`, `isAtEnd`, `snapshot`: O(1).
- `tryPeekPrevious`/`tryPeekNext`: **O(p)**. Pair-rank selection walks the grouped enumeration from
  the beginning rather than descending a cached prefix count.
- `getCursorAtPair` and `getCursorAtGroup`: **O(p)** for the same reason.
- `movePrevious`, `moveNext`, `seek`: O(1), with the same O(p) cost on the next peek.
- `add`/`tryAdd`: the grouped insertion itself is O(w_k + c_k) outer plus O(w_v + c_v) inner index
  work with O(log k + log v) sequence closure, but locating the resulting gap re-scans the published
  successor, making the whole call **O(p)**.
- `deletePrevious`/`deleteNext`: O(p) to identify the focused pair plus the grouped removal's index
  and sequence work.

## Validation

The Ordered suites port the C# examples, boundaries, representative rules, grouped-multimap order
and empty-group contraction, exhaustive small movement and relation tables, repeated relabel
histories, sibling branches, eager/failing normalization,
stable sorting, retained iterators/readers, public surface, package export, and dependency-boundary
checks. A fast-check branching command model compares every retained version against an independent
policy-aware ordered-list model. Run:

```powershell
cd src/TypeScript
npx vitest run test/ordered
npm run validate
npm pack --dry-run
```
