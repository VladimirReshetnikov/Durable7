#include <tools/data_structures/ordered/ordered_map.h>

#include <stdlib.h>
#include <string.h>

struct tds_ordered_map_context {
    size_t ref_count;
    tds_ordered_map_policy policy;
    tds_ordered_policy key_policy;
    tds_hamt_policy value_policy;
};

static bool tds_ordered_map_valid(const tds_ordered_map* map)
{
    return map != NULL && map->context != NULL;
}

static void tds_ordered_map_context_retain(
    struct tds_ordered_map_context* context)
{
    if (context != NULL) {
        ++context->ref_count;
    }
}

static void tds_ordered_map_context_release(
    struct tds_ordered_map_context* context)
{
    if (context != NULL && --context->ref_count == 0u) {
        free(context);
    }
}

static void tds_ordered_map_value_copy(
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

static void tds_ordered_map_value_destroy(
    const ft_value_type* type,
    void* value)
{
    if (type->destroy != NULL) {
        type->destroy(value, type->context);
    }
}

static bool tds_ordered_map_values_equal(
    const struct tds_ordered_map_context* context,
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

static uint32_t tds_ordered_map_hash(const void* key, void* raw_context)
{
    const struct tds_ordered_map_context* context =
        (const struct tds_ordered_map_context*)raw_context;
    return context->policy.hash(key, context->policy.context);
}

static bool tds_ordered_map_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    const struct tds_ordered_map_context* context =
        (const struct tds_ordered_map_context*)raw_context;
    if (left == right) {
        return true;
    }
    if (context->policy.key_equal != NULL) {
        return context->policy.key_equal(
            left, right, context->policy.context);
    }
    return memcmp(left, right, context->policy.key_type.size) == 0;
}

static bool tds_ordered_map_hamt_value_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    return tds_ordered_map_values_equal(
        (const struct tds_ordered_map_context*)raw_context, left, right);
}

static void* tds_ordered_map_retain_typed(
    const ft_value_type* type,
    const void* value)
{
    void* copy = malloc(type->size);
    if (copy != NULL) {
        tds_ordered_map_value_copy(type, copy, value);
    }
    return copy;
}

static void* tds_ordered_map_retain_key(
    const void* key,
    void* raw_context)
{
    const struct tds_ordered_map_context* context =
        (const struct tds_ordered_map_context*)raw_context;
    return tds_ordered_map_retain_typed(&context->policy.key_type, key);
}

static void* tds_ordered_map_retain_value(
    const void* value,
    void* raw_context)
{
    const struct tds_ordered_map_context* context =
        (const struct tds_ordered_map_context*)raw_context;
    return tds_ordered_map_retain_typed(&context->policy.value_type, value);
}

static void tds_ordered_map_release_key(void* key, void* raw_context)
{
    const struct tds_ordered_map_context* context =
        (const struct tds_ordered_map_context*)raw_context;
    tds_ordered_map_value_destroy(&context->policy.key_type, key);
    free(key);
}

static void tds_ordered_map_release_value(void* value, void* raw_context)
{
    const struct tds_ordered_map_context* context =
        (const struct tds_ordered_map_context*)raw_context;
    tds_ordered_map_value_destroy(&context->policy.value_type, value);
    free(value);
}

static tds_ordered_status tds_ordered_map_from_hamt(tds_hamt_status status)
{
    switch (status) {
    case TDS_HAMT_OK:
        return TDS_ORDERED_OK;
    case TDS_HAMT_OUT_OF_MEMORY:
        return TDS_ORDERED_OUT_OF_MEMORY;
    case TDS_HAMT_OVERFLOW:
        return TDS_ORDERED_OVERFLOW;
    case TDS_HAMT_DUPLICATE_KEY:
    case TDS_HAMT_INVALID_ARGUMENT:
    case TDS_HAMT_TRANSIENT_CONSUMED:
    case TDS_HAMT_TRANSIENT_MODIFIED:
    default:
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
}

static void tds_ordered_map_adopt(
    struct tds_ordered_map_context* context,
    tds_ordered_set* keys,
    tds_hamt_map* values,
    tds_ordered_map* result)
{
    (void)memset(result, 0, sizeof(*result));
    result->context = context;
    tds_ordered_map_context_retain(context);
    tds_ordered_set_move(&result->keys, keys);
    result->values = *values;
    (void)memset(values, 0, sizeof(*values));
}

void tds_ordered_map_policy_init(
    tds_ordered_map_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    tds_ordered_hash_fn hash,
    tds_ordered_equal_fn key_equal,
    tds_ordered_map_value_equal_fn value_equal,
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

tds_ordered_status tds_ordered_map_init(
    tds_ordered_map* map,
    const tds_ordered_map_policy* policy)
{
    if (map == NULL || policy == NULL || policy->key_type.size == 0u
        || policy->value_type.size == 0u || policy->hash == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    struct tds_ordered_map_context* context =
        (struct tds_ordered_map_context*)calloc(1u, sizeof(*context));
    if (context == NULL) {
        return TDS_ORDERED_OUT_OF_MEMORY;
    }
    context->ref_count = 1u;
    context->policy = *policy;
    tds_ordered_policy_init(
        &context->key_policy,
        &context->policy.key_type,
        tds_ordered_map_hash,
        tds_ordered_map_key_equal,
        context);

    context->value_policy = tds_hamt_policy_default();
    context->value_policy.hash = tds_ordered_map_hash;
    context->value_policy.key_equal = tds_ordered_map_key_equal;
    context->value_policy.value_equal = tds_ordered_map_hamt_value_equal;
    context->value_policy.retain_key = tds_ordered_map_retain_key;
    context->value_policy.retain_value = tds_ordered_map_retain_value;
    context->value_policy.release_key = tds_ordered_map_release_key;
    context->value_policy.release_value = tds_ordered_map_release_value;
    context->value_policy.context = context;

    (void)memset(map, 0, sizeof(*map));
    const tds_ordered_status status =
        tds_ordered_set_init(&map->keys, &context->key_policy);
    if (status != TDS_ORDERED_OK) {
        free(context);
        return status;
    }
    map->values = tds_hamt_map_create(&context->value_policy);
    map->context = context;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_clone(
    const tds_ordered_map* source,
    tds_ordered_map* destination)
{
    if (!tds_ordered_map_valid(source) || destination == NULL
        || source == destination) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    (void)memset(destination, 0, sizeof(*destination));
    const tds_ordered_status status =
        tds_ordered_set_clone(&source->keys, &destination->keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    destination->values = tds_hamt_map_clone(&source->values);
    destination->context = source->context;
    tds_ordered_map_context_retain(destination->context);
    return TDS_ORDERED_OK;
}

void tds_ordered_map_move(
    tds_ordered_map* destination,
    tds_ordered_map* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    destination->context = source->context;
    source->context = NULL;
    tds_ordered_set_move(&destination->keys, &source->keys);
    destination->values = source->values;
    (void)memset(&source->values, 0, sizeof(source->values));
}

void tds_ordered_map_destroy(tds_ordered_map* map)
{
    if (map == NULL) {
        return;
    }
    tds_ordered_set_destroy(&map->keys);
    tds_hamt_map_destroy(&map->values);
    tds_ordered_map_context_release(map->context);
    (void)memset(map, 0, sizeof(*map));
}

const tds_ordered_map_policy* tds_ordered_map_policy_of(
    const tds_ordered_map* map)
{
    return tds_ordered_map_valid(map) ? &map->context->policy : NULL;
}

bool tds_ordered_map_empty(const tds_ordered_map* map)
{
    return !tds_ordered_map_valid(map) || tds_ordered_set_empty(&map->keys);
}

size_t tds_ordered_map_size(const tds_ordered_map* map)
{
    return tds_ordered_map_valid(map) ? tds_ordered_set_size(&map->keys) : 0u;
}

bool tds_ordered_map_contains_key(
    const tds_ordered_map* map,
    const void* key)
{
    return tds_ordered_map_valid(map) && key != NULL
        && tds_hamt_map_contains_key(&map->values, key);
}

bool tds_ordered_map_try_get(
    const tds_ordered_map* map,
    const void* equal_key,
    const void** actual_key,
    const void** value)
{
    if (!tds_ordered_map_valid(map) || equal_key == NULL) {
        return false;
    }
    const void* stored_value = NULL;
    if (!tds_hamt_map_try_get(&map->values, equal_key, &stored_value)) {
        return false;
    }
    const void* stored_key = NULL;
    if (!tds_ordered_set_try_get_value(
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

tds_ordered_status tds_ordered_map_entry_at(
    const tds_ordered_map* map,
    size_t index,
    const void** key,
    const void** value)
{
    if (!tds_ordered_map_valid(map)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const void* stored_key = NULL;
    const tds_ordered_status status =
        tds_ordered_set_at(&map->keys, index, &stored_key);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    const void* stored_value = NULL;
    if (!tds_hamt_map_try_get(&map->values, stored_key, &stored_value)) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    if (key != NULL) {
        *key = stored_key;
    }
    if (value != NULL) {
        *value = stored_value;
    }
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_front(
    const tds_ordered_map* map,
    const void** key,
    const void** value)
{
    if (!tds_ordered_map_valid(map)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_map_empty(map)
        ? TDS_ORDERED_EMPTY
        : tds_ordered_map_entry_at(map, 0u, key, value);
}

tds_ordered_status tds_ordered_map_back(
    const tds_ordered_map* map,
    const void** key,
    const void** value)
{
    if (!tds_ordered_map_valid(map)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_map_empty(map)
        ? TDS_ORDERED_EMPTY
        : tds_ordered_map_entry_at(
            map, tds_ordered_map_size(map) - 1u, key, value);
}

bool tds_ordered_map_index_of_key(
    const tds_ordered_map* map,
    const void* equal_key,
    size_t* index)
{
    return tds_ordered_map_valid(map) && equal_key != NULL && index != NULL
        && tds_ordered_set_index_of(&map->keys, equal_key, index);
}

static tds_ordered_status tds_ordered_map_insert_core(
    const tds_ordered_map* map,
    size_t index,
    const void* key,
    const void* value,
    bool* added,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || key == NULL || value == NULL
        || added == NULL || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (index > tds_ordered_map_size(map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    if (tds_ordered_map_contains_key(map, key)) {
        *added = false;
        return tds_ordered_map_clone(map, result);
    }

    tds_ordered_set keys;
    tds_ordered_status status =
        tds_ordered_set_insert(&map->keys, index, key, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values;
    const tds_hamt_status map_status =
        tds_hamt_map_add(&map->values, key, value, &values);
    if (map_status != TDS_HAMT_OK) {
        tds_ordered_set_destroy(&keys);
        return tds_ordered_map_from_hamt(map_status);
    }
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    *added = true;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_try_add(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    bool* added,
    tds_ordered_map* result)
{
    return tds_ordered_map_insert_core(
        map, tds_ordered_map_size(map), key, value, added, result);
}

tds_ordered_status tds_ordered_map_add(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (tds_ordered_map_contains_key(map, key)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    bool added = false;
    return tds_ordered_map_insert_core(
        map, tds_ordered_map_size(map), key, value, &added, result);
}

tds_ordered_status tds_ordered_map_add_first(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (tds_ordered_map_contains_key(map, key)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    bool added = false;
    return tds_ordered_map_insert_core(
        map, 0u, key, value, &added, result);
}

tds_ordered_status tds_ordered_map_insert(
    const tds_ordered_map* map,
    size_t index,
    const void* key,
    const void* value,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (index > tds_ordered_map_size(map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    if (tds_ordered_map_contains_key(map, key)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    bool added = false;
    return tds_ordered_map_insert_core(
        map, index, key, value, &added, result);
}

tds_ordered_status tds_ordered_map_set(
    const tds_ordered_map* map,
    const void* key,
    const void* value,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || key == NULL || value == NULL
        || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const void* old_value = NULL;
    if (!tds_hamt_map_try_get(&map->values, key, &old_value)) {
        bool added = false;
        return tds_ordered_map_insert_core(
            map, tds_ordered_map_size(map), key, value, &added, result);
    }
    if (tds_ordered_map_values_equal(map->context, old_value, value)) {
        return tds_ordered_map_clone(map, result);
    }

    tds_ordered_set keys;
    tds_ordered_status status = tds_ordered_set_clone(&map->keys, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values;
    const tds_hamt_status map_status =
        tds_hamt_map_set(&map->values, key, value, &values);
    if (map_status != TDS_HAMT_OK) {
        tds_ordered_set_destroy(&keys);
        return tds_ordered_map_from_hamt(map_status);
    }
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

typedef tds_ordered_status (*tds_ordered_map_order_edit_fn)(
    const tds_ordered_set*, const void*, tds_ordered_set*);

static tds_ordered_status tds_ordered_map_reorder(
    const tds_ordered_map* map,
    const void* key,
    tds_ordered_map_order_edit_fn edit,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || key == NULL || edit == NULL
        || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set keys;
    const tds_ordered_status status = edit(&map->keys, key, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values = tds_hamt_map_clone(&map->values);
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_move_to_first(
    const tds_ordered_map* map,
    const void* equal_key,
    tds_ordered_map* result)
{
    return tds_ordered_map_reorder(
        map, equal_key, tds_ordered_set_move_to_first, result);
}

tds_ordered_status tds_ordered_map_move_to_last(
    const tds_ordered_map* map,
    const void* equal_key,
    tds_ordered_map* result)
{
    return tds_ordered_map_reorder(
        map, equal_key, tds_ordered_set_move_to_last, result);
}

tds_ordered_status tds_ordered_map_move_to(
    const tds_ordered_map* map,
    size_t final_index,
    const void* equal_key,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || equal_key == NULL || result == NULL
        || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set keys;
    const tds_ordered_status status = tds_ordered_set_move_to(
        &map->keys, final_index, equal_key, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values = tds_hamt_map_clone(&map->values);
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_try_remove(
    const tds_ordered_map* map,
    const void* equal_key,
    bool* removed,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || equal_key == NULL || removed == NULL
        || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (!tds_ordered_map_contains_key(map, equal_key)) {
        *removed = false;
        return tds_ordered_map_clone(map, result);
    }
    tds_ordered_set keys;
    tds_ordered_status status =
        tds_ordered_set_remove(&map->keys, equal_key, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values;
    const tds_hamt_status map_status =
        tds_hamt_map_remove(&map->values, equal_key, &values);
    if (map_status != TDS_HAMT_OK) {
        tds_ordered_set_destroy(&keys);
        return tds_ordered_map_from_hamt(map_status);
    }
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    *removed = true;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_remove(
    const tds_ordered_map* map,
    const void* equal_key,
    tds_ordered_map* result)
{
    bool removed = false;
    return tds_ordered_map_try_remove(
        map, equal_key, &removed, result);
}

tds_ordered_status tds_ordered_map_remove_at(
    const tds_ordered_map* map,
    size_t index,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const void* key = NULL;
    tds_ordered_status status =
        tds_ordered_set_at(&map->keys, index, &key);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_ordered_set keys;
    status = tds_ordered_set_remove_at(&map->keys, index, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values;
    const tds_hamt_status map_status =
        tds_hamt_map_remove(&map->values, key, &values);
    if (map_status != TDS_HAMT_OK) {
        tds_ordered_set_destroy(&keys);
        return tds_ordered_map_from_hamt(map_status);
    }
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_remove_first(
    const tds_ordered_map* map,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_map_empty(map)
        ? TDS_ORDERED_EMPTY
        : tds_ordered_map_remove_at(map, 0u, result);
}

tds_ordered_status tds_ordered_map_remove_last(
    const tds_ordered_map* map,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_map_empty(map)
        ? TDS_ORDERED_EMPTY
        : tds_ordered_map_remove_at(
            map, tds_ordered_map_size(map) - 1u, result);
}

tds_ordered_status tds_ordered_map_clear(
    const tds_ordered_map* map,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set keys;
    tds_ordered_status status = tds_ordered_set_clear(&map->keys, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values;
    const tds_hamt_status map_status =
        tds_hamt_map_clear(&map->values, &values);
    if (map_status != TDS_HAMT_OK) {
        tds_ordered_set_destroy(&keys);
        return tds_ordered_map_from_hamt(map_status);
    }
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_map_values_for_keys(
    const tds_ordered_map* source,
    const tds_ordered_set* keys,
    tds_hamt_map* values)
{
    tds_hamt_map current = tds_hamt_map_create(
        &source->context->value_policy);
    const size_t count = tds_ordered_set_size(keys);
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        tds_ordered_status status = tds_ordered_set_at(keys, index, &key);
        if (status != TDS_ORDERED_OK) {
            tds_hamt_map_destroy(&current);
            return status;
        }
        const void* value = NULL;
        if (!tds_hamt_map_try_get(&source->values, key, &value)) {
            tds_hamt_map_destroy(&current);
            return TDS_ORDERED_INVARIANT_VIOLATION;
        }
        tds_hamt_map next;
        const tds_hamt_status map_status =
            tds_hamt_map_add(&current, key, value, &next);
        if (map_status != TDS_HAMT_OK) {
            tds_hamt_map_destroy(&current);
            return tds_ordered_map_from_hamt(map_status);
        }
        tds_hamt_map_destroy(&current);
        current = next;
    }
    *values = current;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_get_range(
    const tds_ordered_map* map,
    size_t index,
    size_t count,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set keys;
    tds_ordered_status status =
        tds_ordered_set_get_range(&map->keys, index, count, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values;
    status = tds_ordered_map_values_for_keys(map, &keys, &values);
    if (status != TDS_ORDERED_OK) {
        tds_ordered_set_destroy(&keys);
        return status;
    }
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_take(
    const tds_ordered_map* map,
    size_t count,
    tds_ordered_map* result)
{
    return tds_ordered_map_get_range(map, 0u, count, result);
}

tds_ordered_status tds_ordered_map_drop(
    const tds_ordered_map* map,
    size_t count,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (count > tds_ordered_map_size(map)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_map_get_range(
        map, count, tds_ordered_map_size(map) - count, result);
}

tds_ordered_status tds_ordered_map_reverse(
    const tds_ordered_map* map,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || result == NULL || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set keys;
    const tds_ordered_status status =
        tds_ordered_set_reverse(&map->keys, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values = tds_hamt_map_clone(&map->values);
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

typedef struct tds_ordered_map_sort_context {
    const tds_ordered_map* map;
    tds_ordered_map_compare_fn compare;
    void* compare_context;
} tds_ordered_map_sort_context;

static int tds_ordered_map_compare_keys(
    const void* left_key,
    const void* right_key,
    void* raw_context)
{
    const tds_ordered_map_sort_context* context =
        (const tds_ordered_map_sort_context*)raw_context;
    const void* left_value = NULL;
    const void* right_value = NULL;
    (void)tds_hamt_map_try_get(
        &context->map->values, left_key, &left_value);
    (void)tds_hamt_map_try_get(
        &context->map->values, right_key, &right_value);
    return context->compare(
        left_key,
        left_value,
        right_key,
        right_value,
        context->compare_context);
}

tds_ordered_status tds_ordered_map_sort(
    const tds_ordered_map* map,
    tds_ordered_map_compare_fn compare,
    void* compare_context,
    tds_ordered_map* result)
{
    if (!tds_ordered_map_valid(map) || compare == NULL || result == NULL
        || result == map) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_map_sort_context context = {
        map, compare, compare_context
    };
    tds_ordered_set keys;
    const tds_ordered_status status = tds_ordered_set_sort(
        &map->keys, tds_ordered_map_compare_keys, &context, &keys);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_hamt_map values = tds_hamt_map_clone(&map->values);
    tds_ordered_map_adopt(map->context, &keys, &values, result);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_map_visit(
    const tds_ordered_map* map,
    tds_ordered_map_visit_fn visitor,
    void* context)
{
    if (!tds_ordered_map_valid(map) || visitor == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = tds_ordered_map_size(map);
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        const void* value = NULL;
        const tds_ordered_status status =
            tds_ordered_map_entry_at(map, index, &key, &value);
        if (status != TDS_ORDERED_OK) {
            return status;
        }
        visitor(key, value, context);
    }
    return TDS_ORDERED_OK;
}

bool tds_ordered_map_debug_validate(const tds_ordered_map* map)
{
    if (!tds_ordered_map_valid(map)
        || !tds_ordered_set_debug_validate(&map->keys)
        || !tds_hamt_map_debug_validate_canonical(&map->values)
        || tds_ordered_set_size(&map->keys)
            != tds_hamt_map_count(&map->values)) {
        return false;
    }
    const size_t count = tds_ordered_map_size(map);
    for (size_t index = 0u; index != count; ++index) {
        const void* key = NULL;
        if (tds_ordered_set_at(&map->keys, index, &key) != TDS_ORDERED_OK
            || !tds_hamt_map_contains_key(&map->values, key)) {
            return false;
        }
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&map->values, &iterator);
    const void* key = NULL;
    const void* value = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
        (void)value;
        if (!tds_ordered_set_contains(&map->keys, key)) {
            return false;
        }
    }
    return true;
}

bool tds_ordered_map_debug_shares_order(
    const tds_ordered_map* left,
    const tds_ordered_map* right)
{
    return tds_ordered_map_valid(left) && tds_ordered_map_valid(right)
        && tds_ordered_set_debug_shares_order(&left->keys, &right->keys);
}

bool tds_ordered_map_debug_shares_values(
    const tds_ordered_map* left,
    const tds_ordered_map* right)
{
    return tds_ordered_map_valid(left) && tds_ordered_map_valid(right)
        && tds_hamt_map_shares_root(&left->values, &right->values);
}
