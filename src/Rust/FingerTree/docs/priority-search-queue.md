# Rust Priority-Search Queue

- Created (UTC): 2026-07-12T03:50:36Z
- Repository HEAD: 3990536c5a80891074610b1d7d7921d7a2656bc0
- Audience: Maintainers and reviewers of the Rust priority-core port
- Scope: `PrioritySearchQueue<K, P, V>` semantics, representation, ownership, complexity, and evidence

`PrioritySearchQueue<K, P, V>` is a persistent ordered map augmented with a subtree priority
winner. It is a separate direct AVL core rather than an adapter over `SortedMap` or the measured
`PriorityQueue`: the cached winner supports O(1) global minimum and pruning by both key range and
priority threshold.

## Semantics and operation bounds

The key `OrderPolicy<K>` defines both navigation and key equivalence. There is one entry per
equivalence class. The first concrete key representative is retained through every replacement;
priority and payload are last-wins. The priority `OrderPolicy<P>` selects smaller priorities first.
When priorities compare equal, the retained key that comes first under the key policy wins.

| Operation | Worst-case work | Result |
| --- | --- | --- |
| `minimum` | O(1) | Borrowed winner entry or `None` |
| `contains_key`, `get_entry` | O(log n) | Presence or borrowed entry |
| `set_item`, `try_add` | O(log n) | Persistent queue/result handle |
| `remove`, `try_remove` | O(log n) | Persistent queue/result handle |
| `delete_minimum`, `minimum_view` | O(log n) | Owned entry plus persistent remainder |
| `iter` | O(n) | Borrowed entries in key order |
| `enumerate_at_most` | O(log n + v) | Borrowed entries in key order |
| `validate_structure` | O(n) | Statistics or invariant error |

For range traversal, `v` is the number of nodes whose cached subtree winners do not permit pruning;
it can be n. Both key bounds and the priority threshold are inclusive.

## Exact replacement and representatives

An equivalent-key `set_item` is a structural no-op only when all three checks agree:

1. the priority policy compares incoming and stored priorities equal;
2. ordinary `P: PartialEq` equality reports them equal;
3. ordinary `V: PartialEq` equality reports the payloads equal.

Neither relation alone discards a representation change. These equality bounds are scoped only to
`set_item`. Bulk construction uses an equality-free always-replace path to preserve last-wins
semantics; `try_add` rejects an equivalent key without inspecting priority or payload equality.
Absent removal and duplicate `try_add` reuse the exact root.

## Arc ownership and nullable-equivalent values

Each `PrioritySearchEntry` owns an internal handle whose key, priority, and payload are themselves
`Arc`-retained. Rebuilt AVL nodes clone handles rather than payloads. Borrowed reads and iterators,
owned remove/minimum results, validation, and sharing diagnostics therefore require no component
`Clone` bounds. `key_handle`, `priority_handle`, and `value_handle` expose owned shared component
handles when a caller must retain a representative independently.

This result shape is unambiguous for `Option` components. `get_entry(&None) == Some(entry)` can hold
an entry whose key, priority, and payload are all `None`; outer `None` alone means absence. The same
rule applies to `minimum`, `PrioritySearchRemoveResult::entry`, and minimum views.

## Winner cache and range pruning

Every immutable AVL node caches:

- its entry and left/right child handles;
- subtree count and height;
- an owned handle to the subtree priority-then-key winner.

Path copying rebuilds those fields bottom-up and performs the ordinary four AVL rotation shapes.
`enumerate_at_most` compares its lower and upper key once before returning; an inverted range is an
eager `PrioritySearchRangeError`. The returned iterator borrows the queue and the three query values.
It uses an explicit work stack, visits entries in key order, and skips a complete subtree whenever
that node's cached winner priority is above the threshold.

## Invariants and diagnostics

`validate_structure` uses an explicit worklist to check strict policy-defined key bounds, cached
count and height, balance factors in `[-1, 1]`, and exact winner-handle identity after independent
winner recomputation. Recursive update/removal depth is bounded by the validated AVL height.

`shares_root_with`, `shares_node_for_key`, and `shared_node_count` expose identity-only persistence
evidence. They neither expose node addresses nor permit representation mutation.

## Validation evidence

`tests/priority_search_queue.rs` covers:

- priority-policy equality versus ordinary priority/payload equality and exact root reuse;
- first concrete key identity under case-insensitive equivalence and equivalent-key removal;
- `Option` keys, priorities, and payloads plus component types that deliberately omit `Clone`;
- all AVL rotation/deletion paths and 50,000 ascending inserts with logarithmic height;
- a 20,000-operation deterministic model with retained immutable snapshots;
- exact comparison equations proving root-level impossible-threshold pruning and one-path exact-key
  traversal, plus inclusive mixed-range result order;
- no-op, root, far-subtree, and node-count sharing evidence;
- deterministic priority-then-key tie deletion under descending key and bucketed priority policies;
- `Send + Sync` type assertions and eight concurrent immutable readers.
