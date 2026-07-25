#include <durable7/finger_tree/daba_lite.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdio.h>
#include <stdint.h>
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

static void int_identity(void* destination, void* context)
{
    (void)context;
    *(int*)destination = 0;
}

static void int_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(int*)destination = *(const int*)value;
}

static void int_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    *(int*)destination = *(const int*)left + *(const int*)right;
}

static void init_int_policy(ft_daba_policy* policy)
{
    ft_value_type value;
    ft_measure_policy monoid;
    ft_value_type_init(&value, sizeof(int));
    monoid.size = sizeof(int);
    monoid.identity = int_identity;
    monoid.measure = int_measure;
    monoid.combine = int_combine;
    monoid.context = NULL;
    ft_daba_policy_init(policy, &value, &monoid);
}

static void test_basic_window(void)
{
    ft_daba_policy policy;
    ft_daba_lite daba;
    ft_daba_lite_statistics statistics;
    int aggregate = -1;
    bool evicted = true;
    init_int_policy(&policy);
    REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
    REQUIRE(ft_daba_lite_empty(&daba));
    REQUIRE(ft_daba_lite_validate(&daba, &statistics));
    REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &aggregate), FT_STATUS_OK);
    REQUIRE(aggregate == 0);
    for (int value = 1; value <= 1000; ++value) {
        REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
    }
    REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &aggregate), FT_STATUS_OK);
    REQUIRE(aggregate == 500500);
    REQUIRE(ft_daba_lite_validate(&daba, &statistics));
    for (int value = 1; value <= 1000; ++value) {
        REQUIRE_STATUS(ft_daba_lite_try_evict(&daba, &evicted), FT_STATUS_OK);
        REQUIRE(evicted);
    }
    REQUIRE(ft_daba_lite_empty(&daba));
    REQUIRE(ft_daba_lite_validate(&daba, &statistics));
    REQUIRE_STATUS(ft_daba_lite_try_evict(&daba, &evicted), FT_STATUS_OK);
    REQUIRE(!evicted);
    REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_EMPTY);
    ft_daba_lite_destroy(&daba);
}

static void test_handle_move_lifecycle(void)
{
    ft_daba_policy policy;
    ft_daba_lite source;
    ft_daba_lite destination = { NULL, NULL };
    ft_daba_lite_statistics statistics;
    int aggregate = 0;
    const int first = 13;
    const int second = 29;
    init_int_policy(&policy);
    REQUIRE_STATUS(ft_daba_lite_create(&source, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_daba_lite_insert(&source, &first), FT_STATUS_OK);
    REQUIRE_STATUS(ft_daba_lite_insert(&source, &second), FT_STATUS_OK);

    ft_daba_lite_move(&destination, &source);
    REQUIRE(source.policy == NULL && source.rep == NULL);
    REQUIRE(ft_daba_lite_empty(&source));
    REQUIRE(ft_daba_lite_size(&source) == 0);
    REQUIRE(!ft_daba_lite_validate(&source, NULL));
    REQUIRE_STATUS(ft_daba_lite_aggregate(&source, &aggregate), FT_STATUS_INVALID_ARGUMENT);
    ft_daba_lite_destroy(&source);

    REQUIRE(ft_daba_lite_validate(&destination, &statistics));
    REQUIRE(statistics.count == 2);
    REQUIRE_STATUS(ft_daba_lite_aggregate(&destination, &aggregate), FT_STATUS_OK);
    REQUIRE(aggregate == first + second);
    REQUIRE_STATUS(ft_daba_lite_evict(&destination), FT_STATUS_OK);
    REQUIRE_STATUS(ft_daba_lite_aggregate(&destination, &aggregate), FT_STATUS_OK);
    REQUIRE(aggregate == second);
    ft_daba_lite_destroy(&destination);
    REQUIRE(destination.policy == NULL && destination.rep == NULL);
}

typedef struct matrix {
    int64_t m00;
    int64_t m01;
    int64_t m10;
    int64_t m11;
} matrix;

static matrix matrix_identity_value(void)
{
    const matrix result = { 1, 0, 0, 1 };
    return result;
}

static matrix matrix_create(int64_t seed)
{
    static const int64_t modulus = INT64_C(1000003);
    const int64_t positive = seed < 0 ? -seed : seed;
    const matrix result = {
        (positive * 17 + 3) % modulus,
        (positive * 29 + 5) % modulus,
        (positive * 43 + 7) % modulus,
        (positive * 61 + 11) % modulus
    };
    return result;
}

static matrix matrix_multiply(matrix left, matrix right)
{
    static const int64_t modulus = INT64_C(1000003);
    const matrix result = {
        (left.m00 * right.m00 + left.m01 * right.m10) % modulus,
        (left.m00 * right.m01 + left.m01 * right.m11) % modulus,
        (left.m10 * right.m00 + left.m11 * right.m10) % modulus,
        (left.m10 * right.m01 + left.m11 * right.m11) % modulus
    };
    return result;
}

static bool matrix_equal(matrix left, matrix right)
{
    return left.m00 == right.m00 && left.m01 == right.m01 &&
        left.m10 == right.m10 && left.m11 == right.m11;
}

static void matrix_identity(void* destination, void* context)
{
    (void)context;
    *(matrix*)destination = matrix_identity_value();
}

static void matrix_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(matrix*)destination = *(const matrix*)value;
}

static void matrix_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    *(matrix*)destination = matrix_multiply(*(const matrix*)left, *(const matrix*)right);
}

static void init_matrix_policy(ft_daba_policy* policy)
{
    ft_value_type value;
    ft_measure_policy monoid;
    ft_value_type_init(&value, sizeof(matrix));
    monoid.size = sizeof(matrix);
    monoid.identity = matrix_identity;
    monoid.measure = matrix_measure;
    monoid.combine = matrix_combine;
    monoid.context = NULL;
    ft_daba_policy_init(policy, &value, &monoid);
}

static matrix fold_matrices(const matrix* values, size_t count)
{
    matrix result = matrix_identity_value();
    for (size_t index = 0; index < count; ++index) {
        result = matrix_multiply(result, values[index]);
    }
    return result;
}

static bool matrix_state_matches(const ft_daba_lite* daba, const matrix* model, size_t count)
{
    matrix aggregate;
    ft_daba_lite_statistics statistics;
    return ft_daba_lite_size(daba) == count && ft_daba_lite_validate(daba, &statistics) &&
        statistics.count == count && ft_daba_lite_aggregate(daba, &aggregate) == FT_STATUS_OK &&
        matrix_equal(aggregate, fold_matrices(model, count));
}

static void test_noncommutative_exhaustive_histories(void)
{
    const matrix left = matrix_create(2);
    const matrix right = matrix_create(7);
    ft_daba_policy policy;
    init_matrix_policy(&policy);
    REQUIRE(!matrix_equal(matrix_multiply(left, right), matrix_multiply(right, left)));

    for (uint32_t mask = 0; mask < (UINT32_C(1) << 10); ++mask) {
        matrix model[10];
        size_t count = 0;
        ft_daba_lite daba;
        REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
        for (size_t step = 0; step < 10; ++step) {
            if ((mask & (UINT32_C(1) << step)) != 0) {
                const matrix value = matrix_create((int64_t)mask * 17 + (int64_t)step + 1);
                REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
                model[count++] = value;
            } else if (count == 0) {
                bool evicted = true;
                REQUIRE_STATUS(ft_daba_lite_try_evict(&daba, &evicted), FT_STATUS_OK);
                REQUIRE(!evicted);
            } else {
                REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
                (void)memmove(model, model + 1, (count - 1) * sizeof(matrix));
                --count;
            }
            REQUIRE(matrix_state_matches(&daba, model, count));
        }
        ft_daba_lite_destroy(&daba);
    }
}

static uint32_t next_random(uint32_t* state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void i64_identity(void* destination, void* context)
{
    (void)context;
    *(int64_t*)destination = 0;
}

static void i64_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(int64_t*)destination = *(const int64_t*)value;
}

static void i64_combine(void* destination, const void* left, const void* right, void* context)
{
    (void)context;
    *(int64_t*)destination = *(const int64_t*)left + *(const int64_t*)right;
}

static void init_i64_policy(ft_daba_policy* policy)
{
    ft_value_type value;
    ft_measure_policy monoid;
    ft_value_type_init(&value, sizeof(int64_t));
    monoid.size = sizeof(int64_t);
    monoid.identity = i64_identity;
    monoid.measure = i64_measure;
    monoid.combine = i64_combine;
    monoid.context = NULL;
    ft_daba_policy_init(policy, &value, &monoid);
}

static void test_randomized_variable_window(void)
{
    enum { operation_count = 100000 };
    int64_t* model = (int64_t*)malloc(operation_count * sizeof(int64_t));
    size_t head = 0;
    size_t tail = 0;
    int64_t expected = 0;
    uint32_t state = UINT32_C(20260715);
    ft_daba_policy policy;
    ft_daba_lite daba;
    REQUIRE(model != NULL);
    init_i64_policy(&policy);
    REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);

    for (size_t iteration = 0; iteration < operation_count; ++iteration) {
        if (head == tail || (next_random(&state) & 1U) == 0) {
            const int64_t value = (int64_t)(next_random(&state) % 20001U) - 10000;
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
            model[tail++] = value;
            expected += value;
        } else {
            REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
            expected -= model[head++];
        }
        {
            int64_t actual = 0;
            REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &actual), FT_STATUS_OK);
            REQUIRE(actual == expected);
            REQUIRE(ft_daba_lite_size(&daba) == tail - head);
        }
        if (iteration % 997 == 0) {
            int64_t naive = 0;
            ft_daba_lite_statistics statistics;
            for (size_t index = head; index < tail; ++index) {
                naive += model[index];
            }
            REQUIRE(naive == expected);
            REQUIRE(ft_daba_lite_validate(&daba, &statistics));
        }
    }
    ft_daba_lite_destroy(&daba);
    free(model);
}

static void test_chunk_boundaries_and_churn(void)
{
    static const size_t sizes[] = { 63, 64, 65, 127, 128, 129 };
    ft_daba_policy policy;
    init_matrix_policy(&policy);
    for (size_t case_index = 0; case_index < sizeof(sizes) / sizeof(sizes[0]); ++case_index) {
        const size_t size = sizes[case_index];
        matrix model[129];
        ft_daba_lite daba;
        ft_daba_lite_statistics statistics;
        REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
        for (size_t index = 0; index < size; ++index) {
            const matrix value = matrix_create((int64_t)index + 1);
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
            model[index] = value;
        }
        REQUIRE(ft_daba_lite_validate(&daba, &statistics));
        REQUIRE(statistics.block_count == size / 64 + 1);
        REQUIRE(statistics.slack_slot_count >= 1 && statistics.slack_slot_count <= 127);
        REQUIRE(matrix_state_matches(&daba, model, size));

        for (size_t index = 0; index < 512; ++index) {
            const matrix value = matrix_create((int64_t)(10000 + size * 1000 + index));
            REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
            (void)memmove(model, model + 1, (size - 1) * sizeof(matrix));
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
            model[size - 1] = value;
            REQUIRE(ft_daba_lite_validate(&daba, &statistics));
            REQUIRE(statistics.slack_slot_count >= 1 && statistics.slack_slot_count <= 127);
            REQUIRE(statistics.block_count >= 1 && statistics.block_count <= size / 64 + 2);
            if ((index & 15U) == 0) {
                REQUIRE(matrix_state_matches(&daba, model, size));
            }
        }
        while (!ft_daba_lite_empty(&daba)) {
            REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
        }
        REQUIRE(ft_daba_lite_validate(&daba, &statistics));
        REQUIRE(statistics.block_count == 1);
        REQUIRE(statistics.allocated_slot_capacity == 64);
        REQUIRE(statistics.slack_slot_count == 64);
        ft_daba_lite_destroy(&daba);
    }
}

enum { offset_identity_value = 11 };

typedef struct callback_counts {
    int identities;
    int combines;
} callback_counts;

static void offset_identity(void* destination, void* context)
{
    callback_counts* counts = (callback_counts*)context;
    ++counts->identities;
    *(int*)destination = offset_identity_value;
}

static void offset_measure(void* destination, const void* value, void* context)
{
    (void)context;
    *(int*)destination = *(const int*)value;
}

static void offset_combine(void* destination, const void* left, const void* right, void* context)
{
    callback_counts* counts = (callback_counts*)context;
    ++counts->combines;
    *(int*)destination = *(const int*)left + *(const int*)right - offset_identity_value;
}

static void init_offset_policy(ft_daba_policy* policy, callback_counts* counts)
{
    ft_value_type value;
    ft_measure_policy monoid;
    ft_value_type_init(&value, sizeof(int));
    monoid.size = sizeof(int);
    monoid.identity = offset_identity;
    monoid.measure = offset_measure;
    monoid.combine = offset_combine;
    monoid.context = counts;
    ft_daba_policy_init(policy, &value, &monoid);
}

typedef enum fixup_phase {
    FIXUP_SINGLETON = 0,
    FIXUP_FLIP_AND_SHRINK = 1,
    FIXUP_SHIFT = 2,
    FIXUP_SHRINK = 3,
    FIXUP_PHASE_COUNT = 4
} fixup_phase;

static fixup_phase classify_next_fixup(ft_daba_lite_statistics statistics, bool evicting)
{
    if ((!evicting && statistics.count == 0) || (evicting && statistics.front_length == 1)) {
        return FIXUP_SINGLETON;
    }
    if (statistics.left_length + statistics.right_length + statistics.accumulator_length == 0) {
        return FIXUP_FLIP_AND_SHRINK;
    }
    return statistics.left_length == 0 ? FIXUP_SHIFT : FIXUP_SHRINK;
}

static int fold_offset(const int* model, size_t count)
{
    int result = offset_identity_value;
    for (size_t index = 0; index < count; ++index) {
        result += model[index] - offset_identity_value;
    }
    return result;
}

static void reset_counts(callback_counts* counts)
{
    counts->identities = 0;
    counts->combines = 0;
}

static void test_fixup_phases_and_callback_ceilings(void)
{
    callback_counts counts = { 0, 0 };
    bool phases[FIXUP_PHASE_COUNT] = { false, false, false, false };
    int maximum_insert = 0;
    int maximum_evict = 0;
    int maximum_query = 0;
    int model[130];
    size_t model_count = 0;
    ft_daba_policy policy;
    ft_daba_lite daba;
    init_offset_policy(&policy, &counts);
    REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);

    for (int cycle = 0; cycle < 4; ++cycle) {
        for (int index = 0; index < 130; ++index) {
            ft_daba_lite_statistics statistics;
            int value = cycle * 1000 + index + 20;
            int aggregate = 0;
            reset_counts(&counts);
            REQUIRE(ft_daba_lite_validate(&daba, &statistics));
            REQUIRE(counts.identities == 0 && counts.combines == 0);
            phases[classify_next_fixup(statistics, false)] = true;
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
            model[model_count++] = value;
            REQUIRE(counts.combines >= 1 && counts.combines <= 3);
            if (counts.combines > maximum_insert) {
                maximum_insert = counts.combines;
            }
            reset_counts(&counts);
            REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &aggregate), FT_STATUS_OK);
            REQUIRE(aggregate == fold_offset(model, model_count));
            REQUIRE(counts.combines <= 1);
            if (counts.combines > maximum_query) {
                maximum_query = counts.combines;
            }
        }
        while (model_count != 0) {
            ft_daba_lite_statistics statistics;
            int aggregate = 0;
            reset_counts(&counts);
            REQUIRE(ft_daba_lite_validate(&daba, &statistics));
            REQUIRE(counts.identities == 0 && counts.combines == 0);
            phases[classify_next_fixup(statistics, true)] = true;
            REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
            (void)memmove(model, model + 1, (model_count - 1) * sizeof(int));
            --model_count;
            REQUIRE(counts.combines <= 2);
            if (counts.combines > maximum_evict) {
                maximum_evict = counts.combines;
            }
            reset_counts(&counts);
            REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &aggregate), FT_STATUS_OK);
            REQUIRE(aggregate == fold_offset(model, model_count));
            REQUIRE(counts.combines <= 1);
            if (counts.combines > maximum_query) {
                maximum_query = counts.combines;
            }
        }
    }
    for (size_t index = 0; index < FIXUP_PHASE_COUNT; ++index) {
        REQUIRE(phases[index]);
    }
    REQUIRE(maximum_insert == 3);
    REQUIRE(maximum_evict == 2);
    REQUIRE(maximum_query == 1);
    ft_daba_lite_destroy(&daba);
}

typedef struct failing_allocator {
    size_t attempts;
    size_t fail_after;
    size_t live;
} failing_allocator;

static void* failing_allocate(size_t size, void* context)
{
    failing_allocator* allocator = (failing_allocator*)context;
    if (allocator->attempts++ == allocator->fail_after) {
        return NULL;
    }
    {
        void* allocation = malloc(size == 0 ? 1 : size);
        if (allocation != NULL) {
            ++allocator->live;
        }
        return allocation;
    }
}

static void failing_deallocate(void* allocation, void* context)
{
    failing_allocator* allocator = (failing_allocator*)context;
    if (allocation != NULL) {
        --allocator->live;
        free(allocation);
    }
}

static void attach_failing_allocator(ft_daba_policy* policy, failing_allocator* allocator)
{
    policy->allocator.allocate = failing_allocate;
    policy->allocator.deallocate = failing_deallocate;
    policy->allocator.context = allocator;
}

static bool statistics_equal(ft_daba_lite_statistics left, ft_daba_lite_statistics right)
{
    return left.count == right.count && left.front_length == right.front_length &&
        left.back_length == right.back_length && left.left_length == right.left_length &&
        left.right_length == right.right_length && left.accumulator_length == right.accumulator_length &&
        left.block_count == right.block_count &&
        left.allocated_slot_capacity == right.allocated_slot_capacity &&
        left.slack_slot_count == right.slack_slot_count;
}

static void test_exhaustive_allocation_failure_rollback(void)
{
    for (size_t failure = 0; failure < 4; ++failure) {
        failing_allocator allocator = { 0, failure, 0 };
        ft_daba_policy policy;
        ft_daba_lite daba = { NULL, NULL };
        init_int_policy(&policy);
        attach_failing_allocator(&policy, &allocator);
        REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_NO_MEMORY);
        REQUIRE(daba.rep == NULL && daba.policy == NULL);
        REQUIRE(allocator.live == 0);
    }

    for (size_t failure = 0; failure < 2; ++failure) {
        failing_allocator allocator = { 0, SIZE_MAX, 0 };
        ft_daba_policy policy;
        ft_daba_lite daba;
        ft_daba_lite_statistics before;
        int before_aggregate = 0;
        int after_aggregate = 0;
        init_int_policy(&policy);
        attach_failing_allocator(&policy, &allocator);
        REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
        for (int value = 1; value <= 63; ++value) {
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
        }
        REQUIRE(ft_daba_lite_validate(&daba, &before));
        REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &before_aggregate), FT_STATUS_OK);
        {
            const size_t baseline_live = allocator.live;
            const int value = 64;
            allocator.attempts = 0;
            allocator.fail_after = failure;
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_NO_MEMORY);
            REQUIRE(allocator.live == baseline_live);
        }
        {
            ft_daba_lite_statistics after;
            REQUIRE(ft_daba_lite_validate(&daba, &after));
            REQUIRE(statistics_equal(before, after));
            REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &after_aggregate), FT_STATUS_OK);
            REQUIRE(after_aggregate == before_aggregate);
        }
        allocator.fail_after = SIZE_MAX;
        ft_daba_lite_destroy(&daba);
        REQUIRE(allocator.live == 0);
    }

    for (size_t failure = 0; failure < 2; ++failure) {
        failing_allocator allocator = { 0, SIZE_MAX, 0 };
        ft_daba_policy policy;
        ft_daba_lite daba;
        ft_daba_lite_statistics before;
        int before_aggregate = 0;
        int after_aggregate = 0;
        init_int_policy(&policy);
        attach_failing_allocator(&policy, &allocator);
        REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
        for (int value = 1; value <= 65; ++value) {
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
        }
        REQUIRE(ft_daba_lite_validate(&daba, &before));
        REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &before_aggregate), FT_STATUS_OK);
        {
            const size_t baseline_live = allocator.live;
            allocator.attempts = 0;
            allocator.fail_after = failure;
            REQUIRE_STATUS(ft_daba_lite_clear(&daba), FT_STATUS_NO_MEMORY);
            REQUIRE(allocator.live == baseline_live);
        }
        {
            ft_daba_lite_statistics after;
            REQUIRE(ft_daba_lite_validate(&daba, &after));
            REQUIRE(statistics_equal(before, after));
            REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &after_aggregate), FT_STATUS_OK);
            REQUIRE(after_aggregate == before_aggregate);
        }
        allocator.attempts = 0;
        allocator.fail_after = SIZE_MAX;
        REQUIRE_STATUS(ft_daba_lite_clear(&daba), FT_STATUS_OK);
        REQUIRE(ft_daba_lite_empty(&daba));
        REQUIRE(allocator.live == 4);
        ft_daba_lite_destroy(&daba);
        REQUIRE(allocator.live == 0);
    }
}

enum { reference_capacity = 512 };

typedef struct reference_tracker {
    int references[reference_capacity];
    int violations;
} reference_tracker;

typedef struct reference_value {
    int identifier;
} reference_value;

static void reference_copy(void* destination, const void* source, void* context)
{
    reference_tracker* tracker = (reference_tracker*)context;
    const reference_value value = *(const reference_value*)source;
    *(reference_value*)destination = value;
    if (value.identifier >= 0) {
        ++tracker->references[value.identifier];
    }
}

static void reference_destroy(void* value, void* context)
{
    reference_tracker* tracker = (reference_tracker*)context;
    const int identifier = ((reference_value*)value)->identifier;
    if (identifier >= 0) {
        --tracker->references[identifier];
        if (tracker->references[identifier] < 0) {
            ++tracker->violations;
        }
    }
}

static void reference_identity(void* destination, void* context)
{
    (void)context;
    ((reference_value*)destination)->identifier = -1;
}

static void reference_measure(void* destination, const void* value, void* context)
{
    reference_copy(destination, value, context);
}

static void reference_combine(void* destination, const void* left, const void* right, void* context)
{
    const reference_value* left_value = (const reference_value*)left;
    reference_copy(destination, left_value->identifier >= 0 ? left : right, context);
}

static void init_reference_policy(ft_daba_policy* policy, reference_tracker* tracker)
{
    ft_value_type value;
    ft_measure_policy monoid;
    ft_value_type_init(&value, sizeof(reference_value));
    value.copy = reference_copy;
    value.destroy = reference_destroy;
    value.context = tracker;
    monoid.size = sizeof(reference_value);
    monoid.identity = reference_identity;
    monoid.measure = reference_measure;
    monoid.combine = reference_combine;
    monoid.context = tracker;
    ft_daba_policy_init(policy, &value, &monoid);
}

static bool all_references_released(const reference_tracker* tracker)
{
    for (size_t index = 0; index < reference_capacity; ++index) {
        if (tracker->references[index] != 0) {
            return false;
        }
    }
    return tracker->violations == 0;
}

static void test_prompt_reclamation_and_clear_reuse(void)
{
    reference_tracker tracker;
    ft_daba_policy policy;
    ft_daba_lite daba;
    ft_daba_lite_statistics statistics;
    (void)memset(&tracker, 0, sizeof(tracker));
    init_reference_policy(&policy, &tracker);
    REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
    {
        const reference_value first = { 1 };
        const reference_value second = { 2 };
        REQUIRE_STATUS(ft_daba_lite_insert(&daba, &first), FT_STATUS_OK);
        REQUIRE_STATUS(ft_daba_lite_insert(&daba, &second), FT_STATUS_OK);
        REQUIRE(tracker.references[1] > 0);
        REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
        REQUIRE(tracker.references[1] == 0);
    }
    REQUIRE_STATUS(ft_daba_lite_clear(&daba), FT_STATUS_OK);
    {
        const reference_value victim = { 3 };
        REQUIRE_STATUS(ft_daba_lite_insert(&daba, &victim), FT_STATUS_OK);
        for (int index = 0; index < 128; ++index) {
            const reference_value value = { 10 + index };
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
        }
        REQUIRE(tracker.references[3] > 0);
        for (int index = 0; index < 64; ++index) {
            REQUIRE_STATUS(ft_daba_lite_evict(&daba), FT_STATUS_OK);
        }
        REQUIRE(tracker.references[3] == 0);
        REQUIRE(ft_daba_lite_validate(&daba, &statistics));
        REQUIRE(statistics.block_count == 2);
    }
    REQUIRE_STATUS(ft_daba_lite_clear(&daba), FT_STATUS_OK);
    REQUIRE(ft_daba_lite_validate(&daba, &statistics));
    REQUIRE(statistics.count == 0 && statistics.block_count == 1);
    REQUIRE(all_references_released(&tracker));
    {
        const reference_value reused = { 300 };
        REQUIRE_STATUS(ft_daba_lite_insert(&daba, &reused), FT_STATUS_OK);
        REQUIRE(tracker.references[300] > 0);
    }
    ft_daba_lite_destroy(&daba);
    REQUIRE(all_references_released(&tracker));
}

typedef struct aligned_value {
    long double value;
    void* marker;
} aligned_value;

typedef struct alignment_context {
    bool misaligned;
} alignment_context;

static void check_alignment(const void* value, alignment_context* context)
{
    if ((uintptr_t)value % _Alignof(aligned_value) != 0) {
        context->misaligned = true;
    }
}

static void aligned_copy(void* destination, const void* source, void* context)
{
    alignment_context* alignment = (alignment_context*)context;
    check_alignment(destination, alignment);
    check_alignment(source, alignment);
    *(aligned_value*)destination = *(const aligned_value*)source;
}

static void aligned_identity(void* destination, void* context)
{
    alignment_context* alignment = (alignment_context*)context;
    check_alignment(destination, alignment);
    ((aligned_value*)destination)->value = 0;
    ((aligned_value*)destination)->marker = NULL;
}

static void aligned_measure(void* destination, const void* value, void* context)
{
    aligned_copy(destination, value, context);
}

static void aligned_combine(void* destination, const void* left, const void* right, void* context)
{
    alignment_context* alignment = (alignment_context*)context;
    check_alignment(destination, alignment);
    check_alignment(left, alignment);
    check_alignment(right, alignment);
    ((aligned_value*)destination)->value =
        ((const aligned_value*)left)->value + ((const aligned_value*)right)->value;
    ((aligned_value*)destination)->marker = NULL;
}

static void test_alignment_and_clear_callback_bound(void)
{
    alignment_context alignment = { false };
    callback_counts counts = { 0, 0 };
    ft_value_type aligned_type;
    ft_measure_policy aligned_monoid;
    ft_daba_policy aligned_policy;
    ft_daba_lite aligned_daba;
    ft_value_type_init(&aligned_type, sizeof(aligned_value));
    aligned_type.copy = aligned_copy;
    aligned_type.context = &alignment;
    aligned_monoid.size = sizeof(aligned_value);
    aligned_monoid.identity = aligned_identity;
    aligned_monoid.measure = aligned_measure;
    aligned_monoid.combine = aligned_combine;
    aligned_monoid.context = &alignment;
    ft_daba_policy_init(&aligned_policy, &aligned_type, &aligned_monoid);
    REQUIRE_STATUS(ft_daba_lite_create(&aligned_daba, &aligned_policy), FT_STATUS_OK);
    for (int index = 0; index < 257; ++index) {
        const aligned_value value = { (long double)index, NULL };
        REQUIRE_STATUS(ft_daba_lite_insert(&aligned_daba, &value), FT_STATUS_OK);
    }
    {
        aligned_value aggregate;
        REQUIRE_STATUS(ft_daba_lite_aggregate(&aligned_daba, &aggregate), FT_STATUS_OK);
    }
    REQUIRE(!alignment.misaligned);
    ft_daba_lite_destroy(&aligned_daba);

    {
        ft_daba_policy policy;
        ft_daba_lite daba;
        ft_daba_lite_statistics statistics;
        init_offset_policy(&policy, &counts);
        REQUIRE_STATUS(ft_daba_lite_create(&daba, &policy), FT_STATUS_OK);
        for (int index = 0; index < 257; ++index) {
            const int value = index + 20;
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &value), FT_STATUS_OK);
        }
        reset_counts(&counts);
        REQUIRE_STATUS(ft_daba_lite_clear(&daba), FT_STATUS_OK);
        REQUIRE(counts.identities == 1);
        REQUIRE(counts.combines == 0);
        REQUIRE(ft_daba_lite_validate(&daba, &statistics));
        REQUIRE(statistics.count == 0 && statistics.block_count == 1 &&
            statistics.allocated_slot_capacity == 64 && statistics.slack_slot_count == 64);
        reset_counts(&counts);
        REQUIRE_STATUS(ft_daba_lite_clear(&daba), FT_STATUS_OK);
        REQUIRE(counts.identities == 0 && counts.combines == 0);
        {
            const int first = 20;
            const int second = 30;
            int aggregate = 0;
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &first), FT_STATUS_OK);
            REQUIRE_STATUS(ft_daba_lite_insert(&daba, &second), FT_STATUS_OK);
            REQUIRE_STATUS(ft_daba_lite_aggregate(&daba, &aggregate), FT_STATUS_OK);
            REQUIRE(aggregate == 39);
        }
        ft_daba_lite_destroy(&daba);
    }
}

static void run_test(const char* name, void (*test)(void))
{
    const int before = g_failures;
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
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    run_test("DABA Lite basic FIFO window", test_basic_window);
    run_test("DABA Lite handle move lifecycle", test_handle_move_lifecycle);
    run_test("DABA Lite exhaustive noncommutative histories", test_noncommutative_exhaustive_histories);
    run_test("DABA Lite 100k randomized variable window", test_randomized_variable_window);
    run_test("DABA Lite chunk boundaries and churn", test_chunk_boundaries_and_churn);
    run_test("DABA Lite fixup phases and callback ceilings", test_fixup_phases_and_callback_ceilings);
    run_test("DABA Lite exhaustive allocation rollback", test_exhaustive_allocation_failure_rollback);
    run_test("DABA Lite prompt reclamation and clear reuse", test_prompt_reclamation_and_clear_reuse);
    run_test("DABA Lite alignment and clear callback bound", test_alignment_and_clear_callback_bound);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C DABA Lite tests passed\n");
    return EXIT_SUCCESS;
}
