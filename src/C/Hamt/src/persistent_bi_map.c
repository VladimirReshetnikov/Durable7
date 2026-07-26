/*
 * Implementation of the persistent bidirectional map.
 */

#include <durable7/hamt/persistent_bi_map.h>

#include <stdlib.h>
#include <string.h>

typedef struct d7_hamt_bi_map_policy_state {
    size_t ref_count;
    d7_hamt_set_policy keys;
    d7_hamt_set_policy values;
} d7_hamt_bi_map_policy_state;

static d7_hamt_set_policy normalize_set_policy(const d7_hamt_set_policy *policy) {
    d7_hamt_set_policy result = policy == NULL ? d7_hamt_set_policy_default() : *policy;
    const d7_hamt_set_policy defaults = d7_hamt_set_policy_default();
    if (result.hash == NULL) result.hash = defaults.hash;
    if (result.equal == NULL) result.equal = defaults.equal;
    return result;
}

static uint32_t key_hash(const void *item, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    return state->keys.hash(item, state->keys.context);
}
static uint32_t value_hash(const void *item, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    return state->values.hash(item, state->values.context);
}
static bool keys_equal(const void *left, const void *right, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    return state->keys.equal(left, right, state->keys.context);
}
static bool values_equal(const void *left, const void *right, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    return state->values.equal(left, right, state->values.context);
}
static void *retain_key(const void *item, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    return state->keys.retain_item == NULL
        ? (void *)item
        : state->keys.retain_item(item, state->keys.context);
}
static void *retain_value(const void *item, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    return state->values.retain_item == NULL
        ? (void *)item
        : state->values.retain_item(item, state->values.context);
}
static void release_key(void *item, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    if (state->keys.release_item != NULL) state->keys.release_item(item, state->keys.context);
}
static void release_value(void *item, void *context) {
    d7_hamt_bi_map_policy_state *state = context;
    if (state->values.release_item != NULL) state->values.release_item(item, state->values.context);
}

static bool active_keys_equal(const d7_hamt_bi_map *map, const void *left, const void *right) {
    return map->policy_inverted
        ? values_equal(left, right, map->policy_state)
        : keys_equal(left, right, map->policy_state);
}

static bool active_values_equal(const d7_hamt_bi_map *map, const void *left, const void *right) {
    return map->policy_inverted
        ? keys_equal(left, right, map->policy_state)
        : values_equal(left, right, map->policy_state);
}

static d7_hamt_policy forward_policy(d7_hamt_bi_map_policy_state *state) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = key_hash;
    policy.key_equal = keys_equal;
    policy.value_equal = values_equal;
    policy.retain_key = retain_key;
    policy.retain_value = retain_value;
    policy.release_key = release_key;
    policy.release_value = release_value;
    policy.context = state;
    return policy;
}

static d7_hamt_policy inverse_policy(d7_hamt_bi_map_policy_state *state) {
    d7_hamt_policy policy = d7_hamt_policy_default();
    policy.hash = value_hash;
    policy.key_equal = values_equal;
    policy.value_equal = keys_equal;
    policy.retain_key = retain_value;
    policy.retain_value = retain_key;
    policy.release_key = release_value;
    policy.release_value = release_key;
    policy.context = state;
    return policy;
}

static void state_retain(d7_hamt_bi_map_policy_state *state) {
    if (state != NULL) ++state->ref_count;
}
static void state_release(d7_hamt_bi_map_policy_state *state) {
    if (state != NULL && --state->ref_count == 0) free(state);
}

static void publish(
    const d7_hamt_bi_map *source,
    d7_hamt_bi_map *result,
    d7_hamt_bi_map *next) {
    if (result == source) d7_hamt_bi_map_destroy(result);
    *result = *next;
    memset(next, 0, sizeof(*next));
}

static d7_hamt_bi_map compose(
    d7_hamt_map forward,
    d7_hamt_map inverse,
    d7_hamt_bi_map_policy_state *state,
    bool policy_inverted) {
    d7_hamt_bi_map result;
    result.forward = forward;
    result.inverse = inverse;
    result.policy_state = state;
    result.policy_inverted = policy_inverted;
    state_retain(state);
    return result;
}

d7_hamt_status d7_hamt_bi_map_create(
    const d7_hamt_set_policy *key_policy,
    const d7_hamt_set_policy *value_policy,
    d7_hamt_bi_map *result) {
    if (result == NULL) return D7_HAMT_INVALID_ARGUMENT;
    d7_hamt_bi_map_policy_state *state = malloc(sizeof(*state));
    if (state == NULL) return D7_HAMT_OUT_OF_MEMORY;
    state->ref_count = 1;
    state->keys = normalize_set_policy(key_policy);
    state->values = normalize_set_policy(value_policy);
    const d7_hamt_policy forward = forward_policy(state);
    const d7_hamt_policy inverse = inverse_policy(state);
    result->forward = d7_hamt_map_create(&forward);
    result->inverse = d7_hamt_map_create(&inverse);
    result->policy_state = state;
    result->policy_inverted = false;
    return D7_HAMT_OK;
}

d7_hamt_bi_map d7_hamt_bi_map_clone(const d7_hamt_bi_map *map) {
    d7_hamt_bi_map clone;
    memset(&clone, 0, sizeof(clone));
    if (map == NULL || map->policy_state == NULL) return clone;
    clone.forward = d7_hamt_map_clone(&map->forward);
    clone.inverse = d7_hamt_map_clone(&map->inverse);
    clone.policy_state = map->policy_state;
    clone.policy_inverted = map->policy_inverted;
    state_retain(clone.policy_state);
    return clone;
}

void d7_hamt_bi_map_destroy(d7_hamt_bi_map *map) {
    if (map == NULL) return;
    d7_hamt_map_destroy(&map->forward);
    d7_hamt_map_destroy(&map->inverse);
    state_release(map->policy_state);
    memset(map, 0, sizeof(*map));
}

size_t d7_hamt_bi_map_count(const d7_hamt_bi_map *map) {
    return map == NULL ? 0 : d7_hamt_map_count(&map->forward);
}
bool d7_hamt_bi_map_is_empty(const d7_hamt_bi_map *map) { return d7_hamt_bi_map_count(map) == 0; }
bool d7_hamt_bi_map_contains_key(const d7_hamt_bi_map *map, const void *key) {
    return map != NULL && d7_hamt_map_contains_key(&map->forward, key);
}
bool d7_hamt_bi_map_contains_value(const d7_hamt_bi_map *map, const void *value) {
    return map != NULL && d7_hamt_map_contains_key(&map->inverse, value);
}
bool d7_hamt_bi_map_try_get(const d7_hamt_bi_map *map, const void *key, const void **value) {
    return map != NULL && d7_hamt_map_try_get(&map->forward, key, value);
}
bool d7_hamt_bi_map_try_get_key(const d7_hamt_bi_map *map, const void *value, const void **key) {
    return map != NULL && d7_hamt_map_try_get(&map->inverse, value, key);
}

d7_hamt_status d7_hamt_bi_map_try_add(
    const d7_hamt_bi_map *map,
    const void *key,
    const void *value,
    d7_hamt_bi_map *result,
    bool *added,
    d7_hamt_bi_map_conflict *conflict) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return D7_HAMT_INVALID_ARGUMENT;
    if (added != NULL) *added = false;
    if (conflict != NULL) *conflict = D7_HAMT_BI_MAP_NO_CONFLICT;
    if (d7_hamt_map_contains_key(&map->forward, key)) {
        d7_hamt_bi_map next = d7_hamt_bi_map_clone(map);
        publish(map, result, &next);
        if (conflict != NULL) *conflict = D7_HAMT_BI_MAP_KEY_CONFLICT;
        return D7_HAMT_OK;
    }
    if (d7_hamt_map_contains_key(&map->inverse, value)) {
        d7_hamt_bi_map next = d7_hamt_bi_map_clone(map);
        publish(map, result, &next);
        if (conflict != NULL) *conflict = D7_HAMT_BI_MAP_VALUE_CONFLICT;
        return D7_HAMT_OK;
    }
    d7_hamt_map forward;
    d7_hamt_status status = d7_hamt_map_add(&map->forward, key, value, &forward);
    if (status != D7_HAMT_OK) return status;
    d7_hamt_map inverse;
    status = d7_hamt_map_add(&map->inverse, value, key, &inverse);
    if (status != D7_HAMT_OK) {
        d7_hamt_map_destroy(&forward);
        return status;
    }
    d7_hamt_bi_map next = compose(forward, inverse, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    if (added != NULL) *added = true;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_bi_map_add(
    const d7_hamt_bi_map *map, const void *key, const void *value, d7_hamt_bi_map *result) {
    bool added = false;
    d7_hamt_bi_map_conflict conflict = D7_HAMT_BI_MAP_NO_CONFLICT;
    d7_hamt_bi_map candidate;
    const d7_hamt_status status = d7_hamt_bi_map_try_add(map, key, value, &candidate, &added, &conflict);
    if (status != D7_HAMT_OK) return status;
    if (!added) {
        d7_hamt_bi_map_destroy(&candidate);
        return conflict == D7_HAMT_BI_MAP_KEY_CONFLICT
            ? D7_HAMT_DUPLICATE_KEY
            : D7_HAMT_DUPLICATE_VALUE;
    }
    publish(map, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_bi_map_set(
    const d7_hamt_bi_map *map, const void *key, const void *value, d7_hamt_bi_map *result) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return D7_HAMT_INVALID_ARGUMENT;
    const void *old_value = NULL;
    if (!d7_hamt_map_try_get(&map->forward, key, &old_value)) {
        if (d7_hamt_map_contains_key(&map->inverse, value)) return D7_HAMT_DUPLICATE_VALUE;
        return d7_hamt_bi_map_add(map, key, value, result);
    }
    if (active_values_equal(map, old_value, value)) {
        d7_hamt_bi_map next = d7_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return D7_HAMT_OK;
    }
    if (d7_hamt_map_contains_key(&map->inverse, value)) return D7_HAMT_DUPLICATE_VALUE;
    const void *stored_key = NULL;
    if (!d7_hamt_map_try_get_key(&map->forward, key, &stored_key)) return D7_HAMT_INVALID_ARGUMENT;

    d7_hamt_map fr, fn, ir, in;
    d7_hamt_status status = d7_hamt_map_remove(&map->forward, stored_key, &fr);
    if (status != D7_HAMT_OK) return status;
    status = d7_hamt_map_add(&fr, stored_key, value, &fn);
    d7_hamt_map_destroy(&fr);
    if (status != D7_HAMT_OK) return status;
    status = d7_hamt_map_remove(&map->inverse, old_value, &ir);
    if (status != D7_HAMT_OK) { d7_hamt_map_destroy(&fn); return status; }
    status = d7_hamt_map_add(&ir, value, stored_key, &in);
    d7_hamt_map_destroy(&ir);
    if (status != D7_HAMT_OK) { d7_hamt_map_destroy(&fn); return status; }
    d7_hamt_bi_map next = compose(fn, in, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    return D7_HAMT_OK;
}

static d7_hamt_status remove_pair(
    const d7_hamt_bi_map *map,
    const void *stored_key,
    const void *stored_value,
    d7_hamt_bi_map *result) {
    d7_hamt_map forward;
    d7_hamt_status status = d7_hamt_map_remove(&map->forward, stored_key, &forward);
    if (status != D7_HAMT_OK) return status;
    d7_hamt_map inverse;
    status = d7_hamt_map_remove(&map->inverse, stored_value, &inverse);
    if (status != D7_HAMT_OK) { d7_hamt_map_destroy(&forward); return status; }
    d7_hamt_bi_map next = compose(forward, inverse, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_bi_map_try_remove_key(
    const d7_hamt_bi_map *map, const void *key, d7_hamt_bi_map *result,
    bool *removed, const void **opposite_value) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return D7_HAMT_INVALID_ARGUMENT;
    if (removed != NULL) *removed = false;
    if (opposite_value != NULL) *opposite_value = NULL;
    const void *stored_key = NULL;
    const void *stored_value = NULL;
    if (!d7_hamt_map_try_get_key(&map->forward, key, &stored_key)
        || !d7_hamt_map_try_get(&map->forward, key, &stored_value)) {
        d7_hamt_bi_map next = d7_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return D7_HAMT_OK;
    }
    const bool aliased = result == map;
    const d7_hamt_status status = remove_pair(map, stored_key, stored_value, result);
    if (status == D7_HAMT_OK) {
        if (removed != NULL) *removed = true;
        if (opposite_value != NULL && !aliased) *opposite_value = stored_value;
    }
    return status;
}
d7_hamt_status d7_hamt_bi_map_remove_key(
    const d7_hamt_bi_map *map, const void *key, d7_hamt_bi_map *result) {
    return d7_hamt_bi_map_try_remove_key(map, key, result, NULL, NULL);
}

d7_hamt_status d7_hamt_bi_map_try_remove_value(
    const d7_hamt_bi_map *map, const void *value, d7_hamt_bi_map *result,
    bool *removed, const void **opposite_key) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return D7_HAMT_INVALID_ARGUMENT;
    if (removed != NULL) *removed = false;
    if (opposite_key != NULL) *opposite_key = NULL;
    const void *stored_value = NULL;
    const void *stored_key = NULL;
    if (!d7_hamt_map_try_get_key(&map->inverse, value, &stored_value)
        || !d7_hamt_map_try_get(&map->inverse, value, &stored_key)) {
        d7_hamt_bi_map next = d7_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return D7_HAMT_OK;
    }
    const bool aliased = result == map;
    const d7_hamt_status status = remove_pair(map, stored_key, stored_value, result);
    if (status == D7_HAMT_OK) {
        if (removed != NULL) *removed = true;
        if (opposite_key != NULL && !aliased) *opposite_key = stored_key;
    }
    return status;
}
d7_hamt_status d7_hamt_bi_map_remove_value(
    const d7_hamt_bi_map *map, const void *value, d7_hamt_bi_map *result) {
    return d7_hamt_bi_map_try_remove_value(map, value, result, NULL, NULL);
}

d7_hamt_status d7_hamt_bi_map_clear(const d7_hamt_bi_map *map, d7_hamt_bi_map *result) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return D7_HAMT_INVALID_ARGUMENT;
    if (d7_hamt_bi_map_is_empty(map)) {
        d7_hamt_bi_map next = d7_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return D7_HAMT_OK;
    }
    d7_hamt_map forward;
    d7_hamt_status status = d7_hamt_map_clear(&map->forward, &forward);
    if (status != D7_HAMT_OK) return status;
    d7_hamt_map inverse;
    status = d7_hamt_map_clear(&map->inverse, &inverse);
    if (status != D7_HAMT_OK) { d7_hamt_map_destroy(&forward); return status; }
    d7_hamt_bi_map next = compose(forward, inverse, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_bi_map_inverse(const d7_hamt_bi_map *map, d7_hamt_bi_map *result) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return D7_HAMT_INVALID_ARGUMENT;
    d7_hamt_bi_map next;
    next.forward = d7_hamt_map_clone(&map->inverse);
    next.inverse = d7_hamt_map_clone(&map->forward);
    next.policy_state = map->policy_state;
    next.policy_inverted = !map->policy_inverted;
    state_retain(next.policy_state);
    publish(map, result, &next);
    return D7_HAMT_OK;
}

void d7_hamt_bi_map_iterator_init(const d7_hamt_bi_map *map, d7_hamt_bi_map_iterator *iterator) {
    if (iterator == NULL) return;
    d7_hamt_map_iterator_init(map == NULL ? NULL : &map->forward, &iterator->inner);
}
bool d7_hamt_bi_map_iterator_next(
    d7_hamt_bi_map_iterator *iterator, const void **key, const void **value) {
    return iterator != NULL && d7_hamt_map_iterator_next(&iterator->inner, key, value);
}
bool d7_hamt_bi_map_shares_roots(const d7_hamt_bi_map *left, const d7_hamt_bi_map *right) {
    return left != NULL && right != NULL
        && d7_hamt_map_shares_root(&left->forward, &right->forward)
        && d7_hamt_map_shares_root(&left->inverse, &right->inverse);
}
bool d7_hamt_bi_map_debug_validate(const d7_hamt_bi_map *map) {
    if (map == NULL || map->policy_state == NULL
        || map->forward.count != map->inverse.count
        || !d7_hamt_map_debug_validate_canonical(&map->forward)
        || !d7_hamt_map_debug_validate_canonical(&map->inverse)) return false;
    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&map->forward, &iterator);
    const void *key;
    const void *value;
    while (d7_hamt_map_iterator_next(&iterator, &key, &value)) {
        const void *inverse_key = NULL;
        if (!d7_hamt_map_try_get(&map->inverse, value, &inverse_key)
            || !active_keys_equal(map, key, inverse_key)) return false;
    }
    d7_hamt_map_iterator_init(&map->inverse, &iterator);
    while (d7_hamt_map_iterator_next(&iterator, &value, &key)) {
        const void *forward_value = NULL;
        if (!d7_hamt_map_try_get(&map->forward, key, &forward_value)
            || !active_values_equal(map, value, forward_value)) return false;
    }
    return true;
}
