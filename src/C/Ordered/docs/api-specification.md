# C Persistent Ordered Collections API Specification

- Status: Normative current API and behavior specification
- Created (UTC): 2026-07-15T09:00:00Z
- Repository HEAD: 2d75a79feb424f4476ec32c2d6e4f19263441bf3
- Audience: C consumers, maintainers, reviewers, and sibling-port authors
- Scope: `d7_ordered_set`, `d7_ordered_map`, and `d7_ordered_multimap`

## Ordered multimap

`d7_ordered_multimap` retains independent `d7_ordered_policy` values for keys and values and an
ordered map whose payloads are nonempty ordered sets. The first key representative fixes group
position; the first value representative fixes its position inside that group. Visiting flattens
groups in key order, then values in group order. Duplicate pair addition and absent removal clone a
logically unchanged version; removing the last value removes its group. Every published handle owns
its nested state and follows the same uninitialized-output, clone/move/destroy, overflow, and
failure-atomicity rules as the base ordered collections.

## Ownership and policy

`d7_ordered_set` is a persistent value handle. A successful initializer or operation publishes one
owned handle; use `d7_ordered_set_clone` for another owner, `d7_ordered_set_move` to transfer one,
and `d7_ordered_set_destroy` exactly once per initialized owner. Operations that return a set take
an uninitialized result distinct from every input. They build unpublished foundation versions and
write the result only after all fallible work succeeds.

`d7_ordered_policy` retains an `ft_value_type`, hash callback, optional equality callback, and
callback context. A null equality callback means byte equality over `item_type.size`; hashing is
always explicit. The callback functions and their contexts remain caller-owned and usable until all
sets in the lineage are destroyed. Already-retained immutable versions may be read concurrently;
clone, update, and destruction of structurally shared lineages must be serialized because both C
foundations use non-atomic intrusive ownership.

C APIs receive a non-null pointer to an item value. A nullable application value is represented in
the usual type-erased way by passing the address of a pointer-valued item whose contents may be
null. Returned item pointers and visitor arguments are borrowed from the set.

## Ordering and representatives

Hash/equality policy defines membership classes. Construction and argument normalization enumerate
once in input order and retain the first representative of each class. Duplicate addition and
insertion retain both the representative and position. Explicit movement moves the stored
representative, never the lookup argument, and interprets its index as the final result index.

The implementation owns two persistent indexes:

```text
FingerTree deque: (strictly ascending int64 stamp, representative cell)
CHAMP map:        representative class -> int64 stamp
```

The indexes share the same ref-counted representative cell. Sparse end labels and midpoint labels
handle ordinary edits. If no integer lies in a required gap, one unpublished deterministic rebuild
centers fresh sparse labels over the complete result. The exact gap and relabel cadence are private.

## Results and failures

- `OUT_OF_RANGE` reports invalid positions and ranges; range validation uses `count <= size-index`.
- `EMPTY` reports endpoint reads/removals from an empty set.
- `NOT_FOUND` reports movement of an absent class.
- `OUT_OF_MEMORY` and `OVERFLOW` report allocation and checked-cardinality/label failures.
- `INVARIANT_VIOLATION` reports disagreement between the public foundation views.

Invalid positions are checked before hashing where an explicit positional parameter is supplied.
Failed operations do not mutate an input or write a partially initialized result. Relation answers
and `try_remove` flags are written only after successful completion.

Logical no-ops publish a structural clone sharing both foundation roots: duplicate additions,
movement to the current position, absent removal, empty clear, full range, unchanged stable sort,
and algebra whose ordered representatives are unchanged.

## Ranges, sort, algebra, and relations

Range extraction uses public FingerTree splits. It rebuilds the membership index from retained
entries when those are fewer, and otherwise removes the two discarded edge sequences from the
receiver index. Reversal and changed stable sorting preserve stored representatives, assign fresh
labels, and rebuild both indexes. Sort is a stable one-shot reorder and does not retain a comparison
policy. The type-erased C surface requires an explicit non-null `ft_compare_fn`.

Every algebra and relation operation eagerly normalizes its complete right operand under the
receiver's hash/equality policy before applying shortcuts. A right ordered set's own policy is not
used for receiver membership. Because C is type-erased, set operands must declare the same item
size; their ownership callbacks and equality policies may otherwise differ. Receiver
representatives win shared classes. Ordering is:

| Operation | Ordered result |
| --- | --- |
| union | receiver order, then normalized argument-only order |
| intersection | surviving receiver order |
| difference | surviving receiver order |
| symmetric difference | receiver-only order, then normalized argument-only order |

Duplicate relation operands count once under receiver equality. Proper relations compare normalized
distinct-class counts.

## Complexity

Let `w <= 7` be CHAMP depth, `c` an equal-full-hash collision scan, `n` receiver size, and `m`
argument input count.

| Area | Bound |
| --- | --- |
| membership / representative | O(w + c) |
| positional read | O(log n) worst case through FingerTree |
| index lookup | O(w + c + log² n) — see the stamp-location note below |
| end or positional edit with a label gap | O(w + c + log n) |
| edit requiring relabel | O(n (w + c)) for that produced version |
| successful removal | O(w + c + log n); miss O(w + c) |
| range | O(log n) sequence split plus O(min(kept, removed) (w + c)) reconciliation |
| reverse | O(n (w + c)) |
| stable sort | O(n log n) comparisons plus O(n (w + c)) rebuild |
| algebra | conservative O((n + m) (w + c + log(n + m + 1))) |
| relations | O((n + m) (w + c)) after normalization |

Resolving an equality class to a position is a two-tier operation and costs O(w + c + log² n), not
O(w + c + log n). One CHAMP lookup recovers the class stamp in O(w + c); locating that stamp is then
a binary search of O(log n) iterations over the order sequence, and each iteration reads a positional
entry through a FingerTree descent that is itself O(log n). Every path that maps a class to an index
inherits this tier, including `index_of`, the map's `index_of_key`, explicit movement, and the keyed
cursor factories.

These are asymptotic capability contracts, not benchmark claims. No amortization crosses two
persistent branches that independently relabel.

## Ordered map

`d7_ordered_map` is the payload-bearing sibling declared in
`durable7/ordered/ordered_map.h`. It owns an embedded ordered set of keys and a CHAMP
map from those keys to values under one heap-owned policy context. Its result, callback-lifetime,
non-aliasing, snapshot-concurrency, and first-key-representative rules match the ordered set.

Strict `add`, `add_first`, and positional `insert` reject an existing receiver-equivalent key;
`try_add` instead publishes a structural clone and reports `added == false`. `set` appends an absent
key, but an existing key retains both its first representative and its position. A semantically
equal value is a complete no-op clone; a changed value shares the entire key-order root and changes
only the CHAMP path. Reordering, reversal, and stable sorting share the value root. Removal edits
both indexes, while a range rebuilds the value index for precisely its selected keys.

Reads and visitors return borrowed key/value pointers. `entry_at`, `front`, and `back` use explicit
order; keyed lookup and containment use the receiver's hash/equality policy. Stable sort receives
both key and value for each comparison and does not install a maintained ordering policy.
`d7_ordered_map_debug_validate` checks each component's native invariant, equal counts, and both
directions of cross-index membership.

Let `w <= 7` be CHAMP depth and `c` an equal-full-hash collision scan. Keyed lookup is O(w+c), an
indexed entry read is O(log n + w+c), ordinary insertion/removal/movement is O(log n + w+c), and
value-only replacement is O(w+c). Range extraction is O(log n + k(w+c)) for `k` retained entries;
stable sorting is O(n log n) comparisons plus the ordered-set rebuild cost. These are capability
contracts rather than benchmark results.

## Ordered cursors

`ordered_cursor.h` declares `d7_ordered_set_cursor`, `d7_ordered_map_cursor`, and
`d7_ordered_multimap_cursor` and exports 62 functions across them: 20 for the set, 21 for the map,
and 21 for the multimap. Each cursor is an explicit-position gap over one retained snapshot. The set
and map use `size_t` positions in `0 .. size`; the multimap uses `int64_t` positions in
`0 .. pair_count`. All three are Profile R snapshot-plus-position checkpoints under the
[repository-wide persistent cursor design](../../../../docs/proposals/repository-wide-persistent-cursor-design.md):
every edit delegates to the ordinary persistent operation, and no cursor claims a focused
representation, memoization, callback ceiling, allocation bound, or amortized locality.

### Surface

| Role | Set | Map | Multimap |
| --- | --- | --- | --- |
| positional factory | `get_cursor` | `get_cursor` | `get_cursor` |
| keyed factory | `get_cursor_at_item` | `get_cursor_at_key` | `get_cursor_at_pair`, `get_cursor_at_group` |
| ownership | `_cursor_clone`, `_cursor_move`, `_cursor_destroy` | same | same |
| query | `_cursor_valid`, `_cursor_count`, `_cursor_position`, `_cursor_is_at_start`, `_cursor_is_at_end` | same | same |
| peek | `_cursor_try_peek_previous`, `_cursor_try_peek_next` | same | same |
| navigation | `_cursor_move_previous`, `_cursor_move_next`, `_cursor_seek` | same | same |
| edit | `_cursor_insert`, `_cursor_try_insert`, `_cursor_delete_previous`, `_cursor_delete_next` | plus `_cursor_set_next_value` | `_cursor_add`, `_cursor_try_add`, `_cursor_delete_previous`, `_cursor_delete_next` |
| snapshot | `_cursor_snapshot` | `_cursor_snapshot` | `_cursor_snapshot` |

The map alone has an in-place edit verb; `set_next_value` retains the stored key representative, its
sparse label, and the gap. The multimap alone has two keyed factories and spells insertion `add`.
Fifty-six functions return `d7_ordered_status`, three `_move` functions return `void`, and the
`_valid`, `_is_at_start`, and `_is_at_end` predicates return `bool`. `_cursor_count` and
`_cursor_position` return `0` both for a genuinely empty snapshot and for an invalid cursor; only
`_cursor_valid` distinguishes those states.

Cursors never report a miss through the status enum. `NOT_FOUND` is unused by the cursor surface.
`get_cursor_at_item`, `get_cursor_at_key`, `get_cursor_at_pair`, and `get_cursor_at_group` publish a
`bool* found` and return `OK` with a usable **end gap** on a miss, so a missed lookup is
indistinguishable from a hit at the final position unless the caller reads `found`. `*found` is
written only when the status is `OK`; on failure the caller's flag retains its prior value.
`try_insert` and `try_add` use the inverse `bool* inserted` convention and republish a structural
clone when the item was already present. `INVARIANT_VIOLATION` appears only on the multimap rank
paths.

### Ownership and the C exceptions

Cursors are owned handles, not trivially copyable values. Initialize only through a factory or
`_cursor_clone`, transfer with `_cursor_move`, and call `_cursor_destroy` exactly once per
initialized owner. A zeroed, moved-from, or destroyed cursor is invalid but safely destructible, and
destroy is idempotent: it delegates to the underlying handle destructor, whose guards tolerate an
all-zero value, and then zeroes the cursor.

Three C-specific rules govern correct use, and each is a deliberate departure from the managed ports.

**Result handles must be uninitialized or the exact source.** Producing operations build a complete
candidate and publish through a helper that destroys the result first when `result == source`. Exact
source/result aliasing is therefore supported and failure-atomic: a failed operation leaves both the
source and a distinct result untouched. A *distinct live* handle passed as `result` is a different
matter. It is neither diagnosed nor rejected; the publish path zeroes it and installs the candidate,
so **the retained version it previously owned leaks silently**. The header states the precondition
once, in the block comment above the cursor struct declarations, and no individual declaration
repeats it or states this consequence. Callers must enforce it themselves.

**Peeks borrow; a self-aliased publish invalidates the borrow.** All six peeks write `const void**`
out-parameters that point into the cursor's snapshot; nothing is copied into caller storage. The
header records the borrow only for the set, and the map and multimap peeks carry no lifetime comment
at all. Borrowed pointers remain valid only while the snapshot they came from is alive, so the
following two individually documented guarantees compose into a use-after-free:

```c
const void *item = NULL;
bool found = false;
d7_ordered_set_cursor_try_peek_next(&cursor, &found, &item);
d7_ordered_set_cursor_delete_next(&cursor, &cursor); /* self-alias destroys the snapshot */
/* `item` now dangles. */
```

Copy the borrowed value before any producing call through the same cursor, or retain an independent
clone. Nothing in the headers warns about this.

**A missed peek does not write its out-parameters.** On `found == false` the peek returns `OK` and
leaves `item`, `key`, and `value` at their prior values rather than nulling them. Initialize them
before the call.

One further transparency exception is recorded here because it is observable rather than theoretical:
the three cursor structs embed their collection handle **by value**, and those handles are public
structs. `cursor.set.stamps`, `cursor.map.values`, `cursor.map.keys.stamps`, and
`cursor.map.groups.keys.stamps` are all reachable `d7_hamt_map` values, and
`cursor.map.pair_count` is a writable `int64_t` that the multimap cursor's own validity predicate
depends on. The repository design forbids sparse labels from entering the cursor contract, and the C
port is the only one of the nine that leaks them. Treat every field of a cursor as private: reading
or assigning one is outside the supported contract, and plain struct assignment produces a
double-owner that double-frees. Closing the leak requires an opaque handle plus test rework and has
not been done.

### Complexity

Let `w <= 7` be CHAMP depth, `c` an equal-full-hash collision scan, `n` the set or map size, and `P`
the multimap pair count.

| Operation | Set and map | Multimap |
| --- | --- | --- |
| `_cursor_valid`, `_cursor_count`, `_cursor_position`, `_cursor_is_at_start`, `_cursor_is_at_end` | O(1) | O(1) |
| `_cursor_try_peek_previous`, `_cursor_try_peek_next` | O(log n) | O(P) |
| `_cursor_move_previous`, `_cursor_move_next`, `_cursor_seek` | one snapshot clone | one snapshot clone, O(P) |
| `get_cursor_at_item` / `get_cursor_at_key` | O(w + c + log² n) plus a clone | — |
| `get_cursor_at_pair` / `get_cursor_at_group` | — | O(w + c) on a miss; O(P) on a hit |
| insertion and deletion | ordinary operation plus a clone | O(P) |
| `_cursor_snapshot` | one clone | one clone |

Three costs deserve emphasis because a reader would assume otherwise.

`_cursor_seek`, `_cursor_move_next`, and `_cursor_move_previous` are **not** O(1) integer rewrites.
Each constructs its result through `get_cursor`, which performs a full handle clone of the snapshot.
Self-aliased `_cursor_clone`, and `_cursor_seek` to the current position, short-circuit before any
allocation; nothing else does. Peeks are the only genuinely cheap navigation on the set and map.

Keyed cursor creation on the set and map is **O(w + c + log² n)**, not O(w + c + log n). The stamp
lower bound is a binary search of O(log n) iterations, and each iteration reads a positional entry
through a FingerTree descent that is itself O(log n). This is the true cost of the shared
stamp-location tier, and it is inherited by `index_of` and by every path that resolves a class to a
position.

The multimap cursor is a **flat global pair rank** with no prefix sum over group sizes. Resolving a
rank runs the complete grouped flattening, and because the visitor callback has no stop channel the
traversal continues after the target is found: peeking rank 0 of a million-pair multimap visits all
million pairs. A complete traversal by move-plus-peek is O(P²), and `try_add` performs two full scans
plus a clone, because it rescans the published result to recover the inserted rank. This flat shape
is a recorded deviation from the design's nested group-focus multimap state, shared by all nine
ports; it needs one contract decision rather than nine local patches.

A residual failure mode is documented rather than fixed: the multimap group copy zeroes its
destination before cloning, so a failed clone yields a wholly uninitialized group that `destroy`
correctly rejects instead of raw heap bytes released as pointers. Because the underlying `ft_copy_fn`
signature has no failure channel, that failure is still silent — an allocation failure during a
multimap clone produces a structurally valid multimap with a missing group and an inconsistent
`pair_count`. A fallible copy callback is the real fix.

These are asymptotic capability contracts, not benchmark claims.
