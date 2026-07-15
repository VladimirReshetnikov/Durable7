# HAMT C Usage Guide

- Created (UTC): 2026-07-02T20:07:09Z
- Repository HEAD: c58fc1159beb94e985ca66861bdc2ed3767eb2da
- Audience: C consumers and maintainers using the HAMT, Patricia, and Merkle families
- Scope: Includes, policies, ownership, persistent updates, one-way edit sessions, iteration, set algebra, and persistent hash bags

This guide is the practical companion to the [C API specification](api-specification.md). The public
declarations live in [`hamt.h`](../include/Tools/DataStructures/Hamt/hamt.h),
[`persistent_hash_bag.h`](../include/Tools/DataStructures/Hamt/persistent_hash_bag.h), and the
family-specific headers shown below.

## Include And Link

```c
#include <Tools/DataStructures/Hamt/hamt.h>
#include <Tools/DataStructures/Hamt/persistent_hash_bag.h>
#include <Tools/DataStructures/Hamt/patricia.h>
#include <Tools/DataStructures/Hamt/merkle_search_tree.h>
```

For the content-addressed ordered map, configure a `tds_merkle_policy_config`, create one owning
policy handle, then derive tree versions. Built-in helpers cover canonical signed integers,
nullable UTF-8/bytes, and RFC 4122 GUIDs:

```c
static int key_tag;
static int value_tag;

tds_merkle_policy_config config;
tds_merkle_policy_config_init(&config);
config.policy_id = (tds_merkle_identifier){
    (const unsigned char *)"example-int-map-v1",
    sizeof("example-int-map-v1") - 1};
tds_merkle_i32_type_policy_init(&config.key_type, &key_tag);
tds_merkle_i32_type_policy_init(&config.value_type, &value_tag);
config.key_compare = compare_i32; /* fallible three-way comparator */
tds_merkle_i32_codec_init(&config.key_codec);
tds_merkle_i32_codec_init(&config.value_codec);

tds_merkle_policy policy = {0};
tds_merkle_search_tree tree = {0};
if (tds_merkle_policy_create(&config, &policy) == TDS_MERKLE_OK &&
    tds_merkle_search_tree_init(&tree, &policy) == TDS_MERKLE_OK) {
    int32_t key = 42;
    int32_t value = 100;
    (void)tds_merkle_search_tree_set(&tree, &key, &value, &tree);
}
tds_merkle_search_tree_dispose(&tree);
tds_merkle_policy_dispose(&policy);
```

Do not assign live policy/tree handles to create a second owner; use `copy`, `move`, and `dispose`.
See the [Merkle specification](merkle-search-tree.md) for two-pass codecs, exact wire framing,
compatibility layers, verified persistence, proofs, synchronization, merge, and failure-atomic
publication.

To save and verified-load the current root with the synchronized in-memory store:

```c
tds_merkle_memory_block_store memory = {0};
tds_merkle_block_store store;
tds_merkle_search_tree loaded = {0};
tds_merkle_verification_error verification;
size_t added = 0;

if (tds_merkle_memory_block_store_init(&memory, NULL) == TDS_MERKLE_OK &&
    tds_merkle_memory_block_store_as_store(&memory, &store) == TDS_MERKLE_OK &&
    tds_merkle_search_tree_save(&tree, &store, &added, &verification) == TDS_MERKLE_OK) {
    tds_merkle_digest root = tds_merkle_search_tree_root_hash(&tree);
    (void)tds_merkle_search_tree_load(
        root,
        &policy,
        &store,
        NULL, /* default seven-field verification budget */
        &loaded,
        &verification);
}

tds_merkle_search_tree_dispose(&loaded);
tds_merkle_memory_block_store_dispose(&memory);
```

`store` is a borrowed adapter: keep `memory` (or an owning copy of it) alive through every adapter
call. Store lookups return owning `tds_merkle_block` snapshots that must be disposed. For partial
synchronization, repeatedly call `tds_merkle_search_tree_plan_sync`, export the requested digests,
insert those blocks, and repeat until `tds_merkle_sync_plan_requires_blocks` is false.

The workspace compiles and can run its four native test executables through `build.ps1`:

```powershell
.\build.ps1 -RunTests
```

## Status And Lifetime Pattern

Most update operations return `tds_hamt_status`. Treat `TDS_HAMT_OK` as the only success value.
Maps, sets, and bags are small value structs, but assignment does not retain the root. Use `clone`
when two live values should share one version, and call `destroy` for every initialized handle.

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

Do not copy a live map, set, or bag with plain assignment unless you are moving ownership from one
local variable to another and will destroy it only once.

## One-Way Edit Sessions

Use a transient when one logical owner wants a sequence of point edits followed by exactly one
publication. In this C checkpoint, adoption and publication are O(1) handle operations, while each
changed edit still uses the persistent path-copy implementation. The surface provides lifecycle and
semantic parity; it does not claim that an edit sequence is faster than direct persistent updates.

```c
tds_hamt_map_transient edit;
tds_hamt_status status = tds_hamt_map_to_transient(&map, &edit);
if (status != TDS_HAMT_OK) {
    tds_hamt_map_destroy(&map);
    return status;
}

status = tds_hamt_map_transient_set(&edit, key1, value1);
if (status == TDS_HAMT_OK) {
    bool added = false;
    status = tds_hamt_map_transient_try_add(&edit, key2, value2, &added);
}

tds_hamt_map published;
if (status == TDS_HAMT_OK) {
    status = tds_hamt_map_transient_persist(&edit, &published);
}

/* Destroy the session handle even after publication. */
tds_hamt_map_transient_destroy(&edit);
if (status != TDS_HAMT_OK) {
    tds_hamt_map_destroy(&map);
    return status;
}

/* map is the unchanged source snapshot; published owns the edited version. */
tds_hamt_map_destroy(&published);
tds_hamt_map_destroy(&map);
```

`tds_hamt_map_transient_create` and `tds_hamt_set_transient_create` provide empty-session factories.
The set surface uses the analogous `tds_hamt_set_transient_*` functions. Policies and callback
contexts survive create/adopt/edit/persist unchanged, and equivalent updates keep the first stored
key/item representative. Its subset, proper-subset, superset, proper-superset, overlap, and equality
relations accept either `*_many` item arrays or a persistent `tds_hamt_set`; both forms interpret
the right operand under the active session's receiver policy and collapse duplicate input items.

Do not copy a transient handle with assignment. Use `*_transient_clone` when ownership of one
logical session must be represented by multiple explicit handles, then destroy each handle. A
successful `persist` through any clone consumes the state shared by all clones. Later operations
return `TDS_HAMT_TRANSIENT_CONSUMED`; publication cannot be repeated.

Transient iterators are borrowed and version-bound:

```c
tds_hamt_map_transient_iterator iterator;
status = tds_hamt_map_transient_iterator_init(&edit, &iterator);
while (status == TDS_HAMT_OK) {
    bool has_value = false;
    const void *key = NULL;
    const void *value = NULL;
    status = tds_hamt_map_transient_iterator_next(
        &iterator, &has_value, &key, &value);
    if (status != TDS_HAMT_OK || !has_value) {
        break;
    }
    /* Inspect key/value while the session remains alive and unmodified. */
}
```

A changed edit makes older iterators return `TDS_HAMT_TRANSIENT_MODIFIED`. Logical no-ops and failed
edits leave them valid. Publication makes them return `TDS_HAMT_TRANSIENT_CONSUMED`. Keep at least
one owning session handle alive while using an iterator.

All edit outputs and the session itself are failure-atomic. An allocation or retaining-callback
failure leaves content and output flags unchanged, so callers may retry. Passing a null publication
output returns `TDS_HAMT_INVALID_ARGUMENT` without consuming the session. Creation, adoption, and
publication outputs must not already own a live handle.

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
created with that policy. An allocating retain callback reports failure by returning `NULL` for a
non-`NULL` input; the operation returns `TDS_HAMT_OUT_OF_MEMORY` and unwinds every completed retain.
Retaining a `NULL` input may return `NULL` successfully.

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

Use the factory operations when selecting a value depends on presence and the operation must hash
and descend only once. Factories return borrowed candidates; the map retains the selected candidate
under its policy and reports the concrete retained representative:

```c
static tds_hamt_status add_value(
    const void *lookup_key,
    void *context,
    const void **value) {
    (void)lookup_key;
    *value = context;
    return TDS_HAMT_OK;
}

const void *selected = NULL;
tds_hamt_map next;
tds_hamt_status status = tds_hamt_map_get_or_add(
    &map, key, add_value, candidate, &next, &selected);
```

`tds_hamt_map_add_or_update` additionally accepts an update callback receiving the caller's lookup
key and the stored value. It retains the stored key representative. An equal selected value is a
root-sharing no-op and `selected` is the earlier stored value. All callback pointers are validated
before hashing, and failures leave the source and output parameters unchanged.

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
- corresponding two-set operations (`tds_hamt_set_union`, `tds_hamt_set_intersect`,
  `tds_hamt_set_except`, and `tds_hamt_set_symmetric_except`) and relation predicates, which use
  structural CHAMP combination for callback-compatible operands

Operations that need distinct right-side membership materialize a temporary set under the receiver's
policy. Superset and overlap checks stream the input and can exit early.

## Persistent Hash Bag

Use `tds_hamt_bag` when each receiver-policy item class needs a positive occurrence count. The
default policy uses pointer identity; a custom `tds_hamt_set_policy` supplies value hashing,
equality, and item ownership in exactly the same way as `tds_hamt_set`. The bag owns its internal
count values independently of the item policy.

```c
int apple = 1;
int pear = 2;

tds_hamt_bag stock = tds_hamt_bag_create(NULL);
tds_hamt_status status =
    tds_hamt_bag_add_copies(&stock, &apple, 3, &stock);
if (status == TDS_HAMT_OK) {
    status = tds_hamt_bag_add(&stock, &pear, &stock);
}

if (status == TDS_HAMT_OK) {
    int32_t apples = tds_hamt_bag_count_of(&stock, &apple); /* 3 */
    int64_t all_items = tds_hamt_bag_total_count(&stock);   /* 4 */
    size_t kinds = tds_hamt_bag_distinct_count(&stock);     /* 2 */
    (void)apples;
    (void)all_items;
    (void)kinds;
}

tds_hamt_bag_destroy(&stock);
```

The iterator and algebra fragments below are independent; their named bag handles are assumed to
have been initialized successfully and to remain live for the fragment.

A result may alias its input, as above. On failure the aliased bag remains unchanged and still owns
its original handle. A distinct result must be uninitialized rather than a live bag. Destroy it
only after a successful call. Zero additions and removals return a root-sharing handle without
hashing; negative or greater-than-`INT32_MAX` copy requests return
`TDS_HAMT_INVALID_ARGUMENT` before callbacks. `TDS_HAMT_OVERFLOW` reports checked per-class,
expanded-total, or foreign-policy-collapse overflow.

The first equivalent item supplied under a custom policy remains the stored representative. Recover
it with `tds_hamt_bag_try_get_value`; as with map/set lookup, the output echoes the query pointer on
a miss. `tds_hamt_bag_try_get_entry` returns the same representative together with its copied
`int32_t` multiplicity.

For any live bag, three iterator surfaces separate expanded and distinct work:

```c
tds_hamt_bag_entry_iterator entries;
tds_hamt_bag_entry_iterator_init(&stock, &entries);
tds_hamt_bag_entry entry;
while (tds_hamt_bag_entry_iterator_next(&entries, &entry)) {
    /* entry.item occurs entry.count times. */
}

tds_hamt_bag_iterator expanded;
tds_hamt_bag_iterator_init(&stock, &expanded);
const void *item = NULL;
while (tds_hamt_bag_iterator_next(&expanded, &item)) {
    /* One callback-free step per occurrence. */
}
```

Initialize iterators only while the source is live, and keep it live through traversal. Copying an
iterator value creates an independently advancing cursor. Use
`tds_hamt_bag_distinct_iterator_init` / `next` when only one representative per class is needed.

Given live `stock` and `incoming` bags, algebra always returns a bag under the receiver's policy:

```c
tds_hamt_bag combined;
status = tds_hamt_bag_union(&stock, &incoming, &combined); /* per-class max */
if (status == TDS_HAMT_OK) {
    /* Use combined. */
    tds_hamt_bag_destroy(&combined);
}
```

`intersect` takes per-class minima, `except` performs saturated receiver-minus-argument
subtraction, and `sum` performs checked addition. A policy-incompatible argument is fully and
eagerly normalized under the receiver policy before any no-op shortcut, so collapsed classes,
retaining-callback failures, and collapse overflow are observable. Receiver representatives win
surviving receiver classes. There is deliberately no public bag builder or transient session; use
`create_range` for array construction and persistent point/algebra operations thereafter.

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
| One-way map edit session | `tds_hamt_map_to_transient` / `tds_hamt_map_transient_persist` |
| Borrowed-pointer identity set | `tds_hamt_set_create(NULL)` |
| Value-semantic set over `void*` payloads | `tds_hamt_set` with custom policy callbacks |
| One-way set edit session | `tds_hamt_set_to_transient` / `tds_hamt_set_transient_persist` |
| Set union/intersection/difference | `tds_hamt_set_*_many` APIs |
| Persistent unordered multiset | `tds_hamt_bag` |
| Expanded/distinct/bag-entry traversal | `tds_hamt_bag_iterator`, `tds_hamt_bag_distinct_iterator`, or `tds_hamt_bag_entry_iterator` |
| Bag max/min/difference/checked sum | `tds_hamt_bag_union` / `intersect` / `except` / `sum` |

For cross-language contract alignment, see the repository
[porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
