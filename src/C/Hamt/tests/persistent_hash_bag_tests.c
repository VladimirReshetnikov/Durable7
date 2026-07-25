#include <durable7/hamt/persistent_hash_bag.h>
#include <durable7/test_support/headless_test_process.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef D7_HAMT_TESTING
#error The native hash-bag tests require D7_HAMT_TESTING allocation hooks.
#endif

void d7_hamt_test_fail_allocations_after(size_t successful_allocations);
void d7_hamt_test_reset_allocator(void);
void d7_hamt_bag_test_fail_count_allocations_after(size_t successful_allocations);
void d7_hamt_bag_test_reset_count_allocator(void);

typedef void (*test_fn)(void);

typedef struct test_case {
    const char *name;
    test_fn run;
} test_case;

typedef d7_hamt_status (*bag_algebra_fn)(
    const d7_hamt_bag *receiver,
    const d7_hamt_bag *other,
    d7_hamt_bag *result);

static void fail_at(const char *file, int line, const char *expression) {
    fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    exit(1);
}

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fail_at(__FILE__, __LINE__, #expression); \
        } \
    } while (0)

#define CHECK_STATUS(expression) \
    do { \
        const d7_hamt_status status_value = (expression); \
        if (status_value != D7_HAMT_OK) { \
            fprintf( \
                stderr, \
                "%s:%d: %s returned status %d\n", \
                __FILE__, \
                __LINE__, \
                #expression, \
                (int)status_value); \
            exit(1); \
        } \
    } while (0)

typedef struct bag_item {
    int group;
    int variant;
} bag_item;

typedef enum bag_equality_mode {
    BAG_EQUAL_BY_GROUP,
    BAG_EQUAL_EXACT
} bag_equality_mode;

typedef struct bag_policy_state {
    bag_equality_mode mode;
    size_t hash_calls;
    size_t equal_calls;
    size_t retain_calls;
    size_t release_calls;
    size_t fail_retain_at;
    int64_t live_references;
    bool constant_hash;
} bag_policy_state;

static uint32_t bag_hash(const void *item, void *context) {
    bag_policy_state *state = (bag_policy_state *)context;
    ++state->hash_calls;
    const bag_item *value = (const bag_item *)item;
    if (state->constant_hash) {
        return 0;
    }
    if (state->mode == BAG_EQUAL_BY_GROUP) {
        return (uint32_t)value->group * UINT32_C(2654435761);
    }
    return ((uint32_t)value->group * UINT32_C(2654435761))
        ^ ((uint32_t)value->variant * UINT32_C(2246822519));
}

static bool bag_equal(const void *left, const void *right, void *context) {
    bag_policy_state *state = (bag_policy_state *)context;
    ++state->equal_calls;
    const bag_item *l = (const bag_item *)left;
    const bag_item *r = (const bag_item *)right;
    return l->group == r->group
        && (state->mode == BAG_EQUAL_BY_GROUP || l->variant == r->variant);
}

static void *bag_retain(const void *item, void *context) {
    bag_policy_state *state = (bag_policy_state *)context;
    ++state->retain_calls;
    if (item == NULL) {
        return NULL;
    }
    if (state->fail_retain_at != 0 && state->retain_calls == state->fail_retain_at) {
        return NULL;
    }
    ++state->live_references;
    return (void *)item;
}

static void bag_release(void *item, void *context) {
    bag_policy_state *state = (bag_policy_state *)context;
    if (item != NULL) {
        ++state->release_calls;
        --state->live_references;
        CHECK(state->live_references >= 0);
    }
}

static d7_hamt_set_policy make_policy(bag_policy_state *state) {
    d7_hamt_set_policy policy = d7_hamt_set_policy_default();
    policy.hash = bag_hash;
    policy.equal = bag_equal;
    policy.retain_item = bag_retain;
    policy.release_item = bag_release;
    policy.context = state;
    return policy;
}

static void reset_callback_counts(bag_policy_state *state) {
    state->hash_calls = 0;
    state->equal_calls = 0;
    state->retain_calls = 0;
    state->release_calls = 0;
    state->fail_retain_at = 0;
}

static void assert_entry(
    const d7_hamt_bag *bag,
    const bag_item *probe,
    const bag_item *representative,
    int32_t count) {
    CHECK(d7_hamt_bag_count_of(bag, probe) == count);
    CHECK(d7_hamt_bag_contains(bag, probe) == (count > 0));
    const void *actual = (const void *)(uintptr_t)1;
    const bool found = d7_hamt_bag_try_get_value(bag, probe, &actual);
    CHECK(found == (count > 0));
    CHECK(actual == (count > 0 ? representative : probe));

    d7_hamt_bag_entry entry;
    CHECK(d7_hamt_bag_try_get_entry(bag, probe, &entry) == (count > 0));
    CHECK(entry.item == (count > 0 ? representative : NULL));
    CHECK(entry.count == count);
}

static void assert_output_bytes_unchanged(
    const d7_hamt_bag *before,
    const d7_hamt_bag *after) {
    CHECK(memcmp(before, after, sizeof(*before)) == 0);
}

static d7_hamt_bag sentinel_bag(void) {
    d7_hamt_bag result;
    memset(&result, 0xA5, sizeof(result));
    return result;
}

static void test_construction_queries_representatives_and_one_descent_add(void) {
    bag_policy_state state = { BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false };
    const d7_hamt_set_policy policy = make_policy(&state);
    const bag_item a_first = { 1, 10 };
    const bag_item a_equal = { 1, 20 };
    const bag_item b = { 2, 30 };

    d7_hamt_bag empty = d7_hamt_bag_create(&policy);
    CHECK(d7_hamt_bag_is_empty(&empty));
    CHECK(d7_hamt_bag_distinct_count(&empty) == 0);
    CHECK(d7_hamt_bag_total_count(&empty) == 0);
    CHECK(d7_hamt_bag_debug_validate_canonical(&empty));

    d7_hamt_bag with_a;
    CHECK_STATUS(d7_hamt_bag_add_copies(&empty, &a_first, 2, &with_a));
    d7_hamt_bag source = d7_hamt_bag_clone(&with_a);
    reset_callback_counts(&state);
    d7_hamt_bag more_a;
    CHECK_STATUS(d7_hamt_bag_add_copies(&with_a, &a_equal, 3, &more_a));
    CHECK(state.hash_calls == 1);
    CHECK(d7_hamt_bag_distinct_count(&more_a) == 1);
    CHECK(d7_hamt_bag_total_count(&more_a) == 5);
    assert_entry(&more_a, &a_equal, &a_first, 5);
    assert_entry(&source, &a_equal, &a_first, 2);

    d7_hamt_bag complete;
    CHECK_STATUS(d7_hamt_bag_add_copies(&more_a, &b, 4, &complete));
    CHECK(d7_hamt_bag_distinct_count(&complete) == 2);
    CHECK(d7_hamt_bag_total_count(&complete) == 9);
    assert_entry(&complete, &b, &b, 4);
    CHECK(d7_hamt_bag_debug_validate_canonical(&complete));

    const void *const items[] = { &a_first, &a_equal, &b, &a_equal };
    d7_hamt_bag ranged;
    CHECK_STATUS(d7_hamt_bag_create_range(&policy, items, 4, &ranged));
    CHECK(d7_hamt_bag_total_count(&ranged) == 4);
    assert_entry(&ranged, &a_equal, &a_first, 3);
    assert_entry(&ranged, &b, &b, 1);

    d7_hamt_set_policy retained_policy;
    CHECK_STATUS(d7_hamt_bag_get_policy(&complete, &retained_policy));
    CHECK(retained_policy.hash == policy.hash);
    CHECK(retained_policy.equal == policy.equal);
    CHECK(retained_policy.context == policy.context);

    d7_hamt_bag_destroy(&ranged);
    d7_hamt_bag_destroy(&complete);
    d7_hamt_bag_destroy(&more_a);
    d7_hamt_bag_destroy(&source);
    d7_hamt_bag_destroy(&with_a);
    d7_hamt_bag_destroy(&empty);
    CHECK(state.live_references == 0);

    int default_item = 42;
    d7_hamt_bag default_bag = d7_hamt_bag_create(NULL);
    CHECK_STATUS(d7_hamt_bag_add_copies(
        &default_bag, &default_item, 2, &default_bag));
    CHECK(d7_hamt_bag_count_of(&default_bag, &default_item) == 2);
    CHECK(d7_hamt_bag_debug_validate_canonical(&default_bag));
    d7_hamt_bag_destroy(&default_bag);
}

static void test_copy_count_validation_overflow_and_root_sharing_no_ops(void) {
    bag_policy_state state = { BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false };
    const d7_hamt_set_policy policy = make_policy(&state);
    const bag_item a = { 1, 1 };
    const bag_item missing = { 2, 2 };
    d7_hamt_bag bag = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(&bag, &a, INT32_MAX, &bag));

    reset_callback_counts(&state);
    d7_hamt_bag zero;
    CHECK_STATUS(d7_hamt_bag_add_copies(&bag, &missing, 0, &zero));
    CHECK(d7_hamt_bag_shares_root(&bag, &zero));
    CHECK(state.hash_calls == 0 && state.equal_calls == 0 && state.retain_calls == 0);
    d7_hamt_bag_destroy(&zero);

    d7_hamt_bag zero_remove;
    CHECK_STATUS(d7_hamt_bag_remove_copies(&bag, &missing, 0, &zero_remove));
    CHECK(d7_hamt_bag_shares_root(&bag, &zero_remove));
    CHECK(state.hash_calls == 0 && state.equal_calls == 0 && state.retain_calls == 0);
    d7_hamt_bag_destroy(&zero_remove);

    d7_hamt_bag output = d7_hamt_bag_clone(&bag);
    const void *output_root = d7_hamt_bag_debug_root_identity(&output);
    reset_callback_counts(&state);
    CHECK(d7_hamt_bag_add_copies(&bag, &missing, -1, &output)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_add_copies(&bag, &missing, (int64_t)INT32_MAX + 1, &output)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_remove_copies(&bag, &missing, -1, &output)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_remove_copies(
        &bag, &missing, (int64_t)INT32_MAX + 1, &output)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(state.hash_calls == 0 && state.equal_calls == 0 && state.retain_calls == 0);
    CHECK(d7_hamt_bag_debug_root_identity(&output) == output_root);

    CHECK(d7_hamt_bag_add(&bag, &a, &output) == D7_HAMT_OVERFLOW);
    CHECK(d7_hamt_bag_debug_root_identity(&output) == output_root);
    CHECK(d7_hamt_bag_count_of(&bag, &a) == INT32_MAX);

    d7_hamt_bag forged = d7_hamt_bag_clone(&bag);
    forged.total_count = INT64_MAX;
    reset_callback_counts(&state);
    CHECK(d7_hamt_bag_add(&forged, &missing, &output) == D7_HAMT_OVERFLOW);
    CHECK(state.hash_calls == 0 && state.equal_calls == 0 && state.retain_calls == 0);
    CHECK(d7_hamt_bag_debug_root_identity(&output) == output_root);
    CHECK(!d7_hamt_bag_debug_validate_canonical(&forged));

    CHECK((int)D7_HAMT_OVERFLOW == 6);
    d7_hamt_bag_destroy(&forged);
    d7_hamt_bag_destroy(&output);
    d7_hamt_bag_destroy(&bag);
    CHECK(state.live_references == 0);
}

static void test_remove_clear_and_aliasing_preserve_versions(void) {
    bag_policy_state state = { BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false };
    const d7_hamt_set_policy policy = make_policy(&state);
    const bag_item a = { 1, 1 };
    const bag_item b = { 2, 2 };
    const bag_item missing = { 3, 3 };
    d7_hamt_bag basis = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(&basis, &a, 5, &basis));
    CHECK_STATUS(d7_hamt_bag_add_copies(&basis, &b, 2, &basis));
    d7_hamt_bag retained = d7_hamt_bag_clone(&basis);

    d7_hamt_bag absent;
    CHECK_STATUS(d7_hamt_bag_remove_copies(&basis, &missing, 4, &absent));
    CHECK(d7_hamt_bag_shares_root(&basis, &absent));
    d7_hamt_bag_destroy(&absent);

    reset_callback_counts(&state);
    d7_hamt_bag absent_all;
    CHECK_STATUS(d7_hamt_bag_remove_all(&basis, &missing, &absent_all));
    CHECK(d7_hamt_bag_shares_root(&basis, &absent_all));
    CHECK(state.hash_calls == 1 && state.retain_calls == 0);
    d7_hamt_bag_destroy(&absent_all);

    d7_hamt_bag reduced;
    CHECK_STATUS(d7_hamt_bag_remove_copies(&basis, &a, 3, &reduced));
    assert_entry(&reduced, &a, &a, 2);
    CHECK(d7_hamt_bag_total_count(&reduced) == 4);

    CHECK_STATUS(d7_hamt_bag_remove_copies(&reduced, &a, INT32_MAX, &reduced));
    assert_entry(&reduced, &a, NULL, 0);
    CHECK(d7_hamt_bag_total_count(&reduced) == 2);
    CHECK_STATUS(d7_hamt_bag_remove_all(&reduced, &b, &reduced));
    CHECK(d7_hamt_bag_is_empty(&reduced));
    CHECK(d7_hamt_bag_total_count(&reduced) == 0);
    const void *empty_root = d7_hamt_bag_debug_root_identity(&reduced);
    CHECK_STATUS(d7_hamt_bag_clear(&reduced, &reduced));
    CHECK(d7_hamt_bag_debug_root_identity(&reduced) == empty_root);

    d7_hamt_bag cleared;
    CHECK_STATUS(d7_hamt_bag_clear(&basis, &cleared));
    CHECK(d7_hamt_bag_is_empty(&cleared));
    CHECK(d7_hamt_bag_debug_validate_canonical(&cleared));
    assert_entry(&retained, &a, &a, 5);
    assert_entry(&retained, &b, &b, 2);

    d7_hamt_bag_destroy(&cleared);
    d7_hamt_bag_destroy(&reduced);
    d7_hamt_bag_destroy(&retained);
    d7_hamt_bag_destroy(&basis);
    CHECK(state.live_references == 0);
}

static void test_expanded_distinct_entry_iterators_and_copy_independence(void) {
    bag_policy_state state = { BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, true };
    const d7_hamt_set_policy policy = make_policy(&state);
    const bag_item values[] = { { 1, 1 }, { 2, 2 }, { 3, 3 } };
    const bag_item equal_first = { 1, 99 };
    int32_t counts[] = { 2, 4, 3 };
    d7_hamt_bag bag = d7_hamt_bag_create(&policy);
    for (size_t index = 0; index < 3; ++index) {
        CHECK_STATUS(d7_hamt_bag_add_copies(&bag, &values[index], counts[index], &bag));
    }
    reset_callback_counts(&state);
    CHECK_STATUS(d7_hamt_bag_add(&bag, &equal_first, &bag));
    CHECK(state.hash_calls == 1);
    ++counts[0];
    assert_entry(&bag, &equal_first, &values[0], counts[0]);

    d7_hamt_bag_entry entries[3];
    size_t entry_count = 0;
    d7_hamt_bag_entry_iterator entry_iterator;
    d7_hamt_bag_entry_iterator_init(&bag, &entry_iterator);
    d7_hamt_bag_entry next_entry;
    while (d7_hamt_bag_entry_iterator_next(&entry_iterator, &next_entry)) {
        CHECK(entry_count < 3);
        entries[entry_count++] = next_entry;
    }
    CHECK(entry_count == 3);

    d7_hamt_bag_distinct_iterator distinct_iterator;
    d7_hamt_bag_distinct_iterator_init(&bag, &distinct_iterator);
    for (size_t index = 0; index < entry_count; ++index) {
        const void *item = NULL;
        CHECK(d7_hamt_bag_distinct_iterator_next(&distinct_iterator, &item));
        CHECK(item == entries[index].item);
    }
    const void *item = (const void *)(uintptr_t)1;
    CHECK(!d7_hamt_bag_distinct_iterator_next(&distinct_iterator, &item));
    CHECK(item == NULL);

    d7_hamt_bag_iterator expanded;
    d7_hamt_bag_iterator_init(&bag, &expanded);
    int64_t observed = 0;
    for (size_t index = 0; index < entry_count; ++index) {
        for (int32_t copy = 0; copy < entries[index].count; ++copy) {
            item = NULL;
            CHECK(d7_hamt_bag_iterator_next(&expanded, &item));
            CHECK(item == entries[index].item);
            ++observed;
        }
    }
    CHECK(observed == d7_hamt_bag_total_count(&bag));
    CHECK(!d7_hamt_bag_iterator_next(&expanded, &item));
    CHECK(item == NULL);

    next_entry.item = (const void *)(uintptr_t)1;
    next_entry.count = 1;
    CHECK(!d7_hamt_bag_entry_iterator_next(&entry_iterator, &next_entry));
    CHECK(next_entry.item == NULL && next_entry.count == 0);

    d7_hamt_bag_iterator first;
    d7_hamt_bag_iterator_init(&bag, &first);
    CHECK(d7_hamt_bag_iterator_next(&first, &item));
    d7_hamt_bag_iterator second = first;
    const void *first_items[10];
    const void *second_items[10];
    size_t first_count = 0;
    size_t second_count = 0;
    const void *next_item = NULL;
    while (d7_hamt_bag_iterator_next(&first, &next_item)) {
        CHECK(first_count < 10);
        first_items[first_count++] = next_item;
    }
    while (d7_hamt_bag_iterator_next(&second, &next_item)) {
        CHECK(second_count < 10);
        second_items[second_count++] = next_item;
    }
    CHECK(first_count == second_count);
    CHECK(memcmp(first_items, second_items, first_count * sizeof(first_items[0])) == 0);

    d7_hamt_bag_destroy(&bag);
    CHECK(state.live_references == 0);
}

static void test_same_policy_algebra_and_receiver_representatives(void) {
    bag_policy_state state = { BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false };
    const d7_hamt_set_policy policy = make_policy(&state);
    const bag_item receiver_a = { 1, 1 };
    const bag_item argument_a = { 1, 2 };
    const bag_item receiver_b = { 2, 1 };
    const bag_item argument_b = { 2, 2 };
    const bag_item argument_c = { 3, 1 };

    d7_hamt_bag left = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(&left, &receiver_a, 2, &left));
    CHECK_STATUS(d7_hamt_bag_add_copies(&left, &receiver_b, 5, &left));
    d7_hamt_bag right = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(&right, &argument_a, 3, &right));
    CHECK_STATUS(d7_hamt_bag_add(&right, &argument_b, &right));
    CHECK_STATUS(d7_hamt_bag_add_copies(&right, &argument_c, 4, &right));

    d7_hamt_bag union_result;
    d7_hamt_bag intersect_result;
    d7_hamt_bag except_result;
    d7_hamt_bag sum_result;
    CHECK_STATUS(d7_hamt_bag_union(&left, &right, &union_result));
    CHECK_STATUS(d7_hamt_bag_intersect(&left, &right, &intersect_result));
    CHECK_STATUS(d7_hamt_bag_except(&left, &right, &except_result));
    CHECK_STATUS(d7_hamt_bag_sum(&left, &right, &sum_result));

    assert_entry(&union_result, &argument_a, &receiver_a, 3);
    assert_entry(&union_result, &argument_b, &receiver_b, 5);
    assert_entry(&union_result, &argument_c, &argument_c, 4);
    CHECK(d7_hamt_bag_total_count(&union_result) == 12);
    assert_entry(&intersect_result, &argument_a, &receiver_a, 2);
    assert_entry(&intersect_result, &argument_b, &receiver_b, 1);
    CHECK(d7_hamt_bag_total_count(&intersect_result) == 3);
    assert_entry(&except_result, &argument_a, NULL, 0);
    assert_entry(&except_result, &argument_b, &receiver_b, 4);
    CHECK(d7_hamt_bag_total_count(&except_result) == 4);
    assert_entry(&sum_result, &argument_a, &receiver_a, 5);
    assert_entry(&sum_result, &argument_b, &receiver_b, 6);
    assert_entry(&sum_result, &argument_c, &argument_c, 4);
    CHECK(d7_hamt_bag_total_count(&sum_result) == 15);
    CHECK(d7_hamt_bag_debug_validate_canonical(&union_result));
    CHECK(d7_hamt_bag_debug_validate_canonical(&intersect_result));
    CHECK(d7_hamt_bag_debug_validate_canonical(&except_result));
    CHECK(d7_hamt_bag_debug_validate_canonical(&sum_result));

    d7_hamt_bag aliased_receiver = d7_hamt_bag_clone(&left);
    CHECK_STATUS(d7_hamt_bag_union(
        &aliased_receiver, &right, &aliased_receiver));
    assert_entry(&aliased_receiver, &argument_a, &receiver_a, 3);
    assert_entry(&aliased_receiver, &argument_c, &argument_c, 4);

    d7_hamt_bag aliased_argument = d7_hamt_bag_clone(&right);
    CHECK_STATUS(d7_hamt_bag_intersect(
        &left, &aliased_argument, &aliased_argument));
    assert_entry(&aliased_argument, &argument_a, &receiver_a, 2);
    assert_entry(&aliased_argument, &argument_b, &receiver_b, 1);

    d7_hamt_bag lower = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_add(&lower, &argument_a, &lower));
    d7_hamt_bag no_op;
    CHECK_STATUS(d7_hamt_bag_union(&left, &lower, &no_op));
    CHECK(d7_hamt_bag_shares_root(&left, &no_op));
    d7_hamt_bag_destroy(&no_op);
    CHECK_STATUS(d7_hamt_bag_intersect(&left, &left, &no_op));
    CHECK(d7_hamt_bag_shares_root(&left, &no_op));
    d7_hamt_bag_destroy(&no_op);
    d7_hamt_bag empty = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_except(&left, &empty, &no_op));
    CHECK(d7_hamt_bag_shares_root(&left, &no_op));
    d7_hamt_bag_destroy(&no_op);
    CHECK_STATUS(d7_hamt_bag_sum(&left, &empty, &no_op));
    CHECK(d7_hamt_bag_shares_root(&left, &no_op));
    d7_hamt_bag_destroy(&no_op);

    d7_hamt_bag self_except;
    CHECK_STATUS(d7_hamt_bag_except(&left, &left, &self_except));
    CHECK(d7_hamt_bag_is_empty(&self_except));
    d7_hamt_set_policy self_except_policy;
    CHECK_STATUS(d7_hamt_bag_get_policy(&self_except, &self_except_policy));
    CHECK(self_except_policy.context == &state);

    d7_hamt_bag self_sum;
    CHECK_STATUS(d7_hamt_bag_sum(&left, &left, &self_sum));
    assert_entry(&self_sum, &receiver_a, &receiver_a, 4);
    assert_entry(&self_sum, &receiver_b, &receiver_b, 10);
    CHECK(d7_hamt_bag_total_count(&self_sum) == 14);

    d7_hamt_bag maxed = d7_hamt_bag_create(&policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(&maxed, &receiver_a, INT32_MAX, &maxed));
    d7_hamt_bag failure_output = d7_hamt_bag_clone(&left);
    const void *failure_root = d7_hamt_bag_debug_root_identity(&failure_output);
    CHECK(d7_hamt_bag_sum(&maxed, &maxed, &failure_output)
        == D7_HAMT_OVERFLOW);
    CHECK(d7_hamt_bag_debug_root_identity(&failure_output) == failure_root);
    const void *maxed_root = d7_hamt_bag_debug_root_identity(&maxed);
    CHECK(d7_hamt_bag_sum(&maxed, &maxed, &maxed) == D7_HAMT_OVERFLOW);
    CHECK(d7_hamt_bag_debug_root_identity(&maxed) == maxed_root);
    CHECK(d7_hamt_bag_total_count(&maxed) == INT32_MAX);

    d7_hamt_bag_destroy(&failure_output);
    d7_hamt_bag_destroy(&maxed);
    d7_hamt_bag_destroy(&self_sum);
    d7_hamt_bag_destroy(&self_except);
    d7_hamt_bag_destroy(&empty);
    d7_hamt_bag_destroy(&lower);
    d7_hamt_bag_destroy(&aliased_argument);
    d7_hamt_bag_destroy(&aliased_receiver);
    d7_hamt_bag_destroy(&sum_result);
    d7_hamt_bag_destroy(&except_result);
    d7_hamt_bag_destroy(&intersect_result);
    d7_hamt_bag_destroy(&union_result);
    d7_hamt_bag_destroy(&right);
    d7_hamt_bag_destroy(&left);
    CHECK(state.live_references == 0);
}

static void test_foreign_policy_eager_normalization_and_collapse_overflow(void) {
    bag_policy_state receiver_state = {
        BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false
    };
    bag_policy_state argument_state = {
        BAG_EQUAL_EXACT, 0, 0, 0, 0, 0, 0, false
    };
    const d7_hamt_set_policy receiver_policy = make_policy(&receiver_state);
    const d7_hamt_set_policy argument_policy = make_policy(&argument_state);
    const bag_item receiver_value = { 1, 0 };
    const bag_item first = { 1, 1 };
    const bag_item second = { 1, 2 };

    d7_hamt_bag argument = d7_hamt_bag_create(&argument_policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(&argument, &first, 2, &argument));
    CHECK_STATUS(d7_hamt_bag_add_copies(&argument, &second, 3, &argument));
    d7_hamt_bag_entry_iterator order;
    d7_hamt_bag_entry_iterator_init(&argument, &order);
    d7_hamt_bag_entry first_entry;
    CHECK(d7_hamt_bag_entry_iterator_next(&order, &first_entry));

    d7_hamt_bag receiver_empty = d7_hamt_bag_create(&receiver_policy);
    d7_hamt_bag normalized_union;
    CHECK_STATUS(d7_hamt_bag_union(&receiver_empty, &argument, &normalized_union));
    assert_entry(&normalized_union, &receiver_value, (const bag_item *)first_entry.item, 5);
    d7_hamt_set_policy result_policy;
    CHECK_STATUS(d7_hamt_bag_get_policy(&normalized_union, &result_policy));
    CHECK(result_policy.context == &receiver_state);

    d7_hamt_bag receiver = d7_hamt_bag_create(&receiver_policy);
    CHECK_STATUS(d7_hamt_bag_add(&receiver, &receiver_value, &receiver));
    d7_hamt_bag receiver_wins;
    CHECK_STATUS(d7_hamt_bag_union(&receiver, &argument, &receiver_wins));
    assert_entry(&receiver_wins, &first, &receiver_value, 5);

    d7_hamt_bag foreign_intersection;
    CHECK_STATUS(d7_hamt_bag_intersect(
        &receiver, &argument, &foreign_intersection));
    CHECK(d7_hamt_bag_shares_root(&receiver, &foreign_intersection));
    assert_entry(&foreign_intersection, &second, &receiver_value, 1);

    d7_hamt_bag eager_no_op_output = d7_hamt_bag_clone(&receiver);
    const void *eager_no_op_root =
        d7_hamt_bag_debug_root_identity(&eager_no_op_output);
    d7_hamt_bag_test_fail_count_allocations_after(0);
    CHECK(d7_hamt_bag_intersect(&receiver, &argument, &eager_no_op_output)
        == D7_HAMT_OUT_OF_MEMORY);
    d7_hamt_bag_test_reset_count_allocator();
    CHECK(d7_hamt_bag_debug_root_identity(&eager_no_op_output)
        == eager_no_op_root);

    d7_hamt_bag foreign_except;
    CHECK_STATUS(d7_hamt_bag_except(&receiver, &argument, &foreign_except));
    CHECK(d7_hamt_bag_is_empty(&foreign_except));

    d7_hamt_bag foreign_sum;
    CHECK_STATUS(d7_hamt_bag_sum(&receiver, &argument, &foreign_sum));
    assert_entry(&foreign_sum, &second, &receiver_value, 6);

    d7_hamt_bag aliased_foreign_argument = d7_hamt_bag_clone(&argument);
    CHECK_STATUS(d7_hamt_bag_union(
        &receiver, &aliased_foreign_argument, &aliased_foreign_argument));
    assert_entry(&aliased_foreign_argument, &first, &receiver_value, 5);
    d7_hamt_set_policy aliased_policy;
    CHECK_STATUS(d7_hamt_bag_get_policy(&aliased_foreign_argument, &aliased_policy));
    CHECK(aliased_policy.context == &receiver_state);

    d7_hamt_bag retain_failure_output = d7_hamt_bag_clone(&receiver);
    const void *retain_failure_root =
        d7_hamt_bag_debug_root_identity(&retain_failure_output);
    reset_callback_counts(&receiver_state);
    receiver_state.fail_retain_at = 1;
    CHECK(d7_hamt_bag_union(&receiver, &argument, &retain_failure_output)
        == D7_HAMT_OUT_OF_MEMORY);
    CHECK(d7_hamt_bag_debug_root_identity(&retain_failure_output)
        == retain_failure_root);
    CHECK(d7_hamt_bag_debug_validate_canonical(&receiver));
    CHECK(d7_hamt_bag_debug_validate_canonical(&argument));
    receiver_state.fail_retain_at = 0;

    d7_hamt_bag overflowing_argument = d7_hamt_bag_create(&argument_policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(
        &overflowing_argument, &first, INT32_MAX, &overflowing_argument));
    CHECK_STATUS(d7_hamt_bag_add(&overflowing_argument, &second, &overflowing_argument));
    d7_hamt_bag receiver_max = d7_hamt_bag_create(&receiver_policy);
    CHECK_STATUS(d7_hamt_bag_add_copies(
        &receiver_max, &receiver_value, INT32_MAX, &receiver_max));
    d7_hamt_bag output = d7_hamt_bag_clone(&receiver);
    const void *output_root = d7_hamt_bag_debug_root_identity(&output);
    reset_callback_counts(&receiver_state);
    CHECK(d7_hamt_bag_union(&receiver_max, &overflowing_argument, &output)
        == D7_HAMT_OVERFLOW);
    CHECK(receiver_state.hash_calls > 0);
    CHECK(d7_hamt_bag_debug_root_identity(&output) == output_root);
    CHECK(d7_hamt_bag_debug_validate_canonical(&receiver_max));
    CHECK(d7_hamt_bag_debug_validate_canonical(&overflowing_argument));

    d7_hamt_bag_destroy(&output);
    d7_hamt_bag_destroy(&retain_failure_output);
    d7_hamt_bag_destroy(&receiver_max);
    d7_hamt_bag_destroy(&overflowing_argument);
    d7_hamt_bag_destroy(&foreign_sum);
    d7_hamt_bag_destroy(&foreign_except);
    d7_hamt_bag_destroy(&foreign_intersection);
    d7_hamt_bag_destroy(&eager_no_op_output);
    d7_hamt_bag_destroy(&aliased_foreign_argument);
    d7_hamt_bag_destroy(&receiver_wins);
    d7_hamt_bag_destroy(&receiver);
    d7_hamt_bag_destroy(&normalized_union);
    d7_hamt_bag_destroy(&receiver_empty);
    d7_hamt_bag_destroy(&argument);
    CHECK(receiver_state.live_references == 0);
    CHECK(argument_state.live_references == 0);
}

static void test_validation_and_allocation_failures_leave_outputs_and_owners_unchanged(void) {
    bag_policy_state state = { BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false };
    const d7_hamt_set_policy policy = make_policy(&state);
    bag_item items[10];
    d7_hamt_bag source = d7_hamt_bag_create(&policy);
    for (size_t index = 0; index < 10; ++index) {
        items[index].group = (int)index;
        items[index].variant = (int)(index + 100);
        CHECK_STATUS(d7_hamt_bag_add(&source, &items[index], &source));
    }
    const void *source_root = d7_hamt_bag_debug_root_identity(&source);
    const int64_t source_total = d7_hamt_bag_total_count(&source);

    d7_hamt_bag output = d7_hamt_bag_clone(&source);
    const void *output_root = d7_hamt_bag_debug_root_identity(&output);
    CHECK(d7_hamt_bag_union(&source, NULL, &output) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_union(NULL, &source, &output) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_add(NULL, &items[0], &output) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_add(&source, &items[0], NULL) == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_bag_debug_root_identity(&output) == output_root);
    d7_hamt_bag_destroy(&output);

    reset_callback_counts(&state);
    d7_hamt_bag invalid_range_output = sentinel_bag();
    const d7_hamt_bag invalid_range_before = invalid_range_output;
    CHECK(d7_hamt_bag_create_range(&policy, NULL, 1, &invalid_range_output)
        == D7_HAMT_INVALID_ARGUMENT);
    assert_output_bytes_unchanged(&invalid_range_before, &invalid_range_output);
    CHECK(state.hash_calls == 0 && state.equal_calls == 0 && state.retain_calls == 0);

    bag_item added = { 99, 1 };
    bool saw_map_failure = false;
    bool map_completed = false;
    for (size_t fail_after = 0; fail_after < 128 && !map_completed; ++fail_after) {
        d7_hamt_bag candidate = sentinel_bag();
        const d7_hamt_bag before = candidate;
        d7_hamt_test_fail_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_bag_add(&source, &added, &candidate);
        d7_hamt_test_reset_allocator();
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_map_failure = true;
            assert_output_bytes_unchanged(&before, &candidate);
        } else {
            CHECK(status == D7_HAMT_OK);
            map_completed = true;
            d7_hamt_bag_destroy(&candidate);
        }
        CHECK(d7_hamt_bag_debug_root_identity(&source) == source_root);
        CHECK(d7_hamt_bag_total_count(&source) == source_total);
        CHECK(d7_hamt_bag_debug_validate_canonical(&source));
    }
    CHECK(saw_map_failure && map_completed);

    bool saw_count_failure = false;
    bool count_completed = false;
    for (size_t fail_after = 0; fail_after < 128 && !count_completed; ++fail_after) {
        d7_hamt_bag candidate = sentinel_bag();
        const d7_hamt_bag before = candidate;
        d7_hamt_bag_test_fail_count_allocations_after(fail_after);
        const d7_hamt_status status = d7_hamt_bag_add(&source, &added, &candidate);
        d7_hamt_bag_test_reset_count_allocator();
        if (status == D7_HAMT_OUT_OF_MEMORY) {
            saw_count_failure = true;
            assert_output_bytes_unchanged(&before, &candidate);
        } else {
            CHECK(status == D7_HAMT_OK);
            count_completed = true;
            d7_hamt_bag_destroy(&candidate);
        }
        CHECK(d7_hamt_bag_debug_root_identity(&source) == source_root);
        CHECK(d7_hamt_bag_debug_validate_canonical(&source));
    }
    CHECK(saw_count_failure && count_completed);

    reset_callback_counts(&state);
    state.fail_retain_at = 1;
    output = sentinel_bag();
    const d7_hamt_bag output_before = output;
    CHECK(d7_hamt_bag_add(&source, &added, &output) == D7_HAMT_OUT_OF_MEMORY);
    assert_output_bytes_unchanged(&output_before, &output);
    CHECK(d7_hamt_bag_debug_root_identity(&source) == source_root);
    CHECK(d7_hamt_bag_debug_validate_canonical(&source));
    state.fail_retain_at = 0;

    const void *const range_items[] = { &items[0], &items[1], &items[2] };
    d7_hamt_bag range_output = sentinel_bag();
    const d7_hamt_bag range_before = range_output;
    d7_hamt_bag_test_fail_count_allocations_after(0);
    CHECK(d7_hamt_bag_create_range(&policy, range_items, 3, &range_output)
        == D7_HAMT_OUT_OF_MEMORY);
    d7_hamt_bag_test_reset_count_allocator();
    assert_output_bytes_unchanged(&range_before, &range_output);

    d7_hamt_bag_destroy(&source);
    CHECK(state.live_references == 0);
}

static void test_algebra_allocation_failure_sweeps_are_atomic(void) {
    bag_policy_state receiver_state = {
        BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false
    };
    bag_policy_state argument_state = {
        BAG_EQUAL_EXACT, 0, 0, 0, 0, 0, 0, false
    };
    const d7_hamt_set_policy receiver_policy = make_policy(&receiver_state);
    const d7_hamt_set_policy argument_policy = make_policy(&argument_state);
    bag_item receiver_items[5];
    bag_item argument_items[8];
    d7_hamt_bag receiver = d7_hamt_bag_create(&receiver_policy);
    d7_hamt_bag argument = d7_hamt_bag_create(&argument_policy);
    for (size_t index = 0; index < 5; ++index) {
        receiver_items[index].group = (int)index;
        receiver_items[index].variant = 0;
        CHECK_STATUS(d7_hamt_bag_add_copies(
            &receiver, &receiver_items[index], (int64_t)index + 1, &receiver));
    }
    for (size_t index = 0; index < 8; ++index) {
        argument_items[index].group = (int)(index % 5);
        argument_items[index].variant = (int)(index + 1);
        CHECK_STATUS(d7_hamt_bag_add_copies(
            &argument, &argument_items[index], (int64_t)(index % 3) + 1, &argument));
    }
    const void *receiver_root = d7_hamt_bag_debug_root_identity(&receiver);
    const void *argument_root = d7_hamt_bag_debug_root_identity(&argument);
    const bag_algebra_fn operations[] = {
        d7_hamt_bag_union,
        d7_hamt_bag_intersect,
        d7_hamt_bag_except,
        d7_hamt_bag_sum
    };
    const size_t operation_count = sizeof(operations) / sizeof(operations[0]);

    for (size_t operation = 0; operation < operation_count; ++operation) {
        bool saw_failure = false;
        bool completed = false;
        for (size_t fail_after = 0; fail_after < 512 && !completed; ++fail_after) {
            d7_hamt_bag output = sentinel_bag();
            const d7_hamt_bag before = output;
            d7_hamt_test_fail_allocations_after(fail_after);
            const d7_hamt_status status =
                operations[operation](&receiver, &argument, &output);
            d7_hamt_test_reset_allocator();
            if (status == D7_HAMT_OUT_OF_MEMORY) {
                saw_failure = true;
                assert_output_bytes_unchanged(&before, &output);
            } else {
                CHECK(status == D7_HAMT_OK);
                completed = true;
                CHECK(d7_hamt_bag_debug_validate_canonical(&output));
                d7_hamt_bag_destroy(&output);
            }
            CHECK(d7_hamt_bag_debug_root_identity(&receiver) == receiver_root);
            CHECK(d7_hamt_bag_debug_root_identity(&argument) == argument_root);
            CHECK(d7_hamt_bag_debug_validate_canonical(&receiver));
            CHECK(d7_hamt_bag_debug_validate_canonical(&argument));
        }
        CHECK(saw_failure && completed);
    }

    for (size_t operation = 0; operation < operation_count; ++operation) {
        bool saw_count_failure = false;
        bool count_completed = false;
        for (size_t fail_after = 0;
             fail_after < 512 && !count_completed;
             ++fail_after) {
            d7_hamt_bag output = sentinel_bag();
            const d7_hamt_bag before = output;
            d7_hamt_bag_test_fail_count_allocations_after(fail_after);
            const d7_hamt_status status =
                operations[operation](&receiver, &argument, &output);
            d7_hamt_bag_test_reset_count_allocator();
            if (status == D7_HAMT_OUT_OF_MEMORY) {
                saw_count_failure = true;
                assert_output_bytes_unchanged(&before, &output);
            } else {
                CHECK(status == D7_HAMT_OK);
                count_completed = true;
                CHECK(d7_hamt_bag_debug_validate_canonical(&output));
                d7_hamt_bag_destroy(&output);
            }
            CHECK(d7_hamt_bag_debug_root_identity(&receiver) == receiver_root);
            CHECK(d7_hamt_bag_debug_root_identity(&argument) == argument_root);
            CHECK(d7_hamt_bag_debug_validate_canonical(&receiver));
            CHECK(d7_hamt_bag_debug_validate_canonical(&argument));
        }
        CHECK(saw_count_failure && count_completed);
    }

    d7_hamt_bag_destroy(&argument);
    d7_hamt_bag_destroy(&receiver);
    CHECK(receiver_state.live_references == 0);
    CHECK(argument_state.live_references == 0);
}

static uint32_t next_random(uint32_t *state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

typedef struct bag_snapshot {
    d7_hamt_bag bag;
    int32_t counts[12];
    int64_t total;
    bool active;
} bag_snapshot;

static void assert_model(
    const d7_hamt_bag *bag,
    const bag_item *items,
    const int32_t *counts,
    int64_t total) {
    size_t distinct = 0;
    for (size_t index = 0; index < 12; ++index) {
        CHECK(d7_hamt_bag_count_of(bag, &items[index]) == counts[index]);
        if (counts[index] > 0) {
            ++distinct;
        }
    }
    CHECK(d7_hamt_bag_distinct_count(bag) == distinct);
    CHECK(d7_hamt_bag_total_count(bag) == total);
    CHECK(d7_hamt_bag_debug_validate_canonical(bag));
}

static void test_deterministic_model_history_and_retained_snapshots(void) {
    bag_policy_state policy_state = {
        BAG_EQUAL_BY_GROUP, 0, 0, 0, 0, 0, 0, false
    };
    const d7_hamt_set_policy policy = make_policy(&policy_state);
    bag_item items[12];
    int32_t counts[12] = { 0 };
    int64_t total = 0;
    for (size_t index = 0; index < 12; ++index) {
        items[index].group = (int)index;
        items[index].variant = (int)(100 + index);
    }
    bag_snapshot snapshots[16];
    memset(snapshots, 0, sizeof(snapshots));
    size_t next_snapshot = 0;

    d7_hamt_bag bag = d7_hamt_bag_create(&policy);
    uint32_t random = UINT32_C(0xC0FFEE);
    for (size_t step = 0; step < 1000; ++step) {
        const size_t index = next_random(&random) % 12u;
        const int32_t copies = (int32_t)(next_random(&random) % 4u);
        const uint32_t operation = next_random(&random) % 5u;
        if (operation == 0) {
            CHECK_STATUS(d7_hamt_bag_add_copies(&bag, &items[index], copies, &bag));
            counts[index] += copies;
            total += copies;
        } else if (operation == 1) {
            CHECK_STATUS(d7_hamt_bag_remove_copies(&bag, &items[index], copies, &bag));
            const int32_t removed = copies < counts[index] ? copies : counts[index];
            counts[index] -= removed;
            total -= removed;
        } else if (operation == 2) {
            CHECK_STATUS(d7_hamt_bag_remove_all(&bag, &items[index], &bag));
            total -= counts[index];
            counts[index] = 0;
        } else if (operation == 3) {
            const void *root = d7_hamt_bag_debug_root_identity(&bag);
            CHECK_STATUS(d7_hamt_bag_add_copies(&bag, &items[index], 0, &bag));
            CHECK(d7_hamt_bag_debug_root_identity(&bag) == root);
        } else if (step % 97u == 0) {
            CHECK_STATUS(d7_hamt_bag_clear(&bag, &bag));
            memset(counts, 0, sizeof(counts));
            total = 0;
        }

        assert_model(&bag, items, counts, total);
        if (step % 67u == 0) {
            bag_snapshot *snapshot = &snapshots[next_snapshot % 16u];
            if (snapshot->active) {
                d7_hamt_bag_destroy(&snapshot->bag);
            }
            snapshot->bag = d7_hamt_bag_clone(&bag);
            memcpy(snapshot->counts, counts, sizeof(counts));
            snapshot->total = total;
            snapshot->active = true;
            ++next_snapshot;
        }
    }

    for (size_t index = 0; index < 16; ++index) {
        if (snapshots[index].active) {
            assert_model(
                &snapshots[index].bag,
                items,
                snapshots[index].counts,
                snapshots[index].total);
            d7_hamt_bag_destroy(&snapshots[index].bag);
        }
    }
    d7_hamt_bag_destroy(&bag);
    CHECK(policy_state.live_references == 0);
}

static const test_case tests[] = {
    { "construction queries representatives and one-descent add",
      test_construction_queries_representatives_and_one_descent_add },
    { "copy-count validation overflow and root-sharing no-ops",
      test_copy_count_validation_overflow_and_root_sharing_no_ops },
    { "remove clear and aliasing preserve versions",
      test_remove_clear_and_aliasing_preserve_versions },
    { "expanded distinct entry iterators and copy independence",
      test_expanded_distinct_entry_iterators_and_copy_independence },
    { "same-policy algebra and receiver representatives",
      test_same_policy_algebra_and_receiver_representatives },
    { "foreign-policy eager normalization and collapse overflow",
      test_foreign_policy_eager_normalization_and_collapse_overflow },
    { "validation and allocation failures leave outputs and owners unchanged",
      test_validation_and_allocation_failures_leave_outputs_and_owners_unchanged },
    { "algebra allocation failure sweeps are atomic",
      test_algebra_allocation_failure_sweeps_are_atomic },
    { "deterministic model history and retained snapshots",
      test_deterministic_model_history_and_retained_snapshots }
};

int main(void) {
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    for (size_t index = 0; index < test_count; ++index) {
        tests[index].run();
        printf("[PASS] %s\n", tests[index].name);
    }
    printf("%zu test(s) passed\n", test_count);
    return EXIT_SUCCESS;
}
