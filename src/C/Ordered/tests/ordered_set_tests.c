#include <tools/data_structures/ordered/ordered_set.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static void fail_at(const char* file, int line, const char* expression)
{
    (void)fprintf(stderr, "%s:%d: requirement failed: %s\n", file, line, expression);
    ++g_failures;
}

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fail_at(__FILE__, __LINE__, #expression); \
            return; \
        } \
    } while (0)

#define REQUIRE_STATUS(expression) \
    do { \
        const tds_ordered_status actual_status__ = (expression); \
        if (actual_status__ != TDS_ORDERED_OK) { \
            (void)fprintf( \
                stderr, \
                "%s:%d: %s returned %d\n", \
                __FILE__, \
                __LINE__, \
                #expression, \
                (int)actual_status__); \
            ++g_failures; \
            return; \
        } \
    } while (0)

typedef struct int_buffer {
    int values[256];
    size_t count;
} int_buffer;

typedef struct ownership_counts {
    size_t copies;
    size_t destroys;
    size_t hash_calls;
} ownership_counts;

typedef struct int_model {
    int values[128];
    size_t count;
} int_model;

static uint32_t hash_int(const void* item, void* context)
{
    (void)context;
    uint32_t value = (uint32_t)*(const int*)item;
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value;
}

static bool equal_int(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

static int normalized_mod10(int value)
{
    int normalized = value % 10;
    return normalized < 0 ? normalized + 10 : normalized;
}

static uint32_t hash_mod10(const void* item, void* context)
{
    (void)context;
    return (uint32_t)normalized_mod10(*(const int*)item);
}

static bool equal_mod10(const void* left, const void* right, void* context)
{
    (void)context;
    return normalized_mod10(*(const int*)left) == normalized_mod10(*(const int*)right);
}

static int compare_parity(const void* left, const void* right, void* context)
{
    (void)context;
    const int left_parity = normalized_mod10(*(const int*)left) & 1;
    const int right_parity = normalized_mod10(*(const int*)right) & 1;
    return (left_parity > right_parity) - (left_parity < right_parity);
}

static void copy_counted(void* destination, const void* source, void* context)
{
    ownership_counts* counts = (ownership_counts*)context;
    ++counts->copies;
    *(int*)destination = *(const int*)source;
}

static void destroy_counted(void* value, void* context)
{
    ownership_counts* counts = (ownership_counts*)context;
    ++counts->destroys;
    *(int*)value = 0;
}

static uint32_t hash_counted(const void* item, void* context)
{
    ownership_counts* counts = (ownership_counts*)context;
    ++counts->hash_calls;
    return hash_int(item, NULL);
}

static void init_int_policy(tds_ordered_policy* policy)
{
    ft_value_type item_type;
    ft_value_type_init(&item_type, sizeof(int));
    tds_ordered_policy_init(policy, &item_type, hash_int, equal_int, NULL);
}

static void init_mod10_policy(tds_ordered_policy* policy)
{
    ft_value_type item_type;
    ft_value_type_init(&item_type, sizeof(int));
    tds_ordered_policy_init(policy, &item_type, hash_mod10, equal_mod10, NULL);
}

static void init_counted_policy(
    tds_ordered_policy* policy,
    ownership_counts* counts)
{
    ft_value_type item_type;
    ft_value_type_init(&item_type, sizeof(int));
    item_type.copy = copy_counted;
    item_type.destroy = destroy_counted;
    item_type.context = counts;
    tds_ordered_policy_init(policy, &item_type, hash_counted, equal_int, counts);
}

static void collect_int(const void* item, void* context)
{
    int_buffer* buffer = (int_buffer*)context;
    if (buffer->count < sizeof(buffer->values) / sizeof(buffer->values[0])) {
        buffer->values[buffer->count++] = *(const int*)item;
    }
}

static bool set_matches(
    const tds_ordered_set* set,
    const int* expected,
    size_t count)
{
    if (tds_ordered_set_size(set) != count || !tds_ordered_set_debug_validate(set)) {
        return false;
    }
    for (size_t index = 0u; index != count; ++index) {
        const void* item = NULL;
        if (tds_ordered_set_at(set, index, &item) != TDS_ORDERED_OK ||
            item == NULL || *(const int*)item != expected[index]) {
            return false;
        }
    }

    int_buffer buffer;
    buffer.count = 0u;
    if (tds_ordered_set_visit(set, collect_int, &buffer) != TDS_ORDERED_OK ||
        buffer.count != count) {
        return false;
    }
    for (size_t index = 0u; index != count; ++index) {
        if (buffer.values[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

static void replace_set(tds_ordered_set* current, tds_ordered_set* next)
{
    tds_ordered_set_destroy(current);
    tds_ordered_set_move(current, next);
}

static void test_construction_and_representatives(void)
{
    tds_ordered_policy policy;
    init_mod10_policy(&policy);
    const int values[] = {1, 11, 2, 12, 3, 21};
    tds_ordered_set set;
    REQUIRE_STATUS(tds_ordered_set_from_array(
        &set,
        &policy,
        values,
        sizeof(values) / sizeof(values[0])));
    const int expected[] = {1, 2, 3};
    REQUIRE(set_matches(&set, expected, sizeof(expected) / sizeof(expected[0])));
    REQUIRE(!tds_ordered_set_empty(&set));
    REQUIRE(tds_ordered_set_policy(&set) != NULL);

    const int lookup = 31;
    const void* actual = NULL;
    REQUIRE(tds_ordered_set_try_get_value(&set, &lookup, &actual));
    REQUIRE(actual != NULL && *(const int*)actual == 1);
    size_t index = SIZE_MAX;
    REQUIRE(tds_ordered_set_index_of(&set, &lookup, &index));
    REQUIRE(index == 0u);

    const void* endpoint = NULL;
    REQUIRE_STATUS(tds_ordered_set_front(&set, &endpoint));
    REQUIRE(*(const int*)endpoint == 1);
    REQUIRE_STATUS(tds_ordered_set_back(&set, &endpoint));
    REQUIRE(*(const int*)endpoint == 3);

    const int duplicate = 41;
    tds_ordered_set unchanged;
    REQUIRE_STATUS(tds_ordered_set_add(&set, &duplicate, &unchanged));
    REQUIRE(tds_ordered_set_debug_shares_order(&set, &unchanged));
    REQUIRE(tds_ordered_set_debug_shares_index(&set, &unchanged));
    REQUIRE(set_matches(&unchanged, expected, sizeof(expected) / sizeof(expected[0])));

    const void* item_pointers[] = {
        &values[0], &values[1], &values[2], &values[3], &values[4]
    };
    tds_ordered_set from_items;
    REQUIRE_STATUS(tds_ordered_set_from_items(
        &from_items,
        &policy,
        item_pointers,
        sizeof(item_pointers) / sizeof(item_pointers[0])));
    REQUIRE(set_matches(&from_items, expected, sizeof(expected) / sizeof(expected[0])));

    tds_ordered_set_destroy(&from_items);
    tds_ordered_set_destroy(&unchanged);
    tds_ordered_set_destroy(&set);
}

static void test_positional_edits_and_removal(void)
{
    tds_ordered_policy policy;
    init_int_policy(&policy);
    const int initial[] = {1, 2, 3};
    tds_ordered_set current;
    REQUIRE_STATUS(tds_ordered_set_from_array(&current, &policy, initial, 3u));

    int value = 4;
    tds_ordered_set next;
    REQUIRE_STATUS(tds_ordered_set_add_first(&current, &value, &next));
    replace_set(&current, &next);
    const int after_first[] = {4, 1, 2, 3};
    REQUIRE(set_matches(&current, after_first, 4u));

    value = 5;
    REQUIRE_STATUS(tds_ordered_set_insert(&current, 2u, &value, &next));
    replace_set(&current, &next);
    const int after_insert[] = {4, 1, 5, 2, 3};
    REQUIRE(set_matches(&current, after_insert, 5u));

    value = 1;
    REQUIRE_STATUS(tds_ordered_set_move_to(&current, 4u, &value, &next));
    replace_set(&current, &next);
    const int after_move[] = {4, 5, 2, 3, 1};
    REQUIRE(set_matches(&current, after_move, 5u));

    value = 3;
    REQUIRE_STATUS(tds_ordered_set_move_to_first(&current, &value, &next));
    replace_set(&current, &next);
    const int after_move_first[] = {3, 4, 5, 2, 1};
    REQUIRE(set_matches(&current, after_move_first, 5u));

    value = 4;
    REQUIRE_STATUS(tds_ordered_set_move_to_last(&current, &value, &next));
    replace_set(&current, &next);
    const int after_move_last[] = {3, 5, 2, 1, 4};
    REQUIRE(set_matches(&current, after_move_last, 5u));

    value = 99;
    REQUIRE(tds_ordered_set_move_to_first(&current, &value, &next) == TDS_ORDERED_NOT_FOUND);
    REQUIRE(tds_ordered_set_move_to(&current, 5u, &value, &next) == TDS_ORDERED_OUT_OF_RANGE);

    bool removed = false;
    value = 2;
    REQUIRE_STATUS(tds_ordered_set_try_remove(&current, &value, &removed, &next));
    REQUIRE(removed);
    replace_set(&current, &next);
    const int after_remove[] = {3, 5, 1, 4};
    REQUIRE(set_matches(&current, after_remove, 4u));

    value = 99;
    REQUIRE_STATUS(tds_ordered_set_try_remove(&current, &value, &removed, &next));
    REQUIRE(!removed);
    REQUIRE(tds_ordered_set_debug_shares_order(&current, &next));
    replace_set(&current, &next);

    REQUIRE_STATUS(tds_ordered_set_remove_at(&current, 1u, &next));
    replace_set(&current, &next);
    REQUIRE_STATUS(tds_ordered_set_remove_first(&current, &next));
    replace_set(&current, &next);
    REQUIRE_STATUS(tds_ordered_set_remove_last(&current, &next));
    replace_set(&current, &next);
    const int remaining[] = {1};
    REQUIRE(set_matches(&current, remaining, 1u));

    REQUIRE_STATUS(tds_ordered_set_clear(&current, &next));
    replace_set(&current, &next);
    REQUIRE(tds_ordered_set_empty(&current));
    REQUIRE(tds_ordered_set_remove_first(&current, &next) == TDS_ORDERED_EMPTY);
    REQUIRE(tds_ordered_set_remove_last(&current, &next) == TDS_ORDERED_EMPTY);
    tds_ordered_set_destroy(&current);
}

static void test_ranges_reverse_and_stable_sort(void)
{
    tds_ordered_policy policy;
    init_int_policy(&policy);
    const int values[] = {5, 2, 3, 4, 1};
    tds_ordered_set set;
    REQUIRE_STATUS(tds_ordered_set_from_array(&set, &policy, values, 5u));

    tds_ordered_set range;
    REQUIRE_STATUS(tds_ordered_set_get_range(&set, 1u, 3u, &range));
    const int expected_range[] = {2, 3, 4};
    REQUIRE(set_matches(&range, expected_range, 3u));

    tds_ordered_set taken;
    REQUIRE_STATUS(tds_ordered_set_take(&set, 2u, &taken));
    const int expected_take[] = {5, 2};
    REQUIRE(set_matches(&taken, expected_take, 2u));

    tds_ordered_set dropped;
    REQUIRE_STATUS(tds_ordered_set_drop(&set, 3u, &dropped));
    const int expected_drop[] = {4, 1};
    REQUIRE(set_matches(&dropped, expected_drop, 2u));

    tds_ordered_set reversed;
    REQUIRE_STATUS(tds_ordered_set_reverse(&set, &reversed));
    const int expected_reverse[] = {1, 4, 3, 2, 5};
    REQUIRE(set_matches(&reversed, expected_reverse, 5u));

    tds_ordered_set sorted;
    REQUIRE_STATUS(tds_ordered_set_sort(&set, compare_parity, NULL, &sorted));
    const int expected_sort[] = {2, 4, 5, 3, 1};
    REQUIRE(set_matches(&sorted, expected_sort, 5u));

    tds_ordered_set unchanged;
    REQUIRE_STATUS(tds_ordered_set_sort(&sorted, compare_parity, NULL, &unchanged));
    REQUIRE(tds_ordered_set_debug_shares_order(&sorted, &unchanged));
    REQUIRE(tds_ordered_set_debug_shares_index(&sorted, &unchanged));

    tds_ordered_set invalid_result;
    (void)memset(&invalid_result, 0, sizeof(invalid_result));
    REQUIRE(tds_ordered_set_get_range(&set, 4u, 2u, &invalid_result) ==
        TDS_ORDERED_OUT_OF_RANGE);
    REQUIRE(invalid_result.context == NULL);
    tds_ordered_set_destroy(&unchanged);
    tds_ordered_set_destroy(&sorted);
    tds_ordered_set_destroy(&reversed);
    tds_ordered_set_destroy(&dropped);
    tds_ordered_set_destroy(&taken);
    tds_ordered_set_destroy(&range);
    tds_ordered_set_destroy(&set);
}

static void test_algebra_and_relations(void)
{
    tds_ordered_policy mod_policy;
    tds_ordered_policy exact_policy;
    init_mod10_policy(&mod_policy);
    init_int_policy(&exact_policy);
    const int receiver_values[] = {1, 2, 3};
    tds_ordered_set receiver;
    REQUIRE_STATUS(tds_ordered_set_from_array(
        &receiver,
        &mod_policy,
        receiver_values,
        3u));

    const int a = 11;
    const int b = 4;
    const int c = 14;
    const int d = 2;
    const void* arguments[] = {&a, &b, &c, &d};
    tds_ordered_set result;
    REQUIRE_STATUS(tds_ordered_set_union_many(&receiver, arguments, 4u, &result));
    const int expected_union[] = {1, 2, 3, 4};
    REQUIRE(set_matches(&result, expected_union, 4u));
    tds_ordered_set_destroy(&result);

    REQUIRE_STATUS(tds_ordered_set_intersect_many(&receiver, arguments, 4u, &result));
    const int expected_intersect[] = {1, 2};
    REQUIRE(set_matches(&result, expected_intersect, 2u));
    tds_ordered_set_destroy(&result);

    REQUIRE_STATUS(tds_ordered_set_except_many(&receiver, arguments, 4u, &result));
    const int expected_except[] = {3};
    REQUIRE(set_matches(&result, expected_except, 1u));
    tds_ordered_set_destroy(&result);

    REQUIRE_STATUS(tds_ordered_set_symmetric_except_many(
        &receiver,
        arguments,
        4u,
        &result));
    const int expected_symmetric[] = {3, 4};
    REQUIRE(set_matches(&result, expected_symmetric, 2u));
    tds_ordered_set_destroy(&result);

    const int right_values[] = {11, 21, 4};
    tds_ordered_set right;
    REQUIRE_STATUS(tds_ordered_set_from_array(&right, &exact_policy, right_values, 3u));
    REQUIRE_STATUS(tds_ordered_set_union(&receiver, &right, &result));
    REQUIRE(set_matches(&result, expected_union, 4u));
    tds_ordered_set_destroy(&result);

    bool answer = false;
    REQUIRE_STATUS(tds_ordered_set_is_subset_of_many(&receiver, arguments, 4u, &answer));
    REQUIRE(!answer);
    REQUIRE_STATUS(tds_ordered_set_is_proper_subset_of_many(&receiver, arguments, 4u, &answer));
    REQUIRE(!answer);
    REQUIRE_STATUS(tds_ordered_set_is_superset_of_many(&receiver, arguments, 4u, &answer));
    REQUIRE(!answer);
    REQUIRE_STATUS(tds_ordered_set_is_proper_superset_of_many(&receiver, arguments, 4u, &answer));
    REQUIRE(!answer);
    REQUIRE_STATUS(tds_ordered_set_overlaps_many(&receiver, arguments, 4u, &answer));
    REQUIRE(answer);
    REQUIRE_STATUS(tds_ordered_set_equals_many(&receiver, arguments, 4u, &answer));
    REQUIRE(!answer);

    const int same_a = 11;
    const int same_b = 12;
    const int same_c = 13;
    const int same_duplicate = 21;
    const void* same_items[] = {&same_a, &same_b, &same_c, &same_duplicate};
    REQUIRE_STATUS(tds_ordered_set_equals_many(&receiver, same_items, 4u, &answer));
    REQUIRE(answer);
    REQUIRE_STATUS(tds_ordered_set_is_subset_of(&receiver, &right, &answer));
    REQUIRE(!answer);
    REQUIRE_STATUS(tds_ordered_set_overlaps(&receiver, &right, &answer));
    REQUIRE(answer);

    tds_ordered_set_destroy(&right);
    tds_ordered_set_destroy(&receiver);
}

static void test_relabel_persistence_and_failure_atomicity(void)
{
    ownership_counts counts;
    (void)memset(&counts, 0, sizeof(counts));
    tds_ordered_policy policy;
    init_counted_policy(&policy, &counts);
    const int initial[] = {0, 1};
    tds_ordered_set original;
    REQUIRE_STATUS(tds_ordered_set_from_array(&original, &policy, initial, 2u));
    tds_ordered_set current;
    REQUIRE_STATUS(tds_ordered_set_clone(&original, &current));

    for (int value = 2; value != 72; ++value) {
        tds_ordered_set next;
        REQUIRE_STATUS(tds_ordered_set_insert(&current, 1u, &value, &next));
        REQUIRE(tds_ordered_set_debug_validate(&next));
        replace_set(&current, &next);
    }
    REQUIRE(tds_ordered_set_size(&current) == 72u);
    REQUIRE(set_matches(&original, initial, 2u));

    tds_ordered_set untouched;
    (void)memset(&untouched, 0, sizeof(untouched));
    tds_ordered_set untouched_before = untouched;
    counts.hash_calls = 0u;
    const int value = 5;
    REQUIRE(tds_ordered_set_insert(&current, 999u, &value, &untouched) ==
        TDS_ORDERED_OUT_OF_RANGE);
    REQUIRE(memcmp(&untouched, &untouched_before, sizeof(untouched)) == 0);
    REQUIRE(counts.hash_calls == 0u);
    REQUIRE(tds_ordered_set_move_to(&current, 999u, &value, &untouched) ==
        TDS_ORDERED_OUT_OF_RANGE);
    REQUIRE(memcmp(&untouched, &untouched_before, sizeof(untouched)) == 0);
    REQUIRE(counts.hash_calls == 0u);

    bool answer = true;
    const void* bad_items[] = {&value, NULL};
    REQUIRE(tds_ordered_set_equals_many(&current, bad_items, 2u, &answer) ==
        TDS_ORDERED_INVALID_ARGUMENT);
    REQUIRE(answer);
    REQUIRE(tds_ordered_set_debug_validate(&current));

    tds_ordered_set_destroy(&current);
    tds_ordered_set_destroy(&original);
    REQUIRE(counts.copies == counts.destroys);
}

static size_t model_find(const int_model* model, int value)
{
    for (size_t index = 0u; index != model->count; ++index) {
        if (model->values[index] == value) {
            return index;
        }
    }
    return SIZE_MAX;
}

static void model_insert(int_model* model, size_t index, int value)
{
    if (model_find(model, value) != SIZE_MAX) {
        return;
    }
    (void)memmove(
        model->values + index + 1u,
        model->values + index,
        (model->count - index) * sizeof(model->values[0]));
    model->values[index] = value;
    ++model->count;
}

static void model_remove_at(int_model* model, size_t index)
{
    (void)memmove(
        model->values + index,
        model->values + index + 1u,
        (model->count - index - 1u) * sizeof(model->values[0]));
    --model->count;
}

static void model_move(int_model* model, size_t source, size_t destination)
{
    if (source == destination) {
        return;
    }
    const int value = model->values[source];
    model_remove_at(model, source);
    (void)memmove(
        model->values + destination + 1u,
        model->values + destination,
        (model->count - destination) * sizeof(model->values[0]));
    model->values[destination] = value;
    ++model->count;
}

static uint32_t next_random(uint32_t* state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void test_generated_model(void)
{
    tds_ordered_policy policy;
    init_int_policy(&policy);
    tds_ordered_set current;
    REQUIRE_STATUS(tds_ordered_set_init(&current, &policy));
    int_model model;
    model.count = 0u;
    uint32_t random = UINT32_C(0x6d2b79f5);

    for (size_t step = 0u; step != 1000u; ++step) {
        const uint32_t bits = next_random(&random);
        const unsigned operation = (unsigned)(bits % 8u);
        const int value = (int)((bits >> 8) % 64u);
        tds_ordered_set next;

        if (operation == 0u) {
            REQUIRE_STATUS(tds_ordered_set_add(&current, &value, &next));
            model_insert(&model, model.count, value);
        } else if (operation == 1u) {
            REQUIRE_STATUS(tds_ordered_set_add_first(&current, &value, &next));
            model_insert(&model, 0u, value);
        } else if (operation == 2u) {
            const size_t index = model.count == 0u
                ? 0u
                : (size_t)(next_random(&random) % (uint32_t)(model.count + 1u));
            REQUIRE_STATUS(tds_ordered_set_insert(&current, index, &value, &next));
            model_insert(&model, index, value);
        } else if (operation == 3u && model.count != 0u) {
            const size_t source = (size_t)(next_random(&random) % (uint32_t)model.count);
            const size_t destination = (size_t)(next_random(&random) % (uint32_t)model.count);
            const int present = model.values[source];
            REQUIRE_STATUS(tds_ordered_set_move_to(&current, destination, &present, &next));
            model_move(&model, source, destination);
        } else if (operation == 4u) {
            REQUIRE_STATUS(tds_ordered_set_remove(&current, &value, &next));
            const size_t index = model_find(&model, value);
            if (index != SIZE_MAX) {
                model_remove_at(&model, index);
            }
        } else if (operation == 5u && model.count != 0u) {
            const size_t index = (size_t)(next_random(&random) % (uint32_t)model.count);
            REQUIRE_STATUS(tds_ordered_set_remove_at(&current, index, &next));
            model_remove_at(&model, index);
        } else if (operation == 6u) {
            REQUIRE_STATUS(tds_ordered_set_reverse(&current, &next));
            for (size_t left = 0u, right = model.count == 0u ? 0u : model.count - 1u;
                 left < right;
                 ++left, --right) {
                const int temporary = model.values[left];
                model.values[left] = model.values[right];
                model.values[right] = temporary;
            }
        } else {
            const size_t start = model.count == 0u
                ? 0u
                : (size_t)(next_random(&random) % (uint32_t)(model.count + 1u));
            const size_t remaining = model.count - start;
            const size_t count = remaining == 0u
                ? 0u
                : (size_t)(next_random(&random) % (uint32_t)(remaining + 1u));
            REQUIRE_STATUS(tds_ordered_set_get_range(&current, start, count, &next));
            (void)memmove(
                model.values,
                model.values + start,
                count * sizeof(model.values[0]));
            model.count = count;
        }

        replace_set(&current, &next);
        REQUIRE(set_matches(&current, model.values, model.count));
    }
    tds_ordered_set_destroy(&current);
}

static void run_test(const char* name, void (*test)(void))
{
    const int before = g_failures;
    test();
    if (g_failures == before) {
        (void)printf("[pass] %s\n", name);
        (void)fflush(stdout);
    } else {
        (void)fprintf(stderr, "[fail] %s\n", name);
        (void)fflush(stderr);
    }
}

int main(void)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    run_test("construction and representatives", test_construction_and_representatives);
    run_test("positional edits and removal", test_positional_edits_and_removal);
    run_test("ranges reverse and stable sort", test_ranges_reverse_and_stable_sort);
    run_test("algebra and relations", test_algebra_and_relations);
    run_test("relabel persistence and failure atomicity", test_relabel_persistence_and_failure_atomicity);
    run_test("generated independent model", test_generated_model);

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C Ordered tests passed\n");
    return EXIT_SUCCESS;
}
