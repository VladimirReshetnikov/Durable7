# HAMT C Usage Guide

- Created (UTC): 2026-07-02T20:07:09Z
- Repository HEAD: c58fc1159beb94e985ca66861bdc2ed3767eb2da
- Audience: C consumers and maintainers using `tds_hamt_map` and `tds_hamt_set`
- Scope: Public include path, policy setup, ownership rules, persistent update patterns, iteration, and set algebra

This guide is the practical companion to the [C API specification](api-specification.md). The public
declarations live in [`hamt.h`](../include/Tools/DataStructures/Hamt/hamt.h).

## Include And Link

```c
#include <Tools/DataStructures/Hamt/hamt.h>
#include <Tools/DataStructures/Hamt/patricia.h>
```

The workspace builds a static library and test executable through `build.ps1`:

```powershell
.\build.ps1 -RunTests
```

## Status And Lifetime Pattern

Most update operations return `tds_hamt_status`. Treat `TDS_HAMT_OK` as the only success value.
Maps and sets are small value structs, but assignment does not retain the root. Use `clone` when two
live values should share one version, and call `destroy` for every initialized map or set.

For "replace the current snapshot" updates:

```c
tds_hamt_map next;
tds_hamt_status status = tds_hamt_map_set(&map, key, value, &next);
if (status != TDS_HAMT_OK) {
    tds_hamt_map_destroy(&map);
    return status;
}

tds_hamt_map_destroy(&map);
map = next;
```

For retained snapshots:

```c
tds_hamt_map snapshot = tds_hamt_map_clone(&map);

/* Use map and snapshot independently. */

tds_hamt_map_destroy(&snapshot);
tds_hamt_map_destroy(&map);
```

Do not copy a live map or set with plain assignment unless you are moving ownership from one local
variable to another and will destroy it only once.

## Policies

The default map and set policies hash and compare pointer identity and store borrowed pointers. For
value semantics, supply hash/equality callbacks:

```c
static uint32_t int_hash(const void* item, void* context)
{
    (void)context;
    uint32_t value = (uint32_t)*(const int*)item;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    return value;
}

static bool int_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

tds_hamt_policy policy = tds_hamt_policy_default();
policy.hash = int_hash;
policy.key_equal = int_equal;
policy.value_equal = int_equal;
```

Use retain/release callbacks when keys, values, or set items need owned lifetime management. With
null retain/release callbacks, the collection stores the pointer values it is given and does not free
or copy pointed-to data. Any callback context pointer must remain valid for every map or set version
created with that policy. Retain callbacks cannot report allocation failure through
`TDS_HAMT_OUT_OF_MEMORY`; if they allocate, they must either succeed or use a caller-defined fatal or
non-local error policy. Returning `NULL` stores `NULL` as the retained payload.

Hash/equality callbacks must obey the normal hash-table contract: equivalent keys or items must
produce equal 32-bit hash values.

## Persistent Map

```c
tds_hamt_map map = tds_hamt_map_create(&policy);

int key1 = 1;
int value1 = 10;
tds_hamt_map one;
tds_hamt_status status = tds_hamt_map_set(&map, &key1, &value1, &one);
if (status != TDS_HAMT_OK) {
    tds_hamt_map_destroy(&map);
    return status;
}

const void* found = NULL;
if (tds_hamt_map_try_get(&one, &key1, &found)) {
    int value = *(const int*)found;
    (void)value;
}

tds_hamt_map_destroy(&one);
tds_hamt_map_destroy(&map);
```

Use `tds_hamt_map_set` for add-or-replace and `tds_hamt_map_add` for duplicate-rejecting insert.
`tds_hamt_map_try_add` reports duplicates without treating them as errors:

```c
bool added = false;
tds_hamt_map result;
tds_hamt_status status = tds_hamt_map_try_add(&map, key, value, &result, &added);
```

`tds_hamt_map_try_remove` reports whether anything was removed and returns the stored value pointer
through an out parameter:

```c
bool removed = false;
const void* removed_value = NULL;
tds_hamt_map without_key;
tds_hamt_status status = tds_hamt_map_try_remove(
    &map,
    key,
    &without_key,
    &removed,
    &removed_value);
```

The removed value pointer is governed by the source map's lifetime and policy; the call does not
transfer ownership. When `result` aliases the source map the previous version is released inside
the call, so the removed value pointer is reported as `NULL`; use a distinct `result` value when
the removed value is needed.

## Bulk Updates

Bulk map updates use arrays of `tds_hamt_entry` and last-wins semantics:

```c
tds_hamt_entry entries[] = {
    { key1, value1 },
    { key2, value2 },
    { key1, newer_value1 },
};

tds_hamt_map updated;
tds_hamt_status status = tds_hamt_map_set_many(&map, entries, 3, &updated);
```

`tds_hamt_map_create_range` builds a map from scratch with the same last-wins behavior.

## Stored Equivalent Keys

When an update uses a key that is equivalent under the policy, the map keeps the originally stored
key pointer. Use `tds_hamt_map_try_get_key` to recover it:

```c
const void* actual_key = NULL;
if (tds_hamt_map_try_get_key(&map, query_key, &actual_key)) {
    /* actual_key points at the stored equivalent key. */
}
```

The set equivalent is `tds_hamt_set_try_get_value`.

## Iteration

Enumeration order follows the HAMT shape. It is stable for an unchanged version but is not insertion
order or sorted order.

```c
tds_hamt_map_iterator iterator;
tds_hamt_map_iterator_init(&map, &iterator);

const void* key = NULL;
const void* value = NULL;
while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
    /* Inspect key and value. */
}
```

Iterators do not retain the map. Keep the source map alive while iterating. A copied iterator
advances independently.

## Persistent Set

```c
tds_hamt_set_policy set_policy = tds_hamt_set_policy_default();
set_policy.hash = int_hash;
set_policy.equal = int_equal;

tds_hamt_set set = tds_hamt_set_create(&set_policy);

int item = 42;
tds_hamt_set next;
tds_hamt_status status = tds_hamt_set_add(&set, &item, &next);
if (status == TDS_HAMT_OK) {
    tds_hamt_set_destroy(&set);
    set = next;
}

bool present = tds_hamt_set_contains(&set, &item);
(void)present;

tds_hamt_set_destroy(&set);
return status;
```

`tds_hamt_set_add` is idempotent and returns a root-sharing result when the item is already present.
Use `tds_hamt_set_try_add` when the caller needs to know whether membership changed.

## Set Algebra

Set algebra APIs take arrays of item pointers:

```c
const void* right[] = { item1, item2, item3 };

tds_hamt_set unioned;
tds_hamt_status status = tds_hamt_set_union_many(&set, right, 3, &unioned);
```

Available operations:

- `tds_hamt_set_union_many`
- `tds_hamt_set_intersect_many`
- `tds_hamt_set_except_many`
- `tds_hamt_set_symmetric_except_many`
- subset/superset, overlap, and equality predicates over `*_many` inputs

Operations that need distinct right-side membership materialize a temporary set under the receiver's
policy. Superset and overlap checks stream the input and can exit early.

## Integer Patricia Maps And Sets

Use the Patricia family when keys are signed 32- or 64-bit integers and ordered traversal or
structural merge matters. Values remain type-erased, but keys do not need hash/equality callbacks:

```c
tds_int_map left = tds_int_map_create(NULL);
tds_int_map right = tds_int_map_create(NULL);

tds_int_map_set(&left, -10, left_value, &left);
tds_int_map_set(&right, -10, right_value, &right);
tds_int_map_set(&right, 20, another_value, &right);

tds_int_map merged;
tds_hamt_status status = tds_int_map_union(&left, &right, &merged); /* right wins at -10 */

tds_int_map_destroy(&merged);
tds_int_map_destroy(&right);
tds_int_map_destroy(&left);
```

`tds_int_map_visit` and `tds_long_map_visit` enumerate in ascending signed-key order. The set
visitors have the same ordering. For overlapping values, `tds_int_map_union_with` /
`tds_long_map_union_with` and the corresponding `intersect_with` operations accept a typed callback
of `(key, left, right, context) -> value`. The returned pointer is retained through the map's
`tds_patricia_value_policy`; it is not implicitly copied by the combining API itself.

## Concurrency And Lineages

HAMT nodes are immutable, so already-retained snapshots may be read concurrently. Retain the required handles
before publishing them to reader threads and keep each handle alive until its reader has stopped using it.

The intrusive reference counts are not atomic. Do not concurrently clone, update, clear, combine, or destroy
versions that share nodes, even when the threads start from different handle variables: persistent siblings
still share untouched paths. Serialize derivation and disposal per structural lineage, then publish completed
snapshots for concurrent read-only use. Policy callbacks and borrowed payload objects must independently obey
the same reader-safety contract.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Borrowed-pointer identity map | `tds_hamt_map_create(NULL)` |
| Value-semantic map over `void*` payloads | `tds_hamt_map` with custom policy callbacks |
| Duplicate-rejecting insert | `tds_hamt_map_add` or `tds_hamt_map_try_add` |
| Stored equivalent key recovery | `tds_hamt_map_try_get_key` |
| Borrowed-pointer identity set | `tds_hamt_set_create(NULL)` |
| Value-semantic set over `void*` payloads | `tds_hamt_set` with custom policy callbacks |
| Set union/intersection/difference | `tds_hamt_set_*_many` APIs |

For cross-language contract alignment, see the repository
[porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
