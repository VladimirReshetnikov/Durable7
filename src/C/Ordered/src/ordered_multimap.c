/*
 * Implementation of the insertion-ordered persistent multimap.
 */

#include <durable7/ordered/ordered_multimap.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct d7_ordered_multimap_context {
    size_t references;
    d7_ordered_policy key_policy;
    d7_ordered_policy value_policy;
    d7_ordered_map_policy group_policy;
} d7_ordered_multimap_context;

static bool d7_ordered_multimap_valid(const d7_ordered_multimap* map)
{
    return map != NULL && map->context != NULL && map->pair_count >= 0;
}

static void d7_ordered_multimap_context_retain(
    d7_ordered_multimap_context* context)
{
    if (context != NULL) {
        ++context->references;
    }
}

static void d7_ordered_multimap_context_release(
    d7_ordered_multimap_context* context)
{
    if (context != NULL && --context->references == 0u) {
        free(context);
    }
}

static void d7_ordered_multimap_group_copy(
    void* destination,
    const void* source,
    void* raw_context)
{
    (void)raw_context;
    /* The ft_copy_fn signature has no failure channel, and d7_ordered_set_clone writes the
     * destination only on success. Zero it first so a failed clone leaves a wholly
     * uninitialized group that d7_ordered_set_destroy rejects, rather than raw heap bytes
     * whose non-NULL field patterns would be released as live pointers. */
    (void)memset(destination, 0, sizeof(d7_ordered_set));
    (void)d7_ordered_set_clone(
        (const d7_ordered_set*)source,
        (d7_ordered_set*)destination);
}

static void d7_ordered_multimap_group_destroy(void* value, void* raw_context)
{
    (void)raw_context;
    d7_ordered_set_destroy((d7_ordered_set*)value);
}

static bool d7_ordered_multimap_group_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    (void)raw_context;
    return d7_ordered_set_debug_shares_index(
        (const d7_ordered_set*)left,
        (const d7_ordered_set*)right);
}

static void d7_ordered_multimap_publish(
    const d7_ordered_multimap* source,
    d7_ordered_multimap* result,
    d7_ordered_multimap* candidate)
{
    if (result == source) {
        d7_ordered_multimap_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static d7_ordered_status d7_ordered_multimap_publish_clone(
    const d7_ordered_multimap* source,
    d7_ordered_multimap* result)
{
    d7_ordered_multimap candidate;
    const d7_ordered_status status =
        d7_ordered_multimap_clone(source, &candidate);
    if (status == D7_ORDERED_OK) {
        d7_ordered_multimap_publish(source, result, &candidate);
    }
    return status;
}

d7_ordered_status d7_ordered_multimap_init(
    d7_ordered_multimap* map,
    const d7_ordered_policy* key_policy,
    const d7_ordered_policy* value_policy)
{
    if (map == NULL || key_policy == NULL || value_policy == NULL
        || key_policy->item_type.size == 0u
        || value_policy->item_type.size == 0u
        || key_policy->hash == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_multimap_context* context =
        (d7_ordered_multimap_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return D7_ORDERED_OUT_OF_MEMORY;
    }
    context->references = 1u;
    context->key_policy = *key_policy;
    context->value_policy = *value_policy;
    ft_value_type group_type;
    ft_value_type_init(&group_type, sizeof(d7_ordered_set));
    group_type.copy = d7_ordered_multimap_group_copy;
    group_type.destroy = d7_ordered_multimap_group_destroy;
    group_type.context = context;
    d7_ordered_map_policy_init(
        &context->group_policy,
        &key_policy->item_type,
        &group_type,
        key_policy->hash,
        key_policy->equal,
        d7_ordered_multimap_group_equal,
        key_policy->context);
    const d7_ordered_status status =
        d7_ordered_map_init(&map->groups, &context->group_policy);
    if (status != D7_ORDERED_OK) {
        free(context);
        return status;
    }
    map->pair_count = 0;
    map->context = context;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_clone(
    const d7_ordered_multimap* source,
    d7_ordered_multimap* destination)
{
    if (!d7_ordered_multimap_valid(source) || destination == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_ORDERED_OK;
    }
    const d7_ordered_status status =
        d7_ordered_map_clone(&source->groups, &destination->groups);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    destination->pair_count = source->pair_count;
    destination->context = source->context;
    d7_ordered_multimap_context_retain(destination->context);
    return D7_ORDERED_OK;
}

void d7_ordered_multimap_move(
    d7_ordered_multimap* destination,
    d7_ordered_multimap* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void d7_ordered_multimap_destroy(d7_ordered_multimap* map)
{
    if (map != NULL) {
        d7_ordered_map_destroy(&map->groups);
        d7_ordered_multimap_context_release(map->context);
        (void)memset(map, 0, sizeof(*map));
    }
}

size_t d7_ordered_multimap_key_count(const d7_ordered_multimap* map)
{
    return d7_ordered_multimap_valid(map)
        ? d7_ordered_map_size(&map->groups) : 0u;
}

int64_t d7_ordered_multimap_pair_count(const d7_ordered_multimap* map)
{
    return d7_ordered_multimap_valid(map) ? map->pair_count : 0;
}

bool d7_ordered_multimap_empty(const d7_ordered_multimap* map)
{
    return d7_ordered_multimap_pair_count(map) == 0;
}

bool d7_ordered_multimap_contains_key(
    const d7_ordered_multimap* map,
    const void* key)
{
    return d7_ordered_multimap_valid(map)
        && d7_ordered_map_contains_key(&map->groups, key);
}

bool d7_ordered_multimap_try_get_values(
    const d7_ordered_multimap* map,
    const void* key,
    const d7_ordered_set** values)
{
    const void* raw = NULL;
    if (values != NULL) {
        *values = NULL;
    }
    if (!d7_ordered_multimap_valid(map)
        || !d7_ordered_map_try_get(&map->groups, key, NULL, &raw)) {
        return false;
    }
    if (values != NULL) {
        *values = (const d7_ordered_set*)raw;
    }
    return true;
}

bool d7_ordered_multimap_contains(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value)
{
    const d7_ordered_set* values = NULL;
    return d7_ordered_multimap_try_get_values(map, key, &values)
        && d7_ordered_set_contains(values, value);
}

bool d7_ordered_multimap_try_get_key(
    const d7_ordered_multimap* map,
    const void* equal_key,
    const void** actual_key)
{
    const void* value = NULL;
    return d7_ordered_multimap_valid(map)
        && d7_ordered_map_try_get(
            &map->groups, equal_key, actual_key, &value);
}

d7_ordered_status d7_ordered_multimap_add(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    d7_ordered_multimap* result)
{
    if (!d7_ordered_multimap_valid(map) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const d7_ordered_set* stored = NULL;
    d7_ordered_set values;
    d7_ordered_status status;
    if (d7_ordered_multimap_try_get_values(map, key, &stored)) {
        status = d7_ordered_set_add(stored, value, &values);
        if (status != D7_ORDERED_OK) {
            return status;
        }
        if (d7_ordered_set_debug_shares_index(stored, &values)) {
            d7_ordered_set_destroy(&values);
            return d7_ordered_multimap_publish_clone(map, result);
        }
    } else {
        status = d7_ordered_set_init(&values, &map->context->value_policy);
        if (status != D7_ORDERED_OK) {
            return status;
        }
        d7_ordered_set inserted;
        status = d7_ordered_set_add(&values, value, &inserted);
        d7_ordered_set_destroy(&values);
        if (status != D7_ORDERED_OK) {
            return status;
        }
        values = inserted;
    }
    if (map->pair_count == INT64_MAX) {
        d7_ordered_set_destroy(&values);
        return D7_ORDERED_OVERFLOW;
    }
    d7_ordered_map groups;
    status = stored == NULL
        ? d7_ordered_map_add(&map->groups, key, &values, &groups)
        : d7_ordered_map_set(&map->groups, key, &values, &groups);
    d7_ordered_set_destroy(&values);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_ordered_multimap candidate = {
        groups, map->pair_count + 1, map->context };
    d7_ordered_multimap_context_retain(candidate.context);
    d7_ordered_multimap_publish(map, result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_remove(
    const d7_ordered_multimap* map,
    const void* key,
    const void* value,
    d7_ordered_multimap* result)
{
    if (!d7_ordered_multimap_valid(map) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const d7_ordered_set* stored = NULL;
    if (!d7_ordered_multimap_try_get_values(map, key, &stored)) {
        return d7_ordered_multimap_publish_clone(map, result);
    }
    d7_ordered_set values;
    bool removed = false;
    d7_ordered_status status =
        d7_ordered_set_try_remove(stored, value, &removed, &values);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    if (!removed) {
        d7_ordered_set_destroy(&values);
        return d7_ordered_multimap_publish_clone(map, result);
    }
    d7_ordered_map groups;
    status = d7_ordered_set_empty(&values)
        ? d7_ordered_map_remove(&map->groups, key, &groups)
        : d7_ordered_map_set(&map->groups, key, &values, &groups);
    d7_ordered_set_destroy(&values);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_ordered_multimap candidate = {
        groups, map->pair_count - 1, map->context };
    d7_ordered_multimap_context_retain(candidate.context);
    d7_ordered_multimap_publish(map, result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_remove_key(
    const d7_ordered_multimap* map,
    const void* key,
    d7_ordered_multimap* result)
{
    if (!d7_ordered_multimap_valid(map) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const d7_ordered_set* stored = NULL;
    if (!d7_ordered_multimap_try_get_values(map, key, &stored)) {
        return d7_ordered_multimap_publish_clone(map, result);
    }
    const size_t removed = d7_ordered_set_size(stored);
    d7_ordered_map groups;
    const d7_ordered_status status =
        d7_ordered_map_remove(&map->groups, key, &groups);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_ordered_multimap candidate = {
        groups, map->pair_count - (int64_t)removed, map->context };
    d7_ordered_multimap_context_retain(candidate.context);
    d7_ordered_multimap_publish(map, result, &candidate);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_multimap_clear(
    const d7_ordered_multimap* map,
    d7_ordered_multimap* result)
{
    if (!d7_ordered_multimap_valid(map) || result == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (d7_ordered_multimap_empty(map)) {
        return d7_ordered_multimap_publish_clone(map, result);
    }
    d7_ordered_map groups;
    const d7_ordered_status status =
        d7_ordered_map_clear(&map->groups, &groups);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_ordered_multimap candidate = { groups, 0, map->context };
    d7_ordered_multimap_context_retain(candidate.context);
    d7_ordered_multimap_publish(map, result, &candidate);
    return D7_ORDERED_OK;
}

typedef struct d7_ordered_multimap_visit_context {
    d7_ordered_multimap_visit_fn visitor;
    void* visitor_context;
    const void* key;
} d7_ordered_multimap_visit_context;

static void d7_ordered_multimap_visit_value(const void* value, void* raw_context)
{
    d7_ordered_multimap_visit_context* context =
        (d7_ordered_multimap_visit_context*)raw_context;
    context->visitor(context->key, value, context->visitor_context);
}

static void d7_ordered_multimap_visit_group(
    const void* key,
    const void* raw_values,
    void* raw_context)
{
    d7_ordered_multimap_visit_context* context =
        (d7_ordered_multimap_visit_context*)raw_context;
    context->key = key;
    (void)d7_ordered_set_visit(
        (const d7_ordered_set*)raw_values,
        d7_ordered_multimap_visit_value,
        context);
}

d7_ordered_status d7_ordered_multimap_visit(
    const d7_ordered_multimap* map,
    d7_ordered_multimap_visit_fn visitor,
    void* context)
{
    if (!d7_ordered_multimap_valid(map) || visitor == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_multimap_visit_context bridge = {
        visitor, context, NULL };
    return d7_ordered_map_visit(
        &map->groups, d7_ordered_multimap_visit_group, &bridge);
}

static void d7_ordered_multimap_validate_group(
    const void* key,
    const void* raw_values,
    void* raw_context)
{
    (void)key;
    int64_t* pairs = (int64_t*)raw_context;
    const d7_ordered_set* values = (const d7_ordered_set*)raw_values;
    if (d7_ordered_set_empty(values)
        || !d7_ordered_set_debug_validate(values)
        || d7_ordered_set_size(values) > (size_t)(INT64_MAX - *pairs)) {
        *pairs = -1;
    } else {
        *pairs += (int64_t)d7_ordered_set_size(values);
    }
}

bool d7_ordered_multimap_debug_validate(const d7_ordered_multimap* map)
{
    if (!d7_ordered_multimap_valid(map)
        || !d7_ordered_map_debug_validate(&map->groups)) {
        return false;
    }
    int64_t pairs = 0;
    if (d7_ordered_map_visit(
            &map->groups, d7_ordered_multimap_validate_group, &pairs)
        != D7_ORDERED_OK) {
        return false;
    }
    return pairs == map->pair_count;
}

bool d7_ordered_multimap_debug_shares_groups(
    const d7_ordered_multimap* left,
    const d7_ordered_multimap* right)
{
    return d7_ordered_multimap_valid(left)
        && d7_ordered_multimap_valid(right)
        && d7_ordered_map_debug_shares_values(
            &left->groups, &right->groups);
}
