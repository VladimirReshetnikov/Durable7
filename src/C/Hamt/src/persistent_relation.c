#include <durable7/hamt/persistent_relation.h>

#include <string.h>

static bool d7_hamt_relation_valid(const d7_hamt_relation* relation)
{
    return relation != NULL
        && relation->forward.context != NULL
        && relation->reverse.context != NULL;
}

static void d7_hamt_relation_publish(
    const d7_hamt_relation* source,
    d7_hamt_relation* result,
    d7_hamt_relation* candidate)
{
    if (result == source) {
        d7_hamt_relation_destroy(result);
    }
    *result = *candidate;
    (void)memset(candidate, 0, sizeof(*candidate));
}

static d7_hamt_status d7_hamt_relation_publish_clone(
    const d7_hamt_relation* source,
    d7_hamt_relation* result)
{
    d7_hamt_relation candidate;
    const d7_hamt_status status = d7_hamt_relation_clone(source, &candidate);
    if (status == D7_HAMT_OK) {
        d7_hamt_relation_publish(source, result, &candidate);
    }
    return status;
}

d7_hamt_status d7_hamt_relation_init(
    d7_hamt_relation* relation,
    const d7_hamt_set_policy* left_policy,
    const d7_hamt_set_policy* right_policy)
{
    if (relation == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_status status = d7_hamt_multimap_init(
        &relation->forward, left_policy, right_policy);
    if (status != D7_HAMT_OK) {
        return status;
    }
    status = d7_hamt_multimap_init(
        &relation->reverse, right_policy, left_policy);
    if (status != D7_HAMT_OK) {
        d7_hamt_multimap_destroy(&relation->forward);
        return status;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_relation_clone(
    const d7_hamt_relation* source,
    d7_hamt_relation* destination)
{
    if (!d7_hamt_relation_valid(source) || destination == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return D7_HAMT_OK;
    }
    d7_hamt_status status =
        d7_hamt_multimap_clone(&source->forward, &destination->forward);
    if (status != D7_HAMT_OK) {
        return status;
    }
    status = d7_hamt_multimap_clone(&source->reverse, &destination->reverse);
    if (status != D7_HAMT_OK) {
        d7_hamt_multimap_destroy(&destination->forward);
    }
    return status;
}

void d7_hamt_relation_move(
    d7_hamt_relation* destination,
    d7_hamt_relation* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void d7_hamt_relation_destroy(d7_hamt_relation* relation)
{
    if (relation == NULL) {
        return;
    }
    d7_hamt_multimap_destroy(&relation->forward);
    d7_hamt_multimap_destroy(&relation->reverse);
    (void)memset(relation, 0, sizeof(*relation));
}

size_t d7_hamt_relation_left_count(const d7_hamt_relation* relation)
{
    return d7_hamt_relation_valid(relation)
        ? d7_hamt_multimap_key_count(&relation->forward)
        : 0u;
}

size_t d7_hamt_relation_right_count(const d7_hamt_relation* relation)
{
    return d7_hamt_relation_valid(relation)
        ? d7_hamt_multimap_key_count(&relation->reverse)
        : 0u;
}

int64_t d7_hamt_relation_pair_count(const d7_hamt_relation* relation)
{
    return d7_hamt_relation_valid(relation)
        ? d7_hamt_multimap_pair_count(&relation->forward)
        : 0;
}

bool d7_hamt_relation_empty(const d7_hamt_relation* relation)
{
    return d7_hamt_relation_pair_count(relation) == 0;
}

bool d7_hamt_relation_contains(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right)
{
    return d7_hamt_relation_valid(relation)
        && d7_hamt_multimap_contains(&relation->forward, left, right);
}

bool d7_hamt_relation_try_get_rights(
    const d7_hamt_relation* relation,
    const void* left,
    const d7_hamt_set** rights)
{
    return d7_hamt_relation_valid(relation)
        && d7_hamt_multimap_try_get_values(&relation->forward, left, rights);
}

bool d7_hamt_relation_try_get_lefts(
    const d7_hamt_relation* relation,
    const void* right,
    const d7_hamt_set** lefts)
{
    return d7_hamt_relation_valid(relation)
        && d7_hamt_multimap_try_get_values(&relation->reverse, right, lefts);
}

d7_hamt_status d7_hamt_relation_add(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right,
    d7_hamt_relation* result)
{
    if (!d7_hamt_relation_valid(relation) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_left = left;
    const void* stored_right = right;
    const void* actual = NULL;
    if (d7_hamt_multimap_try_get_key(
            &relation->forward, left, &actual)) {
        stored_left = actual;
    }
    actual = NULL;
    if (d7_hamt_multimap_try_get_key(
            &relation->reverse, right, &actual)) {
        stored_right = actual;
    }
    if (d7_hamt_multimap_contains(
        &relation->forward, stored_left, stored_right)) {
        return d7_hamt_relation_publish_clone(relation, result);
    }

    d7_hamt_multimap forward;
    d7_hamt_status status = d7_hamt_multimap_add(
        &relation->forward, stored_left, stored_right, &forward);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_multimap reverse;
    status = d7_hamt_multimap_add(
        &relation->reverse, stored_right, stored_left, &reverse);
    if (status != D7_HAMT_OK) {
        d7_hamt_multimap_destroy(&forward);
        return status;
    }

    d7_hamt_relation candidate = { forward, reverse };
    d7_hamt_relation_publish(relation, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_relation_remove(
    const d7_hamt_relation* relation,
    const void* left,
    const void* right,
    d7_hamt_relation* result)
{
    if (!d7_hamt_relation_valid(relation) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_left = NULL;
    const void* stored_right = NULL;
    if (!d7_hamt_multimap_try_get_key(
            &relation->forward, left, &stored_left)
        || !d7_hamt_multimap_try_get_key(
            &relation->reverse, right, &stored_right)
        || !d7_hamt_multimap_contains(
            &relation->forward, stored_left, stored_right)) {
        return d7_hamt_relation_publish_clone(relation, result);
    }

    d7_hamt_multimap forward;
    d7_hamt_status status = d7_hamt_multimap_remove(
        &relation->forward, stored_left, stored_right, &forward);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_multimap reverse;
    status = d7_hamt_multimap_remove(
        &relation->reverse, stored_right, stored_left, &reverse);
    if (status != D7_HAMT_OK) {
        d7_hamt_multimap_destroy(&forward);
        return status;
    }
    d7_hamt_relation candidate = { forward, reverse };
    d7_hamt_relation_publish(relation, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_relation_remove_left(
    const d7_hamt_relation* relation,
    const void* left,
    d7_hamt_relation* result)
{
    if (!d7_hamt_relation_valid(relation) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_left = NULL;
    const d7_hamt_set* rights = NULL;
    if (!d7_hamt_multimap_try_get_key(
            &relation->forward, left, &stored_left)
        || !d7_hamt_multimap_try_get_values(
            &relation->forward, stored_left, &rights)) {
        return d7_hamt_relation_publish_clone(relation, result);
    }

    d7_hamt_relation current;
    d7_hamt_status status = d7_hamt_relation_clone(relation, &current);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_set_iterator iterator;
    d7_hamt_set_iterator_init(rights, &iterator);
    const void* right = NULL;
    while (d7_hamt_set_iterator_next(&iterator, &right)) {
        status = d7_hamt_relation_remove(
            &current, stored_left, right, &current);
        if (status != D7_HAMT_OK) {
            d7_hamt_relation_destroy(&current);
            return status;
        }
    }
    d7_hamt_relation_publish(relation, result, &current);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_relation_remove_right(
    const d7_hamt_relation* relation,
    const void* right,
    d7_hamt_relation* result)
{
    if (!d7_hamt_relation_valid(relation) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    const void* stored_right = NULL;
    const d7_hamt_set* lefts = NULL;
    if (!d7_hamt_multimap_try_get_key(
            &relation->reverse, right, &stored_right)
        || !d7_hamt_multimap_try_get_values(
            &relation->reverse, stored_right, &lefts)) {
        return d7_hamt_relation_publish_clone(relation, result);
    }

    d7_hamt_relation current;
    d7_hamt_status status = d7_hamt_relation_clone(relation, &current);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_set_iterator iterator;
    d7_hamt_set_iterator_init(lefts, &iterator);
    const void* left = NULL;
    while (d7_hamt_set_iterator_next(&iterator, &left)) {
        status = d7_hamt_relation_remove(
            &current, left, stored_right, &current);
        if (status != D7_HAMT_OK) {
            d7_hamt_relation_destroy(&current);
            return status;
        }
    }
    d7_hamt_relation_publish(relation, result, &current);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_relation_clear(
    const d7_hamt_relation* relation,
    d7_hamt_relation* result)
{
    if (!d7_hamt_relation_valid(relation) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (d7_hamt_relation_empty(relation)) {
        return d7_hamt_relation_publish_clone(relation, result);
    }
    d7_hamt_multimap forward;
    d7_hamt_status status =
        d7_hamt_multimap_clear(&relation->forward, &forward);
    if (status != D7_HAMT_OK) {
        return status;
    }
    d7_hamt_multimap reverse;
    status = d7_hamt_multimap_clear(&relation->reverse, &reverse);
    if (status != D7_HAMT_OK) {
        d7_hamt_multimap_destroy(&forward);
        return status;
    }
    d7_hamt_relation candidate = { forward, reverse };
    d7_hamt_relation_publish(relation, result, &candidate);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_relation_inverse(
    const d7_hamt_relation* relation,
    d7_hamt_relation* result)
{
    if (!d7_hamt_relation_valid(relation) || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    d7_hamt_relation candidate;
    d7_hamt_status status = d7_hamt_multimap_clone(
        &relation->reverse, &candidate.forward);
    if (status != D7_HAMT_OK) {
        return status;
    }
    status = d7_hamt_multimap_clone(
        &relation->forward, &candidate.reverse);
    if (status != D7_HAMT_OK) {
        d7_hamt_multimap_destroy(&candidate.forward);
        return status;
    }
    d7_hamt_relation_publish(relation, result, &candidate);
    return D7_HAMT_OK;
}

typedef struct d7_hamt_relation_validation_context {
    const d7_hamt_relation* relation;
    bool valid;
    int64_t pairs;
} d7_hamt_relation_validation_context;

static void d7_hamt_relation_validate_pair(
    const void* left,
    const void* right,
    void* raw_context)
{
    d7_hamt_relation_validation_context* context =
        (d7_hamt_relation_validation_context*)raw_context;
    if (!d7_hamt_multimap_contains(
            &context->relation->reverse, right, left)) {
        context->valid = false;
    }
    ++context->pairs;
}

bool d7_hamt_relation_debug_validate(const d7_hamt_relation* relation)
{
    if (!d7_hamt_relation_valid(relation)
        || !d7_hamt_multimap_debug_validate(&relation->forward)
        || !d7_hamt_multimap_debug_validate(&relation->reverse)
        || d7_hamt_multimap_pair_count(&relation->forward)
            != d7_hamt_multimap_pair_count(&relation->reverse)) {
        return false;
    }
    d7_hamt_relation_validation_context context = { relation, true, 0 };
    if (d7_hamt_multimap_visit(
            &relation->forward,
            d7_hamt_relation_validate_pair,
            &context) != D7_HAMT_OK) {
        return false;
    }
    return context.valid
        && context.pairs == d7_hamt_relation_pair_count(relation);
}

bool d7_hamt_relation_debug_shares_roots(
    const d7_hamt_relation* left,
    const d7_hamt_relation* right)
{
    return d7_hamt_relation_valid(left)
        && d7_hamt_relation_valid(right)
        && d7_hamt_multimap_debug_shares_root(
            &left->forward, &right->forward)
        && d7_hamt_multimap_debug_shares_root(
            &left->reverse, &right->reverse);
}
