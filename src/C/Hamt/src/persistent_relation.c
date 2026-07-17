#include <Tools/DataStructures/Hamt/persistent_relation.h>

#include <string.h>

static bool tds_hamt_relation_valid(const tds_hamt_relation* relation)
{
    return relation != NULL
        && relation->forward.context != NULL
        && relation->reverse.context != NULL;
}

static void tds_hamt_relation_publish(
    const tds_hamt_relation* source,
    tds_hamt_relation* result,
    tds_hamt_relation* candidate)
{
    if (result == source) {
        tds_hamt_relation_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static tds_hamt_status tds_hamt_relation_publish_clone(
    const tds_hamt_relation* source,
    tds_hamt_relation* result)
{
    tds_hamt_relation candidate;
    const tds_hamt_status status = tds_hamt_relation_clone(source, &candidate);
    if (status == TDS_HAMT_OK) {
        tds_hamt_relation_publish(source, result, &candidate);
    }
    return status;
}

tds_hamt_status tds_hamt_relation_init(
    tds_hamt_relation* relation,
    const tds_hamt_set_policy* left_policy,
    const tds_hamt_set_policy* right_policy)
{
    if (relation == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_status status = tds_hamt_multimap_init(
        &relation->forward, left_policy, right_policy);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    status = tds_hamt_multimap_init(
        &relation->reverse, right_policy, left_policy);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&relation->forward);
        return status;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_relation_clone(
    const tds_hamt_relation* source,
    tds_hamt_relation* destination)
{
    if (!tds_hamt_relation_valid(source) || destination == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return TDS_HAMT_OK;
    }
    tds_hamt_status status =
        tds_hamt_multimap_clone(&source->forward, &destination->forward);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    status = tds_hamt_multimap_clone(&source->reverse, &destination->reverse);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&destination->forward);
    }
    return status;
}

void tds_hamt_relation_move(
    tds_hamt_relation* destination,
    tds_hamt_relation* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void tds_hamt_relation_destroy(tds_hamt_relation* relation)
{
    if (relation == NULL) {
        return;
    }
    tds_hamt_multimap_destroy(&relation->forward);
    tds_hamt_multimap_destroy(&relation->reverse);
    (void)memset(relation, 0, sizeof(*relation));
}

size_t tds_hamt_relation_left_count(const tds_hamt_relation* relation)
{
    return tds_hamt_relation_valid(relation)
        ? tds_hamt_multimap_key_count(&relation->forward)
        : 0u;
}

size_t tds_hamt_relation_right_count(const tds_hamt_relation* relation)
{
    return tds_hamt_relation_valid(relation)
        ? tds_hamt_multimap_key_count(&relation->reverse)
        : 0u;
}

int64_t tds_hamt_relation_pair_count(const tds_hamt_relation* relation)
{
    return tds_hamt_relation_valid(relation)
        ? tds_hamt_multimap_pair_count(&relation->forward)
        : 0;
}

bool tds_hamt_relation_empty(const tds_hamt_relation* relation)
{
    return tds_hamt_relation_pair_count(relation) == 0;
}

bool tds_hamt_relation_contains(
    const tds_hamt_relation* relation,
    const void* left,
    const void* right)
{
    return tds_hamt_relation_valid(relation)
        && tds_hamt_multimap_contains(&relation->forward, left, right);
}

bool tds_hamt_relation_try_get_rights(
    const tds_hamt_relation* relation,
    const void* left,
    const tds_hamt_set** rights)
{
    return tds_hamt_relation_valid(relation)
        && tds_hamt_multimap_try_get_values(&relation->forward, left, rights);
}

bool tds_hamt_relation_try_get_lefts(
    const tds_hamt_relation* relation,
    const void* right,
    const tds_hamt_set** lefts)
{
    return tds_hamt_relation_valid(relation)
        && tds_hamt_multimap_try_get_values(&relation->reverse, right, lefts);
}

tds_hamt_status tds_hamt_relation_add(
    const tds_hamt_relation* relation,
    const void* left,
    const void* right,
    tds_hamt_relation* result)
{
    if (!tds_hamt_relation_valid(relation) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_left = left;
    const void* stored_right = right;
    const void* actual = NULL;
    if (tds_hamt_multimap_try_get_key(
            &relation->forward, left, &actual)) {
        stored_left = actual;
    }
    actual = NULL;
    if (tds_hamt_multimap_try_get_key(
            &relation->reverse, right, &actual)) {
        stored_right = actual;
    }
    if (tds_hamt_multimap_contains(
        &relation->forward, stored_left, stored_right)) {
        return tds_hamt_relation_publish_clone(relation, result);
    }

    tds_hamt_multimap forward;
    tds_hamt_status status = tds_hamt_multimap_add(
        &relation->forward, stored_left, stored_right, &forward);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap reverse;
    status = tds_hamt_multimap_add(
        &relation->reverse, stored_right, stored_left, &reverse);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&forward);
        return status;
    }

    tds_hamt_relation candidate = { forward, reverse };
    tds_hamt_relation_publish(relation, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_relation_remove(
    const tds_hamt_relation* relation,
    const void* left,
    const void* right,
    tds_hamt_relation* result)
{
    if (!tds_hamt_relation_valid(relation) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_left = NULL;
    const void* stored_right = NULL;
    if (!tds_hamt_multimap_try_get_key(
            &relation->forward, left, &stored_left)
        || !tds_hamt_multimap_try_get_key(
            &relation->reverse, right, &stored_right)
        || !tds_hamt_multimap_contains(
            &relation->forward, stored_left, stored_right)) {
        return tds_hamt_relation_publish_clone(relation, result);
    }

    tds_hamt_multimap forward;
    tds_hamt_status status = tds_hamt_multimap_remove(
        &relation->forward, stored_left, stored_right, &forward);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap reverse;
    status = tds_hamt_multimap_remove(
        &relation->reverse, stored_right, stored_left, &reverse);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&forward);
        return status;
    }
    tds_hamt_relation candidate = { forward, reverse };
    tds_hamt_relation_publish(relation, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_relation_remove_left(
    const tds_hamt_relation* relation,
    const void* left,
    tds_hamt_relation* result)
{
    if (!tds_hamt_relation_valid(relation) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_left = NULL;
    const tds_hamt_set* rights = NULL;
    if (!tds_hamt_multimap_try_get_key(
            &relation->forward, left, &stored_left)
        || !tds_hamt_multimap_try_get_values(
            &relation->forward, stored_left, &rights)) {
        return tds_hamt_relation_publish_clone(relation, result);
    }

    tds_hamt_relation current;
    tds_hamt_status status = tds_hamt_relation_clone(relation, &current);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(rights, &iterator);
    const void* right = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &right)) {
        status = tds_hamt_relation_remove(
            &current, stored_left, right, &current);
        if (status != TDS_HAMT_OK) {
            tds_hamt_relation_destroy(&current);
            return status;
        }
    }
    tds_hamt_relation_publish(relation, result, &current);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_relation_remove_right(
    const tds_hamt_relation* relation,
    const void* right,
    tds_hamt_relation* result)
{
    if (!tds_hamt_relation_valid(relation) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_right = NULL;
    const tds_hamt_set* lefts = NULL;
    if (!tds_hamt_multimap_try_get_key(
            &relation->reverse, right, &stored_right)
        || !tds_hamt_multimap_try_get_values(
            &relation->reverse, stored_right, &lefts)) {
        return tds_hamt_relation_publish_clone(relation, result);
    }

    tds_hamt_relation current;
    tds_hamt_status status = tds_hamt_relation_clone(relation, &current);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(lefts, &iterator);
    const void* left = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &left)) {
        status = tds_hamt_relation_remove(
            &current, left, stored_right, &current);
        if (status != TDS_HAMT_OK) {
            tds_hamt_relation_destroy(&current);
            return status;
        }
    }
    tds_hamt_relation_publish(relation, result, &current);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_relation_clear(
    const tds_hamt_relation* relation,
    tds_hamt_relation* result)
{
    if (!tds_hamt_relation_valid(relation) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (tds_hamt_relation_empty(relation)) {
        return tds_hamt_relation_publish_clone(relation, result);
    }
    tds_hamt_multimap forward;
    tds_hamt_status status =
        tds_hamt_multimap_clear(&relation->forward, &forward);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_multimap reverse;
    status = tds_hamt_multimap_clear(&relation->reverse, &reverse);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&forward);
        return status;
    }
    tds_hamt_relation candidate = { forward, reverse };
    tds_hamt_relation_publish(relation, result, &candidate);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_relation_inverse(
    const tds_hamt_relation* relation,
    tds_hamt_relation* result)
{
    if (!tds_hamt_relation_valid(relation) || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_relation candidate;
    tds_hamt_status status = tds_hamt_multimap_clone(
        &relation->reverse, &candidate.forward);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    status = tds_hamt_multimap_clone(
        &relation->forward, &candidate.reverse);
    if (status != TDS_HAMT_OK) {
        tds_hamt_multimap_destroy(&candidate.forward);
        return status;
    }
    tds_hamt_relation_publish(relation, result, &candidate);
    return TDS_HAMT_OK;
}

typedef struct tds_hamt_relation_validation_context {
    const tds_hamt_relation* relation;
    bool valid;
    int64_t pairs;
} tds_hamt_relation_validation_context;

static void tds_hamt_relation_validate_pair(
    const void* left,
    const void* right,
    void* raw_context)
{
    tds_hamt_relation_validation_context* context =
        (tds_hamt_relation_validation_context*)raw_context;
    if (!tds_hamt_multimap_contains(
            &context->relation->reverse, right, left)) {
        context->valid = false;
    }
    ++context->pairs;
}

bool tds_hamt_relation_debug_validate(const tds_hamt_relation* relation)
{
    if (!tds_hamt_relation_valid(relation)
        || !tds_hamt_multimap_debug_validate(&relation->forward)
        || !tds_hamt_multimap_debug_validate(&relation->reverse)
        || tds_hamt_multimap_pair_count(&relation->forward)
            != tds_hamt_multimap_pair_count(&relation->reverse)) {
        return false;
    }
    tds_hamt_relation_validation_context context = { relation, true, 0 };
    if (tds_hamt_multimap_visit(
            &relation->forward,
            tds_hamt_relation_validate_pair,
            &context) != TDS_HAMT_OK) {
        return false;
    }
    return context.valid
        && context.pairs == tds_hamt_relation_pair_count(relation);
}

bool tds_hamt_relation_debug_shares_roots(
    const tds_hamt_relation* left,
    const tds_hamt_relation* right)
{
    return tds_hamt_relation_valid(left)
        && tds_hamt_relation_valid(right)
        && tds_hamt_multimap_debug_shares_root(
            &left->forward, &right->forward)
        && tds_hamt_multimap_debug_shares_root(
            &left->reverse, &right->reverse);
}
