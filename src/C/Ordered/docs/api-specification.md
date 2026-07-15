# C Persistent Ordered Set API Specification

- Status: Normative current API and behavior specification
- Created (UTC): 2026-07-15T09:00:00Z
- Repository HEAD: 2d75a79feb424f4476ec32c2d6e4f19263441bf3
- Audience: C consumers, maintainers, reviewers, and sibling-port authors
- Scope: `tds_ordered_set` in `tools/data_structures/ordered/ordered_set.h`

## Ownership and policy

`tds_ordered_set` is a persistent value handle. A successful initializer or operation publishes one
owned handle; use `tds_ordered_set_clone` for another owner, `tds_ordered_set_move` to transfer one,
and `tds_ordered_set_destroy` exactly once per initialized owner. Operations that return a set take
an uninitialized result distinct from every input. They build unpublished foundation versions and
write the result only after all fallible work succeeds.

`tds_ordered_policy` retains an `ft_value_type`, hash callback, optional equality callback, and
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
| index lookup | O(w + c + log n) |
| end or positional edit with a label gap | O(w + c + log n) |
| edit requiring relabel | O(n (w + c)) for that produced version |
| successful removal | O(w + c + log n); miss O(w + c) |
| range | O(log n) sequence split plus O(min(kept, removed) (w + c)) reconciliation |
| reverse | O(n (w + c)) |
| stable sort | O(n log n) comparisons plus O(n (w + c)) rebuild |
| algebra | conservative O((n + m) (w + c + log(n + m + 1))) |
| relations | O((n + m) (w + c)) after normalization |

These are asymptotic capability contracts, not benchmark claims. No amortization crosses two
persistent branches that independently relabel.
