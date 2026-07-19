# C++ Persistent Ordered Collections API Notes

- Created (UTC): 2026-07-15T09:20:15Z
- Repository HEAD: 88164edb086096800b2fb32eeaa7e7a1e556e183
- Updated (UTC): 2026-07-16T22:52:15Z
- Audience: C++ API consumers, maintainers, and cross-language parity reviewers
- Scope: `persistent_ordered_set`, `persistent_ordered_map`, and `persistent_ordered_multimap`

## Persistent Ordered Multimap

`persistent_ordered_multimap<Key, Value, ...>` owns an ordered map of nonempty ordered value sets.
Key-group order and each group's value order are independent first-insertion orders under retained
key/value hash and equivalence policies. Pair addition is idempotent, removing a final value removes
its group, flattened iteration is grouped, and checked 64-bit pair counts are distinct from group
counts. Nested edits preserve C++ value semantics, immutable roots, and exception atomicity.

## Persistent Ordered Map

`persistent_ordered_map<Key, Value, Hash, KeyEqual, ValueEqual>` adds payload-bearing keyed entries
without adopting Tungsten semantics. A persistent deque owns `(stamp, key, value)` entries while a
CHAMP map stores only `key -> stamp`; arbitrary payloads are therefore not duplicated in the hash
index. Construction retains the first key representative and position and the last distinct value.
`set_item` retains the stored key, label, and position, and an equal-value update shares the complete
membership root.

The map provides keyed lookup and representative recovery, `entry_at`/`front`/`back`, strict
`add`/`add_first`/`insert`, conditional `try_add`, explicit movement, keyed and positional removal,
policy-preserving clear and ranges, take/drop, reverse, stable sort, and ordered entry/key/value
materialization. Sparse labels, deterministic relabeling, exception atomicity, and asymptotic bounds
match the set. `validate_invariants` additionally proves that every ordered key has exactly one
matching CHAMP label and that values occur only in engaged deque entries.

## Persistent Ordered Set

## Type And Policy

```cpp
template <
    class T,
    class Hash = std::hash<T>,
    class KeyEqual = std::equal_to<T>>
class persistent_ordered_set;
```

`T`, `Hash`, and `KeyEqual` satisfy `std::copyable`. Assignment is part of the honest contract:
relabeling and stable sorting permute entry values, while persistent-map candidates assign their
retained policy values. The retained hash and equality policy values define membership, duplicate
collapse, representative lookup, receiver-side algebra normalization, and relations.
Hash-compatible equality remains the caller's obligation. Policy accessors return `const Hash&`
and `const KeyEqual&` from the retained CHAMP index.

The neutral Ordered target includes only public HAMT and FingerTree headers. It does not include,
link, wrap, subclass, source-share with, or test against a Tungsten artifact.

## Construction And Lookup

`empty_set()` uses default policies. `create(hash, equal)` creates a policy-preserving empty, while
`create_range(range, hash, equal)` consumes an input range once in source order. The first value of
each equality class fixes both its representative and initial position.

State and lookup members are:

- `size`/`count`, `empty`/`is_empty`, `hash_function`, and `key_eq`;
- `front`/`first`, `back`/`last`, and checked `operator[]`/`at`/`get_at`;
- `contains`, pointer-returning `try_get_value`/`try_get`, and signed `index_of`; and
- projecting forward iteration plus `to_vector`.

Each Ordered iterator owns the immutable order root captured at `begin()`. It therefore remains
usable after the originating facade is destroyed, and dereferenced representatives remain alive
while that iterator or another snapshot retaining the root remains alive. Copies are independent
forward iterators over the same captured version.

`try_get_value` returns the stored representative or null. Its pointer follows the underlying HAMT
node-lifetime rule: retain a set version that owns the node while using the pointer. `index_of`
returns `-1` for an absent class.

## Addition, Movement, And Removal

`add`, `add_first`, and `insert` install only absent classes. A present class returns a value sharing
the receiver's membership root, leaves its position unchanged, and retains its first representative.
`insert` accepts positions through `size()` and validates the position before hashing.

`move_to_first`, `move_to_last`, and `move_to` retain the stored representative. `move_to` accepts
an element position and treats it as the final result position after removing the old occurrence.
An absent movement throws `std::out_of_range`; movement to the current position shares the receiver
membership root.

`remove`, `try_remove`, `remove_at`, `remove_first`, `remove_last`, and `clear` publish immutable
successors. Misses and clearing an empty set preserve root sharing. Empty endpoint operations throw
`std::logic_error`; invalid positions and ranges throw `std::out_of_range`.

## Ranges And Order

`get_range(index, count)`, `take(count)`, and `drop(count)` preserve the equality policy and stored
representatives. A full range shares the receiver root; an empty range retains the receiver policy.
The sequence is split once. Index reconciliation rebuilds from kept entries when they are the
smaller side and otherwise removes discarded edge entries from the receiver index.

`reverse` assigns fresh private labels and rebuilds both indexes. `sort(compare)` is stable: old
order breaks comparator ties, the equality policy does not change, and later additions append
normally. Counts zero and one do not invoke the comparator. If output order is unchanged, the
membership root is shared. Unlike the C# `Array.Sort` surface, C++ propagates a comparator exception
unchanged, following standard-library algorithm conventions; the source remains immutable.

## Algebra And Relations

`union_with`, `intersect_with`, `except_with`, and `symmetric_except_with` accept either another
ordered set or any compatible input range. Every operation first enumerates and collapses the
entire argument under the receiver's `Hash`/`KeyEqual` values. The first argument representative of
each collapsed class wins. This eager phase occurs before an operation-specific shortcut.

Result order is:

| Operation | Ordered result |
| --- | --- |
| union | receiver representatives, then argument-only representatives |
| intersection | surviving receiver representatives |
| difference | surviving receiver representatives |
| symmetric difference | receiver-only representatives, then argument-only representatives |

The receiver representative wins every surviving receiver class. Logical no-ops share the
receiver membership root where the C++ value-semantic facade can express that efficiently.

`is_subset_of`, `is_proper_subset_of`, `is_superset_of`, `is_proper_superset_of`, `overlaps`, and
`set_equals` apply the same eager receiver-policy normalization. Argument duplicates therefore
count once, and a late range or policy failure is not hidden by an early decisive value.

## Representation, Persistence, And Validation

The private representation is a stamp-ordered `persistent_deque<Entry>` plus a
`persistent_hash_map<T, int64_t, Hash, KeyEqual>`. Invariants are:

- both indexes have equal cardinality;
- sequence labels strictly ascend;
- each sequence representative has the same label in the map;
- each map class identifies one sequence representative; and
- every published version owns immutable substrate roots.

Ordinary insertion selects a label before, after, or midway between adjacent labels. When a gap or
signed endpoint is exhausted, the unpublished candidate is deterministically relabeled at a private
spacing and both indexes are rebuilt. `validate_invariants()` recomputes deque, CHAMP, label, and
cross-index invariants; `debug_validate()` converts any diagnostic failure to `false`.
`shares_index_with` exposes CHAMP-root identity solely for persistence/no-op tests.

All callback and allocation work constructs unpublished candidates. An exception leaves every
published source and retained branch unchanged. Published versions support concurrent read-only
lookup and iteration without locks.

## Complexity

Let `w <= 7` be the 32-bit CHAMP depth and `c` an equal-full-hash collision scan.

| Member family | Bound |
| --- | --- |
| construction | O(m (w + c) + n) plus final sequence/index builds |
| hashed lookup | O(w + c) |
| positional lookup | O(log n) worst; deque endpoint/finger amortization also applies |
| `index_of` | O(w + c + log n) |
| end insertion | O(w + c) amortized on a linear history |
| positional insertion/movement with a gap | O(w + c + log n) |
| relabeling insertion/movement | O(n (w + c)) |
| successful removal | O(w + c + log n); miss O(w + c) |
| ranges | O(log n) plus O(min(kept, removed) (w + c)) |
| reverse | O(n (w + c)) |
| stable sort | O(n log n) comparisons plus changed-result rebuild |
| algebra | conservative O((n + m) (w + c + log(n + m + 1))) |
| relations | O((n + m) (w + c)) after normalization |
| enumeration/copy | O(n) |

These are asymptotic capability contracts, not benchmark claims. Private label spacing and relabel
cadence are not public API.

## Cursors

`persistent_ordered_cursors.hpp` ships `persistent_ordered_set_cursor<T, Hash, KeyEqual>`,
`persistent_ordered_map_cursor<Key, Value, Hash, KeyEqual, ValueEqual>`, and
`persistent_ordered_multimap_cursor<Key, Value, KeyHash, KeyEqual, ValueHash, ValueEqual>` under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md).
Each is an immutable value holding a retained snapshot plus an explicit-position gap. All three are
Profile R checkpoints delegating every edit to the ordinary persistent operation, and none claims the
C# rope tier's focused representation, memo cell, callback ceiling, allocation bound, or amortized
locality. No stamp or label reaches the cursor surface; the two collection headers keep every label
member private.

Factories are **free functions found by ADL**, not members — the opposite of the HAMT port, whose
Patricia and Merkle cursors use collection members:

```cpp
get_cursor(set, position = 0);          get_cursor_at_item(set, item);
get_cursor(map, position = 0);          get_cursor_at_key(map, key);
get_cursor(multimap, position = 0);     get_cursor_at_pair(multimap, key, value);
                                        get_cursor_at_group(multimap, key);
```

There is deliberately no `get_cursor_lower_bound`, `get_cursor_upper_bound`, or `get_cursor_at_end`:
these collections are insertion-ordered rather than key-sorted, so there is no ordered bound to seek.
`get_cursor(collection, collection.size())` is the end gap. Searches return
`ordered_cursor_search_result<Cursor> { Cursor cursor; bool found; }` and insertions return
`ordered_cursor_insert_result<Cursor> { Cursor cursor; bool inserted; }`. A search miss yields a
cursor **at the end**, not at a lower bound — another deliberate divergence from HAMT, where a miss
yields the lower-bound gap.

The set and map expose `size`, `position`, `is_at_start`, `is_at_end`, `try_peek_previous`,
`try_peek_next`, `move_previous`, `move_next`, `seek`, `insert`, `try_insert`, `delete_previous`,
`delete_next`, and `snapshot`; the map adds `set_next_value`, which retains the stored key, its
label, and the gap. The multimap spells insertion `add`/`try_add`, has no value-update verb, and uses
`std::int64_t` positions bounded by `pair_count()`. Set and map peeks return borrowed
`const value_type*`/`const entry_type*` that are lvalue-only, with the `const&&` overloads
`= delete`d so a reference cannot outlive a temporary cursor; multimap peeks instead return
`std::optional<value_type>` **by value**, because a flattened pair is materialized rather than stored.

Unlike the HAMT cursors there is no gap-agreement check, so no `std::invalid_argument` appears on
this surface. Errors are `std::out_of_range` from the constructors for a position outside the
collection, `std::logic_error` for movement past an end or an edit with no adjacent entry, and
`std::overflow_error` when a position or label is exhausted.

Default construction is unavailable because each class declares a two-argument constructor that
suppresses the implicit default — but it is *not* explicitly deleted, and the header records no
intent. Copy and move are implicitly defaulted, so **move is destructive**: a moved-from cursor holds
a moved-from collection while retaining its scalar position, breaking the constructor's
`position <= size` invariant. `size()` typically reports zero while `position()` reports the old rank,
and `is_at_end()` can report `false` with nothing to peek. The multimap is the sharpest case, since
its constructor rejects a negative position that a moved-from object bypasses entirely. Treat a
moved-from ordered cursor as unusable for anything but destruction or assignment, and copy when both
values must stay live. `merkle_search_tree_cursor` in the HAMT port has adopted a copy-on-move rule
that removes this hazard; these three have not.

`snapshot()` returns the retained collection by value, sharing its roots. A no-op edit returns
`*this`, discriminated by **membership** rather than root identity: set `insert` and multimap `add`
test `contains` first. Map `insert` has no such guard and always delegates. A same-position `seek`
returns the receiver.

Complexity. For the set and map, `size`, `position`, the two predicates, and `snapshot` are O(1);
`move_previous`, `move_next`, and `seek` are O(1) struct rebuilds; peeks are one O(log n) deque
descent; and edits are the delegated `insert`/`remove_at`/`set_item` cost. Because movement defers
all work to the peek, a complete traversal by move-plus-peek is O(n log n) — use the collection's own
iteration for a linear walk.

The multimap cursor is the outlier and is **linear, not logarithmic**. It is a flat global pair rank
over the grouped flattening with no prefix sum over group sizes, and the pair-visiting helper has no
early exit, so resolving a rank always walks every pair. `try_peek_previous`, `try_peek_next`,
`get_cursor_at_pair`, and `get_cursor_at_group` are each Θ(`pair_count`), and `add` pays a full scan
to recover the inserted rank *on top of* the structural insert. A complete traversal by
move-plus-peek is Θ(`pair_count`²). This flat shape is a recorded deviation from the design's nested
group-focus multimap state, shared by all nine ports, and needs one contract decision rather than
nine local patches.
