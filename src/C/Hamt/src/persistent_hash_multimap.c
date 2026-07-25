#include <durable7/hamt/persistent_hash_multimap.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct d7_hamt_multimap_context {
    atomic_size_t references;
    d7_hamt_set_policy key_policy;
    d7_hamt_set_policy value_policy;
} d7_hamt_multimap_context;

static uint32_t d7_hamt_multimap_key_hash(const void* item, void* raw_context)
{
    d7_hamt_multimap_context* context = (d7_hamt_multimap_context*)raw_context;
    return context->key_policy.hash(item, context->key_policy.context);
}

static bool d7_hamt_multimap_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    d7_hamt_multimap_context* context = (d7_hamt_multimap_context*)raw_context;
    return context->key_policy.equal(left, right, context->key_policy.context);
}

static void* d7_hamt_multimap_key_retain(const void* item, void* raw_context)
{
    d7_hamt_multimap_context* context = (d7_hamt_multimap_context*)raw_context;
    return context->key_policy.retain_item(item, context->key_policy.context);
}

static void d7_hamt_multimap_key_release(void* item, void* raw_context)
{
    d7_hamt_multimap_context* context = (d7_hamt_multimap_context*)raw_context;
    context->key_policy.release_item(item, context->key_policy.context);
}

static bool d7_hamt_multimap_group_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    (void)raw_context;
    return left != NULL && right != NULL
        && d7_hamt_set_shares_root(
            (const d7_hamt_set*)left,
            (const d7_hamt_set*)right);
}

static void* d7_hamt_multimap_group_retain(const void* value, void* raw_context)
{
    (void)raw_context;
    if (value == NULL) {
        return NULL;
    }
    d7_hamt_set* copy = (d7_hamt_set*)malloc(sizeof(*copy));
    if (copy != NULL) {
        *copy = d7_hamt_set_clone((const d7_hamt_set*)value);
    }
    return copy;
}

static void d7_hamt_multimap_group_release(void* value, void* raw_context)
{
    (void)raw_context;
    if (value != NULL) {
        d7_hamt_set_destroy((d7_hamt_set*)value);
        free(value);
    }
}

static d7_hamt_policy d7_hamt_multimap_group_policy(
    d7_hamt_multimap_context* context)
{
    d7_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_multimap_key_hash;
    policy.key_equal = d7_hamt_multimap_key_equal;
    policy.value_equal = d7_hamt_multimap_group_equal;
    policy.retain_key = d7_hamt_multimap_key_retain;
    policy.retain_value = d7_hamt_multimap_group_retain;
    policy.release_key = d7_hamt_multimap_key_release;
    policy.release_value = d7_hamt_multimap_group_release;
    policy.context = context;
    return policy;
}

static void d7_hamt_multimap_context_retain(d7_hamt_multimap_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void d7_hamt_multimap_context_release(d7_hamt_multimap_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static bool d7_hamt_multimap_valid(const d7_hamt_multimap* map)
{
    return map != NULL && map->context != NULL && map->pair_count >= 0;
}

static void d7_hamt_multimap_publish(
    const d7_hamt_multimap* source,
    d7_hamt_multimap* result,
    d7_hamt_multimap* candidate)
{
    if (result == source) {
        d7_hamt_multimap_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static d7_hamt_status d7_hamt_multimap_publish_clone(
    const d7_hamt_multimap* source,
    d7_hamt_multimap* result)
{
    d7_hamt_multimap candidate;
    const d7_hamt_status status =
        d7_hamt_multimap_clone(source, &candidate);
    if (status == D7_HAMT_OK) {
        d7_hamt_multimap_publish(source, result, &candidate);
    }
    return status;
}

d7_hamt_status d7_hamt_multimap_init(
    d7_hamt_multimap* map,
    const d7_hamt_set_policy* key_policy,
    const d7_hamt_set_policy* value_policy)
{
    if (map == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_multimap_context* context =
        (d7_hamt_multimap_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return D7_HAMT_OUT_OF_MEMORY;
    }
    atomic_init(&context->references, 1u);
    context->key_policy = key_policy == NULL
        ? d7_hamt_set_policy_default()
        : *key_policy;
    context->value_policy = value_policy == NULL
        ? d7_hamt_set_policy_default()
        : *value_policy;

    const d7_hamt_policy policy = d7_hamt_multimap_group_policy(context);
    map->groups = d7_hamt_map_create(&policy);
    map->pair_count = 0;
    map->context = context;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_multimap_clone(
    const d7_hamt_multimap* source,
    d7_hamt_multimap* destination)
{
    if (!d7_hamt_multimap_valid(source) || destination == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_HAMT_OK;
    }
    destination->groups = d7_hamt_map_clone(&source->groups);
    destination->pair_count = source->pair_count;
    destination->context = source->context;
    d7_hamt_multimap_context_retain(destination->context);
    return D7_HAMT_OK;
}

void d7_hamt_multimap_move(
    d7_hamt_multimap* destination,
    d7_hamt_multimap* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void d7_hamt_multimap_destroy(d7_hamt_multimap* map)
{
    if (map == NULL) {
        return;
    }
    d7_hamt_map_destroy(&map->groups);
    d7_hamt_multimap_context_release(map->context);
    (void)memset(map, 0, sizeof(*map));
}

size_t d7_hamt_multimap_key_count(const d7_hamt_multimap* map)
{
    return d7_hamt_multimap_valid(map)
        ? d7_hamt_map_count(&map->groups)
        : 0u;
}

int64_t d7_hamt_multimap_pair_count(const d7_hamt_multimap* map)
{
    return d7_hamt_multimap_valid(map) ? map->pair_count : 0;
}

bool d7_hamt_multimap_empty(const d7_hamt_multimap* map)
{
    return d7_hamt_multimap_pair_count(map) == 0;
}

bool d7_hamt_multimap_contains_key(
    const d7_hamt_multimap* map,
    const void* key)
{
    return d7_hamt_multimap_valid(map)
        && d7_hamt_map_contains_key(&map->groups, key);
}

bool d7_hamt_multimap_try_get_values(
    const d7_hamt_multimap* map,
    const void* key,
    const d7_hamt_set** values)
{
    if (values != NULL) {
        *values = NULL;
    }
    if (!d7_hamt_multimap_valid(map)) {
        return false;
    }
    const void* found = NULL;
    const bool present = d7_hamt_map_try_get(&map->groups, key, &found);
    if (present && values != NULL) {
        *values = (const d7_hamt_set*)found;
    }
    return present;
}

bool d7_hamt_multimap_contains(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value)
{
    const d7_hamt_set* values = NULL;
    return d7_hamt_multimap_try_get_values(map, key, &values)
        && values != NULL
        && d7_hamt_set_contains(values, value);
}

bool d7_hamt_multimap_try_get_key(
    const d7_hamt_multimap* map,
    const void* equal_key,
    const void** actual_key)
{
    if (actual_key != NULL) {
        *actual_key = NULL;
    }
    return d7_hamt_multimap_valid(map)
        && d7_hamt_map_try_get_key(&map->groups, equal_key, actual_key);
}

d7_hamt_status d7_hamt_multimap_add(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value,
    d7_hamt_multimap* result)
{
    if (!d7_hamt_multimap_valid(map) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const d7_hamt_set* stored = NULL;
    d7_hamt_set values;
    bool added = false;
    d7_hamt_status status;
    if (d7_hamt_multimap_try_get_values(map, key, &stored)) {
        status = d7_hamt_set_try_add(stored, value, &values, &added);
    } else {
        values = d7_hamt_set_create(&map->context->value_policy);
        d7_hamt_set inserted;
        status = d7_hamt_set_try_add(&values, value, &inserted, &added);
        d7_hamt_set_destroy(&values);
        if (status == D7_HAMT_OK) {
            values = inserted;
        }
    }
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (!added) {
        d7_hamt_set_destroy(&values);
        return d7_hamt_multimap_publish_clone(map, result);
    }
    if (map->pair_count == INT64_MAX) {
        d7_hamt_set_destroy(&values);
        return D7_HAMT_OVERFLOW;
    }

    d7_hamt_map groups;
    status = stored == NULL
        ? d7_hamt_map_add(&map->groups, key, &values, &groups)
        : d7_hamt_map_set(&map->groups, key, &values, &groups);
    d7_hamt_set_destroy(&values);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = map->pair_count + 1;
    candidate.context = map->context;
    d7_hamt_multimap_context_retain(candidate.context);
    d7_hamt_multimap_publish(map, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_multimap_remove(
    const d7_hamt_multimap* map,
    const void* key,
    const void* value,
    d7_hamt_multimap* result)
{
    if (!d7_hamt_multimap_valid(map) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const d7_hamt_set* stored = NULL;
    if (!d7_hamt_multimap_try_get_values(map, key, &stored)) {
        return d7_hamt_multimap_publish_clone(map, result);
    }

    d7_hamt_set values;
    bool removed = false;
    d7_hamt_status status =
        d7_hamt_set_try_remove(stored, value, &values, &removed);
    if (status != D7_HAMT_OK) {
        return status;
    }
    if (!removed) {
        d7_hamt_set_destroy(&values);
        return d7_hamt_multimap_publish_clone(map, result);
    }

    d7_hamt_map groups;
    status = d7_hamt_set_is_empty(&values)
        ? d7_hamt_map_remove(&map->groups, key, &groups)
        : d7_hamt_map_set(&map->groups, key, &values, &groups);
    d7_hamt_set_destroy(&values);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = map->pair_count - 1;
    candidate.context = map->context;
    d7_hamt_multimap_context_retain(candidate.context);
    d7_hamt_multimap_publish(map, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_multimap_remove_key(
    const d7_hamt_multimap* map,
    const void* key,
    d7_hamt_multimap* result)
{
    if (!d7_hamt_multimap_valid(map) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const d7_hamt_set* stored = NULL;
    if (!d7_hamt_multimap_try_get_values(map, key, &stored)) {
        return d7_hamt_multimap_publish_clone(map, result);
    }
    const size_t removed_count = d7_hamt_set_count(stored);
    if (removed_count > (size_t)map->pair_count) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_map groups;
    const d7_hamt_status status =
        d7_hamt_map_remove(&map->groups, key, &groups);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = map->pair_count - (int64_t)removed_count;
    candidate.context = map->context;
    d7_hamt_multimap_context_retain(candidate.context);
    d7_hamt_multimap_publish(map, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_multimap_clear(
    const d7_hamt_multimap* map,
    d7_hamt_multimap* result)
{
    if (!d7_hamt_multimap_valid(map) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (d7_hamt_multimap_empty(map)) {
        return d7_hamt_multimap_publish_clone(map, result);
    }
    d7_hamt_map groups;
    const d7_hamt_status status = d7_hamt_map_clear(&map->groups, &groups);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_multimap candidate;
    candidate.groups = groups;
    candidate.pair_count = 0;
    candidate.context = map->context;
    d7_hamt_multimap_context_retain(candidate.context);
    d7_hamt_multimap_publish(map, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_multimap_visit(
    const d7_hamt_multimap* map,
    d7_hamt_multimap_visit_fn visitor,
    void* context)
{
    if (!d7_hamt_multimap_valid(map) || visitor == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_map_iterator groups;
    d7_hamt_map_iterator_init(&map->groups, &groups);
    const void* key = NULL;
    const void* raw_values = NULL;
    while (d7_hamt_map_iterator_next(&groups, &key, &raw_values)) {
        const d7_hamt_set* values = (const d7_hamt_set*)raw_values;
        d7_hamt_set_iterator iterator;
        d7_hamt_set_iterator_init(values, &iterator);
        const void* value = NULL;
        while (d7_hamt_set_iterator_next(&iterator, &value)) {
            visitor(key, value, context);
        }
    }
    return D7_HAMT_OK;
}

bool d7_hamt_multimap_debug_validate(const d7_hamt_multimap* map)
{
    if (!d7_hamt_multimap_valid(map)
        || !d7_hamt_map_debug_validate_canonical(&map->groups)) {
        return false;
    }
    int64_t pairs = 0;
    d7_hamt_map_iterator groups;
    d7_hamt_map_iterator_init(&map->groups, &groups);
    const void* key = NULL;
    const void* raw_values = NULL;
    while (d7_hamt_map_iterator_next(&groups, &key, &raw_values)) {
        (void)key;
        const d7_hamt_set* values = (const d7_hamt_set*)raw_values;
        const size_t count = d7_hamt_set_count(values);
        if (values == NULL || count == 0u
            || !d7_hamt_map_debug_validate_canonical(&values->map)
            || count > (size_t)(INT64_MAX - pairs)) {
            return false;
        }
        pairs += (int64_t)count;
    }
    return pairs == map->pair_count;
}

bool d7_hamt_multimap_debug_shares_root(
    const d7_hamt_multimap* left,
    const d7_hamt_multimap* right)
{
    return d7_hamt_multimap_valid(left)
        && d7_hamt_multimap_valid(right)
        && d7_hamt_map_shares_root(&left->groups, &right->groups);
}
