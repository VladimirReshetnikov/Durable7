# Rust Persistent Ordered Collections API Notes

- Created (UTC): 2026-07-15T00:00:00Z
- Repository HEAD: a47ada790d8028a744990c4608c32ab001376683
- Audience: Rust API consumers, maintainers, and port reviewers
- Scope: `PersistentOrderedSet`, `PersistentOrderedMap`, and `PersistentOrderedMultimap`

## Persistent ordered map

`PersistentOrderedMap<K, V, S = RandomState>` composes a positional deque of stamped key/value
entries with a `PersistentHashMap<K, i64, S>` navigation index. Values occur only in the deque.
`get` performs a CHAMP lookup followed by binary search over strictly increasing private stamps;
positional reads need no hashing.

`add`/`add_first` and valid `insert` are strict and return `DuplicateKey`; `try_add` is nonthrowing.
`set_item` appends an absent key or replaces only the payload in place, retaining the first key
representative and explicit position. Equal values share both roots. Movement is explicit through
`move_to_first`/`move_to_last`/`move_to`; removal, ranges, reversal, and stable `sort_by` retain the
set's sparse-label and deterministic relabel rules. The map deliberately exposes no key algebra.

`validate_structure` checks count agreement, strict stamps, bidirectional key coverage, stamp
agreement, and representative equivalence. Representative identity follows Rust clone semantics;
use `Arc<K>` when allocation identity matters.

## Persistent ordered multimap

`PersistentOrderedMultimap<K, V, SK = RandomState, SV = RandomState>` composes an outer
`PersistentOrderedMap` with one `PersistentOrderedSet` per key. Key-group order follows first key
insertion, and each group's value order follows first insertion within that group. Pair iteration is
therefore grouped by key; the facade does not retain a globally interleaved pair-arrival history.

Key and value `Eq`/`Hash` domains are independent, as are their `BuildHasher` policies. Equivalent
key and value representatives are retained until their group or pair is removed. Duplicate pair
insertion shares the original nested roots. Removing the last value contracts the empty group;
re-adding it later appends a new key group. `remove_key` removes a whole group, and `clear` retains
both hash policies.

`get_values` returns the immutable ordered value set for a group, while `get_key` and `get_value`
recover retained representatives. `key_count` and `pair_count` distinguish the two cardinalities.
Validation checks the outer map, every nonempty value group, and the cached total pair count.
Lookup and point edits inherit hashed outer/group probes and persistent path copying; iteration is
linear in the emitted keys or pairs.

## Type and policy

```rust
pub struct PersistentOrderedSet<T, S = RandomState>
```

`T: Eq + Hash` defines membership classes. `S: BuildHasher + Clone` defines receiver-owned hash
routing. The collection deliberately does not implement `PartialEq`: ordered sequence equality is
available through `iter`/`to_vec`, and membership equality is explicit through `set_equals`.

`new` and `from_items` use `RandomState`. `with_hasher` and `from_items_with_hasher` retain a
caller-supplied builder. Iterable construction consumes the source once, preserves first
representatives and first positions, and discards later class-equivalent values.

## Lookup and ordering

`contains` and `get_stored` probe the CHAMP index. `get_stored` returns the installed representative,
not the lookup argument. `get`, indexing, `first`, `last`, and `iter` read positional order without
hashing. `index_of` performs one membership lookup followed by a binary search over strictly
ascending private stamps.

`add`, `add_first`, and valid `insert` add only absent classes. Duplicates are root-sharing no-ops.
`move_to_first`, `move_to_last`, and `move_to` are the only implicit-position-changing operations;
they retain the stored representative, interpret the supplied position as its final result index,
and report absent values or invalid positions through `OrderedSetMoveError`.

`remove` is a no-op on a miss. `try_remove` returns an `OrderedSetRemoveResult` whose miss contains a
root-sharing clone. Position and endpoint removal use `Option`. `clear` retains the receiver's hash
builder and reuses both roots when already empty.

## Ranges and reordering

`get_range(index, count)` checks `count <= len - index`, avoiding addition overflow. `take` and
`drop` accept counts through `len`. A full range shares both roots; an empty range retains the hash
builder. Nonempty range extraction keeps the persistent sequence slice. It rebuilds the membership
index when the kept side is smaller and otherwise removes discarded edge entries, preserving the
O(min(kept, removed)) reconciliation factor.

`reverse` and changed sorts rebuild fresh sparse stamps. `sort` uses `Ord`; `sort_by` accepts a
caller comparator. Sorting is stable because old stamps break comparator ties. It is one-shot: no
ordering policy is retained for future insertions. Counts zero and one do not call the comparator,
and an unchanged sorted sequence shares both roots.

## Algebra and relations

`union`, `intersect`, `except`, and `symmetric_except` accept owned iterables. The `_set`
convenience forms accept another ordered set. All forms first exhaust and normalize the argument
under a clone of the receiver's hash builder, retaining its first representatives. No shortcut can
hide a late iterator, hash, equality, or clone failure.

Result order is:

| Operation | Result order |
| --- | --- |
| union | receiver representatives, then argument-only representatives |
| intersection | surviving receiver representatives |
| difference | surviving receiver representatives |
| symmetric difference | receiver-only representatives, then argument-only representatives |

Receiver representatives win shared classes. `union`, `intersect`, and `except` share both roots
when their ordered representative sequence is unchanged; symmetric difference does so when the
normalized argument is empty.

The six relation methods use the same eager normalization and distinct-class counts. Duplicate
argument values count once under `Eq`/`Hash`.

## Persistent ordered cursors

The public `cursors` module supplies `PersistentOrderedSetCursor<T, S>`,
`PersistentOrderedMapCursor<K, V, S>`, and `PersistentOrderedMultimapCursor<K, V, SK, SV>`, plus the
two result carriers `OrderedCursorSearch<C> { found, cursor }` and
`OrderedCursorInsert<C> { added, cursor }`. All five are re-exported from the crate root.

Each cursor is a **Profile R root-plus-position semantic checkpoint**: it retains one complete
collection value plus a validated `usize` gap, publishes both indexes atomically by delegating to
the ordinary persistent operation, and retains no ordered-sequence context frames. None of the C#
rope tier's focused representation, snapshot memo, callback ceiling, allocation bound, or amortized
locality is claimed here. Sparse stamps never enter the cursor contract; a cursor position is a gap
count, not a label.

Rust ownership supplies the invalid-default contract that the C# struct cursors state explicitly.
These types have no `Default` and no observable moved-from state, and use after a move is a
compile-time error, so every nameable cursor is fully initialized. A cursor over an empty collection
is an ordinary value with `position() == 0` for which `is_at_start` and `is_at_end` are both true.

### Axes, factories, and navigation

The set and map cursors use the collection's explicit position order — insertion order as modified
by `move_to*`, `reverse`, and one-shot sorting. The multimap cursor uses the grouped pair rank of
`iter`: key-group order first, then value order inside each group. It is a rank over a derived
enumeration, not a retained global pair-arrival history.

`cursor_at(position)` on all three returns `Option`, accepting `0..=len` for the set and map and
`0..=pair_count()` for the multimap. `find_cursor` is the equality-search factory: the set takes a
value, the map takes a key, and the multimap takes a `(key, value)` pair. The multimap additionally
offers `find_group_cursor(key)`, which locates the first pair of a key's group. Every search factory
returns `OrderedCursorSearch`; a hit places the gap immediately **before** the stored entry, and a
miss returns `found: false` with a usable end cursor at `len` / `pair_count()`.

Navigation is `position`, `is_at_start`, `is_at_end`, borrowed `peek_previous` / `peek_next`,
`move_previous` / `move_next`, `seek(position)`, and `snapshot()`. The set and map cursors add `len`
and `is_empty`; the multimap cursor exposes `pair_count()` instead. Boundary movement and an
out-of-range seek return `None`, and `seek` to the current position returns a root-sharing clone
without re-validating. Unlike the FingerTree crate's sequence cursors, `snapshot()` here returns a
**borrow** of the retained collection (`&PersistentOrderedSet<T, S>` and siblings) rather than an
owned clone; clone the borrow when an owned value is wanted. Snapshotting never consumes the cursor,
and every retained cursor remains an independently branchable version.

### The two discriminators

`found` and `added` are deliberately different fields on deliberately different types, so no generic
code written over either carrier can confuse them:

- `OrderedCursorSearch::found` reports **presence**: `true` means an equivalent entry already exists
  in the receiver. It is produced only by `find_cursor` and `find_group_cursor`, which never edit.
- `OrderedCursorInsert::added` reports **publication**: `true` means the attempt created a new
  entry. It is produced only by `try_insert`. A rejected duplicate reports `added: false` and
  returns a cursor whose retained version shares roots with the receiver.

The two therefore have opposite senses for the same key — a key that `find_cursor` reports as
`found: true` is exactly a key that `try_insert` reports as `added: false` — which is why they are
not one field on one type.

### Edits and gap conventions

The set cursor's `insert(value)` is infallible and returns `Self`, because a duplicate is the
collection's own root-sharing no-op. The map cursor's `insert(key, value)` returns
`Result<Self, DuplicateKey>`, re-using the HAMT crate's error type. The multimap cursor's
`insert(key, value)` is infallible and takes an explicit key: it appends the value to that key's
existing group, or appends a new trailing group, exactly as `PersistentOrderedMultimap::insert`
does. It does **not** insert into the group the cursor currently focuses, and the resulting gap is
therefore derived from where the pair actually landed in grouped order, which may be far from the
receiver's gap. Everything else is `Option`-valued: `set_next_value`, `delete_previous`, and
`delete_next` return `None` when the addressed neighbor does not exist.

| Cursor and operation | Resulting gap |
| --- | --- |
| set `insert` / `try_insert` on an absent class | `position + 1` |
| set `insert` / `try_insert` on a duplicate | `position`, unchanged, both roots retained |
| map `insert` on an absent key | `position + 1` |
| map `try_insert` on an absent key | `position + 1` |
| map `try_insert` on a duplicate | the existing key's index — the gap **moves** to before the retained entry |
| map `set_next_value` | `position`, unchanged |
| multimap `insert` / `try_insert` on an absent pair | one past the inserted pair's grouped rank |
| multimap `insert` / `try_insert` on a duplicate pair | `position`, unchanged, groups root retained |
| any `delete_previous` | `position - 1` |
| any `delete_next` | `position`, unchanged |

The map's duplicate `try_insert` is the one case where a rejected edit relocates the gap; the set
and multimap keep it. This is intentional — the map result focuses the retained entry so a caller
can immediately `set_next_value` — but it is a real asymmetry across the three types and callers
should not assume a uniform rule.

There is no `replace_next` on the set or multimap cursors: replacing a representative would conflict
with first-representative retention and could collide with another equality class. The map's
`set_next_value` is the only in-place payload edit; it clones the stored key and routes through
`set_item`, so the first key representative, its stamp, and its explicit position all survive, and
an equal value shares both roots. All three cursors retain their source collection's exact
`BuildHasher` policies, including on an empty result.

Failure atomicity follows the collections. A panic in hashing, equality, cloning, or allocation
abandons only an unpublished successor; the receiver cursor, its snapshot, and every retained branch
remain valid. No half-updated membership index is observable, because the ordinary operation
publishes the ordered sequence and the CHAMP index together.

### Honest local complexity

Let `w <= 7` be CHAMP depth and `c` an equal-hash collision scan.

For the **set and map cursors**: `cursor_at`, `position`, `len`, `move_previous`, `move_next`, and
`seek` are O(1) — they clone two `Arc` roots and rewrite an integer. `peek_previous` and `peek_next`
are O(log n), one positional descent of the deque. `find_cursor` is O(w + c + log² n): one CHAMP
probe recovers the private stamp, then `index_of_stamp` binary-searches the ordered sequence and
pays an O(log n) positional descent for **every** probe, giving the squared-logarithmic stamp
location tier. Insertion and deletion combine an O(w + c) index update with an O(log n) sequence
edit; when the sparse-stamp gap between neighbors is exhausted, the ordinary relabel fallback
rebuilds one complete version at O(n (w + c)) and the cursor is reconstructed at the contractually
resulting gap. `snapshot` is O(1) and borrows.

For the **multimap cursor** the bounds are materially worse and must not be read across from the
other two. `pair_count`, `position`, `move_previous`, `move_next`, and `seek` are O(1), but
`peek_previous` and `peek_next` walk the flattening iterator with `iter().nth(position)` and are
therefore O(position), i.e. O(n) worst case in the pair count. `find_cursor` and
`find_group_cursor` scan with `iter().position(..)` and are O(n). `insert` performs the ordinary
grouped insertion and then re-scans the successor with `iter().position(..)` to locate the new
pair's rank, so it is O(n) plus the hashed outer/group edit. `delete_previous` and `delete_next`
recover their target through the same O(position) walk before removing it. The outer key groups do
not cache value-group prefix counts, so no logarithmic random pair rank is available and none is
claimed.

## Persistence and diagnostics

`shares_roots_with`, `shares_order_storage_with`, and `shares_membership_root_with` expose sharing
without turning Rust facade values into identity-bearing objects. `validate_structure` checks count
agreement, strict stamps, bidirectional index coverage, stamp agreement, and representative
equivalence, returning `PersistentOrderedSetStatistics` or a typed invariant error.

The implementation uses safe Rust only. It references the public `durable7_hamt` and
`durable7_fingertree` crates and nothing else outside the standard library, and it declares no
`unsafe` block.
