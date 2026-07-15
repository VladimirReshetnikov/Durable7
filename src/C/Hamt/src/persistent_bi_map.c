#include <Tools/DataStructures/Hamt/persistent_bi_map.h>

#include <stdlib.h>
#include <string.h>

typedef struct tds_hamt_bi_map_policy_state {
    size_t ref_count;
    tds_hamt_set_policy keys;
    tds_hamt_set_policy values;
} tds_hamt_bi_map_policy_state;

static tds_hamt_set_policy normalize_set_policy(const tds_hamt_set_policy *policy) {
    tds_hamt_set_policy result = policy == NULL ? tds_hamt_set_policy_default() : *policy;
    const tds_hamt_set_policy defaults = tds_hamt_set_policy_default();
    if (result.hash == NULL) result.hash = defaults.hash;
    if (result.equal == NULL) result.equal = defaults.equal;
    return result;
}

static uint32_t key_hash(const void *item, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    return state->keys.hash(item, state->keys.context);
}
static uint32_t value_hash(const void *item, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    return state->values.hash(item, state->values.context);
}
static bool keys_equal(const void *left, const void *right, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    return state->keys.equal(left, right, state->keys.context);
}
static bool values_equal(const void *left, const void *right, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    return state->values.equal(left, right, state->values.context);
}
static void *retain_key(const void *item, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    return state->keys.retain_item == NULL
        ? (void *)item
        : state->keys.retain_item(item, state->keys.context);
}
static void *retain_value(const void *item, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    return state->values.retain_item == NULL
        ? (void *)item
        : state->values.retain_item(item, state->values.context);
}
static void release_key(void *item, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    if (state->keys.release_item != NULL) state->keys.release_item(item, state->keys.context);
}
static void release_value(void *item, void *context) {
    tds_hamt_bi_map_policy_state *state = context;
    if (state->values.release_item != NULL) state->values.release_item(item, state->values.context);
}

static bool active_keys_equal(const tds_hamt_bi_map *map, const void *left, const void *right) {
    return map->policy_inverted
        ? values_equal(left, right, map->policy_state)
        : keys_equal(left, right, map->policy_state);
}

static bool active_values_equal(const tds_hamt_bi_map *map, const void *left, const void *right) {
    return map->policy_inverted
        ? keys_equal(left, right, map->policy_state)
        : values_equal(left, right, map->policy_state);
}

static tds_hamt_policy forward_policy(tds_hamt_bi_map_policy_state *state) {
    tds_hamt_policy policy = tds_hamt_policy_default();
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

static tds_hamt_policy inverse_policy(tds_hamt_bi_map_policy_state *state) {
    tds_hamt_policy policy = tds_hamt_policy_default();
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

static void state_retain(tds_hamt_bi_map_policy_state *state) {
    if (state != NULL) ++state->ref_count;
}
static void state_release(tds_hamt_bi_map_policy_state *state) {
    if (state != NULL && --state->ref_count == 0) free(state);
}

static void publish(
    const tds_hamt_bi_map *source,
    tds_hamt_bi_map *result,
    tds_hamt_bi_map *next) {
    if (result == source) tds_hamt_bi_map_destroy(result);
    *result = *next;
    memset(next, 0, sizeof(*next));
}

static tds_hamt_bi_map compose(
    tds_hamt_map forward,
    tds_hamt_map inverse,
    tds_hamt_bi_map_policy_state *state,
    bool policy_inverted) {
    tds_hamt_bi_map result;
    result.forward = forward;
    result.inverse = inverse;
    result.policy_state = state;
    result.policy_inverted = policy_inverted;
    state_retain(state);
    return result;
}

tds_hamt_status tds_hamt_bi_map_create(
    const tds_hamt_set_policy *key_policy,
    const tds_hamt_set_policy *value_policy,
    tds_hamt_bi_map *result) {
    if (result == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    tds_hamt_bi_map_policy_state *state = malloc(sizeof(*state));
    if (state == NULL) return TDS_HAMT_OUT_OF_MEMORY;
    state->ref_count = 1;
    state->keys = normalize_set_policy(key_policy);
    state->values = normalize_set_policy(value_policy);
    const tds_hamt_policy forward = forward_policy(state);
    const tds_hamt_policy inverse = inverse_policy(state);
    result->forward = tds_hamt_map_create(&forward);
    result->inverse = tds_hamt_map_create(&inverse);
    result->policy_state = state;
    result->policy_inverted = false;
    return TDS_HAMT_OK;
}

tds_hamt_bi_map tds_hamt_bi_map_clone(const tds_hamt_bi_map *map) {
    tds_hamt_bi_map clone;
    memset(&clone, 0, sizeof(clone));
    if (map == NULL || map->policy_state == NULL) return clone;
    clone.forward = tds_hamt_map_clone(&map->forward);
    clone.inverse = tds_hamt_map_clone(&map->inverse);
    clone.policy_state = map->policy_state;
    clone.policy_inverted = map->policy_inverted;
    state_retain(clone.policy_state);
    return clone;
}

void tds_hamt_bi_map_destroy(tds_hamt_bi_map *map) {
    if (map == NULL) return;
    tds_hamt_map_destroy(&map->forward);
    tds_hamt_map_destroy(&map->inverse);
    state_release(map->policy_state);
    memset(map, 0, sizeof(*map));
}

size_t tds_hamt_bi_map_count(const tds_hamt_bi_map *map) {
    return map == NULL ? 0 : tds_hamt_map_count(&map->forward);
}
bool tds_hamt_bi_map_is_empty(const tds_hamt_bi_map *map) { return tds_hamt_bi_map_count(map) == 0; }
bool tds_hamt_bi_map_contains_key(const tds_hamt_bi_map *map, const void *key) {
    return map != NULL && tds_hamt_map_contains_key(&map->forward, key);
}
bool tds_hamt_bi_map_contains_value(const tds_hamt_bi_map *map, const void *value) {
    return map != NULL && tds_hamt_map_contains_key(&map->inverse, value);
}
bool tds_hamt_bi_map_try_get(const tds_hamt_bi_map *map, const void *key, const void **value) {
    return map != NULL && tds_hamt_map_try_get(&map->forward, key, value);
}
bool tds_hamt_bi_map_try_get_key(const tds_hamt_bi_map *map, const void *value, const void **key) {
    return map != NULL && tds_hamt_map_try_get(&map->inverse, value, key);
}

tds_hamt_status tds_hamt_bi_map_try_add(
    const tds_hamt_bi_map *map,
    const void *key,
    const void *value,
    tds_hamt_bi_map *result,
    bool *added,
    tds_hamt_bi_map_conflict *conflict) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    if (added != NULL) *added = false;
    if (conflict != NULL) *conflict = TDS_HAMT_BI_MAP_NO_CONFLICT;
    if (tds_hamt_map_contains_key(&map->forward, key)) {
        tds_hamt_bi_map next = tds_hamt_bi_map_clone(map);
        publish(map, result, &next);
        if (conflict != NULL) *conflict = TDS_HAMT_BI_MAP_KEY_CONFLICT;
        return TDS_HAMT_OK;
    }
    if (tds_hamt_map_contains_key(&map->inverse, value)) {
        tds_hamt_bi_map next = tds_hamt_bi_map_clone(map);
        publish(map, result, &next);
        if (conflict != NULL) *conflict = TDS_HAMT_BI_MAP_VALUE_CONFLICT;
        return TDS_HAMT_OK;
    }
    tds_hamt_map forward;
    tds_hamt_status status = tds_hamt_map_add(&map->forward, key, value, &forward);
    if (status != TDS_HAMT_OK) return status;
    tds_hamt_map inverse;
    status = tds_hamt_map_add(&map->inverse, value, key, &inverse);
    if (status != TDS_HAMT_OK) {
        tds_hamt_map_destroy(&forward);
        return status;
    }
    tds_hamt_bi_map next = compose(forward, inverse, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    if (added != NULL) *added = true;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bi_map_add(
    const tds_hamt_bi_map *map, const void *key, const void *value, tds_hamt_bi_map *result) {
    bool added = false;
    tds_hamt_bi_map_conflict conflict = TDS_HAMT_BI_MAP_NO_CONFLICT;
    tds_hamt_bi_map candidate;
    const tds_hamt_status status = tds_hamt_bi_map_try_add(map, key, value, &candidate, &added, &conflict);
    if (status != TDS_HAMT_OK) return status;
    if (!added) {
        tds_hamt_bi_map_destroy(&candidate);
        return conflict == TDS_HAMT_BI_MAP_KEY_CONFLICT
            ? TDS_HAMT_DUPLICATE_KEY
            : TDS_HAMT_DUPLICATE_VALUE;
    }
    publish(map, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bi_map_set(
    const tds_hamt_bi_map *map, const void *key, const void *value, tds_hamt_bi_map *result) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    const void *old_value = NULL;
    if (!tds_hamt_map_try_get(&map->forward, key, &old_value)) {
        if (tds_hamt_map_contains_key(&map->inverse, value)) return TDS_HAMT_DUPLICATE_VALUE;
        return tds_hamt_bi_map_add(map, key, value, result);
    }
    if (active_values_equal(map, old_value, value)) {
        tds_hamt_bi_map next = tds_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return TDS_HAMT_OK;
    }
    if (tds_hamt_map_contains_key(&map->inverse, value)) return TDS_HAMT_DUPLICATE_VALUE;
    const void *stored_key = NULL;
    if (!tds_hamt_map_try_get_key(&map->forward, key, &stored_key)) return TDS_HAMT_INVALID_ARGUMENT;

    tds_hamt_map fr, fn, ir, in;
    tds_hamt_status status = tds_hamt_map_remove(&map->forward, stored_key, &fr);
    if (status != TDS_HAMT_OK) return status;
    status = tds_hamt_map_add(&fr, stored_key, value, &fn);
    tds_hamt_map_destroy(&fr);
    if (status != TDS_HAMT_OK) return status;
    status = tds_hamt_map_remove(&map->inverse, old_value, &ir);
    if (status != TDS_HAMT_OK) { tds_hamt_map_destroy(&fn); return status; }
    status = tds_hamt_map_add(&ir, value, stored_key, &in);
    tds_hamt_map_destroy(&ir);
    if (status != TDS_HAMT_OK) { tds_hamt_map_destroy(&fn); return status; }
    tds_hamt_bi_map next = compose(fn, in, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    return TDS_HAMT_OK;
}

static tds_hamt_status remove_pair(
    const tds_hamt_bi_map *map,
    const void *stored_key,
    const void *stored_value,
    tds_hamt_bi_map *result) {
    tds_hamt_map forward;
    tds_hamt_status status = tds_hamt_map_remove(&map->forward, stored_key, &forward);
    if (status != TDS_HAMT_OK) return status;
    tds_hamt_map inverse;
    status = tds_hamt_map_remove(&map->inverse, stored_value, &inverse);
    if (status != TDS_HAMT_OK) { tds_hamt_map_destroy(&forward); return status; }
    tds_hamt_bi_map next = compose(forward, inverse, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bi_map_try_remove_key(
    const tds_hamt_bi_map *map, const void *key, tds_hamt_bi_map *result,
    bool *removed, const void **opposite_value) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    if (removed != NULL) *removed = false;
    if (opposite_value != NULL) *opposite_value = NULL;
    const void *stored_key = NULL;
    const void *stored_value = NULL;
    if (!tds_hamt_map_try_get_key(&map->forward, key, &stored_key)
        || !tds_hamt_map_try_get(&map->forward, key, &stored_value)) {
        tds_hamt_bi_map next = tds_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return TDS_HAMT_OK;
    }
    const bool aliased = result == map;
    const tds_hamt_status status = remove_pair(map, stored_key, stored_value, result);
    if (status == TDS_HAMT_OK) {
        if (removed != NULL) *removed = true;
        if (opposite_value != NULL && !aliased) *opposite_value = stored_value;
    }
    return status;
}
tds_hamt_status tds_hamt_bi_map_remove_key(
    const tds_hamt_bi_map *map, const void *key, tds_hamt_bi_map *result) {
    return tds_hamt_bi_map_try_remove_key(map, key, result, NULL, NULL);
}

tds_hamt_status tds_hamt_bi_map_try_remove_value(
    const tds_hamt_bi_map *map, const void *value, tds_hamt_bi_map *result,
    bool *removed, const void **opposite_key) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    if (removed != NULL) *removed = false;
    if (opposite_key != NULL) *opposite_key = NULL;
    const void *stored_value = NULL;
    const void *stored_key = NULL;
    if (!tds_hamt_map_try_get_key(&map->inverse, value, &stored_value)
        || !tds_hamt_map_try_get(&map->inverse, value, &stored_key)) {
        tds_hamt_bi_map next = tds_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return TDS_HAMT_OK;
    }
    const bool aliased = result == map;
    const tds_hamt_status status = remove_pair(map, stored_key, stored_value, result);
    if (status == TDS_HAMT_OK) {
        if (removed != NULL) *removed = true;
        if (opposite_key != NULL && !aliased) *opposite_key = stored_key;
    }
    return status;
}
tds_hamt_status tds_hamt_bi_map_remove_value(
    const tds_hamt_bi_map *map, const void *value, tds_hamt_bi_map *result) {
    return tds_hamt_bi_map_try_remove_value(map, value, result, NULL, NULL);
}

tds_hamt_status tds_hamt_bi_map_clear(const tds_hamt_bi_map *map, tds_hamt_bi_map *result) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    if (tds_hamt_bi_map_is_empty(map)) {
        tds_hamt_bi_map next = tds_hamt_bi_map_clone(map);
        publish(map, result, &next);
        return TDS_HAMT_OK;
    }
    tds_hamt_map forward;
    tds_hamt_status status = tds_hamt_map_clear(&map->forward, &forward);
    if (status != TDS_HAMT_OK) return status;
    tds_hamt_map inverse;
    status = tds_hamt_map_clear(&map->inverse, &inverse);
    if (status != TDS_HAMT_OK) { tds_hamt_map_destroy(&forward); return status; }
    tds_hamt_bi_map next = compose(forward, inverse, map->policy_state, map->policy_inverted);
    publish(map, result, &next);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bi_map_inverse(const tds_hamt_bi_map *map, tds_hamt_bi_map *result) {
    if (map == NULL || result == NULL || map->policy_state == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    tds_hamt_bi_map next;
    next.forward = tds_hamt_map_clone(&map->inverse);
    next.inverse = tds_hamt_map_clone(&map->forward);
    next.policy_state = map->policy_state;
    next.policy_inverted = !map->policy_inverted;
    state_retain(next.policy_state);
    publish(map, result, &next);
    return TDS_HAMT_OK;
}

void tds_hamt_bi_map_iterator_init(const tds_hamt_bi_map *map, tds_hamt_bi_map_iterator *iterator) {
    if (iterator == NULL) return;
    tds_hamt_map_iterator_init(map == NULL ? NULL : &map->forward, &iterator->inner);
}
bool tds_hamt_bi_map_iterator_next(
    tds_hamt_bi_map_iterator *iterator, const void **key, const void **value) {
    return iterator != NULL && tds_hamt_map_iterator_next(&iterator->inner, key, value);
}
bool tds_hamt_bi_map_shares_roots(const tds_hamt_bi_map *left, const tds_hamt_bi_map *right) {
    return left != NULL && right != NULL
        && tds_hamt_map_shares_root(&left->forward, &right->forward)
        && tds_hamt_map_shares_root(&left->inverse, &right->inverse);
}
bool tds_hamt_bi_map_debug_validate(const tds_hamt_bi_map *map) {
    if (map == NULL || map->policy_state == NULL
        || map->forward.count != map->inverse.count
        || !tds_hamt_map_debug_validate_canonical(&map->forward)
        || !tds_hamt_map_debug_validate_canonical(&map->inverse)) return false;
    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&map->forward, &iterator);
    const void *key;
    const void *value;
    while (tds_hamt_map_iterator_next(&iterator, &key, &value)) {
        const void *inverse_key = NULL;
        if (!tds_hamt_map_try_get(&map->inverse, value, &inverse_key)
            || !active_keys_equal(map, key, inverse_key)) return false;
    }
    tds_hamt_map_iterator_init(&map->inverse, &iterator);
    while (tds_hamt_map_iterator_next(&iterator, &value, &key)) {
        const void *forward_value = NULL;
        if (!tds_hamt_map_try_get(&map->forward, key, &forward_value)
            || !active_values_equal(map, value, forward_value)) return false;
    }
    return true;
}
