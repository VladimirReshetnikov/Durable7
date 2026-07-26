/*
 * Implementation of the insertion-ordered persistent map.
 */

#include <durable7/ordered/ordered_map.h>

#include <stdlib.h>
#include <string.h>

struct d7_ordered_map_context {
    size_t ref_count;
    d7_ordered_map_policy policy;
    d7_ordered_policy key_policy;
    d7_hamt_policy value_policy;
};

static bool d7_ordered_map_valid(const d7_ordered_map* map)
{
    return map != NULL && map->context != NULL;
}

static void d7_ordered_map_context_retain(
    struct d7_ordered_map_context* context)
{
    if (context != NULL) {
        ++context->ref_count;
    }
}

static void d7_ordered_map_context_release(
    struct d7_ordered_map_context* context)
{
    if (context != NULL && --context->ref_count == 0u) {
        free(context);
    }
}

static void d7_ordered_map_value_copy(
    const ft_value_type* type,
    void* destination,
    const void* source)
{
    if (type->copy != NULL) {
        type->copy(destination, source, type->context);
    } else {
        (void)memcpy(destination, source, type->size);
    }
}

static void d7_ordered_map_value_destroy(
    const ft_value_type* type,
    void* value)
{
    if (type->destroy != NULL) {
        type->destroy(value, type->context);
    }
}

static bool d7_ordered_map_values_equal(
    const struct d7_ordered_map_context* context,
    const void* left,
    const void* right)
{
    if (left == right) {
        return true;
    }
    if (context->policy.value_equal != NULL) {
        return context->policy.value_equal(
            left, right, context->policy.context);
    }
    return memcmp(left, right, context->policy.value_type.size) == 0;
}

static uint32_t d7_ordered_map_hash(const void* key, void* raw_context)
{
    const struct d7_ordered_map_context* context =
        (const struct d7_ordered_map_context*)raw_context;
    return context->policy.hash(key, context->policy.context);
}

static bool d7_ordered_map_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    const struct d7_ordered_map_context* context =
        (const struct d7_ordered_map_context*)raw_context;
    if (left == right) {
        return true;
    }
    if (context->policy.key_equal != NULL) {
        return context->policy.key_equal(
            left, right, context->policy.context);
    }
    return memcmp(left, right, context->policy.key_type.size) == 0;
}

static bool d7_ordered_map_hamt_value_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    return d7_ordered_map_values_equal(
        (const struct d7_ordered_map_context*)raw_context, left, right);
}

static void* d7_ordered_map_retain_typed(
    const ft_value_type* type,
    const void* value)
{
    void* copy = malloc(type->size);
    if (copy != NULL) {
        d7_ordered_map_value_copy(type, copy, value);
    }
    return copy;
}

static void* d7_ordered_map_retain_key(
    const void* key,
    void* raw_context)
{
    const struct d7_ordered_map_context* context =
        (const struct d7_ordered_map_context*)raw_context;
    return d7_ordered_map_retain_typed(&context->policy.key_type, key);
}

static void* d7_ordered_map_retain_value(
    const void* value,
    void* raw_context)
{
    const struct d7_ordered_map_context* context =
        (const struct d7_ordered_map_context*)raw_context;
    return d7_ordered_map_retain_typed(&context->policy.value_type, value);
}

static void d7_ordered_map_release_key(void* key, void* raw_context)
{
    const struct d7_ordered_map_context* context =
        (const struct d7_ordered_map_context*)raw_context;
    d7_ordered_map_value_destroy(&context->policy.key_type, key);
    free(key);
}

static void d7_ordered_map_release_value(void* value, void* raw_context)
{
    const struct d7_ordered_map_context* context =
        (const struct d7_ordered_map_context*)raw_context;
    d7_ordered_map_value_destroy(&context->policy.value_type, value);
    free(value);
}

static d7_ordered_status d7_ordered_map_from_hamt(d7_hamt_status status)
{
    switch (status) {
    case D7_HAMT_OK:
        return D7_ORDERED_OK;
    case D7_HAMT_OUT_OF_MEMORY:
        return D7_ORDERED_OUT_OF_MEMORY;
    case D7_HAMT_OVERFLOW:
        return D7_ORDERED_OVERFLOW;
    case D7_HAMT_DUPLICATE_KEY:
    case D7_HAMT_INVALID_ARGUMENT:
    case D7_HAMT_TRANSIENT_CONSUMED:
    case D7_HAMT_TRANSIENT_MODIFIED:
    default:
        return D7_ORDERED_INVALID_ARGUMENT;
    }
}

static void d7_ordered_map_adopt(
    struct d7_ordered_map_context* context,
    d7_ordered_set* keys,
    d7_hamt_map* values,
    d7_ordered_map* result)
{
    (void)memset(result, 0, sizeof(*result));
    result->context = context;
    d7_ordered_map_context_retain(context);
    d7_ordered_set_move(&result->keys, keys);
    result->values = *values;
    (void)memset(values, 0, sizeof(*values));
}

void d7_ordered_map_policy_init(
    d7_ordered_map_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    d7_ordered_hash_fn hash,
    d7_ordered_equal_fn key_equal,
    d7_ordered_map_value_equal_fn value_equal,
    void* context)
{
    if (policy == NULL) {
        return;
    }
    (void)memset(policy, 0, sizeof(*policy));
    if (key_type != NULL) {
        policy->key_type = *key_type;
    }
    if (value_type != NULL) {
        policy->value_type = *value_type;
    }
    policy->hash = hash;
    policy->key_equal = key_equal;
    policy->value_equal = value_equal;
    policy->context = context;
}

d7_ordered_status d7_ordered_map_init(
    d7_ordered_map* map,
    const d7_ordered_map_policy* policy)
{
    if (map == NULL || policy == NULL || policy->key_type.size == 0u
        || policy->value_type.size == 0u || policy->hash == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    struct d7_ordered_map_context* context =
        (struct d7_ordered_map_context*)calloc(1u, sizeof(*context));
    if (context == NULL) {
        return D7_ORDERED_OUT_OF_MEMORY;
    }
    context->ref_count = 1u;
    context->policy = *policy;
    d7_ordered_policy_init(
        &context->key_policy,
        &context->policy.key_type,
        d7_ordered_map_hash,
        d7_ordered_map_key_equal,
        context);

    context->value_policy = d7_hamt_policy_default();
    context->value_policy.hash = d7_ordered_map_hash;
    context->value_policy.key_equal = d7_ordered_map_key_equal;
    context->value_policy.value_equal = d7_ordered_map_hamt_value_equal;
    context->value_policy.retain_key = d7_ordered_map_retain_key;
    context->value_policy.retain_value = d7_ordered_map_retain_value;
    context->value_policy.release_key = d7_ordered_map_release_key;
    context->value_policy.release_value = d7_ordered_map_release_value;
    context->value_policy.context = context;

    (void)memset(map, 0, sizeof(*map));
    const d7_ordered_status status =
        d7_ordered_set_init(&map->keys, &context->key_policy);
    if (status != D7_ORDERED_OK) {
        free(context);
        return status;
    }
    map->values = d7_hamt_map_create(&context->value_policy);
    map->context = context;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_clone(
    const d7_ordered_map* source,
    d7_ordered_map* destination)
{
    if (!d7_ordered_map_valid(source) || destination == NULL
        || source == destination) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    (void)memset(destination, 0, sizeof(*destination));
    const d7_ordered_status status =
        d7_ordered_set_clone(&source->keys, &destination->keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    destination->values = d7_hamt_map_clone(&source->values);
    destination->context = source->context;
    d7_ordered_map_context_retain(destination->context);
    return D7_ORDERED_OK;
}

void d7_ordered_map_move(
    d7_ordered_map* destination,
    d7_ordered_map* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    destination->context = source->context;
    source->context = NULL;
    d7_ordered_set_move(&destination->keys, &source->keys);
    destination->values = source->values;
    (void)memset(&source->values, 0, sizeof(source->values));
}

void d7_ordered_map_destroy(d7_ordered_map* map)
{
    if (map == NULL) {
        return;
    }
    d7_ordered_set_destroy(&map->keys);
    d7_hamt_map_destroy(&map->values);
    d7_ordered_map_context_release(map->context);
    (void)memset(map, 0, sizeof(*map));
}

const d7_ordered_map_policy* d7_ordered_map_policy_of(
    const d7_ordered_map* map)
{
    return d7_ordered_map_valid(map) ? &map->context->policy : NULL;
}

bool d7_ordered_map_empty(const d7_ordered_map* map)
{
    return !d7_ordered_map_valid(map) || d7_ordered_set_empty(&map->keys);
}

size_t d7_ordered_map_size(const d7_ordered_map* map)
{
    return d7_ordered_map_valid(map) ? d7_ordered_set_size(&map->keys) : 0u;
}

bool d7_ordered_map_contains_key(
    const d7_ordered_map* map,
    const void* key)
{
    return d7_ordered_map_valid(map) && key != NULL
        && d7_hamt_map_contains_key(&map->values, key);
}

bool d7_ordered_map_try_get(
    const d7_ordered_map* map,
    const void* equal_key,
    const void** actual_key,
    const void** value)
{
    if (!d7_ordered_map_valid(map) || equal_key == NULL) {
        return false;
    }
    const void* stored_value = NULL;
    if (!d7_hamt_map_try_get(&map->values, equal_key, &stored_value)) {
        return false;
    }
    const void* stored_key = NULL;
    if (!d7_ordered_set_try_get_value(
            &map->keys, equal_key, &stored_key)) {
        return false;
    }
    if (actual_key != NULL) {
        *actual_key = stored_key;
    }
    if (value != NULL) {
        *value = stored_value;
    }
    return true;
}

d7_ordered_status d7_ordered_map_entry_at(
    const d7_ordered_map* map,
    size_t index,
    const void** key,
    const void** value)
{
    if (!d7_ordered_map_valid(map)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const void* stored_key = NULL;
    const d7_ordered_status status =
        d7_ordered_set_at(&map->keys, index, &stored_key);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    const void* stored_value = NULL;
    if (!d7_hamt_map_try_get(&map->values, stored_key, &stored_value)) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    if (key != NULL) {
        *key = stored_key;
    }
    if (value != NULL) {
        *value = stored_value;
    }
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_front(
    const d7_ordered_map* map,
    const void** key,
    const void** value)
{
    if (!d7_ordered_map_valid(map)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_map_empty(map)
        ? D7_ORDERED_EMPTY
        : d7_ordered_map_entry_at(map, 0u, key, value);
}

d7_ordered_status d7_ordered_map_back(
    const d7_ordered_map* map,
    const void** key,
    const void** value)
{
    if (!d7_ordered_map_valid(map)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_map_empty(map)
        ? D7_ORDERED_EMPTY
        : d7_ordered_map_entry_at(
            map, d7_ordered_map_size(map) - 1u, key, value);
}

bool d7_ordered_map_index_of_key(
    const d7_ordered_map* map,
    const void* equal_key,
    size_t* index)
{
    return d7_ordered_map_valid(map) && equal_key != NULL && index != NULL
        && d7_ordered_set_index_of(&map->keys, equal_key, index);
}

static d7_ordered_status d7_ordered_map_insert_core(
    const d7_ordered_map* map,
    size_t index,
    const void* key,
    const void* value,
    bool* added,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || key == NULL || value == NULL
        || added == NULL || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (index > d7_ordered_map_size(map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    if (d7_ordered_map_contains_key(map, key)) {
        *added = false;
        return d7_ordered_map_clone(map, result);
    }

    d7_ordered_set keys;
    d7_ordered_status status =
        d7_ordered_set_insert(&map->keys, index, key, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values;
    const d7_hamt_status map_status =
        d7_hamt_map_add(&map->values, key, value, &values);
    if (map_status != D7_HAMT_OK) {
        d7_ordered_set_destroy(&keys);
        return d7_ordered_map_from_hamt(map_status);
    }
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    *added = true;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_try_add(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    bool* added,
    d7_ordered_map* result)
{
    return d7_ordered_map_insert_core(
        map, d7_ordered_map_size(map), key, value, added, result);
}

d7_ordered_status d7_ordered_map_add(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (d7_ordered_map_contains_key(map, key)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    bool added = false;
    return d7_ordered_map_insert_core(
        map, d7_ordered_map_size(map), key, value, &added, result);
}

d7_ordered_status d7_ordered_map_add_first(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (d7_ordered_map_contains_key(map, key)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    bool added = false;
    return d7_ordered_map_insert_core(
        map, 0u, key, value, &added, result);
}

d7_ordered_status d7_ordered_map_insert(
    const d7_ordered_map* map,
    size_t index,
    const void* key,
    const void* value,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (index > d7_ordered_map_size(map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    if (d7_ordered_map_contains_key(map, key)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    bool added = false;
    return d7_ordered_map_insert_core(
        map, index, key, value, &added, result);
}

d7_ordered_status d7_ordered_map_set(
    const d7_ordered_map* map,
    const void* key,
    const void* value,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const void* old_value = NULL;
    if (!d7_hamt_map_try_get(&map->values, key, &old_value)) {
        bool added = false;
        return d7_ordered_map_insert_core(
            map, d7_ordered_map_size(map), key, value, &added, result);
    }
    if (d7_ordered_map_values_equal(map->context, old_value, value)) {
        return d7_ordered_map_clone(map, result);
    }

    d7_ordered_set keys;
    d7_ordered_status status = d7_ordered_set_clone(&map->keys, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values;
    const d7_hamt_status map_status =
        d7_hamt_map_set(&map->values, key, value, &values);
    if (map_status != D7_HAMT_OK) {
        d7_ordered_set_destroy(&keys);
        return d7_ordered_map_from_hamt(map_status);
    }
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

typedef d7_ordered_status (*d7_ordered_map_order_edit_fn)(
    const d7_ordered_set*, const void*, d7_ordered_set*);

static d7_ordered_status d7_ordered_map_reorder(
    const d7_ordered_map* map,
    const void* key,
    d7_ordered_map_order_edit_fn edit,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || key == NULL || edit == NULL
        || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set keys;
    const d7_ordered_status status = edit(&map->keys, key, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values = d7_hamt_map_clone(&map->values);
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_move_to_first(
    const d7_ordered_map* map,
    const void* equal_key,
    d7_ordered_map* result)
{
    return d7_ordered_map_reorder(
        map, equal_key, d7_ordered_set_move_to_first, result);
}

d7_ordered_status d7_ordered_map_move_to_last(
    const d7_ordered_map* map,
    const void* equal_key,
    d7_ordered_map* result)
{
    return d7_ordered_map_reorder(
        map, equal_key, d7_ordered_set_move_to_last, result);
}

d7_ordered_status d7_ordered_map_move_to(
    const d7_ordered_map* map,
    size_t final_index,
    const void* equal_key,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || equal_key == NULL || result == NULL
        || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set keys;
    const d7_ordered_status status = d7_ordered_set_move_to(
        &map->keys, final_index, equal_key, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values = d7_hamt_map_clone(&map->values);
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_try_remove(
    const d7_ordered_map* map,
    const void* equal_key,
    bool* removed,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || equal_key == NULL || removed == NULL
        || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (!d7_ordered_map_contains_key(map, equal_key)) {
        *removed = false;
        return d7_ordered_map_clone(map, result);
    }
    d7_ordered_set keys;
    d7_ordered_status status =
        d7_ordered_set_remove(&map->keys, equal_key, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values;
    const d7_hamt_status map_status =
        d7_hamt_map_remove(&map->values, equal_key, &values);
    if (map_status != D7_HAMT_OK) {
        d7_ordered_set_destroy(&keys);
        return d7_ordered_map_from_hamt(map_status);
    }
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    *removed = true;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_remove(
    const d7_ordered_map* map,
    const void* equal_key,
    d7_ordered_map* result)
{
    bool removed = false;
    return d7_ordered_map_try_remove(
        map, equal_key, &removed, result);
}

d7_ordered_status d7_ordered_map_remove_at(
    const d7_ordered_map* map,
    size_t index,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const void* key = NULL;
    d7_ordered_status status =
        d7_ordered_set_at(&map->keys, index, &key);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_ordered_set keys;
    status = d7_ordered_set_remove_at(&map->keys, index, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values;
    const d7_hamt_status map_status =
        d7_hamt_map_remove(&map->values, key, &values);
    if (map_status != D7_HAMT_OK) {
        d7_ordered_set_destroy(&keys);
        return d7_ordered_map_from_hamt(map_status);
    }
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_remove_first(
    const d7_ordered_map* map,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_map_empty(map)
        ? D7_ORDERED_EMPTY
        : d7_ordered_map_remove_at(map, 0u, result);
}

d7_ordered_status d7_ordered_map_remove_last(
    const d7_ordered_map* map,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_map_empty(map)
        ? D7_ORDERED_EMPTY
        : d7_ordered_map_remove_at(
            map, d7_ordered_map_size(map) - 1u, result);
}

d7_ordered_status d7_ordered_map_clear(
    const d7_ordered_map* map,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set keys;
    d7_ordered_status status = d7_ordered_set_clear(&map->keys, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values;
    const d7_hamt_status map_status =
        d7_hamt_map_clear(&map->values, &values);
    if (map_status != D7_HAMT_OK) {
        d7_ordered_set_destroy(&keys);
        return d7_ordered_map_from_hamt(map_status);
    }
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_map_values_for_keys(
    const d7_ordered_map* source,
    const d7_ordered_set* keys,
    d7_hamt_map* values)
{
    d7_hamt_map current = d7_hamt_map_create(
        &source->context->value_policy);
    const size_t count = d7_ordered_set_size(keys);
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        d7_ordered_status status = d7_ordered_set_at(keys, index, &key);
        if (status != D7_ORDERED_OK) {
            d7_hamt_map_destroy(&current);
            return status;
        }
        const void* value = NULL;
        if (!d7_hamt_map_try_get(&source->values, key, &value)) {
            d7_hamt_map_destroy(&current);
            return D7_ORDERED_INVARIANT_VIOLATION;
        }
        d7_hamt_map next;
        const d7_hamt_status map_status =
            d7_hamt_map_add(&current, key, value, &next);
        if (map_status != D7_HAMT_OK) {
            d7_hamt_map_destroy(&current);
            return d7_ordered_map_from_hamt(map_status);
        }
        d7_hamt_map_destroy(&current);
        current = next;
    }
    *values = current;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_get_range(
    const d7_ordered_map* map,
    size_t index,
    size_t count,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set keys;
    d7_ordered_status status =
        d7_ordered_set_get_range(&map->keys, index, count, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values;
    status = d7_ordered_map_values_for_keys(map, &keys, &values);
    if (status != D7_ORDERED_OK) {
        d7_ordered_set_destroy(&keys);
        return status;
    }
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_take(
    const d7_ordered_map* map,
    size_t count,
    d7_ordered_map* result)
{
    return d7_ordered_map_get_range(map, 0u, count, result);
}

d7_ordered_status d7_ordered_map_drop(
    const d7_ordered_map* map,
    size_t count,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (count > d7_ordered_map_size(map)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_map_get_range(
        map, count, d7_ordered_map_size(map) - count, result);
}

d7_ordered_status d7_ordered_map_reverse(
    const d7_ordered_map* map,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || result == NULL || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set keys;
    const d7_ordered_status status =
        d7_ordered_set_reverse(&map->keys, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values = d7_hamt_map_clone(&map->values);
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

typedef struct d7_ordered_map_sort_context {
    const d7_ordered_map* map;
    d7_ordered_map_compare_fn compare;
    void* compare_context;
} d7_ordered_map_sort_context;

static int d7_ordered_map_compare_keys(
    const void* left_key,
    const void* right_key,
    void* raw_context)
{
    const d7_ordered_map_sort_context* context =
        (const d7_ordered_map_sort_context*)raw_context;
    const void* left_value = NULL;
    const void* right_value = NULL;
    (void)d7_hamt_map_try_get(
        &context->map->values, left_key, &left_value);
    (void)d7_hamt_map_try_get(
        &context->map->values, right_key, &right_value);
    return context->compare(
        left_key,
        left_value,
        right_key,
        right_value,
        context->compare_context);
}

d7_ordered_status d7_ordered_map_sort(
    const d7_ordered_map* map,
    d7_ordered_map_compare_fn compare,
    void* compare_context,
    d7_ordered_map* result)
{
    if (!d7_ordered_map_valid(map) || compare == NULL || result == NULL
        || result == map) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_map_sort_context context = {
        map, compare, compare_context
    };
    d7_ordered_set keys;
    const d7_ordered_status status = d7_ordered_set_sort(
        &map->keys, d7_ordered_map_compare_keys, &context, &keys);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_hamt_map values = d7_hamt_map_clone(&map->values);
    d7_ordered_map_adopt(map->context, &keys, &values, result);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_map_visit(
    const d7_ordered_map* map,
    d7_ordered_map_visit_fn visitor,
    void* context)
{
    if (!d7_ordered_map_valid(map) || visitor == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = d7_ordered_map_size(map);
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        const void* value = NULL;
        const d7_ordered_status status =
            d7_ordered_map_entry_at(map, index, &key, &value);
        if (status != D7_ORDERED_OK) {
            return status;
        }
        visitor(key, value, context);
    }
    return D7_ORDERED_OK;
}

bool d7_ordered_map_debug_validate(const d7_ordered_map* map)
{
    if (!d7_ordered_map_valid(map)
        || !d7_ordered_set_debug_validate(&map->keys)
        || !d7_hamt_map_debug_validate_canonical(&map->values)
        || d7_ordered_set_size(&map->keys)
            != d7_hamt_map_count(&map->values)) {
        return false;
    }
    const size_t count = d7_ordered_map_size(map);
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        if (d7_ordered_set_at(&map->keys, index, &key) != D7_ORDERED_OK
            || !d7_hamt_map_contains_key(&map->values, key)) {
            return false;
        }
    }
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&map->values, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        (void)value;
        if (!d7_ordered_set_contains(&map->keys, key)) {
            return false;
        }
    }
    return true;
}

bool d7_ordered_map_debug_shares_order(
    const d7_ordered_map* left,
    const d7_ordered_map* right)
{
    return d7_ordered_map_valid(left) && d7_ordered_map_valid(right)
        && d7_ordered_set_debug_shares_order(&left->keys, &right->keys);
}

bool d7_ordered_map_debug_shares_values(
    const d7_ordered_map* left,
    const d7_ordered_map* right)
{
    return d7_ordered_map_valid(left) && d7_ordered_map_valid(right)
        && d7_hamt_map_shares_root(&left->values, &right->values);
}
