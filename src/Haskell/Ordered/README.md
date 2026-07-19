# Haskell Persistent Ordered Collections

- Created (UTC): 2026-07-15T09:12:49Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: users and maintainers of the neutral Haskell ordered-set port
- Scope: ownership, representation, public behavior, and validation of `tools-data-structures-ordered`

This package is the independently owned Haskell port of the repository's neutral insertion-ordered
set and map. It composes the public CHAMP map from `tools-data-structures-hamt` with the public persistent
deque from `tools-data-structures-fingertree`; it has no dependency on the application-specific
Tungsten package and does not use Tungsten behavior as an oracle.

`Data.Structures.Ordered.PersistentOrderedSet` stores one ordered `Entry` per comparer equivalence
class and one CHAMP entry from that representative to its private signed 64-bit order label. Labels
are sparsely allocated and deterministically rebuilt when a local gap is exhausted. The spacing and
relabel cadence are private implementation details.

The observable contract matches the C# semantic baseline where Haskell has corresponding language
shapes:

- construction keeps the first representative and its first-occurrence position;
- `add`, `addFirst`, and `insertAt` never replace or implicitly move an existing class;
- `moveToFirst`, `moveToLast`, and `moveTo` retain the stored representative;
- invalid positions, absent movement, and empty endpoint removal use `Maybe` instead of exceptions;
- range extraction, reversal, and stable one-shot sorting retain the membership policy;
- algebra and relations eagerly normalize their list argument under the receiver's policy, retaining
  the first argument representative of each collapsed class; and
- logical no-ops reuse the receiver's CHAMP root, observable through `sharesIndexWith`.

The dual-index invariant requires equal cardinalities, strictly ascending labels, one order entry per
policy equivalence class, matching labels on both sides, and agreement on the stored representative.
`validStructure` checks this contract together with the CHAMP substrate's canonical-shape validator.
Values are immutable and therefore safe for concurrent evaluation when caller-supplied hash/equality
callbacks are themselves safe.

`Data.Structures.Ordered.PersistentOrderedMap` composes that ordered key set with a CHAMP payload
index. Its API covers strict positional insertion, existing-value replacement without movement,
explicit movement, keyed/positional removal, ranges, reversal, stable entry sorting, ordered
enumeration, component-root sharing diagnostics, and two-way validation. The first key
representative and position win while construction's last payload wins.

### Documented deviation: the ordered map has no value-equality policy

`PersistentOrderedMap` carries a key `HashPolicy` and nothing else, because `HashPolicy` describes
only key hashing and key equality. `set` therefore always publishes a successor version, and the
cursor's `orderedMapCursorSetNextValue` inherits that: writing a payload the caller considers equal
to the stored one returns a new version rather than the receiver. Sibling ports whose payload index
is value-equality aware — Kotlin and C# among them — return the receiver for such a no-op.

Closing the gap requires a value policy on the map, which would change the `emptyWith` and
`fromListWith` signatures and every construction site, including `PersistentOrderedMultimap`'s.
That is a breaking API change and is deliberately not taken here. `PersistentOrderedMultimap` does
carry an independent value `HashPolicy`, so its grouped membership and its cursor's insert no-op are
value-policy aware; the deviation is confined to `PersistentOrderedMap` payload writes.

`Data.Structures.Ordered.PersistentOrderedMultimap` composes an ordered map of nonempty ordered
value sets under independent runtime policies. It retains first key and value representatives,
enumerates key groups and their values in nested first-insertion order, removes empty groups, and
tracks group and pair counts separately.

Because `delete` locates a pair by content and returns its receiver on a miss, the multimap cursor's
`orderedMultimapCursorDeletePrevious` and `orderedMultimapCursorDeleteNext` validate the pair count
before publishing and report `Nothing` when nothing was removed, rather than announcing a successful
deletion that changed nothing. `orderedMultimapCursorInsert` likewise derives its following gap from
group boundaries instead of re-scanning for the inserted pair. Both matter for a value that is not
reflexive under the value policy, such as a `NaN`, which the collection accepts but a content
re-lookup can never find again.

## Persistent cursors

`Data.Structures.Ordered.Cursor` adds one immutable gap cursor per family:
`PersistentOrderedSetCursor a`, `PersistentOrderedMapCursor k v`, and
`PersistentOrderedMultimapCursor k v`. Each is an opaque snapshot-plus-position value retaining one
exact collection version plus a validated position in `0 .. size`, denoting the gap between the
entries before and at that position. The navigation axis is the package's own explicit
insertion/position order for the set and map, and the flattened grouped pair order for the multimap.
Private signed 64-bit order labels never enter the cursor contract, and neither does the CHAMP index
shape.

Two shared result carriers appear throughout: `OrderedCursorSearch`, with fields
`orderedCursorFound` and `orderedSearchCursor`, and `OrderedCursorInsert`, with fields
`orderedCursorAdded` and `orderedInsertionCursor`.

Positional factories are `orderedSetCursorAt`, `orderedMapCursorAt`, and `orderedMultimapCursorAt`,
each returning `Nothing` outside `0 .. size`. Equality factories are `findOrderedSetCursor`,
`findOrderedMapCursor`, `findOrderedMultimapCursor` (by key and value), and
`findOrderedMultimapGroupCursor` (the first pair of a key group). Because insertion order has no
sorted lower bound to fall back on, a miss deliberately returns the **end** cursor together with
`orderedCursorFound == False`, rather than an inferred insertion gap; the cursor stays usable and
the caller decides where to place the value.

`…CursorPosition`, `…CursorSize`, `…CursorAtStart`, and `…CursorAtEnd` query a cursor without
touching either index. `…CursorPeekPrevious` and `…CursorPeekNext` return the neighbouring item,
`(k, v)` entry, or `(k, v)` pair in `Maybe`. `…CursorMovePrevious`, `…CursorMoveNext`, and
`…CursorSeek` return a new cursor over the same logical version. `…CursorSnapshot` returns the
retained collection and never consumes the cursor, so every retained ancestor stays branchable.

Edits keep these gap conventions and delegate to the ordinary persistent operations, so both the
ordered sequence and the hashed index publish together or not at all:

- `orderedSetCursorInsert` adds an absent class at the gap and returns the gap after it; an
  equivalent present class is an exact no-op that neither moves the gap nor replaces the stored
  representative. `orderedSetCursorTryInsert` reports which case occurred and, on a duplicate,
  returns the receiver unchanged.
- `orderedMapCursorInsert` strictly adds an absent key at the gap and returns the gap after it.
  Since the retained position is valid by construction, its `Nothing` means exactly one thing: the
  key is already present. `orderedMapCursorTryInsert` reports the same outcome, but on a duplicate
  it *repositions* onto the stored entry instead of preserving the receiver's gap — the one place
  where the set and map try-insert spellings deliberately differ.
- `orderedMapCursorSetNextValue` rewrites the next entry's payload, retains its stored key
  representative and position, and keeps the gap fixed. It has no value-equality no-op; see the
  documented deviation above.
- `orderedMultimapCursorInsert` follows grouped semantics rather than the raw gap: the value joins
  the end of its key's group, or a fresh last group when the key is absent, and the returned gap is
  that group's end. A duplicate pair preserves the receiver.
- Every `…CursorDeletePrevious` removes the preceding entry and moves the gap left; every
  `…CursorDeleteNext` removes the next entry and keeps the gap fixed.

Cursors are opaque pure values with hidden constructors. No uninitialized, moved-from, or disposed
state is representable, so the invalid-default contract that the C, C++, C#, and Rust ports must
enforce at run time is discharged here by the type system — a consequence of immutability, not an
omitted check. Three internal `error` calls remain and are all dual-index invariant assertions
rather than caller-reachable failures: a position that would overflow `maxBound :: Int`, and the two
insertions whose success the retained cursor invariant has already established.

### Cursor complexity

These are Profile R snapshot-plus-position checkpoints in the sense of the repository-wide
persistent cursor design. A cursor is exactly `(collection, position)`, retains no path frames, and
implements every edit through the ordinary persistent operation. They claim none of the C# rope
tier's focused representation, memoized snapshot, callback ceiling, allocation bound, or
amortized-locality properties.

Let `w` be the bounded CHAMP depth and `c` an equal-hash collision scan.

- All three `…CursorSize` reads are O(1), as are all `…CursorPosition`, `…CursorAtStart`,
  `…CursorAtEnd`, and `…CursorSnapshot` calls; movement and seek are O(1) because they rewrite only
  an integer.
- Set peeks are O(log n); map peeks add one payload probe, giving O(log n + w + c).
- `findOrderedSetCursor` and `findOrderedMapCursor` are O(w + c + log n): one hashed probe for the
  order label, then a measured search for its position.
- Set and map insertion and deletion are O(log n + w + c). When a local label gap is exhausted, that
  one produced version instead pays a deterministic relabel of the whole order, O(n (w + c)); no
  relabel cost is amortized across retained branches.
- The multimap cursor is the honest exception. Its size is an O(1) cached pair count and its
  movement is O(1), but `orderedMultimapCursorPeekPrevious`, `orderedMultimapCursorPeekNext`,
  `findOrderedMultimapCursor`, `findOrderedMultimapGroupCursor`, and the group-end gap derived by
  `orderedMultimapCursorInsert` each materialize the flattened grouped enumeration and are therefore
  Θ(n) in the pair count. A traversal by repeated move-plus-peek is Θ(n²). The outer ordered map
  caches no value-group prefix counts, so a logarithmic flattened pair rank is not available to
  build on, and this port does not pretend otherwise.

Run the focused, single-job gate from `src/Haskell`:

```powershell
.\test.ps1 -Workspace Ordered
```

The gate builds with `-Wall -Wcompat`, runs examples plus deterministic model, collision/relabel,
policy, failure-atomicity, sharing, and concurrent-read tests, and is not a benchmark.
