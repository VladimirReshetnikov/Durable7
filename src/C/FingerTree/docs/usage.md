# C FingerTree Usage Guide

- Created (UTC): 2026-07-02T20:00:22Z
- Repository HEAD: 5ef7f6073e462bd436719988d3acda7a1fd74a0d
- Audience: C consumers and maintainers using the public `ft_*` API
- Scope: Public API setup, lifetime rules, persistent update patterns, and facade quick starts

This guide is a practical companion to the [API notes](api-notes.md). It shows how to set up value
and measure policies, how to manage persistent handles, and which facade to start with for common
C use cases. The normative public declarations live in
[`fingertree.h`](../include/tools/data_structures/finger_tree/fingertree.h).

## Include And Link

Include the public header:

```c
#include <tools/data_structures/finger_tree/fingertree.h>
```

The workspace builds a static C library through the CMake presets documented in
[validation.md](validation.md). Sample executables under [`samples`](../samples/) are also registered
as CTest smoke tests.

## Status And Cleanup Pattern

Most operations return `ft_status`. Treat `FT_STATUS_OK` as the only success value:

```c
ft_status status = ft_tree_push_back(&tree, &value, &next);
if (status != FT_STATUS_OK) {
    ft_tree_dispose(&tree);
    return status;
}
```

Dispose only values that were successfully initialized or returned by a successful operation. For
persistent updates, the common "replace current snapshot" pattern is:

```c
ft_tree next;
ft_status status = ft_tree_push_back(&tree, &value, &next);
if (status != FT_STATUS_OK) {
    ft_tree_dispose(&tree);
    return status;
}

ft_tree_dispose(&tree);
tree = next;
```

If you want to retain both versions, copy or keep both handles and dispose both later:

```c
ft_tree snapshot;
if (ft_tree_copy(&tree, &snapshot) != FT_STATUS_OK) {
    ft_tree_dispose(&tree);
    return FT_STATUS_NO_MEMORY;
}

/* Use tree and snapshot independently. */

ft_tree_dispose(&snapshot);
ft_tree_dispose(&tree);
```

## Value And Policy Setup

`ft_value_type` describes the element representation. For plain trivially copyable values, initialize
it with the element size:

```c
ft_value_type int_type;
ft_value_type_init(&int_type, sizeof(int));
```

For the size-measured deque/tree family, combine that value type with a size measure:

```c
ft_tree_policy policy;
ft_tree_policy_init_size(&policy, &int_type);
```

Policies supplied to `ft_tree`, `ft_persistent_deque`, `ft_reversible_deque`, `ft_sorted_set`, and
`ft_sorted_multiset` are referenced by the resulting handles. Keep the policy object, and any
callback context it points at, alive until every handle using it has been disposed. Wrappers such as
`ft_sorted_map`, `ft_rope`, `ft_measured_rope`, `ft_priority_queue`, and interval/text facades own
their immediate wrapper policy state, but any callback context pointers you pass still need normal C
lifetime discipline.

Use `copy` and `destroy` callbacks in `ft_value_type` when elements own memory or reference-counted
resources. If the callbacks are null, the library byte-copies values of `size` bytes and does not
perform element cleanup.

## Persistent Deque And Generic Tree

`ft_persistent_deque` is an alias over the size-measured `ft_tree` surface. Use it when you need a
persistent indexed sequence with endpoint operations:

```c
ft_value_type int_type;
ft_value_type_init(&int_type, sizeof(int));

ft_tree_policy policy;
ft_tree_policy_init_size(&policy, &int_type);

ft_persistent_deque deque;
ft_status status = ft_persistent_deque_init(&deque, &policy);
if (status != FT_STATUS_OK) {
    return status;
}

for (int value = 0; value != 4; ++value) {
    ft_persistent_deque next;
    status = ft_persistent_deque_push_back(&deque, &value, &next);
    if (status != FT_STATUS_OK) {
        ft_persistent_deque_dispose(&deque);
        return status;
    }

    ft_persistent_deque_dispose(&deque);
    deque = next;
}

int second = 0;
status = ft_persistent_deque_at(&deque, 1, &second);

int replacement = 42;
ft_persistent_deque updated;
status = ft_persistent_deque_set_at(&deque, 1, &replacement, &updated);
ft_persistent_deque_dispose(&deque);
if (status == FT_STATUS_OK) {
    ft_persistent_deque_dispose(&updated);
}
return status;
```

Use the generic `ft_tree` operations directly when you need a custom monoid measure and
measure-guided `locate` or `split`. A locate operation reports both whether a boundary element was
found and the accumulated measure before that boundary.

## Reversible Deque

Use `ft_reversible_deque` when O(1) logical reversal is part of the endpoint/index/concat workflow. Reversal
mirrors the shared reversible tree root by flipping orientation bits:

```c
ft_reversible_deque reversed;
ft_status status = ft_reversible_deque_reverse(&deque, &reversed);
```

Dispose both handles if you keep both. Endpoint and index operations respect the logical orientation without
reifying the sequence. Concatenation and split also operate through the logical orientation, including when either
operand was previously reversed:

```c
ft_reversible_deque joined;
status = ft_reversible_deque_concat(&reversed_left, &right, &joined);

ft_reversible_deque_split_result split;
status = ft_reversible_deque_split_at(&joined, 4, &split);

ft_reversible_deque_dispose(&split.left);
ft_reversible_deque_dispose(&split.right);
ft_reversible_deque_dispose(&joined);
```

## Sorted Set, Multiset, And Map

Sorted wrappers require a comparator:

```c
static int compare_ints(const void* left, const void* right, void* context)
{
    (void)context;
    int l = *(const int*)left;
    int r = *(const int*)right;
    return (l > r) - (l < r);
}
```

`ft_sorted_set` keeps one representative for each equal value:

```c
ft_sorted_set set;
ft_status status = ft_sorted_set_init(&set, &policy, compare_ints, NULL);
if (status != FT_STATUS_OK) {
    return status;
}

int value = 42;
ft_sorted_set next;
status = ft_sorted_set_add(&set, &value, &next);
if (status == FT_STATUS_OK) {
    ft_sorted_set_dispose(&set);
    set = next;
}

ft_sorted_set_dispose(&set);
return status;
```

Use `ft_sorted_multiset` when duplicates are meaningful. Use `ft_sorted_map` for key/value pairs;
`ft_sorted_map_insert` rejects duplicates with `FT_STATUS_ALREADY_EXISTS`, while
`ft_sorted_map_set` inserts or replaces.

## Priority Queue

`ft_priority_queue` is a persistent minimum-priority queue. The priority comparator determines which
priority drains first, and equal priorities drain in insertion order:

```c
ft_priority_queue queue;
ft_status status = ft_priority_queue_init(&queue, &int_type, &int_type, compare_ints, NULL);
if (status != FT_STATUS_OK) {
    return status;
}

int value = 10;
int priority = 1;
ft_priority_queue next;
status = ft_priority_queue_push(&queue, &value, &priority, &next);
if (status == FT_STATUS_OK) {
    ft_priority_queue_dispose(&queue);
    queue = next;
}

bool found = false;
int popped_value = 0;
int popped_priority = 0;
ft_priority_queue rest;
if (status == FT_STATUS_OK) {
    status = ft_priority_queue_try_pop(&queue, &found, &popped_value, &popped_priority, &rest);
}

if (status == FT_STATUS_OK && found) {
    ft_priority_queue_dispose(&queue);
    queue = rest;
}

ft_priority_queue_dispose(&queue);
return status;
```

## Interval Trees

Use `ft_interval_tree_i64` for signed 64-bit closed intervals:

```c
ft_interval_tree_i64 intervals;
ft_status status = ft_interval_tree_i64_init(&intervals);
if (status != FT_STATUS_OK) {
    return status;
}

ft_interval_i64 interval = { .low = 5, .high = 8 };
ft_interval_tree_i64 next;
status = ft_interval_tree_i64_insert(&intervals, interval, &next);
if (status == FT_STATUS_OK) {
    ft_interval_tree_i64_dispose(&intervals);
    intervals = next;
}

ft_interval_i64 query = { .low = 6, .high = 7 };
size_t overlaps = ft_interval_tree_i64_count_overlaps(&intervals, query);
(void)overlaps;

ft_interval_tree_i64_dispose(&intervals);
return status;
```

Use `ft_interval_tree` for caller-defined endpoint types. Invalid intervals where `high` compares
less than `low` return `FT_STATUS_INVALID_ARGUMENT`.

## Ropes And Text

Use `ft_rope` for persistent chunked positional sequences, `ft_measured_rope` for chunked sequences
with caller-supplied cumulative measures, and `ft_text_rope` for newline-aware character content:

```c
ft_text_rope rope;
ft_status status = ft_text_rope_from_cstr("alpha\nbeta\n", &rope);
if (status != FT_STATUS_OK) {
    return status;
}

ft_line_column location;
status = ft_text_rope_line_column_of(&rope, 8, &location);
if (status == FT_STATUS_OK) {
    /* location.line and location.column are zero-based. */
}

ft_text_rope edited;
if (status == FT_STATUS_OK) {
    status = ft_text_rope_insert_char(&rope, 0, '#', &edited);
}

if (status == FT_STATUS_OK) {
    ft_text_rope_dispose(&rope);
    rope = edited;
}

ft_text_rope_dispose(&rope);
return status;
```

`ft_text_rope_line_count` follows the current text-rope facade semantics tested by the C workspace:
an empty trailing line after a final newline is counted.

## Concurrency And Lifetime

The data structures are immutable after publication, and internal reps use atomic reference counts.
Independently held handles may be read, copied, updated into new handles, and disposed concurrently
when each thread owns or synchronizes access to the handle object it mutates or disposes.

Do not have two threads concurrently write, dispose, or replace the same C handle variable without
external synchronization. The library protects shared immutable reps, not unsynchronized mutation of
your local `ft_*` structs.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Persistent indexed sequence with endpoint edits | `ft_persistent_deque` |
| Custom monoid measure, measure-guided locate, or split | `ft_tree` |
| O(1) logical reverse over a persistent sequence | `ft_reversible_deque` |
| Unique sorted values | `ft_sorted_set` |
| Sorted values with duplicates | `ft_sorted_multiset` |
| Sorted key/value lookup and rank access | `ft_sorted_map` |
| Minimum-priority draining with stable equal priorities | `ft_priority_queue` |
| Closed intervals over `int64_t` endpoints | `ft_interval_tree_i64` |
| Closed intervals over custom endpoint types | `ft_interval_tree` |
| Chunked positional sequence | `ft_rope` |
| Chunked sequence with custom cumulative measure | `ft_measured_rope` |
| Newline-aware character content | `ft_text_rope` |

For coverage details, see [validation.md](validation.md). For cross-language contract alignment, see
the repository [porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
