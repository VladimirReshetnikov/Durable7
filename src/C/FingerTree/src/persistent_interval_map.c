#include <durable7/finger_tree/persistent_interval_map.h>

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
/* The UCRT C <stddef.h> does not declare max_align_t even in /std:c11 mode.
 * The Windows ABI gives long double the maximum fundamental alignment that
 * malloc must support. Other C11 implementations use the standard type. */
typedef long double ft_interval_map_max_align_t;
#else
typedef max_align_t ft_interval_map_max_align_t;
#endif

typedef struct ft_interval_map_key_object {
    atomic_size_t references;
    ft_interval_map_max_align_t alignment;
    unsigned char data[];
} ft_interval_map_key_object;

typedef struct ft_interval_map_key {
    ft_interval_map_key_object* object;
    const void* low;
    const void* high;
} ft_interval_map_key;

typedef struct ft_interval_map_context {
    atomic_size_t references;
    ft_value_type endpoint_type;
    ft_value_type value_type;
    ft_compare_fn compare_endpoint;
    void* compare_context;
    ft_interval_map_value_equal_fn value_equal;
    void* value_equal_context;
} ft_interval_map_context;

static void ft_interval_map_value_copy(
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

static void ft_interval_map_value_destroy(
    const ft_value_type* type,
    void* value)
{
    if (type->destroy != NULL) {
        type->destroy(value, type->context);
    }
}

static void ft_interval_map_context_retain(ft_interval_map_context* context)
{
    if (context != NULL) {
        (void)atomic_fetch_add_explicit(
            &context->references, 1u, memory_order_relaxed);
    }
}

static void ft_interval_map_context_release(ft_interval_map_context* context)
{
    if (context != NULL
        && atomic_fetch_sub_explicit(
            &context->references, 1u, memory_order_acq_rel) == 1u) {
        free(context);
    }
}

static ft_status ft_interval_map_key_init(
    ft_interval_map_context* context,
    const void* low,
    const void* high,
    ft_interval_map_key* key)
{
    if (context->endpoint_type.size > (SIZE_MAX - sizeof(ft_interval_map_key_object)) / 2u) {
        return FT_STATUS_OVERFLOW;
    }
    const size_t allocation_size = sizeof(ft_interval_map_key_object)
        + 2u * context->endpoint_type.size;
    ft_interval_map_key_object* object =
        (ft_interval_map_key_object*)malloc(allocation_size);
    if (object == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    atomic_init(&object->references, 1u);
    key->object = object;
    key->low = object->data;
    key->high = object->data + context->endpoint_type.size;
    ft_interval_map_value_copy(&context->endpoint_type, (void*)key->low, low);
    ft_interval_map_value_copy(&context->endpoint_type, (void*)key->high, high);
    return FT_STATUS_OK;
}

static void ft_interval_map_key_copy(void* destination, const void* source, void* raw_context)
{
    (void)raw_context;
    const ft_interval_map_key* value = (const ft_interval_map_key*)source;
    ft_interval_map_key* copy = (ft_interval_map_key*)destination;
    *copy = *value;
    if (copy->object != NULL) {
        (void)atomic_fetch_add_explicit(
            &copy->object->references, 1u, memory_order_relaxed);
    }
}

static void ft_interval_map_key_destroy(void* value, void* raw_context)
{
    ft_interval_map_context* context = (ft_interval_map_context*)raw_context;
    ft_interval_map_key* key = (ft_interval_map_key*)value;
    if (key->object != NULL
        && atomic_fetch_sub_explicit(
            &key->object->references, 1u, memory_order_acq_rel) == 1u) {
        ft_interval_map_value_destroy(&context->endpoint_type, (void*)key->low);
        ft_interval_map_value_destroy(&context->endpoint_type, (void*)key->high);
        free(key->object);
    }
    (void)memset(key, 0, sizeof(*key));
}

static int ft_interval_map_key_compare(
    const void* left,
    const void* right,
    void* raw_context)
{
    ft_interval_map_context* context = (ft_interval_map_context*)raw_context;
    const ft_interval_map_key* l = (const ft_interval_map_key*)left;
    const ft_interval_map_key* r = (const ft_interval_map_key*)right;
    const int low = context->compare_endpoint(
        l->low, r->low, context->compare_context);
    return low != 0
        ? low
        : context->compare_endpoint(l->high, r->high, context->compare_context);
}

static bool ft_interval_map_valid(const ft_persistent_interval_map* map)
{
    return map != NULL && map->context != NULL;
}

static bool ft_interval_map_interval_valid(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high)
{
    return ft_interval_map_valid(map)
        && low != NULL
        && high != NULL
        && map->context->compare_endpoint(
            low, high, map->context->compare_context) <= 0;
}

static ft_interval_map_key ft_interval_map_probe(
    const void* low,
    const void* high)
{
    ft_interval_map_key key;
    key.object = NULL;
    key.low = low;
    key.high = high;
    return key;
}

static void ft_interval_map_publish(
    const ft_persistent_interval_map* source,
    ft_persistent_interval_map* result,
    ft_persistent_interval_map* candidate)
{
    if (result == source) {
        ft_persistent_interval_map_dispose(result);
    }
    ft_persistent_interval_map_move(result, candidate);
}

static ft_status ft_interval_map_publish_copy(
    const ft_persistent_interval_map* source,
    ft_persistent_interval_map* result)
{
    ft_persistent_interval_map candidate;
    const ft_status status =
        ft_persistent_interval_map_copy(source, &candidate);
    if (status == FT_STATUS_OK) {
        ft_interval_map_publish(source, result, &candidate);
    }
    return status;
}

ft_status ft_persistent_interval_map_init(
    ft_persistent_interval_map* map,
    const ft_value_type* endpoint_type,
    const ft_value_type* value_type,
    ft_compare_fn compare_endpoint,
    void* compare_context,
    ft_interval_map_value_equal_fn value_equal,
    void* value_equal_context)
{
    if (map == NULL || endpoint_type == NULL || endpoint_type->size == 0u
        || value_type == NULL || value_type->size == 0u
        || compare_endpoint == NULL || value_equal == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_interval_map_context* context =
        (ft_interval_map_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    atomic_init(&context->references, 1u);
    context->endpoint_type = *endpoint_type;
    context->value_type = *value_type;
    context->compare_endpoint = compare_endpoint;
    context->compare_context = compare_context;
    context->value_equal = value_equal;
    context->value_equal_context = value_equal_context;

    ft_status status = ft_interval_tree_init(
        &map->intervals, endpoint_type, compare_endpoint, compare_context);
    if (status != FT_STATUS_OK) {
        free(context);
        return status;
    }
    ft_value_type key_type;
    ft_value_type_init(&key_type, sizeof(ft_interval_map_key));
    key_type.copy = ft_interval_map_key_copy;
    key_type.destroy = ft_interval_map_key_destroy;
    key_type.context = context;
    status = ft_sorted_map_init(
        &map->values,
        &key_type,
        value_type,
        ft_interval_map_key_compare,
        context);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(&map->intervals);
        free(context);
        return status;
    }
    map->context = context;
    return FT_STATUS_OK;
}

ft_status ft_persistent_interval_map_copy(
    const ft_persistent_interval_map* source,
    ft_persistent_interval_map* destination)
{
    if (!ft_interval_map_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_status status = ft_interval_tree_copy(
        &source->intervals, &destination->intervals);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_sorted_map_copy(&source->values, &destination->values);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(&destination->intervals);
        return status;
    }
    destination->context = source->context;
    ft_interval_map_context_retain(destination->context);
    return FT_STATUS_OK;
}

void ft_persistent_interval_map_move(
    ft_persistent_interval_map* destination,
    ft_persistent_interval_map* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    /* Both embedded facades contain policy pointers aimed back into their own
     * containing structs. Their move functions repair those address-sensitive
     * links; raw assignment would retain pointers into a temporary candidate. */
    ft_interval_tree_move(&destination->intervals, &source->intervals);
    ft_sorted_map_move(&destination->values, &source->values);
    destination->context = source->context;
    source->context = NULL;
}

void ft_persistent_interval_map_dispose(ft_persistent_interval_map* map)
{
    if (map == NULL) {
        return;
    }
    ft_interval_tree_dispose(&map->intervals);
    ft_sorted_map_dispose(&map->values);
    ft_interval_map_context_release(map->context);
    (void)memset(map, 0, sizeof(*map));
}

bool ft_persistent_interval_map_empty(const ft_persistent_interval_map* map)
{
    return !ft_interval_map_valid(map) || ft_sorted_map_empty(&map->values);
}

size_t ft_persistent_interval_map_size(const ft_persistent_interval_map* map)
{
    return ft_interval_map_valid(map) ? ft_sorted_map_size(&map->values) : 0u;
}

bool ft_persistent_interval_map_contains_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high)
{
    if (!ft_interval_map_interval_valid(map, low, high)) {
        return false;
    }
    const ft_interval_map_key key = ft_interval_map_probe(low, high);
    return ft_sorted_map_contains_key(&map->values, &key);
}

ft_status ft_persistent_interval_map_try_get(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    void* value)
{
    if (!ft_interval_map_interval_valid(map, low, high) || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const ft_interval_map_key key = ft_interval_map_probe(low, high);
    return ft_sorted_map_try_get(&map->values, &key, found, value);
}

ft_status ft_persistent_interval_map_entry_at(
    const ft_persistent_interval_map* map,
    size_t index,
    void* low,
    void* high,
    void* value)
{
    if (!ft_interval_map_valid(map)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_interval_map_key key;
    const ft_status status =
        ft_sorted_map_entry_at(&map->values, index, &key, value);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (low != NULL) {
        ft_interval_map_value_copy(&map->context->endpoint_type, low, key.low);
    }
    if (high != NULL) {
        ft_interval_map_value_copy(&map->context->endpoint_type, high, key.high);
    }
    ft_interval_map_key_destroy(&key, map->context);
    return FT_STATUS_OK;
}

ft_status ft_persistent_interval_map_add(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result)
{
    if (!ft_interval_map_interval_valid(map, low, high)
        || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_persistent_interval_map_contains_key(map, low, high)) {
        return FT_STATUS_ALREADY_EXISTS;
    }
    ft_interval_map_key key;
    ft_status status = ft_interval_map_key_init(map->context, low, high, &key);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_interval_tree intervals;
    status = ft_interval_tree_insert(&map->intervals, low, high, &intervals);
    if (status != FT_STATUS_OK) {
        ft_interval_map_key_destroy(&key, map->context);
        return status;
    }
    ft_sorted_map values;
    status = ft_sorted_map_insert(&map->values, &key, value, &values);
    ft_interval_map_key_destroy(&key, map->context);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(&intervals);
        return status;
    }
    ft_persistent_interval_map candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    ft_interval_tree_move(&candidate.intervals, &intervals);
    ft_sorted_map_move(&candidate.values, &values);
    candidate.context = map->context;
    ft_interval_map_context_retain(candidate.context);
    ft_interval_map_publish(map, result, &candidate);
    return FT_STATUS_OK;
}

ft_status ft_persistent_interval_map_set(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map* result)
{
    if (!ft_interval_map_interval_valid(map, low, high)
        || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const ft_interval_map_key probe = ft_interval_map_probe(low, high);
    bool found = false;
    void* old_value = malloc(map->context->value_type.size);
    if (old_value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_status status =
        ft_sorted_map_try_get(&map->values, &probe, &found, old_value);
    if (status != FT_STATUS_OK) {
        free(old_value);
        return status;
    }
    if (!found) {
        free(old_value);
        return ft_persistent_interval_map_add(map, low, high, value, result);
    }
    const bool equal = map->context->value_equal(
        old_value, value, map->context->value_equal_context);
    ft_interval_map_value_destroy(&map->context->value_type, old_value);
    free(old_value);
    if (equal) {
        return ft_interval_map_publish_copy(map, result);
    }

    bool indexed = false;
    size_t index = 0u;
    status = ft_sorted_map_index_of_key(
        &map->values, &probe, &indexed, &index);
    if (status != FT_STATUS_OK || !indexed) {
        return status != FT_STATUS_OK ? status : FT_STATUS_INVALID_ARGUMENT;
    }
    ft_interval_map_key stored_key;
    status = ft_sorted_map_entry_at(
        &map->values, index, &stored_key, NULL);
    if (status != FT_STATUS_OK) {
        return status;
    }

    /* A probe borrows its endpoint pointers and must never become a stored
     * key. Reuse the owned representative while replacing only its value. */
    ft_sorted_map values;
    status = ft_sorted_map_set(&map->values, &stored_key, value, &values);
    ft_interval_map_key_destroy(&stored_key, map->context);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_interval_tree intervals;
    status = ft_interval_tree_copy(&map->intervals, &intervals);
    if (status != FT_STATUS_OK) {
        ft_sorted_map_dispose(&values);
        return status;
    }
    ft_persistent_interval_map candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    ft_interval_tree_move(&candidate.intervals, &intervals);
    ft_sorted_map_move(&candidate.values, &values);
    candidate.context = map->context;
    ft_interval_map_context_retain(candidate.context);
    ft_interval_map_publish(map, result, &candidate);
    return FT_STATUS_OK;
}

ft_status ft_persistent_interval_map_remove(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map* result)
{
    if (!ft_interval_map_interval_valid(map, low, high) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (!ft_persistent_interval_map_contains_key(map, low, high)) {
        return ft_interval_map_publish_copy(map, result);
    }
    ft_interval_tree intervals;
    ft_status status =
        ft_interval_tree_remove_one(&map->intervals, low, high, &intervals);
    if (status != FT_STATUS_OK) {
        return status;
    }
    const ft_interval_map_key probe = ft_interval_map_probe(low, high);
    ft_sorted_map values;
    status = ft_sorted_map_remove(&map->values, &probe, &values);
    if (status != FT_STATUS_OK) {
        ft_interval_tree_dispose(&intervals);
        return status;
    }
    ft_persistent_interval_map candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    ft_interval_tree_move(&candidate.intervals, &intervals);
    ft_sorted_map_move(&candidate.values, &values);
    candidate.context = map->context;
    ft_interval_map_context_retain(candidate.context);
    ft_interval_map_publish(map, result, &candidate);
    return FT_STATUS_OK;
}

ft_status ft_persistent_interval_map_clear(
    const ft_persistent_interval_map* map,
    ft_persistent_interval_map* result)
{
    if (!ft_interval_map_valid(map) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_persistent_interval_map_empty(map)) {
        return ft_interval_map_publish_copy(map, result);
    }
    ft_persistent_interval_map candidate;
    const ft_status status = ft_persistent_interval_map_init(
        &candidate,
        &map->context->endpoint_type,
        &map->context->value_type,
        map->context->compare_endpoint,
        map->context->compare_context,
        map->context->value_equal,
        map->context->value_equal_context);
    if (status == FT_STATUS_OK) {
        ft_interval_map_publish(map, result, &candidate);
    }
    return status;
}

ft_status ft_persistent_interval_map_try_find_overlap(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    void* overlap_low,
    void* overlap_high,
    void* value)
{
    if (!ft_interval_map_interval_valid(map, query_low, query_high)
        || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = map->context->endpoint_type.size;
    void* low = malloc(size);
    void* high = malloc(size);
    if (low == NULL || high == NULL) {
        free(low);
        free(high);
        return FT_STATUS_NO_MEMORY;
    }
    ft_status status = ft_interval_tree_try_find_overlap(
        &map->intervals, query_low, query_high, found, low, high);
    if (status == FT_STATUS_OK && *found) {
        const ft_interval_map_key probe = ft_interval_map_probe(low, high);
        bool value_found = false;
        status = ft_sorted_map_try_get(
            &map->values, &probe, &value_found, value);
        if (status == FT_STATUS_OK && !value_found) {
            status = FT_STATUS_INVALID_ARGUMENT;
        }
        if (status == FT_STATUS_OK && overlap_low != NULL) {
            ft_interval_map_value_copy(
                &map->context->endpoint_type, overlap_low, low);
        }
        if (status == FT_STATUS_OK && overlap_high != NULL) {
            ft_interval_map_value_copy(
                &map->context->endpoint_type, overlap_high, high);
        }
        ft_interval_map_value_destroy(&map->context->endpoint_type, low);
        ft_interval_map_value_destroy(&map->context->endpoint_type, high);
    }
    free(low);
    free(high);
    return status;
}

size_t ft_persistent_interval_map_count_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high)
{
    return ft_interval_map_interval_valid(map, query_low, query_high)
        ? ft_interval_tree_count_overlaps(
            &map->intervals, query_low, query_high)
        : 0u;
}

typedef struct ft_interval_map_visit_context {
    const ft_persistent_interval_map* map;
    const void* query_low;
    const void* query_high;
    ft_interval_map_visit_fn visitor;
    void* visitor_context;
    bool valid;
} ft_interval_map_visit_context;

static void ft_interval_map_visit_entry(
    const void* raw_key,
    const void* value,
    void* raw_context)
{
    ft_interval_map_visit_context* context =
        (ft_interval_map_visit_context*)raw_context;
    const ft_interval_map_key* key = (const ft_interval_map_key*)raw_key;
    const bool overlaps = context->query_low == NULL
        || (context->map->context->compare_endpoint(
                key->low,
                context->query_high,
                context->map->context->compare_context) <= 0
            && context->map->context->compare_endpoint(
                context->query_low,
                key->high,
                context->map->context->compare_context) <= 0);
    if (overlaps) {
        context->visitor(key->low, key->high, value, context->visitor_context);
    }
}

ft_status ft_persistent_interval_map_visit(
    const ft_persistent_interval_map* map,
    ft_interval_map_visit_fn visitor,
    void* context)
{
    if (!ft_interval_map_valid(map) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_interval_map_visit_context visit = {
        map, NULL, NULL, visitor, context, true
    };
    return ft_sorted_map_visit(
        &map->values, ft_interval_map_visit_entry, &visit);
}

ft_status ft_persistent_interval_map_visit_overlaps(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    ft_interval_map_visit_fn visitor,
    void* context)
{
    if (!ft_interval_map_interval_valid(map, query_low, query_high)
        || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_interval_map_visit_context visit = {
        map, query_low, query_high, visitor, context, true
    };
    return ft_sorted_map_visit(
        &map->values, ft_interval_map_visit_entry, &visit);
}

static void ft_interval_map_validate_entry(
    const void* raw_key,
    const void* value,
    void* raw_context)
{
    (void)value;
    ft_interval_map_visit_context* context =
        (ft_interval_map_visit_context*)raw_context;
    const ft_interval_map_key* key = (const ft_interval_map_key*)raw_key;
    if (key->object == NULL
        || !ft_interval_tree_contains(
            &context->map->intervals, key->low, key->high)) {
        context->valid = false;
    }
}

bool ft_persistent_interval_map_debug_validate(
    const ft_persistent_interval_map* map)
{
    if (!ft_interval_map_valid(map)
        || ft_interval_tree_size(&map->intervals)
            != ft_sorted_map_size(&map->values)) {
        return false;
    }
    ft_interval_map_visit_context context = {
        map, NULL, NULL, NULL, NULL, true
    };
    return ft_sorted_map_visit(
        &map->values, ft_interval_map_validate_entry, &context) == FT_STATUS_OK
        && context.valid;
}

static ft_status ft_interval_map_cursor_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool upper_bound,
    size_t* position,
    bool* found)
{
    if (!ft_interval_map_interval_valid(map, low, high) ||
        position == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const ft_interval_map_key probe = ft_interval_map_probe(low, high);
    size_t rank = 0;
    bool located = false;
    ft_status status = ft_sorted_map_index_of_key(
        &map->values, &probe, &located, &rank);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (upper_bound && located) {
        if (rank == SIZE_MAX) {
            return FT_STATUS_OVERFLOW;
        }
        ++rank;
    }
    *position = rank;
    *found = located;
    return FT_STATUS_OK;
}

static ft_status ft_interval_map_cursor_key_at(
    const ft_persistent_interval_map* map,
    size_t position,
    ft_interval_map_key* key)
{
    (void)memset(key, 0, sizeof(*key));
    return ft_sorted_map_entry_at(&map->values, position, key, NULL);
}

static bool ft_interval_map_cursor_is_valid(
    const ft_persistent_interval_map_cursor* cursor)
{
    return cursor != NULL && ft_interval_map_valid(&cursor->map) &&
        cursor->position <= ft_persistent_interval_map_size(&cursor->map);
}

static ft_status ft_interval_map_cursor_stage(
    const ft_persistent_interval_map* map,
    size_t position,
    ft_persistent_interval_map_cursor* result)
{
    (void)memset(result, 0, sizeof(*result));
    ft_status status = ft_persistent_interval_map_copy(map, &result->map);
    if (status == FT_STATUS_OK) {
        result->position = position;
    }
    return status;
}

static void ft_interval_map_cursor_publish(
    const ft_persistent_interval_map_cursor* source,
    ft_persistent_interval_map_cursor* staged,
    ft_persistent_interval_map_cursor* result)
{
    if (result == source) {
        ft_persistent_interval_map_cursor_dispose(result);
    }
    ft_persistent_interval_map_cursor_move(result, staged);
}

static void ft_interval_map_cursor_publish_map(
    const ft_persistent_interval_map_cursor* source,
    ft_persistent_interval_map* map,
    size_t position,
    ft_persistent_interval_map_cursor* result)
{
    ft_persistent_interval_map_cursor staged;
    (void)memset(&staged, 0, sizeof(staged));
    ft_persistent_interval_map_move(&staged.map, map);
    staged.position = position;
    ft_interval_map_cursor_publish(source, &staged, result);
}

ft_status ft_persistent_interval_map_get_cursor(
    const ft_persistent_interval_map* map,
    size_t position,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_valid(map) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_persistent_interval_map_size(map)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    return ft_interval_map_cursor_stage(map, position, result);
}

static ft_status ft_interval_map_get_bound_cursor(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool upper_bound,
    bool* found,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_interval_valid(map, low, high) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t position = 0;
    bool located = false;
    ft_status status = ft_interval_map_cursor_bound(
        map, low, high, upper_bound, &position, &located);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_persistent_interval_map_cursor staged;
    status = ft_interval_map_cursor_stage(map, position, &staged);
    if (status == FT_STATUS_OK) {
        if (found != NULL) {
            *found = located;
        }
        ft_persistent_interval_map_cursor_move(result, &staged);
    }
    return status;
}

ft_status ft_persistent_interval_map_get_cursor_lower_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map_cursor* result)
{
    return ft_interval_map_get_bound_cursor(
        map, low, high, false, NULL, result);
}

ft_status ft_persistent_interval_map_get_cursor_upper_bound(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    ft_persistent_interval_map_cursor* result)
{
    return ft_interval_map_get_bound_cursor(
        map, low, high, true, NULL, result);
}

ft_status ft_persistent_interval_map_get_cursor_at_key(
    const ft_persistent_interval_map* map,
    const void* low,
    const void* high,
    bool* found,
    ft_persistent_interval_map_cursor* result)
{
    return found == NULL
        ? FT_STATUS_INVALID_ARGUMENT
        : ft_interval_map_get_bound_cursor(
            map, low, high, false, found, result);
}

static ft_status ft_interval_map_find_overlap_from(
    const ft_persistent_interval_map* map,
    size_t start,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_interval_valid(map, query_low, query_high) ||
        found == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_persistent_interval_map_size(map);
    if (start > size) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    bool located = false;
    size_t position = size;
    for (size_t rank = start; rank != size; ++rank) {
        ft_interval_map_key key;
        ft_status status = ft_interval_map_cursor_key_at(map, rank, &key);
        if (status != FT_STATUS_OK) {
            return status;
        }
        const bool past = map->context->compare_endpoint(
            key.low, query_high, map->context->compare_context) > 0;
        const bool overlap = !past && map->context->compare_endpoint(
            query_low, key.high, map->context->compare_context) <= 0;
        ft_interval_map_key_destroy(&key, map->context);
        if (past) {
            break;
        }
        if (overlap) {
            located = true;
            position = rank;
            break;
        }
    }
    ft_persistent_interval_map_cursor staged;
    ft_status status = ft_interval_map_cursor_stage(map, position, &staged);
    if (status == FT_STATUS_OK) {
        *found = located;
        ft_persistent_interval_map_cursor_move(result, &staged);
    }
    return status;
}

ft_status ft_persistent_interval_map_find_overlap_cursor(
    const ft_persistent_interval_map* map,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result)
{
    return ft_interval_map_find_overlap_from(
        map, 0, query_low, query_high, found, result);
}

ft_status ft_persistent_interval_map_find_containing_cursor(
    const ft_persistent_interval_map* map,
    const void* point,
    bool* found,
    ft_persistent_interval_map_cursor* result)
{
    return ft_interval_map_find_overlap_from(
        map, 0, point, point, found, result);
}

ft_status ft_persistent_interval_map_cursor_copy(
    const ft_persistent_interval_map_cursor* source,
    ft_persistent_interval_map_cursor* destination)
{
    if (!ft_interval_map_cursor_is_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_interval_map_cursor_stage(
        &source->map, source->position, destination);
}

void ft_persistent_interval_map_cursor_move(
    ft_persistent_interval_map_cursor* destination,
    ft_persistent_interval_map_cursor* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    ft_persistent_interval_map_move(&destination->map, &source->map);
    destination->position = source->position;
    source->position = 0;
}

void ft_persistent_interval_map_cursor_dispose(ft_persistent_interval_map_cursor* cursor)
{
    if (cursor != NULL) {
        ft_persistent_interval_map_dispose(&cursor->map);
        (void)memset(cursor, 0, sizeof(*cursor));
    }
}

bool ft_persistent_interval_map_cursor_valid(
    const ft_persistent_interval_map_cursor* cursor)
{
    return ft_interval_map_cursor_is_valid(cursor);
}

bool ft_persistent_interval_map_cursor_empty(
    const ft_persistent_interval_map_cursor* cursor)
{
    return !ft_interval_map_cursor_is_valid(cursor) ||
        ft_persistent_interval_map_empty(&cursor->map);
}

size_t ft_persistent_interval_map_cursor_size(
    const ft_persistent_interval_map_cursor* cursor)
{
    return ft_interval_map_cursor_is_valid(cursor)
        ? ft_persistent_interval_map_size(&cursor->map)
        : 0;
}

size_t ft_persistent_interval_map_cursor_position(
    const ft_persistent_interval_map_cursor* cursor)
{
    return ft_interval_map_cursor_is_valid(cursor) ? cursor->position : 0;
}

ft_status ft_persistent_interval_map_cursor_is_at_start(
    const ft_persistent_interval_map_cursor* cursor,
    bool* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == 0;
    return FT_STATUS_OK;
}

ft_status ft_persistent_interval_map_cursor_is_at_end(
    const ft_persistent_interval_map_cursor* cursor,
    bool* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == ft_persistent_interval_map_size(&cursor->map);
    return FT_STATUS_OK;
}

static ft_status ft_interval_map_cursor_peek(
    const ft_persistent_interval_map_cursor* cursor,
    size_t position,
    bool* found,
    void* low,
    void* high,
    void* value)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position >= ft_persistent_interval_map_size(&cursor->map)) {
        *found = false;
        return FT_STATUS_OK;
    }
    ft_status status = ft_persistent_interval_map_entry_at(
        &cursor->map, position, low, high, value);
    if (status == FT_STATUS_OK) {
        *found = true;
    }
    return status;
}

ft_status ft_persistent_interval_map_cursor_try_peek_previous(
    const ft_persistent_interval_map_cursor* cursor,
    bool* found,
    void* low,
    void* high,
    void* value)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        *found = false;
        return FT_STATUS_OK;
    }
    return ft_interval_map_cursor_peek(
        cursor, cursor->position - 1u, found, low, high, value);
}

ft_status ft_persistent_interval_map_cursor_try_peek_next(
    const ft_persistent_interval_map_cursor* cursor,
    bool* found,
    void* low,
    void* high,
    void* value)
{
    return ft_interval_map_cursor_peek(
        cursor,
        cursor == NULL ? 0 : cursor->position,
        found,
        low,
        high,
        value);
}

ft_status ft_persistent_interval_map_cursor_seek_rank(
    const ft_persistent_interval_map_cursor* cursor,
    size_t position,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_persistent_interval_map_size(&cursor->map)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (cursor == result && cursor->position == position) {
        return FT_STATUS_OK;
    }
    ft_persistent_interval_map_cursor staged;
    ft_status status = ft_interval_map_cursor_stage(
        &cursor->map, position, &staged);
    if (status == FT_STATUS_OK) {
        ft_interval_map_cursor_publish(cursor, &staged, result);
    }
    return status;
}

ft_status ft_persistent_interval_map_cursor_move_previous(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_persistent_interval_map_empty(&cursor->map)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_persistent_interval_map_cursor_seek_rank(
        cursor, cursor->position - 1u, result);
}

ft_status ft_persistent_interval_map_cursor_move_next(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_persistent_interval_map_size(&cursor->map);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_persistent_interval_map_cursor_seek_rank(
        cursor, cursor->position + 1u, result);
}

ft_status ft_persistent_interval_map_cursor_seek_next_overlap(
    const ft_persistent_interval_map_cursor* cursor,
    const void* query_low,
    const void* query_high,
    bool* found,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || found == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_persistent_interval_map_size(&cursor->map);
    const size_t start = cursor->position == size
        ? size
        : cursor->position + 1u;
    ft_persistent_interval_map_cursor staged;
    bool located = false;
    ft_status status = ft_interval_map_find_overlap_from(
        &cursor->map,
        start,
        query_low,
        query_high,
        &located,
        &staged);
    if (status == FT_STATUS_OK) {
        *found = located;
        ft_interval_map_cursor_publish(cursor, &staged, result);
    }
    return status;
}

ft_status ft_persistent_interval_map_cursor_insert(
    const ft_persistent_interval_map_cursor* cursor,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t position = 0;
    bool found = false;
    ft_status status = ft_interval_map_cursor_bound(
        &cursor->map, low, high, false, &position, &found);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (found) {
        return FT_STATUS_ALREADY_EXISTS;
    }
    if (position == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ft_persistent_interval_map edited;
    status = ft_persistent_interval_map_add(
        &cursor->map, low, high, value, &edited);
    if (status == FT_STATUS_OK) {
        ft_interval_map_cursor_publish_map(
            cursor, &edited, position + 1u, result);
    }
    return status;
}

ft_status ft_persistent_interval_map_cursor_set_item(
    const ft_persistent_interval_map_cursor* cursor,
    const void* low,
    const void* high,
    const void* value,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t position = 0;
    bool found = false;
    ft_status status = ft_interval_map_cursor_bound(
        &cursor->map, low, high, false, &position, &found);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (!found && position == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ft_persistent_interval_map edited;
    status = ft_persistent_interval_map_set(
        &cursor->map, low, high, value, &edited);
    if (status == FT_STATUS_OK) {
        ft_interval_map_cursor_publish_map(
            cursor, &edited, found ? position : position + 1u, result);
    }
    return status;
}

ft_status ft_persistent_interval_map_cursor_set_next(
    const ft_persistent_interval_map_cursor* cursor,
    const void* value,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == ft_persistent_interval_map_size(&cursor->map)) {
        return ft_persistent_interval_map_empty(&cursor->map)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    ft_interval_map_key key;
    ft_status status = ft_interval_map_cursor_key_at(
        &cursor->map, cursor->position, &key);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_persistent_interval_map edited;
    status = ft_persistent_interval_map_set(
        &cursor->map, key.low, key.high, value, &edited);
    ft_interval_map_key_destroy(&key, cursor->map.context);
    if (status == FT_STATUS_OK) {
        ft_interval_map_cursor_publish_map(
            cursor, &edited, cursor->position, result);
    }
    return status;
}

static ft_status ft_interval_map_cursor_delete_at(
    const ft_persistent_interval_map_cursor* cursor,
    size_t rank,
    size_t position,
    ft_persistent_interval_map_cursor* result)
{
    ft_interval_map_key key;
    ft_status status = ft_interval_map_cursor_key_at(&cursor->map, rank, &key);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_persistent_interval_map edited;
    status = ft_persistent_interval_map_remove(
        &cursor->map, key.low, key.high, &edited);
    ft_interval_map_key_destroy(&key, cursor->map.context);
    if (status == FT_STATUS_OK) {
        ft_interval_map_cursor_publish_map(cursor, &edited, position, result);
    }
    return status;
}

ft_status ft_persistent_interval_map_cursor_delete_previous(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_persistent_interval_map_empty(&cursor->map)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_interval_map_cursor_delete_at(
        cursor, cursor->position - 1u, cursor->position - 1u, result);
}

ft_status ft_persistent_interval_map_cursor_delete_next(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map_cursor* result)
{
    if (!ft_interval_map_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_persistent_interval_map_size(&cursor->map);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_interval_map_cursor_delete_at(
        cursor, cursor->position, cursor->position, result);
}

ft_status ft_persistent_interval_map_cursor_snapshot(
    const ft_persistent_interval_map_cursor* cursor,
    ft_persistent_interval_map* result)
{
    return !ft_interval_map_cursor_is_valid(cursor) || result == NULL
        ? FT_STATUS_INVALID_ARGUMENT
        : ft_persistent_interval_map_copy(&cursor->map, result);
}
