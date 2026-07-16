#include <Tools/DataStructures/Hamt/persistent_hash_multimap.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct tds_hamt_multimap_context {
    atomic_size_t references;
    tds_hamt_set_policy key_policy;
    tds_hamt_set_policy value_policy;
} tds_hamt_multimap_context;

static uint32_t tds_hamt_multimap_key_hash(const void* item, void* raw_context)
{
    tds_hamt_multimap_context* context = (tds_hamt_multimap_context*)raw_context;
    return context->key_policy.hash(item, context->key_policy.context);
}

static bool tds_hamt_multimap_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    tds_hamt_multimap_context* context = (tds_hamt_multimap_context*)raw_context;
    return context->key_policy.equal(left, right, context->key_policy.context);
}

static void* tds_hamt_multimap_key_retain(const void* item, void* raw_context)
{
    tds_hamt_multimap_context* context = (tds_hamt_multimap_context*)raw_context;
    return context->key_policy.retain_item(item, context->key_policy.context);
}

static void tds_hamt_multimap_key_release(void* item, void* raw_context)
{
    tds_hamt_multimap_context* context = (tds_hamt_multimap_context*)raw_context;
    context->key_policy.release_item(item, context->key_policy.context);
}

static bool tds_hamt_multimap_group_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    (void)raw_context;
    return left != NULL && right != NULL
        && tds_hamt_set_shares_root(
            (const tds_hamt_set*)left,
            (const tds_hamt_set*)right);
}

static void* tds_hamt_multimap_group_retain(const void* value, void* raw_context)
{
    (void)raw_context;
    if (value == NULL) {
        return NULL;
    }
    tds_hamt_set* copy = (tds_hamt_set*)malloc(sizeof(*copy));
    if (copy != NULL) {
        *copy = tds_hamt_set_clone((const tds_hamt_set*)value);
    }
    return copy;
}

static void tds_hamt_multimap_group_release(void* value, void* raw_context)
{
    (void)raw_context;
    if (value != NULL) {
        tds_hamt_set_destroy((tds_hamt_set*)value);
        free(value);
    }
}

static tds_hamt_policy tds_hamt_multimap_group_policy(
    tds_hamt_multimap_context* context)
{
    tds_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = tds_hamt_multimap_key_hash;
    policy.key_equal = tds_hamt_multimap_key_equal;
    policy.value_equal = tds_hamt_multimap_group_equal;
    policy.retain_key = tds_hamt_multimap_key_retain;
    policy.retain_value = tds_hamt_multimap_group_retain;
    policy.release_key = tds_hamt_multimap_key_release;
    policy.release_value = tds_hamt_multimap_group_release;
    policy.context = context;
    return policy;
}

static void tds_hamt_multimap_context_retain(tds_hamt_multimap_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void tds_hamt_multimap_context_release(tds_hamt_multimap_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static bool tds_hamt_multimap_valid(const tds_hamt_multimap* map)
{
    return map != NULL && map->context != NULL && map->pair_count >= 0;
}

static void tds_hamt_multimap_publish(
    const tds_hamt_multimap* source,
    tds_hamt_multimap* result,
    tds_hamt_multimap* candidate)
{
    if (result == source) {
        tds_hamt_multimap_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static tds_hamt_status tds_hamt_multimap_publish_clone(
    const tds_hamt_multimap* source,
    tds_hamt_multimap* result)
{
    tds_hamt_multimap candidate;
    const tds_hamt_status status =
        tds_hamt_multimap_clone(source, &candidate);
    if (status == TDS_HAMT_OK) {
        tds_hamt_multimap_publish(source, result, &candidate);
    }
    return status;
}

tds_hamt_status tds_hamt_multimap_init(
    tds_hamt_multimap* map,
    const tds_hamt_set_policy* key_policy,
    const tds_hamt_set_policy* value_policy)
{
    if (map == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_multimap_context* context =
        (tds_hamt_multimap_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    atomic_init(&context->references, 1u);
    context->key_policy = key_policy == NULL
        ? tds_hamt_set_policy_default()
        : *key_policy;
    context->value_policy = value_policy == NULL
        ? tds_hamt_set_policy_default()
        : *value_policy;

    const tds_hamt_policy policy = tds_hamt_multimap_group_policy(context);
    map->groups = tds_hamt_map_create(&policy);
    map->pair_count = 0;
    map->context = context;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_multimap_clone(
    const tds_hamt_multimap* source,
    tds_hamt_multimap* destination)
{
    if (!tds_hamt_multimap_valid(source) || destination == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return TDS_HAMT_OK;
    }
    destination->groups = tds_hamt_map_clone(&source->groups);
    destination->pair_count = source->pair_count;
    destination->context = source->context;
    tds_hamt_multimap_context_retain(destination->context);
    return TDS_HAMT_OK;
}

void tds_hamt_multimap_move(
    tds_hamt_multimap* destination,
    tds_hamt_multimap* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void tds_hamt_multimap_destroy(tds_hamt_multimap* map)
{
    if (map == NULL) {
        return;
    }
    tds_hamt_map_destroy(&map->groups);
    tds_hamt_multimap_context_release(map->context);
    (void)memset(map, 0, sizeof(*map));
}

size_t tds_hamt_multimap_key_count(const tds_hamt_multimap* map)
{
    return tds_hamt_multimap_valid(map)
        ? tds_hamt_map_count(&map->groups)
        : 0u;
}

int64_t tds_hamt_multimap_pair_count(const tds_hamt_multimap* map)
{
    return tds_hamt_multimap_valid(map) ? map->pair_count : 0;
}

bool tds_hamt_multimap_empty(const tds_hamt_multimap* map)
{
    return tds_hamt_multimap_pair_count(map) == 0;
}

bool tds_hamt_multimap_contains_key(
    const tds_hamt_multimap* map,
    const void* key)
{
    return tds_hamt_multimap_valid(map)
        && tds_hamt_map_contains_key(&map->groups, key);
}

bool tds_hamt_multimap_try_get_values(
    const tds_hamt_multimap* map,
    const void* key,
    const tds_hamt_set** values)
{
    if (values != NULL) {
        *values = NULL;
    }
    if (!tds_hamt_multimap_valid(map)) {
        return false;
    }
    const void* found = NULL;
    const bool present = tds_hamt_map_try_get(&map->groups, key, &found);
    if (present && values != NULL) {
        *values = (const tds_hamt_set*)found;
    }
    return present;
}

bool tds_hamt_multimap_contains(
    const tds_hamt_multimap* map,
    const void* key,
    const void* value)
{
    const tds_hamt_set* values = NULL;
    return tds_hamt_multimap_try_get_values(map, key, &values)
        && values != NULL
        && tds_hamt_set_contains(values, value);
}

bool tds_hamt_multimap_try_get_key(
    const tds_hamt_multimap* map,
    const void* equal_key,
    const void** actual_key)
{
    if (actual_key != NULL) {
        *actual_key = NULL;
    }
    return tds_hamt_multimap_valid(map)
        && tds_hamt_map_try_get_key(&map->groups, equal_key, actual_key);
}

tds_hamt_status tds_hamt_multimap_add(
    const tds_hamt_multimap* map,
    const void* key,
    const void* value,
    tds_hamt_multimap* result)
{
    if (!tds_hamt_multimap_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_set* stored = NULL;
    tds_hamt_set values;
    bool added = false;
    tds_hamt_status status;
    if (tds_hamt_multimap_try_get_values(map, key, &stored)) {
        status = tds_hamt_set_try_add(stored, value, &values, &added);
    } else {
        values = tds_hamt_set_create(&map->context->value_policy);
        tds_hamt_set inserted;
        status = tds_hamt_set_try_add(&values, value, &inserted, &added);
        tds_hamt_set_destroy(&values);
        if (status == TDS_HAMT_OK) {
            values = inserted;
        }
    }
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (!added) {
        tds_hamt_set_destroy(&values);
        return tds_hamt_multimap_publish_clone(map, result);
    }
    if (map->pair_count == INT64_MAX) {
        tds_hamt_set_destroy(&values);
        return TDS_HAMT_OVERFLOW;
    }

    tds_hamt_map groups;
    status = stored == NULL
        ? tds_hamt_map_add(&map->groups, key, &values, &groups)
        : tds_hamt_map_set(&map->groups, key, &values, &groups);
    tds_hamt_set_destroy(&values);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = map->pair_count + 1;
    candidate.context = map->context;
    tds_hamt_multimap_context_retain(candidate.context);
    tds_hamt_multimap_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_multimap_remove(
    const tds_hamt_multimap* map,
    const void* key,
    const void* value,
    tds_hamt_multimap* result)
{
    if (!tds_hamt_multimap_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_set* stored = NULL;
    if (!tds_hamt_multimap_try_get_values(map, key, &stored)) {
        return tds_hamt_multimap_publish_clone(map, result);
    }

    tds_hamt_set values;
    bool removed = false;
    tds_hamt_status status =
        tds_hamt_set_try_remove(stored, value, &values, &removed);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (!removed) {
        tds_hamt_set_destroy(&values);
        return tds_hamt_multimap_publish_clone(map, result);
    }

    tds_hamt_map groups;
    status = tds_hamt_set_is_empty(&values)
        ? tds_hamt_map_remove(&map->groups, key, &groups)
        : tds_hamt_map_set(&map->groups, key, &values, &groups);
    tds_hamt_set_destroy(&values);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = map->pair_count - 1;
    candidate.context = map->context;
    tds_hamt_multimap_context_retain(candidate.context);
    tds_hamt_multimap_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_multimap_remove_key(
    const tds_hamt_multimap* map,
    const void* key,
    tds_hamt_multimap* result)
{
    if (!tds_hamt_multimap_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_set* stored = NULL;
    if (!tds_hamt_multimap_try_get_values(map, key, &stored)) {
        return tds_hamt_multimap_publish_clone(map, result);
    }
    const size_t removed_count = tds_hamt_set_count(stored);
    if (removed_count > (size_t)map->pair_count) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map groups;
    const tds_hamt_status status =
        tds_hamt_map_remove(&map->groups, key, &groups);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = map->pair_count - (int64_t)removed_count;
    candidate.context = map->context;
    tds_hamt_multimap_context_retain(candidate.context);
    tds_hamt_multimap_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_multimap_clear(
    const tds_hamt_multimap* map,
    tds_hamt_multimap* result)
{
    if (!tds_hamt_multimap_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_multimap_empty(map)) {
        return tds_hamt_multimap_publish_clone(map, result);
    }
    tds_hamt_map groups;
    const tds_hamt_status status = tds_hamt_map_clear(&map->groups, &groups);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = 0;
    candidate.context = map->context;
    tds_hamt_multimap_context_retain(candidate.context);
    tds_hamt_multimap_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_multimap_visit(
    const tds_hamt_multimap* map,
    tds_hamt_multimap_visit_fn visitor,
    void* context)
{
    if (!tds_hamt_multimap_valid(map) || visitor == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map_iterator groups;
    tds_hamt_map_iterator_init(&map->groups, &groups);
    const void* key = NULL;
    const void* raw_values = NULL;
    while (tds_hamt_map_iterator_next(&groups, &key, &raw_values)) {
        const tds_hamt_set* values = (const tds_hamt_set*)raw_values;
        tds_hamt_set_iterator iterator;
        tds_hamt_set_iterator_init(values, &iterator);
        const void* value = NULL;
        while (tds_hamt_set_iterator_next(&iterator, &value)) {
            visitor(key, value, context);
        }
    }
    return TDS_HAMT_OK;
}

bool tds_hamt_multimap_debug_validate(const tds_hamt_multimap* map)
{
    if (!tds_hamt_multimap_valid(map)
        || !tds_hamt_map_debug_validate_canonical(&map->groups)) {
        return false;
    }
    int64_t pairs = 0;
    tds_hamt_map_iterator groups;
    tds_hamt_map_iterator_init(&map->groups, &groups);
    const void* key = NULL;
    const void* raw_values = NULL;
    while (tds_hamt_map_iterator_next(&groups, &key, &raw_values)) {
        (void)key;
        const tds_hamt_set* values = (const tds_hamt_set*)raw_values;
        const size_t count = tds_hamt_set_count(values);
        if (values == NULL || count == 0u
            || !tds_hamt_map_debug_validate_canonical(&values->map)
            || count > (size_t)(INT64_MAX - pairs)) {
            return false;
        }
        pairs += (int64_t)count;
    }
    return pairs == map->pair_count;
}

bool tds_hamt_multimap_debug_shares_root(
    const tds_hamt_multimap* left,
    const tds_hamt_multimap* right)
{
    return tds_hamt_multimap_valid(left)
        && tds_hamt_multimap_valid(right)
        && tds_hamt_map_shares_root(&left->groups, &right->groups);
}
