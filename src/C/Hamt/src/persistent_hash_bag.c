#include <Tools/DataStructures/Hamt/persistent_hash_bag.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef TDS_HAMT_TESTING
static size_t tds_hamt_bag_test_fail_after = SIZE_MAX;
static size_t tds_hamt_bag_test_allocation_count = 0;

void tds_hamt_bag_test_fail_count_allocations_after(size_t successful_allocations) {
    tds_hamt_bag_test_fail_after = successful_allocations;
    tds_hamt_bag_test_allocation_count = 0;
}

void tds_hamt_bag_test_reset_count_allocator(void) {
    tds_hamt_bag_test_fail_after = SIZE_MAX;
    tds_hamt_bag_test_allocation_count = 0;
}
#endif

typedef struct tds_hamt_bag_add_context {
    int32_t increment;
    int32_t candidate;
} tds_hamt_bag_add_context;

typedef enum tds_hamt_bag_operation {
    TDS_HAMT_BAG_UNION,
    TDS_HAMT_BAG_INTERSECT,
    TDS_HAMT_BAG_EXCEPT,
    TDS_HAMT_BAG_SUM
} tds_hamt_bag_operation;

static void *tds_hamt_bag_allocate_count(void) {
#ifdef TDS_HAMT_TESTING
    if (tds_hamt_bag_test_allocation_count == tds_hamt_bag_test_fail_after) {
        return NULL;
    }
    ++tds_hamt_bag_test_allocation_count;
#endif
    return malloc(sizeof(int32_t));
}

static bool tds_hamt_bag_count_equal(
    const void *left,
    const void *right,
    void *context) {
    (void)context;
    return left != NULL && right != NULL
        && *(const int32_t *)left == *(const int32_t *)right;
}

static void *tds_hamt_bag_count_retain(const void *value, void *context) {
    (void)context;
    if (value == NULL) {
        return NULL;
    }

    int32_t *copy = (int32_t *)tds_hamt_bag_allocate_count();
    if (copy != NULL) {
        *copy = *(const int32_t *)value;
    }
    return copy;
}

static void tds_hamt_bag_count_release(void *value, void *context) {
    (void)context;
    free(value);
}

static tds_hamt_policy tds_hamt_bag_map_policy(const tds_hamt_set_policy *policy) {
    const tds_hamt_set_policy item_policy =
        policy == NULL ? tds_hamt_set_policy_default() : *policy;
    tds_hamt_policy map_policy;
    memset(&map_policy, 0, sizeof(map_policy));
    map_policy.hash = item_policy.hash;
    map_policy.key_equal = item_policy.equal;
    map_policy.value_equal = tds_hamt_bag_count_equal;
    map_policy.retain_key = item_policy.retain_item;
    map_policy.retain_value = tds_hamt_bag_count_retain;
    map_policy.release_key = item_policy.release_item;
    map_policy.release_value = tds_hamt_bag_count_release;
    map_policy.context = item_policy.context;
    return map_policy;
}

static tds_hamt_set_policy tds_hamt_bag_item_policy(const tds_hamt_bag *bag) {
    tds_hamt_set_policy policy;
    memset(&policy, 0, sizeof(policy));
    if (bag != NULL) {
        policy.hash = bag->counts.policy.hash;
        policy.equal = bag->counts.policy.key_equal;
        policy.retain_item = bag->counts.policy.retain_key;
        policy.release_item = bag->counts.policy.release_key;
        policy.context = bag->counts.policy.context;
    }
    return policy;
}

static bool tds_hamt_bag_policies_compatible(
    const tds_hamt_bag *left,
    const tds_hamt_bag *right) {
    const tds_hamt_policy *l = &left->counts.policy;
    const tds_hamt_policy *r = &right->counts.policy;
    return l->hash == r->hash
        && l->key_equal == r->key_equal
        && l->value_equal == r->value_equal
        && l->retain_key == r->retain_key
        && l->retain_value == r->retain_value
        && l->release_key == r->release_key
        && l->release_value == r->release_value
        && l->context == r->context;
}

static void tds_hamt_bag_publish(
    const tds_hamt_bag *first,
    const tds_hamt_bag *second,
    tds_hamt_bag *result,
    tds_hamt_bag *value) {
    if (result == first || result == second) {
        tds_hamt_bag_destroy(result);
    }
    *result = *value;
    value->counts.root = NULL;
    value->counts.count = 0;
    value->total_count = 0;
}

static tds_hamt_status tds_hamt_bag_publish_clone(
    const tds_hamt_bag *bag,
    tds_hamt_bag *result) {
    tds_hamt_bag clone = tds_hamt_bag_clone(bag);
    tds_hamt_bag_publish(bag, NULL, result, &clone);
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_add_factory(
    const void *key,
    void *context,
    const void **value) {
    (void)key;
    tds_hamt_bag_add_context *add = (tds_hamt_bag_add_context *)context;
    add->candidate = add->increment;
    *value = &add->candidate;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_update_factory(
    const void *key,
    const void *stored_value,
    void *context,
    const void **value) {
    (void)key;
    tds_hamt_bag_add_context *add = (tds_hamt_bag_add_context *)context;
    if (stored_value == NULL || *(const int32_t *)stored_value <= 0) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const int32_t stored = *(const int32_t *)stored_value;
    if (add->increment > INT32_MAX - stored) {
        return TDS_HAMT_OVERFLOW;
    }
    add->candidate = stored + add->increment;
    *value = &add->candidate;
    return TDS_HAMT_OK;
}

tds_hamt_bag tds_hamt_bag_create(const tds_hamt_set_policy *policy) {
    const tds_hamt_policy map_policy = tds_hamt_bag_map_policy(policy);
    tds_hamt_bag bag;
    bag.counts = tds_hamt_map_create(&map_policy);
    bag.total_count = 0;
    return bag;
}

tds_hamt_status tds_hamt_bag_create_range(
    const tds_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    tds_hamt_bag *result) {
    if (result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (item_count > (size_t)INT64_MAX) {
        return TDS_HAMT_OVERFLOW;
    }

    tds_hamt_bag current = tds_hamt_bag_create(policy);
    for (size_t index = 0; index < item_count; ++index) {
        const tds_hamt_status status =
            tds_hamt_bag_add(&current, items[index], &current);
        if (status != TDS_HAMT_OK) {
            tds_hamt_bag_destroy(&current);
            return status;
        }
    }

    *result = current;
    return TDS_HAMT_OK;
}

tds_hamt_bag tds_hamt_bag_clone(const tds_hamt_bag *bag) {
    if (bag == NULL) {
        return tds_hamt_bag_create(NULL);
    }
    tds_hamt_bag clone;
    clone.counts = tds_hamt_map_clone(&bag->counts);
    clone.total_count = bag->total_count;
    return clone;
}

void tds_hamt_bag_destroy(tds_hamt_bag *bag) {
    if (bag == NULL) {
        return;
    }
    tds_hamt_map_destroy(&bag->counts);
    bag->total_count = 0;
}

size_t tds_hamt_bag_distinct_count(const tds_hamt_bag *bag) {
    return bag == NULL ? 0 : tds_hamt_map_count(&bag->counts);
}

int64_t tds_hamt_bag_total_count(const tds_hamt_bag *bag) {
    return bag == NULL ? 0 : bag->total_count;
}

bool tds_hamt_bag_is_empty(const tds_hamt_bag *bag) {
    return tds_hamt_bag_distinct_count(bag) == 0;
}

tds_hamt_status tds_hamt_bag_get_policy(
    const tds_hamt_bag *bag,
    tds_hamt_set_policy *policy) {
    if (bag == NULL || policy == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *policy = tds_hamt_bag_item_policy(bag);
    return TDS_HAMT_OK;
}

bool tds_hamt_bag_contains(const tds_hamt_bag *bag, const void *item) {
    return bag != NULL && tds_hamt_map_contains_key(&bag->counts, item);
}

int32_t tds_hamt_bag_count_of(const tds_hamt_bag *bag, const void *item) {
    if (bag == NULL) {
        return 0;
    }
    const void *value = NULL;
    return tds_hamt_map_try_get(&bag->counts, item, &value) && value != NULL
        ? *(const int32_t *)value
        : 0;
}

bool tds_hamt_bag_try_get_value(
    const tds_hamt_bag *bag,
    const void *equal_item,
    const void **actual_item) {
    if (actual_item != NULL) {
        *actual_item = NULL;
    }
    return bag != NULL
        && tds_hamt_map_try_get_key(&bag->counts, equal_item, actual_item);
}

bool tds_hamt_bag_try_get_entry(
    const tds_hamt_bag *bag,
    const void *equal_item,
    tds_hamt_bag_entry *entry) {
    if (entry != NULL) {
        entry->item = NULL;
        entry->count = 0;
    }
    if (bag == NULL) {
        return false;
    }

    const void *count = NULL;
    if (!tds_hamt_map_try_get(&bag->counts, equal_item, &count)) {
        return false;
    }
    const void *actual = NULL;
    if (!tds_hamt_map_try_get_key(&bag->counts, equal_item, &actual)) {
        return false;
    }
    if (entry != NULL) {
        entry->item = actual;
        entry->count = count == NULL ? 0 : *(const int32_t *)count;
    }
    return true;
}

tds_hamt_status tds_hamt_bag_add(
    const tds_hamt_bag *bag,
    const void *item,
    tds_hamt_bag *result) {
    return tds_hamt_bag_add_copies(bag, item, 1, result);
}

tds_hamt_status tds_hamt_bag_add_copies(
    const tds_hamt_bag *bag,
    const void *item,
    int64_t copies,
    tds_hamt_bag *result) {
    if (bag == NULL || result == NULL || copies < 0 || copies > INT32_MAX
        || bag->total_count < 0) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (copies == 0) {
        return tds_hamt_bag_publish_clone(bag, result);
    }
    if (copies > INT64_MAX - bag->total_count) {
        return TDS_HAMT_OVERFLOW;
    }

    tds_hamt_bag_add_context context;
    context.increment = (int32_t)copies;
    context.candidate = 0;
    tds_hamt_map counts;
    const tds_hamt_status status = tds_hamt_map_add_or_update(
        &bag->counts,
        item,
        tds_hamt_bag_add_factory,
        &context,
        tds_hamt_bag_update_factory,
        &context,
        &counts,
        NULL);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_bag value;
    value.counts = counts;
    value.total_count = bag->total_count + copies;
    tds_hamt_bag_publish(bag, NULL, result, &value);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bag_remove(
    const tds_hamt_bag *bag,
    const void *item,
    tds_hamt_bag *result) {
    return tds_hamt_bag_remove_copies(bag, item, 1, result);
}

tds_hamt_status tds_hamt_bag_remove_copies(
    const tds_hamt_bag *bag,
    const void *item,
    int64_t copies,
    tds_hamt_bag *result) {
    if (bag == NULL || result == NULL || copies < 0 || copies > INT32_MAX
        || bag->total_count < 0) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (copies == 0) {
        return tds_hamt_bag_publish_clone(bag, result);
    }

    const void *stored_value = NULL;
    if (!tds_hamt_map_try_get(&bag->counts, item, &stored_value)) {
        return tds_hamt_bag_publish_clone(bag, result);
    }
    if (stored_value == NULL || *(const int32_t *)stored_value <= 0) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const int32_t stored = *(const int32_t *)stored_value;
    const int64_t removed_count = copies >= stored ? stored : copies;
    if (bag->total_count < removed_count) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map counts;
    tds_hamt_status status;
    if (copies >= stored) {
        bool removed = false;
        const void *removed_value = NULL;
        status = tds_hamt_map_try_remove(
            &bag->counts, item, &counts, &removed, &removed_value);
        if (status == TDS_HAMT_OK && !removed) {
            tds_hamt_map_destroy(&counts);
            return TDS_HAMT_INVALID_ARGUMENT;
        }
    } else {
        const int32_t remaining = stored - (int32_t)copies;
        status = tds_hamt_map_set(&bag->counts, item, &remaining, &counts);
    }
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_bag value;
    value.counts = counts;
    value.total_count = bag->total_count - removed_count;
    tds_hamt_bag_publish(bag, NULL, result, &value);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bag_remove_all(
    const tds_hamt_bag *bag,
    const void *item,
    tds_hamt_bag *result) {
    if (bag == NULL || result == NULL || bag->total_count < 0) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map counts;
    bool removed = false;
    const void *removed_value = NULL;
    const tds_hamt_status status = tds_hamt_map_try_remove(
        &bag->counts, item, &counts, &removed, &removed_value);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    if (!removed) {
        tds_hamt_bag value;
        value.counts = counts;
        value.total_count = bag->total_count;
        tds_hamt_bag_publish(bag, NULL, result, &value);
        return TDS_HAMT_OK;
    }
    if (removed_value == NULL || *(const int32_t *)removed_value <= 0
        || bag->total_count < *(const int32_t *)removed_value) {
        tds_hamt_map_destroy(&counts);
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_bag value;
    value.counts = counts;
    value.total_count = bag->total_count - *(const int32_t *)removed_value;
    tds_hamt_bag_publish(bag, NULL, result, &value);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bag_clear(
    const tds_hamt_bag *bag,
    tds_hamt_bag *result) {
    if (bag == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    tds_hamt_map counts;
    const tds_hamt_status status = tds_hamt_map_clear(&bag->counts, &counts);
    if (status != TDS_HAMT_OK) {
        return status;
    }
    tds_hamt_bag value;
    value.counts = counts;
    value.total_count = 0;
    tds_hamt_bag_publish(bag, NULL, result, &value);
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_normalize(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    const tds_hamt_set_policy policy = tds_hamt_bag_item_policy(receiver);
    tds_hamt_bag current = tds_hamt_bag_create(&policy);
    tds_hamt_bag_entry_iterator iterator;
    tds_hamt_bag_entry_iterator_init(other, &iterator);
    tds_hamt_bag_entry entry;
    while (tds_hamt_bag_entry_iterator_next(&iterator, &entry)) {
        if (entry.count <= 0) {
            tds_hamt_bag_destroy(&current);
            return TDS_HAMT_INVALID_ARGUMENT;
        }
        const tds_hamt_status status =
            tds_hamt_bag_add_copies(&current, entry.item, entry.count, &current);
        if (status != TDS_HAMT_OK) {
            tds_hamt_bag_destroy(&current);
            return status;
        }
    }
    if (current.total_count != other->total_count) {
        tds_hamt_bag_destroy(&current);
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = current;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_union_normalized(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    tds_hamt_bag current = tds_hamt_bag_clone(receiver);
    tds_hamt_bag_entry_iterator iterator;
    tds_hamt_bag_entry_iterator_init(other, &iterator);
    tds_hamt_bag_entry entry;
    while (tds_hamt_bag_entry_iterator_next(&iterator, &entry)) {
        const int32_t receiver_count = tds_hamt_bag_count_of(&current, entry.item);
        if (entry.count > receiver_count) {
            const tds_hamt_status status = tds_hamt_bag_add_copies(
                &current, entry.item, entry.count - receiver_count, &current);
            if (status != TDS_HAMT_OK) {
                tds_hamt_bag_destroy(&current);
                return status;
            }
        }
    }
    *result = current;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_intersect_normalized(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    tds_hamt_bag current = tds_hamt_bag_clone(receiver);
    tds_hamt_bag_entry_iterator iterator;
    tds_hamt_bag_entry_iterator_init(receiver, &iterator);
    tds_hamt_bag_entry entry;
    while (tds_hamt_bag_entry_iterator_next(&iterator, &entry)) {
        const int32_t argument_count = tds_hamt_bag_count_of(other, entry.item);
        tds_hamt_status status = TDS_HAMT_OK;
        if (argument_count == 0) {
            status = tds_hamt_bag_remove_all(&current, entry.item, &current);
        } else if (argument_count < entry.count) {
            status = tds_hamt_bag_remove_copies(
                &current, entry.item, entry.count - argument_count, &current);
        }
        if (status != TDS_HAMT_OK) {
            tds_hamt_bag_destroy(&current);
            return status;
        }
    }
    *result = current;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_except_normalized(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    tds_hamt_bag current = tds_hamt_bag_clone(receiver);
    tds_hamt_bag_entry_iterator iterator;
    tds_hamt_bag_entry_iterator_init(receiver, &iterator);
    tds_hamt_bag_entry entry;
    while (tds_hamt_bag_entry_iterator_next(&iterator, &entry)) {
        const int32_t argument_count = tds_hamt_bag_count_of(other, entry.item);
        if (argument_count == 0) {
            continue;
        }
        const tds_hamt_status status = tds_hamt_bag_remove_copies(
            &current, entry.item, argument_count, &current);
        if (status != TDS_HAMT_OK) {
            tds_hamt_bag_destroy(&current);
            return status;
        }
    }
    *result = current;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_sum_normalized(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    if (receiver->total_count < 0 || other->total_count < 0) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (other->total_count > INT64_MAX - receiver->total_count) {
        return TDS_HAMT_OVERFLOW;
    }

    tds_hamt_bag current = tds_hamt_bag_clone(receiver);
    tds_hamt_bag_entry_iterator iterator;
    tds_hamt_bag_entry_iterator_init(other, &iterator);
    tds_hamt_bag_entry entry;
    while (tds_hamt_bag_entry_iterator_next(&iterator, &entry)) {
        const tds_hamt_status status =
            tds_hamt_bag_add_copies(&current, entry.item, entry.count, &current);
        if (status != TDS_HAMT_OK) {
            tds_hamt_bag_destroy(&current);
            return status;
        }
    }
    *result = current;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bag_combine(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag_operation operation,
    tds_hamt_bag *result) {
    if (receiver == NULL || other == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_bag normalized_storage;
    const tds_hamt_bag *normalized = other;
    bool owns_normalized = false;
    if (!tds_hamt_bag_policies_compatible(receiver, other)) {
        const tds_hamt_status status =
            tds_hamt_bag_normalize(receiver, other, &normalized_storage);
        if (status != TDS_HAMT_OK) {
            return status;
        }
        normalized = &normalized_storage;
        owns_normalized = true;
    }

    tds_hamt_bag value;
    tds_hamt_status status;
    switch (operation) {
    case TDS_HAMT_BAG_UNION:
        status = tds_hamt_bag_union_normalized(receiver, normalized, &value);
        break;
    case TDS_HAMT_BAG_INTERSECT:
        status = tds_hamt_bag_intersect_normalized(receiver, normalized, &value);
        break;
    case TDS_HAMT_BAG_EXCEPT:
        status = tds_hamt_bag_except_normalized(receiver, normalized, &value);
        break;
    case TDS_HAMT_BAG_SUM:
        status = tds_hamt_bag_sum_normalized(receiver, normalized, &value);
        break;
    default:
        status = TDS_HAMT_INVALID_ARGUMENT;
        break;
    }

    if (owns_normalized) {
        tds_hamt_bag_destroy(&normalized_storage);
    }
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_bag_publish(receiver, other, result, &value);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_bag_union(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    return tds_hamt_bag_combine(receiver, other, TDS_HAMT_BAG_UNION, result);
}

tds_hamt_status tds_hamt_bag_intersect(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    return tds_hamt_bag_combine(receiver, other, TDS_HAMT_BAG_INTERSECT, result);
}

tds_hamt_status tds_hamt_bag_except(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    return tds_hamt_bag_combine(receiver, other, TDS_HAMT_BAG_EXCEPT, result);
}

tds_hamt_status tds_hamt_bag_sum(
    const tds_hamt_bag *receiver,
    const tds_hamt_bag *other,
    tds_hamt_bag *result) {
    return tds_hamt_bag_combine(receiver, other, TDS_HAMT_BAG_SUM, result);
}

void tds_hamt_bag_iterator_init(
    const tds_hamt_bag *bag,
    tds_hamt_bag_iterator *iterator) {
    if (iterator == NULL) {
        return;
    }
    memset(iterator, 0, sizeof(*iterator));
    tds_hamt_map_iterator_init(bag == NULL ? NULL : &bag->counts, &iterator->inner);
}

bool tds_hamt_bag_iterator_next(
    tds_hamt_bag_iterator *iterator,
    const void **item) {
    if (item != NULL) {
        *item = NULL;
    }
    if (iterator == NULL) {
        return false;
    }
    if (iterator->remaining > 0) {
        --iterator->remaining;
        if (item != NULL) {
            *item = iterator->current_item;
        }
        return true;
    }

    const void *stored_item = NULL;
    const void *stored_count = NULL;
    if (!tds_hamt_map_iterator_next(&iterator->inner, &stored_item, &stored_count)
        || stored_count == NULL || *(const int32_t *)stored_count <= 0) {
        iterator->current_item = NULL;
        iterator->remaining = 0;
        return false;
    }
    iterator->current_item = stored_item;
    iterator->remaining = *(const int32_t *)stored_count - 1;
    if (item != NULL) {
        *item = stored_item;
    }
    return true;
}

void tds_hamt_bag_distinct_iterator_init(
    const tds_hamt_bag *bag,
    tds_hamt_bag_distinct_iterator *iterator) {
    if (iterator != NULL) {
        tds_hamt_map_iterator_init(bag == NULL ? NULL : &bag->counts, &iterator->inner);
    }
}

bool tds_hamt_bag_distinct_iterator_next(
    tds_hamt_bag_distinct_iterator *iterator,
    const void **item) {
    if (item != NULL) {
        *item = NULL;
    }
    if (iterator == NULL) {
        return false;
    }
    const void *count = NULL;
    return tds_hamt_map_iterator_next(&iterator->inner, item, &count);
}

void tds_hamt_bag_entry_iterator_init(
    const tds_hamt_bag *bag,
    tds_hamt_bag_entry_iterator *iterator) {
    if (iterator != NULL) {
        tds_hamt_map_iterator_init(bag == NULL ? NULL : &bag->counts, &iterator->inner);
    }
}

bool tds_hamt_bag_entry_iterator_next(
    tds_hamt_bag_entry_iterator *iterator,
    tds_hamt_bag_entry *entry) {
    if (entry != NULL) {
        entry->item = NULL;
        entry->count = 0;
    }
    if (iterator == NULL) {
        return false;
    }
    const void *item = NULL;
    const void *count = NULL;
    if (!tds_hamt_map_iterator_next(&iterator->inner, &item, &count)) {
        return false;
    }
    if (count == NULL || *(const int32_t *)count <= 0) {
        return false;
    }
    if (entry != NULL) {
        entry->item = item;
        entry->count = *(const int32_t *)count;
    }
    return true;
}

bool tds_hamt_bag_shares_root(
    const tds_hamt_bag *left,
    const tds_hamt_bag *right) {
    return left != NULL && right != NULL
        && tds_hamt_map_shares_root(&left->counts, &right->counts);
}

const void *tds_hamt_bag_debug_root_identity(const tds_hamt_bag *bag) {
    return bag == NULL ? NULL : tds_hamt_map_debug_root_identity(&bag->counts);
}

bool tds_hamt_bag_debug_validate_canonical(const tds_hamt_bag *bag) {
    if (bag == NULL || bag->total_count < 0
        || bag->counts.policy.value_equal != tds_hamt_bag_count_equal
        || bag->counts.policy.retain_value != tds_hamt_bag_count_retain
        || bag->counts.policy.release_value != tds_hamt_bag_count_release
        || !tds_hamt_map_debug_validate_canonical(&bag->counts)) {
        return false;
    }

    int64_t total = 0;
    size_t distinct = 0;
    tds_hamt_bag_entry_iterator iterator;
    tds_hamt_bag_entry_iterator_init(bag, &iterator);
    tds_hamt_bag_entry entry;
    while (tds_hamt_bag_entry_iterator_next(&iterator, &entry)) {
        if (entry.count <= 0 || total > INT64_MAX - entry.count) {
            return false;
        }
        total += entry.count;
        ++distinct;
    }
    return distinct == bag->counts.count && total == bag->total_count;
}
