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

## Persistence and diagnostics

`shares_roots_with`, `shares_order_storage_with`, and `shares_membership_root_with` expose sharing
without turning Rust facade values into identity-bearing objects. `validate_structure` checks count
agreement, strict stamps, bidirectional index coverage, stamp agreement, and representative
equivalence, returning `PersistentOrderedSetStatistics` or a typed invariant error.

The implementation uses safe Rust only. It references the public HAMT and FingerTree crates and
never references the application-specific Tungsten crate.
