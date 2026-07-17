#include <Tools/DataStructures/Hamt/persistent_indexed_map.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct tds_hamt_indexed_entry {
    void* value;
    void* index_key;
} tds_hamt_indexed_entry;

typedef struct tds_hamt_indexed_map_context {
    atomic_size_t references;
    tds_hamt_set_policy key_policy;
    tds_hamt_set_policy value_policy;
    tds_hamt_set_policy index_policy;
    tds_hamt_index_selector_fn selector;
    void* selector_context;
} tds_hamt_indexed_map_context;

static bool tds_hamt_indexed_map_valid(const tds_hamt_indexed_map* map)
{
    return map != NULL && map->context != NULL;
}

static void tds_hamt_indexed_context_retain(tds_hamt_indexed_map_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void tds_hamt_indexed_context_release(tds_hamt_indexed_map_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static uint32_t tds_hamt_indexed_key_hash(const void* key, void* raw_context)
{
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    return context->key_policy.hash(key, context->key_policy.context);
}

static bool tds_hamt_indexed_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    return context->key_policy.equal(left, right, context->key_policy.context);
}

static void* tds_hamt_indexed_key_retain(const void* key, void* raw_context)
{
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    return context->key_policy.retain_item(key, context->key_policy.context);
}

static void tds_hamt_indexed_key_release(void* key, void* raw_context)
{
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    context->key_policy.release_item(key, context->key_policy.context);
}

static bool tds_hamt_indexed_entry_equal(
    const void* raw_left,
    const void* raw_right,
    void* raw_context)
{
    const tds_hamt_indexed_entry* left =
        (const tds_hamt_indexed_entry*)raw_left;
    const tds_hamt_indexed_entry* right =
        (const tds_hamt_indexed_entry*)raw_right;
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    return context->value_policy.equal(
            left->value, right->value, context->value_policy.context)
        && context->index_policy.equal(
            left->index_key, right->index_key, context->index_policy.context);
}

static void* tds_hamt_indexed_entry_retain(const void* raw_entry, void* raw_context)
{
    const tds_hamt_indexed_entry* entry =
        (const tds_hamt_indexed_entry*)raw_entry;
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    tds_hamt_indexed_entry* copy =
        (tds_hamt_indexed_entry*)malloc(sizeof(*copy));
    if (copy == NULL) {
        return NULL;
    }
    copy->value = context->value_policy.retain_item(
        entry->value, context->value_policy.context);
    if (entry->value != NULL && copy->value == NULL) {
        free(copy);
        return NULL;
    }
    copy->index_key = context->index_policy.retain_item(
        entry->index_key, context->index_policy.context);
    if (entry->index_key != NULL && copy->index_key == NULL) {
        context->value_policy.release_item(
            copy->value, context->value_policy.context);
        free(copy);
        return NULL;
    }
    return copy;
}

static void tds_hamt_indexed_entry_release(void* raw_entry, void* raw_context)
{
    tds_hamt_indexed_entry* entry = (tds_hamt_indexed_entry*)raw_entry;
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)raw_context;
    if (entry != NULL) {
        context->value_policy.release_item(
            entry->value, context->value_policy.context);
        context->index_policy.release_item(
            entry->index_key, context->index_policy.context);
        free(entry);
    }
}

static tds_hamt_policy tds_hamt_indexed_primary_policy(
    tds_hamt_indexed_map_context* context)
{
    tds_hamt_policy policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.hash = tds_hamt_indexed_key_hash;
    policy.key_equal = tds_hamt_indexed_key_equal;
    policy.value_equal = tds_hamt_indexed_entry_equal;
    policy.retain_key = tds_hamt_indexed_key_retain;
    policy.release_key = tds_hamt_indexed_key_release;
    policy.retain_value = tds_hamt_indexed_entry_retain;
    policy.release_value = tds_hamt_indexed_entry_release;
    policy.context = context;
    return policy;
}

static void tds_hamt_indexed_publish(
    const tds_hamt_indexed_map* source,
    tds_hamt_indexed_map* result,
    tds_hamt_indexed_map* candidate)
{
    if (result == source) {
        tds_hamt_indexed_map_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static tds_hamt_status tds_hamt_indexed_publish_clone(
    const tds_hamt_indexed_map* source,
    tds_hamt_indexed_map* result)
{
    tds_hamt_indexed_map candidate;
    const tds_hamt_status status = tds_hamt_indexed_map_clone(source, &candidate);
    if (status == TDS_HAMT_OK) {
        tds_hamt_indexed_publish(source, result, &candidate);
    }
    return status;
}

tds_hamt_status tds_hamt_indexed_map_init(
    tds_hamt_indexed_map* map,
    const tds_hamt_set_policy* key_policy,
    const tds_hamt_set_policy* value_policy,
    const tds_hamt_set_policy* index_policy,
    tds_hamt_index_selector_fn selector,
    void* selector_context)
{
    if (map == NULL || key_policy == NULL || value_policy == NULL
        || index_policy == NULL || selector == NULL
        || key_policy->hash == NULL || key_policy->equal == NULL
        || value_policy->equal == NULL || index_policy->hash == NULL
        || index_policy->equal == NULL || key_policy->retain_item == NULL
        || key_policy->release_item == NULL || value_policy->retain_item == NULL
        || value_policy->release_item == NULL || index_policy->retain_item == NULL
        || index_policy->release_item == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_indexed_map_context* context =
        (tds_hamt_indexed_map_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    atomic_init(&context->references, 1u);
    context->key_policy = *key_policy;
    context->value_policy = *value_policy;
    context->index_policy = *index_policy;
    context->selector = selector;
    context->selector_context = selector_context;
    const tds_hamt_policy primary_policy =
        tds_hamt_indexed_primary_policy(context);
    map->primary = tds_hamt_map_create(&primary_policy);
    const tds_hamt_status status = tds_hamt_multimap_init(
        &map->index, index_policy, key_policy);
    if (status != TDS_HAMT_OK) {
        tds_hamt_map_destroy(&map->primary);
        free(context);
        return status;
    }
    map->context = context;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_indexed_map_clone(
    const tds_hamt_indexed_map* source,
    tds_hamt_indexed_map* destination)
{
    if (!tds_hamt_indexed_map_valid(source) || destination == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return TDS_HAMT_OK;
    }
    destination->primary = tds_hamt_map_clone(&source->primary);
    const tds_hamt_status status =
        tds_hamt_multimap_clone(&source->index, &destination->index);
    if (status != TDS_HAMT_OK) {
        tds_hamt_map_destroy(&destination->primary);
        return status;
    }
    destination->context = source->context;
    tds_hamt_indexed_context_retain(destination->context);
    return TDS_HAMT_OK;
}

void tds_hamt_indexed_map_move(
    tds_hamt_indexed_map* destination,
    tds_hamt_indexed_map* source)
{
    if (destination != NULL && source != NULL && destination != source) {
        *destination = *source;
        (void)memset(source, 0, sizeof(*source));
    }
}

void tds_hamt_indexed_map_destroy(tds_hamt_indexed_map* map)
{
    if (map != NULL) {
        tds_hamt_map_destroy(&map->primary);
        tds_hamt_multimap_destroy(&map->index);
        tds_hamt_indexed_context_release(map->context);
        (void)memset(map, 0, sizeof(*map));
    }
}

size_t tds_hamt_indexed_map_count(const tds_hamt_indexed_map* map)
{
    return tds_hamt_indexed_map_valid(map) ? tds_hamt_map_count(&map->primary) : 0u;
}

bool tds_hamt_indexed_map_empty(const tds_hamt_indexed_map* map)
{
    return tds_hamt_indexed_map_count(map) == 0u;
}

size_t tds_hamt_indexed_map_index_key_count(const tds_hamt_indexed_map* map)
{
    return tds_hamt_indexed_map_valid(map)
        ? tds_hamt_multimap_key_count(&map->index) : 0u;
}

bool tds_hamt_indexed_map_contains_key(const tds_hamt_indexed_map* map, const void* key)
{
    return tds_hamt_indexed_map_valid(map)
        && tds_hamt_map_contains_key(&map->primary, key);
}

static const tds_hamt_indexed_entry* tds_hamt_indexed_try_get_entry(
    const tds_hamt_indexed_map* map,
    const void* key)
{
    const void* raw = NULL;
    return tds_hamt_indexed_map_valid(map)
        && tds_hamt_map_try_get(&map->primary, key, &raw)
        ? (const tds_hamt_indexed_entry*)raw : NULL;
}

bool tds_hamt_indexed_map_try_get(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void** value)
{
    if (value != NULL) {
        *value = NULL;
    }
    const tds_hamt_indexed_entry* entry = tds_hamt_indexed_try_get_entry(map, key);
    if (entry != NULL && value != NULL) {
        *value = entry->value;
    }
    return entry != NULL;
}

bool tds_hamt_indexed_map_try_get_key(
    const tds_hamt_indexed_map* map,
    const void* equal_key,
    const void** actual_key)
{
    return tds_hamt_indexed_map_valid(map)
        && tds_hamt_map_try_get_key(&map->primary, equal_key, actual_key);
}

bool tds_hamt_indexed_map_try_get_index_key(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void** index_key)
{
    if (index_key != NULL) {
        *index_key = NULL;
    }
    const tds_hamt_indexed_entry* entry = tds_hamt_indexed_try_get_entry(map, key);
    if (entry != NULL && index_key != NULL) {
        *index_key = entry->index_key;
    }
    return entry != NULL;
}

bool tds_hamt_indexed_map_contains_index_key(
    const tds_hamt_indexed_map* map,
    const void* index_key)
{
    return tds_hamt_indexed_map_valid(map)
        && tds_hamt_multimap_contains_key(&map->index, index_key);
}

bool tds_hamt_indexed_map_try_get_keys_by_index(
    const tds_hamt_indexed_map* map,
    const void* index_key,
    const tds_hamt_set** keys)
{
    return tds_hamt_indexed_map_valid(map)
        && tds_hamt_multimap_try_get_values(&map->index, index_key, keys);
}

size_t tds_hamt_indexed_map_count_by_index(
    const tds_hamt_indexed_map* map,
    const void* index_key)
{
    const tds_hamt_set* keys = NULL;
    return tds_hamt_indexed_map_try_get_keys_by_index(map, index_key, &keys)
        ? tds_hamt_set_count(keys) : 0u;
}

tds_hamt_status tds_hamt_indexed_map_add(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void* value,
    tds_hamt_indexed_map* result)
{
    if (!tds_hamt_indexed_map_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_map_contains_key(&map->primary, key)) {
        return TDS_HAMT_DUPLICATE_KEY;
    }
    const void* selected = NULL;
    tds_hamt_status status = map->context->selector(
        key, value, map->context->selector_context, &selected);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap index;
    status = tds_hamt_multimap_add(&map->index, selected, key, &index);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    const void* actual_index = NULL;
    if (!tds_hamt_multimap_try_get_key(&index, selected, &actual_index)) {
        tds_hamt_multimap_destroy(&index);
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_indexed_entry entry = {
        (void*)value, (void*)actual_index };
    tds_hamt_map primary;
    status = tds_hamt_map_add(&map->primary, key, &entry, &primary);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&index);
        return status;
    }
    tds_hamt_indexed_map candidate = { primary, index, map->context };
    tds_hamt_indexed_context_retain(candidate.context);
    tds_hamt_indexed_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_indexed_map_try_add(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void* value,
    bool* added,
    tds_hamt_indexed_map* result)
{
    if (added == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *added = false;
    if (tds_hamt_indexed_map_contains_key(map, key)) {
        return tds_hamt_indexed_publish_clone(map, result);
    }
    const tds_hamt_status status = tds_hamt_indexed_map_add(map, key, value, result);
    if (status == TDS_HAMT_OK) {
        *added = true;
    }
    return status;
}

tds_hamt_status tds_hamt_indexed_map_set(
    const tds_hamt_indexed_map* map,
    const void* key,
    const void* value,
    tds_hamt_indexed_map* result)
{
    if (!tds_hamt_indexed_map_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_indexed_entry* current = tds_hamt_indexed_try_get_entry(map, key);
    if (current == NULL) {
        return tds_hamt_indexed_map_add(map, key, value, result);
    }
    if (map->context->value_policy.equal(
            current->value, value, map->context->value_policy.context)) {
        return tds_hamt_indexed_publish_clone(map, result);
    }
    const void* stored_key = NULL;
    if (!tds_hamt_map_try_get_key(&map->primary, key, &stored_key)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const void* selected = NULL;
    tds_hamt_status status = map->context->selector(
        stored_key, value, map->context->selector_context, &selected);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap index;
    const void* actual_index = current->index_key;
    if (map->context->index_policy.equal(
            current->index_key, selected, map->context->index_policy.context)) {
        status = tds_hamt_multimap_clone(&map->index, &index);
    } else {
        tds_hamt_multimap removed;
        status = tds_hamt_multimap_remove(
            &map->index, current->index_key, stored_key, &removed);
        if (status == TDS_HAMT_OK) {
            status = tds_hamt_multimap_add(&removed, selected, stored_key, &index);
            tds_hamt_multimap_destroy(&removed);
        }
        if (status == TDS_HAMT_OK
            && !tds_hamt_multimap_try_get_key(&index, selected, &actual_index)) {
            tds_hamt_multimap_destroy(&index);
            return TDS_HAMT_INVALID_ARGUMENT;
        }
    }
    if (status != TDS_HAMT_OK) {
        return status;
    }
    const tds_hamt_indexed_entry entry = {
        (void*)value, (void*)actual_index };
    tds_hamt_map primary;
    status = tds_hamt_map_set(&map->primary, stored_key, &entry, &primary);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&index);
        return status;
    }
    tds_hamt_indexed_map candidate = { primary, index, map->context };
    tds_hamt_indexed_context_retain(candidate.context);
    tds_hamt_indexed_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_indexed_map_remove(
    const tds_hamt_indexed_map* map,
    const void* key,
    tds_hamt_indexed_map* result)
{
    if (!tds_hamt_indexed_map_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const tds_hamt_indexed_entry* current = tds_hamt_indexed_try_get_entry(map, key);
    const void* stored_key = NULL;
    if (current == NULL
        || !tds_hamt_map_try_get_key(&map->primary, key, &stored_key)) {
        return tds_hamt_indexed_publish_clone(map, result);
    }
    tds_hamt_map primary;
    tds_hamt_status status =
        tds_hamt_map_remove(&map->primary, stored_key, &primary);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap index;
    status = tds_hamt_multimap_remove(
        &map->index, current->index_key, stored_key, &index);
    if (status != TDS_HAMT_OK) {
        tds_hamt_map_destroy(&primary);
        return status;
    }
    tds_hamt_indexed_map candidate = { primary, index, map->context };
    tds_hamt_indexed_context_retain(candidate.context);
    tds_hamt_indexed_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_indexed_map_clear(
    const tds_hamt_indexed_map* map,
    tds_hamt_indexed_map* result)
{
    if (!tds_hamt_indexed_map_valid(map) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_indexed_map_empty(map)) {
        return tds_hamt_indexed_publish_clone(map, result);
    }
    tds_hamt_map primary;
    tds_hamt_status status = tds_hamt_map_clear(&map->primary, &primary);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap index;
    status = tds_hamt_multimap_clear(&map->index, &index);
    if (status != TDS_HAMT_OK) {
        tds_hamt_map_destroy(&primary);
        return status;
    }
    tds_hamt_indexed_map candidate = { primary, index, map->context };
    tds_hamt_indexed_context_retain(candidate.context);
    tds_hamt_indexed_publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_indexed_map_visit(
    const tds_hamt_indexed_map* map,
    tds_hamt_indexed_map_visit_fn visitor,
    void* context)
{
    if (!tds_hamt_indexed_map_valid(map) || visitor == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&map->primary, &iterator);
    const void* key = NULL;
    const void* raw_entry = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_entry)) {
        visitor(key, ((const tds_hamt_indexed_entry*)raw_entry)->value, context);
    }
    return TDS_HAMT_OK;
}

typedef struct tds_hamt_index_validation_context {
    const tds_hamt_indexed_map* map;
    bool valid;
    size_t pairs;
} tds_hamt_index_validation_context;

static void tds_hamt_index_validate_pair(
    const void* index_key,
    const void* key,
    void* raw_context)
{
    tds_hamt_index_validation_context* context =
        (tds_hamt_index_validation_context*)raw_context;
    const tds_hamt_indexed_entry* entry =
        tds_hamt_indexed_try_get_entry(context->map, key);
    if (entry == NULL || !context->map->context->index_policy.equal(
            index_key, entry->index_key,
            context->map->context->index_policy.context)) {
        context->valid = false;
    }
    ++context->pairs;
}

bool tds_hamt_indexed_map_debug_validate(const tds_hamt_indexed_map* map)
{
    if (!tds_hamt_indexed_map_valid(map)
        || !tds_hamt_map_debug_validate_canonical(&map->primary)
        || !tds_hamt_multimap_debug_validate(&map->index)) {
        return false;
    }
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&map->primary, &iterator);
    const void* key = NULL;
    const void* raw_entry = NULL;
    while (tds_hamt_map_iterator_next(&iterator, &key, &raw_entry)) {
        const tds_hamt_indexed_entry* entry =
            (const tds_hamt_indexed_entry*)raw_entry;
        if (!tds_hamt_multimap_contains(&map->index, entry->index_key, key)) {
            return false;
        }
    }
    tds_hamt_index_validation_context context = { map, true, 0u };
    if (tds_hamt_multimap_visit(
            &map->index, tds_hamt_index_validate_pair, &context) != TDS_HAMT_OK) {
        return false;
    }
    return context.valid && context.pairs == tds_hamt_map_count(&map->primary);
}

bool tds_hamt_indexed_map_debug_shares_roots(
    const tds_hamt_indexed_map* left,
    const tds_hamt_indexed_map* right)
{
    return tds_hamt_indexed_map_valid(left)
        && tds_hamt_indexed_map_valid(right)
        && tds_hamt_map_shares_root(&left->primary, &right->primary)
        && tds_hamt_multimap_debug_shares_root(&left->index, &right->index);
}
