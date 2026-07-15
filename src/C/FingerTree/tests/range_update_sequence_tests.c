#include <tools/data_structures/finger_tree/range_update_sequence.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#if !defined(__STDC_NO_THREADS__)
#define RANGE_TEST_HAS_THREADS 1
#include <threads.h>
#endif
#endif

static int g_failures = 0;
static const unsigned char g_element_identity = 0;
static const unsigned char g_measure_identity = 0;
static const unsigned char g_tag_identity = 0;
static const unsigned char g_count_element_identity = 0;
static const unsigned char g_count_measure_identity = 0;
static const unsigned char g_count_tag_identity = 0;

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

#define REQUIRE_STATUS(expression, expected) \
    do { \
        const ft_status actual_status__ = (expression); \
        if (actual_status__ != (expected)) { \
            (void)fprintf(stderr, "%s:%d: %s returned %d, expected %d\n", \
                __FILE__, __LINE__, #expression, (int)actual_status__, (int)(expected)); \
            ++g_failures; \
            return; \
        } \
    } while (0)

typedef struct test_element {
    int64_t value;
    const void* nullable;
} test_element;

typedef struct test_measure {
    size_t count;
    int64_t sum;
    int64_t position_weighted_sum;
} test_measure;

typedef struct test_tag {
    bool alternate_identity;
    bool has_assignment;
    int64_t assignment;
    int64_t addition;
} test_tag;

typedef enum callback_kind {
    CALLBACK_NONE = 0,
    CALLBACK_MEASURE_ELEMENT = 1,
    CALLBACK_COMBINE = 2,
    CALLBACK_IS_IDENTITY = 3,
    CALLBACK_COMPOSE = 4,
    CALLBACK_APPLY_ELEMENT = 5,
    CALLBACK_APPLY_MEASURE = 6,
    CALLBACK_MEASURE_EQUALS = 7,
    CALLBACK_KIND_COUNT = 8
} callback_kind;

typedef struct test_context {
    size_t allocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t callback_calls[CALLBACK_KIND_COUNT];
    callback_kind fail_kind;
    size_t fail_callback_at;
} test_context;

static bool callback_should_fail(test_context* context, callback_kind kind)
{
    if (context == NULL) {
        return false;
    }
    ++context->callback_calls[kind];
    return context->fail_kind == kind && context->fail_callback_at != 0 &&
        context->callback_calls[kind] == context->fail_callback_at;
}

static void reset_callbacks(test_context* context)
{
    (void)memset(context->callback_calls, 0, sizeof(context->callback_calls));
    context->fail_kind = CALLBACK_NONE;
    context->fail_callback_at = 0;
}

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* result = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 &&
        state->allocation_calls == state->fail_allocation_at) {
        return NULL;
    }
    result = malloc(size);
    if (result != NULL) {
        ++state->outstanding_allocations;
    }
    return result;
}

static void tracked_deallocate(void* allocation, void* context)
{
    test_context* state = (test_context*)context;
    if (allocation != NULL) {
        --state->outstanding_allocations;
        free(allocation);
    }
}

static ft_status measure_element(
    void* destination,
    const void* element,
    void* context)
{
    const test_element* value = (const test_element*)element;
    test_measure* result = (test_measure*)destination;
    if (callback_should_fail((test_context*)context, CALLBACK_MEASURE_ELEMENT)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    result->count = 1;
    result->sum = value->value;
    result->position_weighted_sum = 0;
    return FT_STATUS_OK;
}

static ft_status combine_measure(
    void* destination,
    const void* left,
    const void* right,
    void* context)
{
    const test_measure* first = (const test_measure*)left;
    const test_measure* second = (const test_measure*)right;
    test_measure* result = (test_measure*)destination;
    if (callback_should_fail((test_context*)context, CALLBACK_COMBINE)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (second->count > SIZE_MAX - first->count) {
        return FT_STATUS_OVERFLOW;
    }
    result->count = first->count + second->count;
    result->sum = first->sum + second->sum;
    result->position_weighted_sum = first->position_weighted_sum +
        second->position_weighted_sum +
        (int64_t)first->count * second->sum;
    return FT_STATUS_OK;
}

static ft_status measure_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    const test_measure* first = (const test_measure*)left;
    const test_measure* second = (const test_measure*)right;
    if (callback_should_fail((test_context*)context, CALLBACK_MEASURE_EQUALS)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *equal = first->count == second->count && first->sum == second->sum &&
        first->position_weighted_sum == second->position_weighted_sum;
    return FT_STATUS_OK;
}

static ft_status is_identity_tag(
    const void* tag,
    bool* is_identity,
    void* context)
{
    const test_tag* value = (const test_tag*)tag;
    if (callback_should_fail((test_context*)context, CALLBACK_IS_IDENTITY)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *is_identity = !value->has_assignment && value->addition == 0;
    return FT_STATUS_OK;
}

static ft_status compose_tag(
    void* destination,
    const void* newer,
    const void* older,
    void* context)
{
    const test_tag* next = (const test_tag*)newer;
    const test_tag* previous = (const test_tag*)older;
    test_tag* result = (test_tag*)destination;
    if (callback_should_fail((test_context*)context, CALLBACK_COMPOSE)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (!next->has_assignment && next->addition == 0) {
        *result = *previous;
    } else if (!previous->has_assignment && previous->addition == 0) {
        *result = *next;
    } else if (next->has_assignment) {
        *result = *next;
    } else if (previous->has_assignment) {
        result->alternate_identity = false;
        result->has_assignment = true;
        result->assignment = previous->assignment + previous->addition + next->addition;
        result->addition = 0;
    } else {
        result->alternate_identity = false;
        result->has_assignment = false;
        result->assignment = 0;
        result->addition = previous->addition + next->addition;
    }
    return FT_STATUS_OK;
}

static ft_status apply_element_tag(
    void* destination,
    const void* tag,
    const void* element,
    void* context)
{
    const test_tag* action = (const test_tag*)tag;
    const test_element* source = (const test_element*)element;
    test_element* result = (test_element*)destination;
    if (callback_should_fail((test_context*)context, CALLBACK_APPLY_ELEMENT)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    result->value = (action->has_assignment ? action->assignment : source->value) +
        action->addition;
    result->nullable = source->nullable;
    return FT_STATUS_OK;
}

static ft_status apply_measure_tag(
    void* destination,
    const void* tag,
    const void* measure,
    size_t count,
    void* context)
{
    const test_tag* action = (const test_tag*)tag;
    const test_measure* source = (const test_measure*)measure;
    test_measure* result = (test_measure*)destination;
    int64_t triangle = 0;
    if (callback_should_fail((test_context*)context, CALLBACK_APPLY_MEASURE)) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (count == 0) {
        (void)memset(result, 0, sizeof(*result));
        return FT_STATUS_OK;
    }
    triangle = (int64_t)(count * (count - 1) / 2);
    result->count = source->count;
    if (action->has_assignment) {
        int64_t value = action->assignment + action->addition;
        result->sum = value * (int64_t)count;
        result->position_weighted_sum = value * triangle;
    } else {
        result->sum = source->sum + action->addition * (int64_t)count;
        result->position_weighted_sum = source->position_weighted_sum +
            action->addition * triangle;
    }
    return FT_STATUS_OK;
}

static test_tag add_tag(int64_t amount)
{
    test_tag result = {false, false, 0, amount};
    return result;
}

static test_tag assign_tag(int64_t value)
{
    test_tag result = {false, true, value, 0};
    return result;
}

static ft_range_update_policy_config make_config(
    test_context* context,
    bool tracked_allocator)
{
    static const test_measure empty = {0, 0, 0};
    static const test_tag identity = {false, false, 0, 0};
    ft_range_update_policy_config config;
    ft_range_update_policy_config_init(
        &config,
        sizeof(test_element),
        &g_element_identity,
        sizeof(test_measure),
        &g_measure_identity,
        sizeof(test_tag),
        &g_tag_identity,
        &empty,
        &identity,
        measure_element,
        combine_measure,
        measure_equals,
        is_identity_tag,
        compose_tag,
        apply_element_tag,
        apply_measure_tag);
    config.algebra_context = context;
    if (tracked_allocator) {
        config.allocator.allocate = tracked_allocate;
        config.allocator.deallocate = tracked_deallocate;
        config.allocator.context = context;
    }
    return config;
}

static test_measure fold_model(const test_element* values, size_t count)
{
    test_measure result = {0, 0, 0};
    size_t index = 0;
    for (index = 0; index < count; ++index) {
        result.position_weighted_sum += (int64_t)index * values[index].value;
        result.sum += values[index].value;
        ++result.count;
    }
    return result;
}

typedef struct visit_context {
    test_element* values;
    size_t capacity;
    size_t count;
} visit_context;

static ft_status collect_element(const void* element, void* context)
{
    visit_context* state = (visit_context*)context;
    if (state->count >= state->capacity) {
        return FT_STATUS_OVERFLOW;
    }
    state->values[state->count++] = *(const test_element*)element;
    return FT_STATUS_OK;
}

static bool sequence_matches(
    const ft_range_update_sequence* sequence,
    const test_element* expected,
    size_t count)
{
    test_element actual[512];
    visit_context visitor = {actual, sizeof(actual) / sizeof(actual[0]), 0};
    test_measure measure = {0};
    test_measure expected_measure = fold_model(expected, count);
    ft_range_update_sequence_statistics statistics = {0};
    bool valid = false;
    bool measures_equal = false;
    size_t index = 0;
    if (count > visitor.capacity ||
        ft_range_update_sequence_size(sequence) != count ||
        ft_range_update_sequence_visit(sequence, collect_element, &visitor) != FT_STATUS_OK ||
        visitor.count != count ||
        ft_range_update_sequence_measure(sequence, &measure) != FT_STATUS_OK ||
        ft_range_update_sequence_validate(sequence, &valid, &statistics) != FT_STATUS_OK ||
        !valid || statistics.count != count ||
        statistics.maximum_absolute_balance_factor > 1 ||
        measure_equals(&measure, &expected_measure, &measures_equal, NULL) != FT_STATUS_OK ||
        !measures_equal) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        test_element indexed = {0};
        if (actual[index].value != expected[index].value ||
            actual[index].nullable != expected[index].nullable ||
            ft_range_update_sequence_at(sequence, index, &indexed) != FT_STATUS_OK ||
            indexed.value != expected[index].value ||
            indexed.nullable != expected[index].nullable) {
            return false;
        }
    }
    return true;
}

static void fill_values(test_element* values, size_t count, int64_t first)
{
    size_t index = 0;
    for (index = 0; index < count; ++index) {
        values[index].value = first + (int64_t)index;
        values[index].nullable = index % 3 == 0 ? NULL : &g_element_identity;
    }
}

static void make_pointers(
    const test_element* values,
    const void** pointers,
    size_t count)
{
    size_t index = 0;
    for (index = 0; index < count; ++index) {
        pointers[index] = &values[index];
    }
}

static bool tags_equivalent(const test_tag* left, const test_tag* right)
{
    int64_t value = 0;
    for (value = -7; value <= 7; ++value) {
        test_element source = {value, NULL};
        test_element first = {0};
        test_element second = {0};
        if (apply_element_tag(&first, left, &source, NULL) != FT_STATUS_OK ||
            apply_element_tag(&second, right, &source, NULL) != FT_STATUS_OK ||
            first.value != second.value) {
            return false;
        }
    }
    return true;
}

static void test_algebra_laws_and_policy_lifecycle(void)
{
    test_context context = {0};
    ft_range_update_policy_config config = make_config(&context, true);
    ft_range_update_policy policy = {0};
    ft_range_update_policy copy = {0};
    test_tag identity = {false, false, 0, 0};
    test_tag alternate = {true, false, 0, 0};
    test_tag tags[] = {
        {false, false, 0, 0},
        {true, false, 0, 0},
        {false, false, 0, 5},
        {false, false, 0, -3},
        {false, true, 7, 0},
        {false, true, 4, 9}};
    size_t oldest = 0;
    size_t middle = 0;
    size_t newest = 0;
    bool identity_result = false;

    REQUIRE_STATUS(ft_range_update_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE(context.outstanding_allocations != 0);
    REQUIRE_STATUS(ft_range_update_policy_copy(&policy, &copy), FT_STATUS_OK);
    REQUIRE(ft_range_update_policy_same(&policy, &copy));
    REQUIRE_STATUS(is_identity_tag(&alternate, &identity_result, NULL), FT_STATUS_OK);
    REQUIRE(identity_result);
    REQUIRE(memcmp(&identity, &alternate, sizeof(identity)) != 0);

    for (oldest = 0; oldest < sizeof(tags) / sizeof(tags[0]); ++oldest) {
        test_tag left_identity;
        test_tag right_identity;
        REQUIRE_STATUS(compose_tag(&left_identity, &identity, &tags[oldest], NULL), FT_STATUS_OK);
        REQUIRE_STATUS(compose_tag(&right_identity, &tags[oldest], &alternate, NULL), FT_STATUS_OK);
        REQUIRE(tags_equivalent(&tags[oldest], &left_identity));
        REQUIRE(tags_equivalent(&tags[oldest], &right_identity));
        for (middle = 0; middle < sizeof(tags) / sizeof(tags[0]); ++middle) {
            for (newest = 0; newest < sizeof(tags) / sizeof(tags[0]); ++newest) {
                test_tag middle_oldest;
                test_tag left;
                test_tag newest_middle;
                test_tag right;
                REQUIRE_STATUS(compose_tag(
                    &middle_oldest, &tags[middle], &tags[oldest], NULL), FT_STATUS_OK);
                REQUIRE_STATUS(compose_tag(
                    &left, &tags[newest], &middle_oldest, NULL), FT_STATUS_OK);
                REQUIRE_STATUS(compose_tag(
                    &newest_middle, &tags[newest], &tags[middle], NULL), FT_STATUS_OK);
                REQUIRE_STATUS(compose_tag(
                    &right, &newest_middle, &tags[oldest], NULL), FT_STATUS_OK);
                REQUIRE(tags_equivalent(&left, &right));
            }
        }
    }
    {
        test_tag assign_after_add;
        test_tag add_after_assign;
        test_element source = {3, NULL};
        test_element first = {0};
        test_element second = {0};
        test_tag add = add_tag(10);
        test_tag assign = assign_tag(7);
        REQUIRE_STATUS(compose_tag(&assign_after_add, &assign, &add, NULL), FT_STATUS_OK);
        REQUIRE_STATUS(compose_tag(&add_after_assign, &add, &assign, NULL), FT_STATUS_OK);
        REQUIRE_STATUS(apply_element_tag(&first, &assign_after_add, &source, NULL), FT_STATUS_OK);
        REQUIRE_STATUS(apply_element_tag(&second, &add_after_assign, &source, NULL), FT_STATUS_OK);
        REQUIRE(first.value == 7);
        REQUIRE(second.value == 17);
    }

    ft_range_update_policy_dispose(&copy);
    ft_range_update_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_surface_boundaries_sharing_and_nullable_payloads(void)
{
    test_context context = {0};
    ft_range_update_policy_config config = make_config(&context, true);
    ft_range_update_policy policy = {0};
    ft_range_update_sequence original = {0};
    ft_range_update_sequence added = {0};
    ft_range_update_sequence assigned = {0};
    test_element values[8];
    const void* pointers[8];
    test_element expected[16];
    test_tag add = add_tag(10);
    test_tag assign = assign_tag(7);
    test_tag alternate = {true, false, 0, 0};
    size_t boundary = 0;
    size_t shared = 0;
    size_t physical = 0;
    const void* original_root = NULL;

    fill_values(values, 8, 1);
    make_pointers(values, pointers, 8);
    (void)memcpy(expected, values, sizeof(values));
    REQUIRE_STATUS(ft_range_update_policy_create(&policy, &config), FT_STATUS_OK);
    reset_callbacks(&context);
    REQUIRE_STATUS(ft_range_update_sequence_from_array(
        &original, &policy, pointers, 8), FT_STATUS_OK);
    REQUIRE(sequence_matches(&original, values, 8));
    original_root = ft_range_update_sequence_root_identity(&original);

    REQUIRE_STATUS(ft_range_update_sequence_apply_range(
        &original, 2, 4, &add, &added), FT_STATUS_OK);
    for (boundary = 2; boundary < 6; ++boundary) {
        expected[boundary].value += 10;
    }
    REQUIRE(sequence_matches(&added, expected, 8));
    REQUIRE_STATUS(ft_range_update_sequence_apply_range(
        &added, 3, 3, &assign, &assigned), FT_STATUS_OK);
    for (boundary = 3; boundary < 6; ++boundary) {
        expected[boundary].value = 7;
    }
    REQUIRE(sequence_matches(&assigned, expected, 8));
    REQUIRE(sequence_matches(&original, values, 8));
    REQUIRE(expected[0].nullable == NULL);
    REQUIRE(expected[1].nullable != NULL);

    {
        test_measure middle = {0};
        test_measure expected_middle = fold_model(expected + 2, 4);
        REQUIRE_STATUS(ft_range_update_sequence_measure_range(
            &assigned, 2, 4, &middle), FT_STATUS_OK);
        REQUIRE(middle.count == expected_middle.count);
        REQUIRE(middle.sum == expected_middle.sum);
        REQUIRE(middle.position_weighted_sum == expected_middle.position_weighted_sum);
    }
    for (boundary = 0; boundary <= 8; ++boundary) {
        ft_range_update_split_result split = {0};
        ft_range_update_sequence joined = {0};
        REQUIRE_STATUS(ft_range_update_sequence_split_at(
            &original, boundary, &split), FT_STATUS_OK);
        REQUIRE_STATUS(ft_range_update_sequence_concat(
            &split.left, &split.right, &joined), FT_STATUS_OK);
        REQUIRE(sequence_matches(&joined, values, 8));
        ft_range_update_sequence_dispose(&joined);
        ft_range_update_sequence_dispose(&split.right);
        ft_range_update_sequence_dispose(&split.left);
    }
    {
        ft_range_update_sequence range = {0};
        REQUIRE_STATUS(ft_range_update_sequence_get_range(
            &assigned, 2, 4, &range), FT_STATUS_OK);
        REQUIRE(sequence_matches(&range, expected + 2, 4));
        ft_range_update_sequence_dispose(&range);
    }
    {
        ft_range_update_sequence identity_result_sequence = {0};
        ft_range_update_sequence empty_result = {0};
        size_t calls_before = 0;
        REQUIRE_STATUS(ft_range_update_sequence_apply_range(
            &original, 1, 4, &alternate, &identity_result_sequence), FT_STATUS_OK);
        REQUIRE(ft_range_update_sequence_root_identity(&identity_result_sequence) == original_root);
        reset_callbacks(&context);
        calls_before = context.callback_calls[CALLBACK_IS_IDENTITY];
        REQUIRE_STATUS(ft_range_update_sequence_apply_range(
            &original, 3, 0, &add, &empty_result), FT_STATUS_OK);
        REQUIRE(context.callback_calls[CALLBACK_IS_IDENTITY] == calls_before);
        REQUIRE(ft_range_update_sequence_root_identity(&empty_result) == original_root);
        ft_range_update_sequence_dispose(&empty_result);
        ft_range_update_sequence_dispose(&identity_result_sequence);
    }
    {
        ft_range_update_sequence full = {0};
        REQUIRE_STATUS(ft_range_update_sequence_apply_range(
            &original, 0, 8, &add, &full), FT_STATUS_OK);
        REQUIRE_STATUS(ft_range_update_sequence_physical_node_count(
            &original, &physical), FT_STATUS_OK);
        REQUIRE(physical == 8);
        REQUIRE_STATUS(ft_range_update_sequence_shared_node_count(
            &original, &full, &shared), FT_STATUS_OK);
        REQUIRE(shared == 7);
        REQUIRE(ft_range_update_sequence_root_identity(&original) !=
            ft_range_update_sequence_root_identity(&full));
        ft_range_update_sequence_dispose(&full);
    }
    reset_callbacks(&context);
    REQUIRE_STATUS(ft_range_update_sequence_apply_range(
        &original, 9, 0, &alternate, &original), FT_STATUS_OUT_OF_RANGE);
    REQUIRE(context.callback_calls[CALLBACK_IS_IDENTITY] == 0);
    REQUIRE(sequence_matches(&original, values, 8));

    ft_range_update_sequence_dispose(&assigned);
    ft_range_update_sequence_dispose(&added);
    ft_range_update_sequence_dispose(&original);
    ft_range_update_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static uint64_t next_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static size_t random_index(uint64_t* state, size_t exclusive_maximum)
{
    return exclusive_maximum == 0
        ? 0
        : (size_t)(next_random(state) % exclusive_maximum);
}

static void test_retained_thousand_step_model(void)
{
    test_context context = {0};
    ft_range_update_policy_config config = make_config(&context, true);
    ft_range_update_policy policy = {0};
    ft_range_update_sequence sequence = {0};
    ft_range_update_sequence snapshots[16] = {{0}};
    test_element snapshot_models[16][64];
    size_t snapshot_counts[16] = {0};
    test_element model[64];
    const void* pointers[64];
    size_t count = 16;
    size_t snapshot_count = 0;
    size_t step = 0;
    uint64_t random = UINT64_C(0x52414e4745555044);

    fill_values(model, count, 0);
    make_pointers(model, pointers, count);
    REQUIRE_STATUS(ft_range_update_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_from_array(
        &sequence, &policy, pointers, count), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_copy(
        &sequence, &snapshots[snapshot_count]), FT_STATUS_OK);
    (void)memcpy(snapshot_models[snapshot_count], model, count * sizeof(model[0]));
    snapshot_counts[snapshot_count++] = count;

    for (step = 0; step < 1000; ++step) {
        size_t boundary = random_index(&random, count + 1);
        size_t position = count == 0 ? 0 : random_index(&random, count);
        size_t start = random_index(&random, count + 1);
        size_t range_count = random_index(&random, count - start + 1);
        ft_range_update_sequence next = {0};
        switch (step % 8) {
        case 0:
            if (count < 64) {
                test_element value = {(int64_t)(next_random(&random) % 2001) - 1000, NULL};
                REQUIRE_STATUS(ft_range_update_sequence_insert_at(
                    &sequence, boundary, &value, &next), FT_STATUS_OK);
                (void)memmove(
                    model + boundary + 1,
                    model + boundary,
                    (count - boundary) * sizeof(model[0]));
                model[boundary] = value;
                ++count;
            }
            break;
        case 1:
            if (count != 0) {
                REQUIRE_STATUS(ft_range_update_sequence_remove_at(
                    &sequence, position, &next), FT_STATUS_OK);
                (void)memmove(
                    model + position,
                    model + position + 1,
                    (count - position - 1) * sizeof(model[0]));
                --count;
            }
            break;
        case 2:
            if (count != 0) {
                test_element value = {(int64_t)(10000 + step), &g_tag_identity};
                REQUIRE_STATUS(ft_range_update_sequence_set_at(
                    &sequence, position, &value, &next), FT_STATUS_OK);
                model[position] = value;
            }
            break;
        case 3:
        case 4:
            {
                test_tag tag = step % 8 == 3
                    ? add_tag((int64_t)(next_random(&random) % 21) - 10)
                    : assign_tag((int64_t)(next_random(&random) % 31) - 15);
                size_t index = 0;
                REQUIRE_STATUS(ft_range_update_sequence_apply_range(
                    &sequence, start, range_count, &tag, &next), FT_STATUS_OK);
                for (index = start; index < start + range_count; ++index) {
                    model[index].value = tag.has_assignment
                        ? tag.assignment + tag.addition
                        : model[index].value + tag.addition;
                }
            }
            break;
        case 5:
            {
                test_measure actual = {0};
                test_measure expected = fold_model(model + start, range_count);
                REQUIRE_STATUS(ft_range_update_sequence_measure_range(
                    &sequence, start, range_count, &actual), FT_STATUS_OK);
                REQUIRE(actual.count == expected.count && actual.sum == expected.sum &&
                    actual.position_weighted_sum == expected.position_weighted_sum);
            }
            break;
        case 6:
            {
                ft_range_update_split_result split = {0};
                REQUIRE_STATUS(ft_range_update_sequence_split_at(
                    &sequence, boundary, &split), FT_STATUS_OK);
                REQUIRE_STATUS(ft_range_update_sequence_concat(
                    &split.left, &split.right, &next), FT_STATUS_OK);
                ft_range_update_sequence_dispose(&split.right);
                ft_range_update_sequence_dispose(&split.left);
            }
            break;
        default:
            {
                ft_range_update_sequence range = {0};
                REQUIRE_STATUS(ft_range_update_sequence_get_range(
                    &sequence, start, range_count, &range), FT_STATUS_OK);
                REQUIRE(sequence_matches(&range, model + start, range_count));
                ft_range_update_sequence_dispose(&range);
            }
            break;
        }
        if (next.policy != NULL) {
            ft_range_update_sequence_dispose(&sequence);
            ft_range_update_sequence_move(&sequence, &next);
        }
        REQUIRE(sequence_matches(&sequence, model, count));
        if (step % 73 == 0 && snapshot_count < 16) {
            REQUIRE_STATUS(ft_range_update_sequence_copy(
                &sequence, &snapshots[snapshot_count]), FT_STATUS_OK);
            (void)memcpy(snapshot_models[snapshot_count], model, count * sizeof(model[0]));
            snapshot_counts[snapshot_count++] = count;
        }
        if (step % 97 == 0) {
            size_t selected = random_index(&random, snapshot_count);
            size_t branch_start = random_index(&random, snapshot_counts[selected] + 1);
            size_t branch_count = random_index(
                &random, snapshot_counts[selected] - branch_start + 1);
            test_element branch_model[64];
            ft_range_update_sequence branch = {0};
            test_tag tag = add_tag(3);
            size_t index = 0;
            (void)memcpy(
                branch_model,
                snapshot_models[selected],
                snapshot_counts[selected] * sizeof(branch_model[0]));
            REQUIRE_STATUS(ft_range_update_sequence_apply_range(
                &snapshots[selected], branch_start, branch_count, &tag, &branch), FT_STATUS_OK);
            for (index = branch_start; index < branch_start + branch_count; ++index) {
                branch_model[index].value += 3;
            }
            REQUIRE(sequence_matches(&branch, branch_model, snapshot_counts[selected]));
            REQUIRE(sequence_matches(
                &snapshots[selected], snapshot_models[selected], snapshot_counts[selected]));
            ft_range_update_sequence_dispose(&branch);
        }
    }
    for (step = 0; step < snapshot_count; ++step) {
        REQUIRE(sequence_matches(
            &snapshots[step], snapshot_models[step], snapshot_counts[step]));
        ft_range_update_sequence_dispose(&snapshots[step]);
    }
    ft_range_update_sequence_dispose(&sequence);
    ft_range_update_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_callback_and_allocator_failure_atomicity(void)
{
    test_context context = {0};
    ft_range_update_policy_config config = make_config(&context, true);
    ft_range_update_policy policy = {0};
    ft_range_update_sequence base = {0};
    ft_range_update_sequence middle_tagged = {0};
    ft_range_update_sequence source = {0};
    test_element values[32];
    test_element expected[32];
    const void* pointers[32];
    test_tag first = add_tag(5);
    test_tag second = add_tag(7);
    test_tag operation_tag = add_tag(3);
    callback_kind kind = CALLBACK_NONE;
    size_t index = 0;
    size_t baseline = 0;

    fill_values(values, 32, 1);
    (void)memcpy(expected, values, sizeof(values));
    make_pointers(values, pointers, 32);
    REQUIRE_STATUS(ft_range_update_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_from_array(
        &base, &policy, pointers, 32), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_apply_range(
        &base, 8, 16, &first, &middle_tagged), FT_STATUS_OK);
    for (index = 8; index < 24; ++index) {
        expected[index].value += 5;
    }
    REQUIRE_STATUS(ft_range_update_sequence_apply_range(
        &middle_tagged, 0, 32, &second, &source), FT_STATUS_OK);
    for (index = 0; index < 32; ++index) {
        expected[index].value += 7;
    }
    REQUIRE(sequence_matches(&source, expected, 32));
    baseline = context.outstanding_allocations;

    for (kind = CALLBACK_MEASURE_ELEMENT; kind <= CALLBACK_APPLY_MEASURE;
         kind = (callback_kind)(kind + 1)) {
        ft_range_update_sequence success = {0};
        size_t observed = 0;
        size_t ordinal = 0;
        reset_callbacks(&context);
        REQUIRE_STATUS(ft_range_update_sequence_apply_range(
            &source, 7, 18, &operation_tag, &success), FT_STATUS_OK);
        observed = context.callback_calls[kind];
        REQUIRE(observed != 0);
        ft_range_update_sequence_dispose(&success);
        REQUIRE(context.outstanding_allocations == baseline);
        for (ordinal = 1; ordinal <= observed; ++ordinal) {
            ft_range_update_sequence failed = {0};
            reset_callbacks(&context);
            context.fail_kind = kind;
            context.fail_callback_at = ordinal;
            REQUIRE_STATUS(ft_range_update_sequence_apply_range(
                &source, 7, 18, &operation_tag, &failed), FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(failed.policy == NULL && failed.root == NULL);
            context.fail_kind = CALLBACK_NONE;
            context.fail_callback_at = 0;
            REQUIRE(context.outstanding_allocations == baseline);
            REQUIRE(sequence_matches(&source, expected, 32));
        }
    }

    {
        bool valid = true;
        ft_range_update_sequence_statistics statistics;
        ft_range_update_sequence_statistics expected_statistics;
        (void)memset(&statistics, 0x5a, sizeof(statistics));
        expected_statistics = statistics;
        reset_callbacks(&context);
        context.fail_kind = CALLBACK_MEASURE_EQUALS;
        context.fail_callback_at = 1;
        REQUIRE_STATUS(ft_range_update_sequence_validate(
            &source, &valid, &statistics), FT_STATUS_CALLBACK_FAILURE);
        REQUIRE(valid);
        REQUIRE(memcmp(
            &statistics, &expected_statistics, sizeof(statistics)) == 0);
        context.fail_kind = CALLBACK_NONE;
        context.fail_callback_at = 0;
        REQUIRE(context.outstanding_allocations == baseline);
        REQUIRE(sequence_matches(&source, expected, 32));
    }

    {
        ft_range_update_sequence success = {0};
        size_t allocations = 0;
        size_t ordinal = 0;
        context.allocation_calls = 0;
        context.fail_allocation_at = 0;
        REQUIRE_STATUS(ft_range_update_sequence_apply_range(
            &source, 7, 18, &operation_tag, &success), FT_STATUS_OK);
        allocations = context.allocation_calls;
        REQUIRE(allocations != 0);
        ft_range_update_sequence_dispose(&success);
        REQUIRE(context.outstanding_allocations == baseline);
        for (ordinal = 1; ordinal <= allocations; ++ordinal) {
            ft_range_update_sequence failed = {0};
            context.allocation_calls = 0;
            context.fail_allocation_at = ordinal;
            REQUIRE_STATUS(ft_range_update_sequence_apply_range(
                &source, 7, 18, &operation_tag, &failed), FT_STATUS_NO_MEMORY);
            REQUIRE(failed.policy == NULL && failed.root == NULL);
            context.fail_allocation_at = 0;
            REQUIRE(context.outstanding_allocations == baseline);
            REQUIRE(sequence_matches(&source, expected, 32));
        }
    }

    reset_callbacks(&context);
    {
        ft_range_update_sequence retry = {0};
        REQUIRE_STATUS(ft_range_update_sequence_apply_range(
            &source, 7, 18, &operation_tag, &retry), FT_STATUS_OK);
        ft_range_update_sequence_dispose(&retry);
    }
    ft_range_update_sequence_dispose(&source);
    ft_range_update_sequence_dispose(&middle_tagged);
    ft_range_update_sequence_dispose(&base);
    ft_range_update_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
}

static ft_status count_measure_element(
    void* destination,
    const void* element,
    void* context)
{
    (void)element;
    (void)context;
    *(size_t*)destination = 1;
    return FT_STATUS_OK;
}

static ft_status count_combine(
    void* destination,
    const void* left,
    const void* right,
    void* context)
{
    size_t first = *(const size_t*)left;
    size_t second = *(const size_t*)right;
    (void)context;
    if (second > SIZE_MAX - first) {
        return FT_STATUS_OVERFLOW;
    }
    *(size_t*)destination = first + second;
    return FT_STATUS_OK;
}

static ft_status count_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    (void)context;
    *equal = *(const size_t*)left == *(const size_t*)right;
    return FT_STATUS_OK;
}

static ft_status count_is_identity(
    const void* tag,
    bool* identity,
    void* context)
{
    (void)context;
    *identity = *(const unsigned char*)tag == 0;
    return FT_STATUS_OK;
}

static ft_status count_compose(
    void* destination,
    const void* newer,
    const void* older,
    void* context)
{
    (void)context;
    *(unsigned char*)destination = (unsigned char)(
        *(const unsigned char*)newer | *(const unsigned char*)older);
    return FT_STATUS_OK;
}

static ft_status count_apply_element(
    void* destination,
    const void* tag,
    const void* element,
    void* context)
{
    (void)tag;
    (void)context;
    *(int*)destination = *(const int*)element;
    return FT_STATUS_OK;
}

static ft_status count_apply_measure(
    void* destination,
    const void* tag,
    const void* measure,
    size_t count,
    void* context)
{
    (void)tag;
    (void)count;
    (void)context;
    *(size_t*)destination = *(const size_t*)measure;
    return FT_STATUS_OK;
}

static ft_range_update_policy_config make_count_config(void)
{
    static const size_t empty = 0;
    static const unsigned char identity = 0;
    ft_range_update_policy_config config;
    ft_range_update_policy_config_init(
        &config,
        sizeof(int),
        &g_count_element_identity,
        sizeof(size_t),
        &g_count_measure_identity,
        sizeof(unsigned char),
        &g_count_tag_identity,
        &empty,
        &identity,
        count_measure_element,
        count_combine,
        count_equals,
        count_is_identity,
        count_compose,
        count_apply_element,
        count_apply_measure);
    return config;
}

static void test_maximum_count_shared_dag_and_overflow(void)
{
    ft_range_update_policy_config config = make_count_config();
    ft_range_update_policy policy = {0};
    ft_range_update_sequence power = {0};
    ft_range_update_sequence almost = {0};
    ft_range_update_sequence full = {0};
    ft_range_update_sequence failed = {0};
    ft_range_update_sequence_statistics statistics = {0};
    const int value = 7;
    const int append_value = 8;
    const void* values[] = {&value};
    size_t measure = 0;
    size_t physical = 0;
    bool valid = false;
    REQUIRE_STATUS(ft_range_update_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_from_array(
        &power, &policy, values, 1), FT_STATUS_OK);
    while (ft_range_update_sequence_size(&power) <= SIZE_MAX / 2) {
        ft_range_update_sequence next = {0};
        REQUIRE_STATUS(ft_range_update_sequence_concat(
            &power, &power, &next), FT_STATUS_OK);
        ft_range_update_sequence_dispose(&power);
        ft_range_update_sequence_move(&power, &next);
    }
    REQUIRE_STATUS(ft_range_update_sequence_remove_at(
        &power, 0, &almost), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_concat(
        &power, &almost, &full), FT_STATUS_OK);
    REQUIRE(ft_range_update_sequence_size(&full) == SIZE_MAX);
    REQUIRE_STATUS(ft_range_update_sequence_measure(&full, &measure), FT_STATUS_OK);
    REQUIRE(measure == SIZE_MAX);
    REQUIRE_STATUS(ft_range_update_sequence_physical_node_count(
        &full, &physical), FT_STATUS_OK);
    REQUIRE(physical < 16384);
    REQUIRE_STATUS(ft_range_update_sequence_validate(
        &full, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.count == SIZE_MAX);
    REQUIRE(statistics.logical_node_count == SIZE_MAX);
    REQUIRE(statistics.physical_node_count == physical);
    REQUIRE_STATUS(ft_range_update_sequence_append(
        &full, &append_value, &failed), FT_STATUS_OVERFLOW);
    REQUIRE(failed.policy == NULL && failed.root == NULL);
    REQUIRE_STATUS(ft_range_update_sequence_concat(
        &full, &power, &failed), FT_STATUS_OVERFLOW);
    REQUIRE(failed.policy == NULL && failed.root == NULL);
    ft_range_update_sequence_dispose(&full);
    ft_range_update_sequence_dispose(&almost);
    ft_range_update_sequence_dispose(&power);
    ft_range_update_policy_dispose(&policy);
}

typedef struct concurrent_context {
    const ft_range_update_sequence* original;
    const ft_range_update_sequence* changed;
    bool success;
} concurrent_context;

static void concurrent_worker(concurrent_context* context)
{
    size_t iteration = 0;
    context->success = true;
    for (iteration = 0; iteration < 64; ++iteration) {
        test_element first = {0};
        test_element middle = {0};
        test_measure original_measure = {0};
        test_measure changed_measure = {0};
        if (ft_range_update_sequence_at(context->original, 0, &first) != FT_STATUS_OK ||
            ft_range_update_sequence_at(context->changed, 128, &middle) != FT_STATUS_OK ||
            ft_range_update_sequence_measure(context->original, &original_measure) != FT_STATUS_OK ||
            ft_range_update_sequence_measure_range(
                context->changed, 64, 128, &changed_measure) != FT_STATUS_OK ||
            first.value != 0 || middle.value != 1128 ||
            original_measure.count != 256 || changed_measure.count != 128) {
            context->success = false;
            return;
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_thread(void* argument)
{
    concurrent_worker((concurrent_context*)argument);
    return 0;
}
#elif defined(RANGE_TEST_HAS_THREADS)
static int concurrent_thread(void* argument)
{
    concurrent_worker((concurrent_context*)argument);
    return 0;
}
#endif

static void test_concurrent_retained_snapshot_readers(void)
{
    enum { value_count = 256, thread_count = 4 };
    ft_range_update_policy_config config = make_config(NULL, false);
    ft_range_update_policy policy = {0};
    ft_range_update_sequence original = {0};
    ft_range_update_sequence changed = {0};
    test_element values[value_count];
    const void* pointers[value_count];
    test_tag tag = add_tag(1000);
    concurrent_context contexts[thread_count];
    int index = 0;
    fill_values(values, value_count, 0);
    make_pointers(values, pointers, value_count);
    REQUIRE_STATUS(ft_range_update_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_from_array(
        &original, &policy, pointers, value_count), FT_STATUS_OK);
    REQUIRE_STATUS(ft_range_update_sequence_apply_range(
        &original, 64, 128, &tag, &changed), FT_STATUS_OK);
    for (index = 0; index < thread_count; ++index) {
        contexts[index].original = &original;
        contexts[index].changed = &changed;
        contexts[index].success = false;
    }
#ifdef _WIN32
    {
        HANDLE threads[thread_count];
        DWORD thread_index = 0;
        for (thread_index = 0; thread_index < thread_count; ++thread_index) {
            threads[thread_index] = CreateThread(
                NULL, 0, concurrent_thread, &contexts[thread_index], 0, NULL);
            REQUIRE(threads[thread_index] != NULL);
        }
        REQUIRE(WaitForMultipleObjects(
            thread_count, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
        for (index = 0; index < thread_count; ++index) {
            REQUIRE(CloseHandle(threads[index]) != 0);
        }
    }
#elif defined(RANGE_TEST_HAS_THREADS)
    {
        thrd_t threads[thread_count];
        for (index = 0; index < thread_count; ++index) {
            REQUIRE(thrd_create(
                &threads[index], concurrent_thread, &contexts[index]) == thrd_success);
        }
        for (index = 0; index < thread_count; ++index) {
            int thread_result = 0;
            REQUIRE(thrd_join(threads[index], &thread_result) == thrd_success);
            REQUIRE(thread_result == 0);
        }
    }
#else
    for (index = 0; index < thread_count; ++index) {
        concurrent_worker(&contexts[index]);
    }
#endif
    for (index = 0; index < thread_count; ++index) {
        REQUIRE(contexts[index].success);
    }
    REQUIRE(sequence_matches(&original, values, value_count));
    ft_range_update_sequence_dispose(&changed);
    ft_range_update_sequence_dispose(&original);
    ft_range_update_policy_dispose(&policy);
}

typedef void (*test_fn)(void);

static void run_test(const char* name, test_fn test)
{
    int before = g_failures;
    test();
    if (before == g_failures) {
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
    run_test("Range-update algebra laws and policy lifecycle", test_algebra_laws_and_policy_lifecycle);
    run_test("Range-update surface boundaries sharing and nullable payloads", test_surface_boundaries_sharing_and_nullable_payloads);
    run_test("Range-update retained thousand-step model", test_retained_thousand_step_model);
    run_test("Range-update callback and allocator failure atomicity", test_callback_and_allocator_failure_atomicity);
    run_test("Range-update maximum-count shared DAG and overflow", test_maximum_count_shared_dag_and_overflow);
    run_test("Range-update concurrent retained snapshot readers", test_concurrent_retained_snapshot_readers);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C range-update sequence tests passed\n");
    return EXIT_SUCCESS;
}
