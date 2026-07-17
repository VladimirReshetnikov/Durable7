#include <tools/data_structures/ordered/ordered_multimap.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct tds_ordered_multimap_context {
    size_t references;
    tds_ordered_policy key_policy;
    tds_ordered_policy value_policy;
    tds_ordered_map_policy group_policy;
} tds_ordered_multimap_context;

static bool tds_ordered_multimap_valid(const tds_ordered_multimap* map)
{
    return map != NULL && map->context != NULL && map->pair_count >= 0;
}

static void tds_ordered_multimap_context_retain(
    tds_ordered_multimap_context* context)
{
    if (context != NULL) {
        ++context->references;
    }
}

static void tds_ordered_multimap_context_release(
    tds_ordered_multimap_context* context)
{
    if (context != NULL && --context->references == 0u) {
        free(context);
    }
}

static void tds_ordered_multimap_group_copy(
    void* destination,
    const void* source,
    void* raw_context)
{
    (void)raw_context;
    (void)tds_ordered_set_clone(
        (const tds_ordered_set*)source,
        (tds_ordered_set*)destination);
}

static void tds_ordered_multimap_group_destroy(void* value, void* raw_context)
{
    (void)raw_context;
    tds_ordered_set_destroy((tds_ordered_set*)value);
}

static bool tds_ordered_multimap_group_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    (void)raw_context;
    return tds_ordered_set_debug_shares_index(
        (const tds_ordered_set*)left,
        (const tds_ordered_set*)right);
}

static void tds_ordered_multimap_publish(
    const tds_ordered_multimap* source,
    tds_ordered_multimap* result,
    tds_ordered_multimap* candidate)
{
    if (result == source) {
        tds_ordered_multimap_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static tds_ordered_status tds_ordered_multimap_publish_clone(
    const tds_ordered_multimap* source,
    tds_ordered_multimap* result)
{
    tds_ordered_multimap candidate;
    const tds_ordered_status status =
        tds_ordered_multimap_clone(source, &candidate);
    if (status == TDS_ORDERED_OK) {
        tds_ordered_multimap_publish(source, result, &candidate);
    }
    return status;
}

tds_ordered_status tds_ordered_multimap_init(
    tds_ordered_multimap* map,
    const tds_ordered_policy* key_policy,
    const tds_ordered_policy* value_policy)
{
    if (map == NULL || key_policy == NULL || value_policy == NULL
        || key_policy->item_type.size == 0u
        || value_policy->item_type.size == 0u
        || key_policy->hash == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_multimap_context* context =
        (tds_ordered_multimap_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return TDS_ORDERED_OUT_OF_MEMORY;
    }
    context->references = 1u;
    context->key_policy = *key_policy;
    context->value_policy = *value_policy;
    ft_value_type group_type;
    ft_value_type_init(&group_type, sizeof(tds_ordered_set));
    group_type.copy = tds_ordered_multimap_group_copy;
    group_type.destroy = tds_ordered_multimap_group_destroy;
    group_type.context = context;
    tds_ordered_map_policy_init(
        &context->group_policy,
        &key_policy->item_type,
        &group_type,
        key_policy->hash,
        key_policy->equal,
        tds_ordered_multimap_group_equal,
        key_policy->context);
    const tds_ordered_status status =
        tds_ordered_map_init(&map->groups, &context->group_policy);
    if (status != TDS_ORDERED_OK) {
        free(context);
        return status;
    }
    map->pair_count = 0;
    map->context = context;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_multimap_clone(
    const tds_ordered_multimap* source,
    tds_ordered_multimap* destination)
{
    if (!tds_ordered_multimap_valid(source) || destination == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return TDS_ORDERED_OK;
    }
    const tds_ordered_status status =
        tds_ordered_map_clone(&source->groups, &destination->groups);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    destination->pair_count = source->pair_count;
    destination->context = source->context;
    tds_ordered_multimap_context_retain(destination->context);
    return TDS_ORDERED_OK;
}

void tds_ordered_multimap_move(
    tds_ordered_multimap* destination,
    tds_ordered_multimap* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void tds_ordered_multimap_destroy(tds_ordered_multimap* map)
{
    if (map != NULL) {
        tds_ordered_map_destroy(&map->groups);
        tds_ordered_multimap_context_release(map->context);
        (void)memset(map, 0, sizeof(*map));
    }
}

size_t tds_ordered_multimap_key_count(const tds_ordered_multimap* map)
{
    return tds_ordered_multimap_valid(map)
        ? tds_ordered_map_size(&map->groups) : 0u;
}

int64_t tds_ordered_multimap_pair_count(const tds_ordered_multimap* map)
{
    return tds_ordered_multimap_valid(map) ? map->pair_count : 0;
}

bool tds_ordered_multimap_empty(const tds_ordered_multimap* map)
{
    return tds_ordered_multimap_pair_count(map) == 0;
}

bool tds_ordered_multimap_contains_key(
    const tds_ordered_multimap* map,
    const void* key)
{
    return tds_ordered_multimap_valid(map)
        && tds_ordered_map_contains_key(&map->groups, key);
}

bool tds_ordered_multimap_try_get_values(
    const tds_ordered_multimap* map,
    const void* key,
    const tds_ordered_set** values)
{
    const void* raw = NULL;
    if (values != NULL) {
        *values = NULL;
    }
    if (!tds_ordered_multimap_valid(map)
        || !tds_ordered_map_try_get(&map->groups, key, NULL, &raw)) {
        return false;
    }
    if (values != NULL) {
        *values = (const tds_ordered_set*)raw;
    }
    return true;
}

bool tds_ordered_multimap_contains(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value)
{
    const tds_ordered_set* values = NULL;
    return tds_ordered_multimap_try_get_values(map, key, &values)
        && tds_ordered_set_contains(values, value);
}

bool tds_ordered_multimap_try_get_key(
    const tds_ordered_multimap* map,
    const void* equal_key,
    const void** actual_key)
{
    const void* value = NULL;
    return tds_ordered_multimap_valid(map)
        && tds_ordered_map_try_get(
            &map->groups, equal_key, actual_key, &value);
}

tds_ordered_status tds_ordered_multimap_add(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value,
    tds_ordered_multimap* result)
{
    if (!tds_ordered_multimap_valid(map) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const tds_ordered_set* stored = NULL;
    tds_ordered_set values;
    tds_ordered_status status;
    if (tds_ordered_multimap_try_get_values(map, key, &stored)) {
        status = tds_ordered_set_add(stored, value, &values);
        if (status != TDS_ORDERED_OK) {
            return status;
        }
        if (tds_ordered_set_debug_shares_index(stored, &values)) {
            tds_ordered_set_destroy(&values);
            return tds_ordered_multimap_publish_clone(map, result);
        }
    } else {
        status = tds_ordered_set_init(&values, &map->context->value_policy);
        if (status != TDS_ORDERED_OK) {
            return status;
        }
        tds_ordered_set inserted;
        status = tds_ordered_set_add(&values, value, &inserted);
        tds_ordered_set_destroy(&values);
        if (status != TDS_ORDERED_OK) {
            return status;
        }
        values = inserted;
    }
    if (map->pair_count == INT64_MAX) {
        tds_ordered_set_destroy(&values);
        return TDS_ORDERED_OVERFLOW;
    }
    tds_ordered_map groups;
    status = stored == NULL
        ? tds_ordered_map_add(&map->groups, key, &values, &groups)
        : tds_ordered_map_set(&map->groups, key, &values, &groups);
    tds_ordered_set_destroy(&values);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_ordered_multimap candidate = {
        groups, map->pair_count + 1, map->context };
    tds_ordered_multimap_context_retain(candidate.context);
    tds_ordered_multimap_publish(map, result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_multimap_remove(
    const tds_ordered_multimap* map,
    const void* key,
    const void* value,
    tds_ordered_multimap* result)
{
    if (!tds_ordered_multimap_valid(map) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const tds_ordered_set* stored = NULL;
    if (!tds_ordered_multimap_try_get_values(map, key, &stored)) {
        return tds_ordered_multimap_publish_clone(map, result);
    }
    tds_ordered_set values;
    bool removed = false;
    tds_ordered_status status =
        tds_ordered_set_try_remove(stored, value, &removed, &values);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    if (!removed) {
        tds_ordered_set_destroy(&values);
        return tds_ordered_multimap_publish_clone(map, result);
    }
    tds_ordered_map groups;
    status = tds_ordered_set_empty(&values)
        ? tds_ordered_map_remove(&map->groups, key, &groups)
        : tds_ordered_map_set(&map->groups, key, &values, &groups);
    tds_ordered_set_destroy(&values);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_ordered_multimap candidate = {
        groups, map->pair_count - 1, map->context };
    tds_ordered_multimap_context_retain(candidate.context);
    tds_ordered_multimap_publish(map, result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_multimap_remove_key(
    const tds_ordered_multimap* map,
    const void* key,
    tds_ordered_multimap* result)
{
    if (!tds_ordered_multimap_valid(map) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const tds_ordered_set* stored = NULL;
    if (!tds_ordered_multimap_try_get_values(map, key, &stored)) {
        return tds_ordered_multimap_publish_clone(map, result);
    }
    const size_t removed = tds_ordered_set_size(stored);
    tds_ordered_map groups;
    const tds_ordered_status status =
        tds_ordered_map_remove(&map->groups, key, &groups);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_ordered_multimap candidate = {
        groups, map->pair_count - (int64_t)removed, map->context };
    tds_ordered_multimap_context_retain(candidate.context);
    tds_ordered_multimap_publish(map, result, &candidate);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_multimap_clear(
    const tds_ordered_multimap* map,
    tds_ordered_multimap* result)
{
    if (!tds_ordered_multimap_valid(map) || result == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (tds_ordered_multimap_empty(map)) {
        return tds_ordered_multimap_publish_clone(map, result);
    }
    tds_ordered_map groups;
    const tds_ordered_status status =
        tds_ordered_map_clear(&map->groups, &groups);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_ordered_multimap candidate = { groups, 0, map->context };
    tds_ordered_multimap_context_retain(candidate.context);
    tds_ordered_multimap_publish(map, result, &candidate);
    return TDS_ORDERED_OK;
}

typedef struct tds_ordered_multimap_visit_context {
    tds_ordered_multimap_visit_fn visitor;
    void* visitor_context;
    const void* key;
} tds_ordered_multimap_visit_context;

static void tds_ordered_multimap_visit_value(const void* value, void* raw_context)
{
    tds_ordered_multimap_visit_context* context =
        (tds_ordered_multimap_visit_context*)raw_context;
    context->visitor(context->key, value, context->visitor_context);
}

static void tds_ordered_multimap_visit_group(
    const void* key,
    const void* raw_values,
    void* raw_context)
{
    tds_ordered_multimap_visit_context* context =
        (tds_ordered_multimap_visit_context*)raw_context;
    context->key = key;
    (void)tds_ordered_set_visit(
        (const tds_ordered_set*)raw_values,
        tds_ordered_multimap_visit_value,
        context);
}

tds_ordered_status tds_ordered_multimap_visit(
    const tds_ordered_multimap* map,
    tds_ordered_multimap_visit_fn visitor,
    void* context)
{
    if (!tds_ordered_multimap_valid(map) || visitor == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_multimap_visit_context bridge = {
        visitor, context, NULL };
    return tds_ordered_map_visit(
        &map->groups, tds_ordered_multimap_visit_group, &bridge);
}

static void tds_ordered_multimap_validate_group(
    const void* key,
    const void* raw_values,
    void* raw_context)
{
    (void)key;
    int64_t* pairs = (int64_t*)raw_context;
    const tds_ordered_set* values = (const tds_ordered_set*)raw_values;
    if (tds_ordered_set_empty(values)
        || !tds_ordered_set_debug_validate(values)
        || tds_ordered_set_size(values) > (size_t)(INT64_MAX - *pairs)) {
        *pairs = -1;
    } else {
        *pairs += (int64_t)tds_ordered_set_size(values);
    }
}

bool tds_ordered_multimap_debug_validate(const tds_ordered_multimap* map)
{
    if (!tds_ordered_multimap_valid(map)
        || !tds_ordered_map_debug_validate(&map->groups)) {
        return false;
    }
    int64_t pairs = 0;
    if (tds_ordered_map_visit(
            &map->groups, tds_ordered_multimap_validate_group, &pairs)
        != TDS_ORDERED_OK) {
        return false;
    }
    return pairs == map->pair_count;
}

bool tds_ordered_multimap_debug_shares_groups(
    const tds_ordered_multimap* left,
    const tds_ordered_multimap* right)
{
    return tds_ordered_multimap_valid(left)
        && tds_ordered_multimap_valid(right)
        && tds_ordered_map_debug_shares_values(
            &left->groups, &right->groups);
}
