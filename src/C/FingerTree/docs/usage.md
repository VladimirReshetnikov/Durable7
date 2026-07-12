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
#include <tools/data_structures/finger_tree/canonical_sorted_set.h>
#include <tools/data_structures/finger_tree/daba_lite.h>
#include <tools/data_structures/finger_tree/rrb_vector.h>
```

The workspace builds a static C library through the CMake presets documented in
[validation.md](validation.md). Sample executables under [`samples`](../samples/) are also registered
as CTest smoke tests. The canonical set's cryptographic rank policy links Windows CNG (`bcrypt`) on
Windows and OpenSSL Crypto on other hosts; CMake supplies that transitive link dependency.

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

Self-owned facade structs such as `ft_sorted_map`, `ft_rope`, `ft_measured_rope`,
`ft_priority_queue`, `ft_interval_tree_i64`, `ft_interval_tree`, and `ft_text_rope`
embed callback policy state that the nested tree points at. When replacing one of those facade
variables with a successful update result, dispose the old value and use the matching `ft_*_move`
helper instead of plain C assignment:

```c
ft_priority_queue next;
status = ft_priority_queue_push(&queue, &value, &priority, &next);
if (status == FT_STATUS_OK) {
    ft_priority_queue_dispose(&queue);
    ft_priority_queue_move(&queue, &next);
}
```

The move helper transfers ownership to the destination, rebases the internal policy pointers, and
zeros the source so accidental later disposal of the moved-from value is harmless. The destination
should be uninitialized or already disposed before the move.

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

## Canonical Zip-Zip Sorted Set

Use `ft_canonical_sorted_set` when set topology must be determined by a policy rather than update
history. Its callbacks are independently fallible, and the set retains a reference-counted policy
handle. This minimal integer policy uses a public seed so builds and test runs reproduce the same
shape:

```c
static ft_status compare_ints(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    int a = *(const int*)left;
    int b = *(const int*)right;
    (void)context;
    *comparison = (a > b) - (a < b);
    return FT_STATUS_OK;
}

static ft_status rank_hash_int(
    const void* value,
    uint64_t* rank_hash,
    void* context)
{
    uint64_t bits = (uint32_t)*(const int*)value;
    (void)context;
    *rank_hash = bits * UINT64_C(0x9e3779b97f4a7c15);
    return FT_STATUS_OK;
}

ft_canonical_policy_config config;
static const unsigned char int_value_type_identity = 0;
ft_canonical_policy_config_init(
    &config,
    sizeof(int),
    &int_value_type_identity,
    compare_ints,
    rank_hash_int,
    NULL);

ft_canonical_policy policy;
ft_status status = ft_canonical_policy_create_seeded(
    &policy, &config, UINT64_C(0x0123456789abcdef));
if (status != FT_STATUS_OK) {
    return status;
}

ft_canonical_sorted_set set;
status = ft_canonical_sorted_set_init(&set, &policy);
if (status != FT_STATUS_OK) {
    ft_canonical_policy_dispose(&policy);
    return status;
}

/* The set retained the policy; this handle is no longer needed. */
ft_canonical_policy_dispose(&policy);

int value = 42;
status = ft_canonical_sorted_set_add(&set, &value, &set); /* exact aliasing is supported */
if (status == FT_STATUS_OK) {
    bool found = false;
    status = ft_canonical_sorted_set_contains(&set, &value, &found);
}

ft_canonical_sorted_set_dispose(&set);
return status;
```

Prefer `ft_canonical_policy_create_random` when untrusted inputs should not predict tree priorities.
Use `create_seeded` for reproducible public topology, or `create_keyed` with a caller-owned secret of
at least 32 bytes when topology must be reproducible only for holders of that key. Keyed creation
copies the bytes; the source buffer may be cleared immediately afterward. Seeded ranks follow the
cross-port `ZZT2` derivation exactly. The comparer defines equality, so comparer-equal values must
produce the same rank hash. Bulk `from_array` construction is insertion-order independent in shape
and preserves the first input representative of each equality class.

The non-null value-type identity argument is a pointer tag, not data consumed by the library. Use one
stable object address for every policy whose stored representation may safely be passed to the same
family of callbacks; a file-scope static object is the usual choice. Cross-policy equality and relation
queries reject mismatched tags before invoking receiver callbacks, even when both values have the same
byte size. The tag object must outlive every policy and set that references it.

When a value copy callback is present, it constructs a new owned value in uninitialized destination
storage. On failure it must leave that storage uninitialized and ownership-free. Compare and rank
callbacks have the same status-returning shape; the library publishes no result until every required
allocation and callback has succeeded. The configured allocator must return storage with fundamental
C alignment. Contexts, callbacks, and allocator hooks remain caller-owned, must outlive all policies
and sets that retain them, and must not reenter an operation in flight on the same policy/set handles.

Union, intersection, and difference require handles derived from the same policy identity. Copy the
policy handle when independently constructing compatible operands; recreating the same seed produces
the same ranks but deliberately not the same algebra identity. Equality and all subset, superset, and
overlap queries accept different policy identities with matching value-type tags by normalizing the
other operand with the receiver's policy. A tag mismatch returns `FT_STATUS_INCOMPATIBLE_POLICY`.
If the two comparators define different equality classes, these queries are receiver-relative and may
be asymmetric.

`ft_canonical_sorted_set_try_get_ref` returns a borrowed representative. Do not modify or destroy it,
and retain the source set version for the entire borrow. Content hashes and identity/sharing/shape
members are representation diagnostics, not serialized cryptographic identities. `validate` checks
the full tree and reports structural failure with `valid == false`; ordinary callback, allocation,
and crypto failures remain explicit `ft_status` values.

## DABA Lite Sliding Aggregate

Use `ft_daba_lite` for a mutable FIFO window whose values form a monoid. The value type and monoid
measure size must match because the queue stores monoid values directly:

```c
static void sum_identity(void* destination, void* context)
{
    (void)context;
    *(long long*)destination = 0;
}

static void sum_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(long long*)destination = *(const long long*)value;
}

static void sum_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    *(long long*)destination = *(const long long*)left + *(const long long*)right;
}

ft_value_type value_type;
ft_value_type_init(&value_type, sizeof(long long));

ft_measure_policy monoid = {
    sizeof(long long), sum_identity, sum_measure, sum_combine, NULL
};
ft_daba_policy policy;
ft_daba_policy_init(&policy, &value_type, &monoid);

ft_daba_lite daba;
ft_status status = ft_daba_lite_create(&daba, &policy);
if (status != FT_STATUS_OK) {
    return status;
}

long long first = 10;
long long second = 20;
status = ft_daba_lite_insert(&daba, &first);
if (status == FT_STATUS_OK)
    status = ft_daba_lite_insert(&daba, &second);

long long aggregate = 0;
if (status == FT_STATUS_OK)
    status = ft_daba_lite_aggregate(&daba, &aggregate); /* 30 */
if (status == FT_STATUS_OK)
    status = ft_daba_lite_evict(&daba);

ft_daba_lite_destroy(&daba);
return status;
```

The policy and contexts must outlive the handle. Do not copy the handle; use `ft_daba_lite_move` when
relocating ownership. The instance is mutable and not thread-safe, and it intentionally has no
iteration API because reversal slots may hold partial aggregates. Insert, eviction, and nonempty
query call combine at most 3, 2, and 1 times. Allocation failures are state-atomic. Callbacks have no
failure return in the shared C policy vocabulary, must return normally, and must not recursively
query or mutate the same handle while an operation is in flight.

An aggregate destination is uninitialized owned storage. For a nontrivial `ft_value_type`, call its
destroy callback on the returned value after use. `ft_daba_lite_clear` is O(n + c): native C must
destroy n owned slot values and release c blocks before returning, even though each ordinary slide is
worst-case O(1). `ft_daba_lite_validate` is callback-free and returns region/block/slack statistics.

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

## RRB Vector And Append Builder

Choose `ft_rrb_vector` when uniform random access and dense 32-way storage matter more than the
deque's endpoint constants. The RRB policy adds semantic equality and allocator callbacks to the
ordinary value copy/destroy policy:

```c
static bool equal_int(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

ft_value_type int_type;
ft_value_type_init(&int_type, sizeof(int));
ft_rrb_policy rrb_policy;
ft_rrb_policy_init(&rrb_policy, &int_type, equal_int, NULL);

ft_rrb_builder builder;
ft_status status = ft_rrb_builder_init(&builder, &rrb_policy);
for (int value = 0; status == FT_STATUS_OK && value != 1000; ++value) {
    status = ft_rrb_builder_append(&builder, &value);
}

ft_rrb_vector vector;
if (status == FT_STATUS_OK) {
    status = ft_rrb_builder_to_vector(&builder, &vector);
}
ft_rrb_builder_dispose(&builder);
if (status != FT_STATUS_OK) {
    return status;
}

int replacement = 42;
status = ft_rrb_vector_set(&vector, 500, &replacement, &vector); /* Alias-safe. */
ft_rrb_vector_dispose(&vector);
return status;
```

The default policy allocator uses `malloc`/`free`; replace both callbacks together before creating
handles when an arena, quota, or deterministic failpoint allocator is required. Concatenation
requires policy pointer identity. Dispose every successfully initialized vector and builder while
its policy and callback contexts are still alive.

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
    ft_priority_queue_move(&queue, &next);
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
    ft_priority_queue_move(&queue, &rest);
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
    ft_interval_tree_i64_move(&intervals, &next);
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
    size_t round_trip;
    status = ft_text_rope_offset_of(&rope, location.line, location.column, &round_trip);
}

ft_text_rope edited;
if (status == FT_STATUS_OK) {
    status = ft_text_rope_insert_char(&rope, 0, '#', &edited);
}

if (status == FT_STATUS_OK) {
    ft_text_rope_dispose(&rope);
    ft_text_rope_move(&rope, &edited);
}

ft_text_rope_dispose(&rope);
return status;
```

`ft_text_rope_line_count` follows the current text-rope facade semantics tested by the C workspace:
an empty trailing line after a final newline is counted. `ft_text_rope_line_of_offset` and
`ft_text_rope_line_start_offset` expose the two component navigations directly. `ft_text_rope_offset_of`
accepts a column equal to a non-final line's character length (the terminating newline's offset) and rejects
columns beyond that boundary.

## Concurrency And Lifetime

The data structures are immutable after publication, and internal reps use atomic reference counts.
Independently held handles may be read, copied, updated into new handles, and disposed concurrently
when each thread owns or synchronizes access to the handle object it mutates or disposes.

Do not have two threads concurrently write, dispose, or replace the same C handle variable without
external synchronization. The library protects shared immutable reps, not unsynchronized mutation of
your local `ft_*` structs. User value-copy/destroy, measure, predicate, comparison, and visitor callbacks must
also be safe for the concurrent call pattern the application permits.

The canonical set additionally publishes lazy content digests atomically. Two readers may perform
the same uncached digest work, but neither observes a partial digest. Its callbacks and allocator are
also reachable from logically read-only operations and therefore must be thread-safe for any permitted
parallel use. Reentrancy into an in-flight canonical operation through the same policy or set handles,
including from a destroy callback during disposal, is unsupported.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Persistent indexed sequence with endpoint edits | `ft_persistent_deque` |
| Custom monoid measure, measure-guided locate, or split | `ft_tree` |
| O(1) logical reverse over a persistent sequence | `ft_reversible_deque` |
| Unique sorted values | `ft_sorted_set` |
| Canonical keyed topology, reproducible shape, or persistent set algebra | `ft_canonical_sorted_set` |
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
