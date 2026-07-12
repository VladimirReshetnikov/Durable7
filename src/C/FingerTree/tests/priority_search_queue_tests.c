#include <tools/data_structures/finger_tree/priority_search_queue.h>
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
#define PSQ_TEST_HAS_THREADS 1
#include <threads.h>
#endif
#endif

static int g_failures = 0;
static const unsigned char g_key_type_identity = 0;
static const unsigned char g_priority_type_identity = 0;
static const unsigned char g_value_type_identity = 0;
static const unsigned char g_other_key_type_identity = 0;

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

typedef struct test_key {
    int order;
    int representative;
} test_key;

typedef struct test_priority {
    int rank;
    int equality_class;
} test_priority;

typedef struct test_value {
    int payload;
    const void* nullable;
} test_value;

typedef struct test_context {
    size_t allocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t key_copy_calls;
    size_t priority_copy_calls;
    size_t value_copy_calls;
    size_t successful_key_copies;
    size_t successful_priority_copies;
    size_t successful_value_copies;
    size_t key_destroy_calls;
    size_t priority_destroy_calls;
    size_t value_destroy_calls;
    size_t fail_key_copy_at;
    size_t fail_priority_copy_at;
    size_t fail_value_copy_at;
    size_t key_compare_calls;
    size_t priority_compare_calls;
    size_t priority_equals_calls;
    size_t value_equals_calls;
    size_t key_equals_calls;
    size_t fail_key_compare_at;
    size_t fail_priority_compare_at;
    size_t fail_priority_equals_at;
    size_t fail_value_equals_at;
    int key_direction;
    int priority_bucket_divisor;
} test_context;

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

static ft_status key_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->key_copy_calls;
    if (state->fail_key_copy_at != 0 && state->key_copy_calls == state->fail_key_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_key*)destination = *(const test_key*)source;
    ++state->successful_key_copies;
    return FT_STATUS_OK;
}

static ft_status priority_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->priority_copy_calls;
    if (state->fail_priority_copy_at != 0 &&
        state->priority_copy_calls == state->fail_priority_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_priority*)destination = *(const test_priority*)source;
    ++state->successful_priority_copies;
    return FT_STATUS_OK;
}

static ft_status value_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->value_copy_calls;
    if (state->fail_value_copy_at != 0 && state->value_copy_calls == state->fail_value_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_value*)destination = *(const test_value*)source;
    ++state->successful_value_copies;
    return FT_STATUS_OK;
}

static void key_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->key_destroy_calls;
}

static void priority_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->priority_destroy_calls;
}

static void value_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->value_destroy_calls;
}

static ft_status key_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    test_context* state = (test_context*)context;
    int first = ((const test_key*)left)->order;
    int second = ((const test_key*)right)->order;
    ++state->key_compare_calls;
    if (state->fail_key_compare_at != 0 &&
        state->key_compare_calls == state->fail_key_compare_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *comparison = ((first > second) - (first < second)) * state->key_direction;
    return FT_STATUS_OK;
}

static ft_status priority_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    test_context* state = (test_context*)context;
    int first = ((const test_priority*)left)->rank;
    int second = ((const test_priority*)right)->rank;
    ++state->priority_compare_calls;
    if (state->fail_priority_compare_at != 0 &&
        state->priority_compare_calls == state->fail_priority_compare_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (state->priority_bucket_divisor > 1) {
        first /= state->priority_bucket_divisor;
        second /= state->priority_bucket_divisor;
    }
    *comparison = (first > second) - (first < second);
    return FT_STATUS_OK;
}

static ft_status priority_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    test_context* state = (test_context*)context;
    ++state->priority_equals_calls;
    if (state->fail_priority_equals_at != 0 &&
        state->priority_equals_calls == state->fail_priority_equals_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *equal = ((const test_priority*)left)->equality_class ==
        ((const test_priority*)right)->equality_class;
    return FT_STATUS_OK;
}

static ft_status value_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    test_context* state = (test_context*)context;
    const test_value* first = (const test_value*)left;
    const test_value* second = (const test_value*)right;
    ++state->value_equals_calls;
    if (state->fail_value_equals_at != 0 &&
        state->value_equals_calls == state->fail_value_equals_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *equal = first->payload == second->payload && first->nullable == second->nullable;
    return FT_STATUS_OK;
}

static ft_status forbidden_key_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    test_context* state = (test_context*)context;
    (void)left;
    (void)right;
    (void)equal;
    ++state->key_equals_calls;
    return FT_STATUS_CALLBACK_FAILURE;
}

static void init_config(ft_psq_policy_config* config, test_context* context)
{
    if (context->key_direction == 0) {
        context->key_direction = 1;
    }
    ft_psq_policy_config_init(
        config,
        sizeof(test_key),
        &g_key_type_identity,
        key_compare,
        sizeof(test_priority),
        &g_priority_type_identity,
        priority_compare,
        priority_equals,
        sizeof(test_value),
        &g_value_type_identity,
        value_equals);
    config->key.copy = key_copy;
    config->key.destroy = key_destroy;
    config->key.equals = forbidden_key_equals;
    config->key.context = context;
    config->priority.copy = priority_copy;
    config->priority.destroy = priority_destroy;
    config->priority.context = context;
    config->value.copy = value_copy;
    config->value.destroy = value_destroy;
    config->value.context = context;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static test_key make_key(int order, int representative)
{
    test_key key = {order, representative};
    return key;
}

static test_priority make_priority(int rank, int equality_class)
{
    test_priority priority = {rank, equality_class};
    return priority;
}

static test_value make_value(int payload, const void* nullable)
{
    test_value value = {payload, nullable};
    return value;
}

static bool queue_valid(const ft_priority_search_queue* queue)
{
    bool valid = false;
    ft_priority_search_queue_statistics statistics;
    return ft_priority_search_queue_validate(queue, &valid, &statistics) == FT_STATUS_OK &&
        valid && statistics.count == ft_priority_search_queue_size(queue) &&
        statistics.height == ft_priority_search_queue_height(queue) &&
        statistics.maximum_absolute_balance_factor <= 1;
}

static bool entry_values(
    const ft_priority_search_entry* entry,
    test_key* key,
    test_priority* priority,
    test_value* value)
{
    ft_priority_search_entry_ref entry_ref;
    if (ft_priority_search_entry_get_ref(entry, &entry_ref) != FT_STATUS_OK) {
        return false;
    }
    *key = *(const test_key*)entry_ref.key;
    *priority = *(const test_priority*)entry_ref.priority;
    *value = *(const test_value*)entry_ref.value;
    return true;
}

static void test_semantics_representatives_noops_and_minimum(void)
{
    test_context context;
    ft_psq_policy_config config;
    ft_psq_policy policy;
    ft_psq_policy distinct_policy;
    ft_priority_search_queue queue;
    ft_priority_search_queue next;
    test_key original_key = make_key(7, 111);
    test_key equivalent_key = make_key(7, 222);
    test_priority original_priority = make_priority(10, 1);
    test_priority comparer_equal = make_priority(10, 2);
    test_priority equality_equal_order_distinct = make_priority(1, 2);
    test_value original_value = make_value(5, NULL);
    test_value changed_value = make_value(6, NULL);
    bool found = false;
    ft_priority_search_entry_ref entry_ref;
    const void* root = NULL;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_psq_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_psq_policy_create(&distinct_policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_priority_search_queue_init(&queue, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_priority_search_queue_set(
            &queue, &original_key, &original_priority, &original_value, &queue),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_priority_search_queue_set(
            &queue, &equivalent_key, &comparer_equal, &original_value, &next),
        FT_STATUS_OK);
    REQUIRE(ft_priority_search_queue_root_identity(&next) !=
        ft_priority_search_queue_root_identity(&queue));
    REQUIRE_STATUS(
        ft_priority_search_queue_try_get_entry_ref(
            &next, &equivalent_key, &found, &entry_ref),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(((const test_key*)entry_ref.key)->representative == 111);
    REQUIRE(((const test_priority*)entry_ref.priority)->equality_class == 2);
    ft_priority_search_queue_dispose(&queue);
    ft_priority_search_queue_move(&queue, &next);

    REQUIRE_STATUS(
        ft_priority_search_queue_set(
            &queue,
            &equivalent_key,
            &equality_equal_order_distinct,
            &original_value,
            &queue),
        FT_STATUS_OK);
    root = ft_priority_search_queue_root_identity(&queue);
    REQUIRE_STATUS(
        ft_priority_search_queue_set(
            &queue,
            &equivalent_key,
            &equality_equal_order_distinct,
            &original_value,
            &next),
        FT_STATUS_OK);
    REQUIRE(ft_priority_search_queue_root_identity(&next) == root);
    ft_priority_search_queue_dispose(&next);
    REQUIRE_STATUS(
        ft_priority_search_queue_set(
            &queue,
            &equivalent_key,
            &equality_equal_order_distinct,
            &changed_value,
            &next),
        FT_STATUS_OK);
    REQUIRE(ft_priority_search_queue_root_identity(&next) != root);
    ft_priority_search_queue_dispose(&queue);
    ft_priority_search_queue_move(&queue, &next);
    {
        bool added = true;
        REQUIRE_STATUS(
            ft_priority_search_queue_try_add(
                &queue,
                &equivalent_key,
                &original_priority,
                &original_value,
                &added,
                &next),
            FT_STATUS_OK);
        REQUIRE(!added);
        REQUIRE(ft_priority_search_queue_root_identity(&next) ==
            ft_priority_search_queue_root_identity(&queue));
        ft_priority_search_queue_dispose(&next);
    }
    REQUIRE(context.key_equals_calls == 0);
    REQUIRE(queue_valid(&queue));

    {
        ft_priority_search_entry removed_entry = {0};
        ft_priority_search_entry copy = {0};
        ft_priority_search_queue empty;
        test_key removed_key;
        test_priority removed_priority;
        test_value removed_value;
        bool removed = false;
        REQUIRE_STATUS(
            ft_priority_search_queue_try_remove(
                &queue, &equivalent_key, &removed, &removed_entry, &queue),
            FT_STATUS_OK);
        REQUIRE(removed && ft_priority_search_queue_empty(&queue));
        REQUIRE(entry_values(
            &removed_entry, &removed_key, &removed_priority, &removed_value));
        REQUIRE(removed_key.representative == 111);
        REQUIRE(removed_priority.rank == 1);
        REQUIRE(removed_value.payload == 6 && removed_value.nullable == NULL);
        REQUIRE_STATUS(ft_priority_search_entry_copy(&removed_entry, &copy), FT_STATUS_OK);
        ft_priority_search_entry_dispose(&removed_entry);
        REQUIRE(entry_values(&copy, &removed_key, &removed_priority, &removed_value));
        ft_priority_search_entry_dispose(&copy);
        REQUIRE_STATUS(ft_priority_search_queue_copy(&queue, &empty), FT_STATUS_OK);
        removed = true;
        REQUIRE_STATUS(
            ft_priority_search_queue_try_delete_minimum(
                &empty, &removed, &removed_entry, &empty),
            FT_STATUS_OK);
        REQUIRE(!removed && ft_priority_search_queue_empty(&empty));
        REQUIRE_STATUS(
            ft_priority_search_entry_get_ref(&removed_entry, &entry_ref),
            FT_STATUS_NOT_FOUND);
        ft_priority_search_entry_dispose(&removed_entry);
        REQUIRE_STATUS(
            ft_priority_search_queue_delete_minimum(&empty, &removed_entry, &next),
            FT_STATUS_EMPTY);
        ft_priority_search_queue_dispose(&empty);
    }

    {
        test_context descending_context;
        ft_psq_policy_config descending_config;
        ft_psq_policy descending_policy;
        ft_priority_search_queue ties;
        (void)memset(&descending_context, 0, sizeof(descending_context));
        descending_context.key_direction = -1;
        descending_context.priority_bucket_divisor = 10;
        init_config(&descending_config, &descending_context);
        REQUIRE_STATUS(
            ft_psq_policy_create(&descending_policy, &descending_config),
            FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_priority_search_queue_init(&ties, &descending_policy),
            FT_STATUS_OK);
        for (int key_value = 0; key_value != 10; ++key_value) {
            test_key key = make_key(key_value, key_value);
            test_priority priority = make_priority(10 + key_value, 100 + key_value);
            test_value value = make_value(key_value, NULL);
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &ties, &key, &priority, &value, &ties),
                FT_STATUS_OK);
        }
        for (int expected = 9; expected >= 0; --expected) {
            ft_priority_search_entry minimum_entry = {0};
            test_key key;
            test_priority priority;
            test_value value;
            REQUIRE_STATUS(
                ft_priority_search_queue_delete_minimum(
                    &ties, &minimum_entry, &ties),
                FT_STATUS_OK);
            REQUIRE(entry_values(&minimum_entry, &key, &priority, &value));
            REQUIRE(key.order == expected && value.payload == expected);
            ft_priority_search_entry_dispose(&minimum_entry);
            REQUIRE(queue_valid(&ties));
        }
        ft_priority_search_queue_dispose(&ties);
        ft_psq_policy_dispose(&descending_policy);
        REQUIRE(descending_context.key_equals_calls == 0);
        REQUIRE(descending_context.successful_key_copies == descending_context.key_destroy_calls);
        REQUIRE(descending_context.successful_priority_copies == descending_context.priority_destroy_calls);
        REQUIRE(descending_context.successful_value_copies == descending_context.value_destroy_calls);
        REQUIRE(descending_context.outstanding_allocations == 0);
    }

    {
        size_t shared = 99;
        ft_priority_search_queue other;
        REQUIRE_STATUS(ft_priority_search_queue_init(&other, &distinct_policy), FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_priority_search_queue_shared_node_count(&queue, &other, &shared),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(shared == 99);
        ft_priority_search_queue_dispose(&other);
    }
    ft_priority_search_queue_dispose(&queue);
    ft_psq_policy_dispose(&distinct_policy);
    ft_psq_policy_dispose(&policy);
    REQUIRE(context.key_equals_calls == 0);
    REQUIRE(context.successful_key_copies == context.key_destroy_calls);
    REQUIRE(context.successful_priority_copies == context.priority_destroy_calls);
    REQUIRE(context.successful_value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef struct ordered_collector {
    int* keys;
    int* priorities;
    size_t capacity;
    size_t count;
} ordered_collector;

static ft_status collect_ordered(ft_priority_search_entry_ref entry, void* context)
{
    ordered_collector* collector = (ordered_collector*)context;
    if (collector->count == collector->capacity) {
        return FT_STATUS_OVERFLOW;
    }
    collector->keys[collector->count] = ((const test_key*)entry.key)->order;
    if (collector->priorities != NULL) {
        collector->priorities[collector->count] =
            ((const test_priority*)entry.priority)->rank;
    }
    ++collector->count;
    return FT_STATUS_OK;
}

static uint32_t reverse_bits(uint32_t value)
{
    value = ((value & UINT32_C(0x55555555)) << 1) |
        ((value >> 1) & UINT32_C(0x55555555));
    value = ((value & UINT32_C(0x33333333)) << 2) |
        ((value >> 2) & UINT32_C(0x33333333));
    value = ((value & UINT32_C(0x0f0f0f0f)) << 4) |
        ((value >> 4) & UINT32_C(0x0f0f0f0f));
    value = ((value & UINT32_C(0x00ff00ff)) << 8) |
        ((value >> 8) & UINT32_C(0x00ff00ff));
    return (value << 16) | (value >> 16);
}

static void test_rotations_deletions_stack_safety_and_sharing(void)
{
    enum { ascending_count = 50000, rotation_count = 2048 };
    test_context context;
    ft_psq_policy_config config;
    ft_psq_policy policy;
    ft_priority_search_queue ascending;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_psq_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_priority_search_queue_init(&ascending, &policy), FT_STATUS_OK);
    for (int index = 0; index != ascending_count; ++index) {
        test_key key = make_key(index, index);
        test_priority priority = make_priority(index % 101, index % 101);
        test_value value = make_value(-index, NULL);
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &ascending, &key, &priority, &value, &ascending),
            FT_STATUS_OK);
    }
    REQUIRE(ft_priority_search_queue_size(&ascending) == ascending_count);
    REQUIRE(ft_priority_search_queue_height(&ascending) <= 64);
    REQUIRE(queue_valid(&ascending));
    {
        test_key missing = make_key(-1, -1);
        ft_priority_search_queue unchanged;
        const void* root = ft_priority_search_queue_root_identity(&ascending);
        REQUIRE_STATUS(
            ft_priority_search_queue_remove(&ascending, &missing, &unchanged),
            FT_STATUS_OK);
        REQUIRE(ft_priority_search_queue_root_identity(&unchanged) == root);
        ft_priority_search_queue_dispose(&unchanged);
    }
    ft_priority_search_queue_dispose(&ascending);

    {
        ft_priority_search_queue queue;
        int* keys = (int*)malloc(rotation_count * sizeof(*keys));
        int* priorities = (int*)malloc(rotation_count * sizeof(*priorities));
        ordered_collector collector = {keys, priorities, rotation_count, 0};
        REQUIRE(keys != NULL && priorities != NULL);
        REQUIRE_STATUS(ft_priority_search_queue_init(&queue, &policy), FT_STATUS_OK);
        for (int position = 0; position != rotation_count; ++position) {
            const int key_value = (int)(reverse_bits((uint32_t)position) >> 21);
            test_key key = make_key(key_value, key_value);
            test_priority priority = make_priority((key_value * 37) % 101, key_value);
            test_value value = make_value(-key_value, NULL);
            REQUIRE_STATUS(
                ft_priority_search_queue_set(&queue, &key, &priority, &value, &queue),
                FT_STATUS_OK);
            if ((position & 127) == 0) {
                REQUIRE(queue_valid(&queue));
            }
        }
        REQUIRE(ft_priority_search_queue_size(&queue) == rotation_count);
        {
            test_key update_key = make_key(0, 999999);
            test_priority update_priority = make_priority(-1, -1);
            test_value update_value = make_value(42, NULL);
            test_key shared_key = make_key(rotation_count - 1, 0);
            const void* shared_before = NULL;
            const void* shared_after = NULL;
            size_t shared_count = 0;
            ft_priority_search_queue updated;
            REQUIRE_STATUS(
                ft_priority_search_queue_node_identity(&queue, &shared_key, &shared_before),
                FT_STATUS_OK);
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &queue, &update_key, &update_priority, &update_value, &updated),
                FT_STATUS_OK);
            REQUIRE_STATUS(
                ft_priority_search_queue_node_identity(&updated, &shared_key, &shared_after),
                FT_STATUS_OK);
            REQUIRE(shared_before != NULL && shared_before == shared_after);
            REQUIRE_STATUS(
                ft_priority_search_queue_shared_node_count(&queue, &updated, &shared_count),
                FT_STATUS_OK);
            REQUIRE(shared_count >= rotation_count - 64);
            ft_priority_search_queue_dispose(&updated);
        }
        for (int key_value = 0; key_value != rotation_count; ++key_value) {
            if (key_value % 3 != 1) {
                test_key key = make_key(key_value, 0);
                REQUIRE_STATUS(
                    ft_priority_search_queue_remove(&queue, &key, &queue),
                    FT_STATUS_OK);
                if ((key_value & 127) == 0) {
                    REQUIRE(queue_valid(&queue));
                }
            }
        }
        REQUIRE_STATUS(
            ft_priority_search_queue_visit(&queue, collect_ordered, &collector),
            FT_STATUS_OK);
        REQUIRE(collector.count == (rotation_count + 1) / 3);
        for (size_t index = 1; index != collector.count; ++index) {
            REQUIRE(keys[index - 1] < keys[index]);
        }
        REQUIRE(queue_valid(&queue));
        ft_priority_search_queue_dispose(&queue);
        free(priorities);
        free(keys);
    }
    ft_psq_policy_dispose(&policy);
    REQUIRE(context.key_equals_calls == 0);
    REQUIRE(context.successful_key_copies == context.key_destroy_calls);
    REQUIRE(context.successful_priority_copies == context.priority_destroy_calls);
    REQUIRE(context.successful_value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_range_threshold_pruning_equations(void)
{
    enum { count = 4095 };
    test_context context;
    ft_psq_policy_config config;
    ft_psq_policy policy;
    ft_priority_search_queue queue;
    int* keys = (int*)malloc(count * sizeof(*keys));
    int* priorities = (int*)malloc(count * sizeof(*priorities));
    ordered_collector collector = {keys, priorities, count, 0};
    test_key minimum_key = make_key(0, 0);
    test_key maximum_key = make_key(count - 1, 0);
    test_priority impossible = make_priority(-1, -1);
    REQUIRE(keys != NULL && priorities != NULL);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_psq_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_priority_search_queue_init(&queue, &policy), FT_STATUS_OK);
    for (int key_value = 0; key_value != count; ++key_value) {
        test_key key = make_key(key_value, key_value);
        test_priority priority = make_priority(key_value % 17, key_value % 17);
        test_value value = make_value(-key_value, NULL);
        REQUIRE_STATUS(
            ft_priority_search_queue_set(&queue, &key, &priority, &value, &queue),
            FT_STATUS_OK);
    }
    REQUIRE(queue_valid(&queue));
    context.key_compare_calls = 0;
    context.priority_compare_calls = 0;
    REQUIRE_STATUS(
        ft_priority_search_queue_visit_at_most(
            &queue,
            &minimum_key,
            &maximum_key,
            &impossible,
            collect_ordered,
            &collector),
        FT_STATUS_OK);
    REQUIRE(collector.count == 0);
    REQUIRE(context.key_compare_calls == 1);
    REQUIRE(context.priority_compare_calls == 1);

    {
        test_key exact_key = make_key(2047, 0);
        test_priority threshold = make_priority(16, 16);
        size_t visited_nodes = 0;
        collector.count = 0;
        context.key_compare_calls = 0;
        context.priority_compare_calls = 0;
        REQUIRE_STATUS(
            ft_priority_search_queue_visit_at_most(
                &queue,
                &exact_key,
                &exact_key,
                &threshold,
                collect_ordered,
                &collector),
            FT_STATUS_OK);
        REQUIRE(collector.count == 1 && keys[0] == 2047);
        REQUIRE(context.priority_compare_calls >= collector.count);
        visited_nodes = context.priority_compare_calls - collector.count;
        REQUIRE(visited_nodes >= 1);
        REQUIRE(visited_nodes <= ft_priority_search_queue_height(&queue));
        REQUIRE(context.key_compare_calls == 1 + 2 * visited_nodes);
    }
    {
        test_key low = make_key(512, 0);
        test_key high = make_key(1024, 0);
        test_priority threshold = make_priority(3, 3);
        size_t expected = 0;
        collector.count = 0;
        REQUIRE_STATUS(
            ft_priority_search_queue_visit_at_most(
                &queue, &low, &high, &threshold, collect_ordered, &collector),
            FT_STATUS_OK);
        for (int key_value = 512; key_value <= 1024; ++key_value) {
            if (key_value % 17 <= 3) {
                REQUIRE(expected < collector.count);
                REQUIRE(keys[expected] == key_value);
                REQUIRE(priorities[expected] == key_value % 17);
                ++expected;
            }
        }
        REQUIRE(collector.count == expected);
        {
            test_key inverted_low = make_key(2, 0);
            test_key inverted_high = make_key(1, 0);
            REQUIRE_STATUS(
                ft_priority_search_queue_visit_at_most(
                    &queue,
                    &inverted_low,
                    &inverted_high,
                    &threshold,
                    collect_ordered,
                    &collector),
                FT_STATUS_INVALID_ARGUMENT);
        }
    }
    ft_priority_search_queue_dispose(&queue);
    ft_psq_policy_dispose(&policy);
    free(priorities);
    free(keys);
    REQUIRE(context.key_equals_calls == 0);
    REQUIRE(context.successful_key_copies == context.key_destroy_calls);
    REQUIRE(context.successful_priority_copies == context.priority_destroy_calls);
    REQUIRE(context.successful_value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

enum { model_minimum_key = -768, model_maximum_key = 768, model_capacity = 1537 };

typedef struct model_slot {
    bool present;
    int representative;
    int priority;
    int value;
} model_slot;

typedef struct retained_model {
    ft_priority_search_queue queue;
    model_slot slots[model_capacity];
} retained_model;

static size_t model_index(int key)
{
    return (size_t)(key - model_minimum_key);
}

static size_t model_count(const model_slot* slots)
{
    size_t count = 0;
    for (size_t index = 0; index != model_capacity; ++index) {
        count += slots[index].present ? 1u : 0u;
    }
    return count;
}

static int model_minimum(const model_slot* slots)
{
    int selected = model_minimum_key;
    bool found = false;
    for (int key = model_minimum_key; key <= model_maximum_key; ++key) {
        const model_slot slot = slots[model_index(key)];
        if (slot.present && (!found ||
            slot.priority < slots[model_index(selected)].priority ||
            (slot.priority == slots[model_index(selected)].priority && key < selected))) {
            selected = key;
            found = true;
        }
    }
    return selected;
}

typedef struct model_check_context {
    const model_slot* slots;
    int previous_key;
    bool has_previous;
    size_t count;
    bool valid;
} model_check_context;

static ft_status check_model_entry(ft_priority_search_entry_ref entry, void* context)
{
    model_check_context* check = (model_check_context*)context;
    const test_key key = *(const test_key*)entry.key;
    const test_priority priority = *(const test_priority*)entry.priority;
    const test_value value = *(const test_value*)entry.value;
    if (key.order < model_minimum_key || key.order > model_maximum_key) {
        check->valid = false;
        return FT_STATUS_OK;
    }
    {
        const model_slot slot = check->slots[model_index(key.order)];
        if (!slot.present || slot.representative != key.representative ||
            slot.priority != priority.rank || slot.value != value.payload ||
            (check->has_previous && check->previous_key >= key.order)) {
            check->valid = false;
        }
    }
    check->previous_key = key.order;
    check->has_previous = true;
    ++check->count;
    return FT_STATUS_OK;
}

static bool queue_matches_model(
    const ft_priority_search_queue* queue,
    const model_slot* slots)
{
    model_check_context check = {slots, 0, false, 0, true};
    const size_t expected_count = model_count(slots);
    if (ft_priority_search_queue_size(queue) != expected_count ||
        ft_priority_search_queue_visit(queue, check_model_entry, &check) != FT_STATUS_OK ||
        !check.valid || check.count != expected_count || !queue_valid(queue)) {
        return false;
    }
    if (expected_count != 0) {
        ft_priority_search_entry minimum_entry = {0};
        ft_priority_search_entry_ref entry_ref;
        bool found = false;
        const int expected_key = model_minimum(slots);
        if (ft_priority_search_queue_try_get_minimum(
                queue, &found, &minimum_entry) != FT_STATUS_OK ||
            !found || ft_priority_search_entry_get_ref(
                &minimum_entry, &entry_ref) != FT_STATUS_OK ||
            ((const test_key*)entry_ref.key)->order != expected_key) {
            ft_priority_search_entry_dispose(&minimum_entry);
            return false;
        }
        ft_priority_search_entry_dispose(&minimum_entry);
    }
    return true;
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

static void test_twenty_thousand_retained_model(void)
{
    enum { operation_count = 20000, snapshot_capacity = 96 };
    test_context context;
    ft_psq_policy_config config;
    ft_psq_policy policy;
    ft_priority_search_queue queue;
    model_slot* slots = (model_slot*)calloc(model_capacity, sizeof(*slots));
    retained_model* snapshots =
        (retained_model*)calloc(snapshot_capacity, sizeof(*snapshots));
    size_t snapshot_count = 0;
    uint64_t random = UINT64_C(0x5a172026cafe);
    REQUIRE(slots != NULL && snapshots != NULL);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_psq_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_priority_search_queue_init(&queue, &policy), FT_STATUS_OK);
    for (int step = 0; step != operation_count; ++step) {
        const int key_value = model_minimum_key +
            (int)(next_random(&random) % model_capacity);
        const size_t index = model_index(key_value);
        const int choice = (int)(next_random(&random) % 8);
        test_key key = make_key(key_value, step);
        if (choice <= 3) {
            test_priority priority = make_priority(
                (int)(next_random(&random) % 64), step);
            test_value value = make_value(step, NULL);
            const int representative = slots[index].present
                ? slots[index].representative
                : step;
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &queue, &key, &priority, &value, &queue),
                FT_STATUS_OK);
            slots[index].present = true;
            slots[index].representative = representative;
            slots[index].priority = priority.rank;
            slots[index].value = step;
        } else if (choice == 4) {
            REQUIRE_STATUS(
                ft_priority_search_queue_remove(&queue, &key, &queue),
                FT_STATUS_OK);
            slots[index].present = false;
        } else if (choice == 5) {
            test_priority priority = make_priority(
                (int)(next_random(&random) % 64), step);
            test_value value = make_value(step, NULL);
            bool added = false;
            const bool expected = !slots[index].present;
            REQUIRE_STATUS(
                ft_priority_search_queue_try_add(
                    &queue, &key, &priority, &value, &added, &queue),
                FT_STATUS_OK);
            REQUIRE(added == expected);
            if (added) {
                slots[index].present = true;
                slots[index].representative = step;
                slots[index].priority = priority.rank;
                slots[index].value = step;
            }
        } else if (model_count(slots) == 0) {
            test_priority priority = make_priority(0, step);
            test_value value = make_value(step, NULL);
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &queue, &key, &priority, &value, &queue),
                FT_STATUS_OK);
            slots[index] = (model_slot){true, step, 0, step};
        } else {
            const int expected_key = model_minimum(slots);
            const model_slot expected = slots[model_index(expected_key)];
            ft_priority_search_entry removed_entry = {0};
            test_key removed_key;
            test_priority removed_priority;
            test_value removed_value;
            REQUIRE_STATUS(
                ft_priority_search_queue_delete_minimum(
                    &queue, &removed_entry, &queue),
                FT_STATUS_OK);
            REQUIRE(entry_values(
                &removed_entry, &removed_key, &removed_priority, &removed_value));
            REQUIRE(removed_key.order == expected_key);
            REQUIRE(removed_key.representative == expected.representative);
            REQUIRE(removed_priority.rank == expected.priority);
            REQUIRE(removed_value.payload == expected.value);
            ft_priority_search_entry_dispose(&removed_entry);
            slots[model_index(expected_key)].present = false;
        }
        if (step % 127 == 0) {
            REQUIRE(queue_matches_model(&queue, slots));
        }
        if (step % 239 == 0) {
            const size_t slot = snapshot_count < snapshot_capacity
                ? snapshot_count++
                : (size_t)(next_random(&random) % snapshot_capacity);
            if (snapshots[slot].queue.policy != NULL) {
                ft_priority_search_queue_dispose(&snapshots[slot].queue);
            }
            REQUIRE_STATUS(
                ft_priority_search_queue_copy(&queue, &snapshots[slot].queue),
                FT_STATUS_OK);
            (void)memcpy(
                snapshots[slot].slots,
                slots,
                model_capacity * sizeof(*slots));
        }
    }
    REQUIRE(queue_matches_model(&queue, slots));
    for (size_t index = 0; index != snapshot_count; ++index) {
        REQUIRE(queue_matches_model(&snapshots[index].queue, snapshots[index].slots));
        ft_priority_search_queue_dispose(&snapshots[index].queue);
    }
    ft_priority_search_queue_dispose(&queue);
    ft_psq_policy_dispose(&policy);
    free(snapshots);
    free(slots);
    REQUIRE(context.key_equals_calls == 0);
    REQUIRE(context.successful_key_copies == context.key_destroy_calls);
    REQUIRE(context.successful_priority_copies == context.priority_destroy_calls);
    REQUIRE(context.successful_value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void require_unchanged(
    const void* actual,
    const void* expected,
    size_t size)
{
    if (memcmp(actual, expected, size) != 0) {
        fail_at(__FILE__, __LINE__, "failure output remained byte-identical");
    }
}

typedef struct failing_visitor_context {
    size_t calls;
    size_t fail_at;
} failing_visitor_context;

static ft_status failing_visitor(ft_priority_search_entry_ref entry, void* context)
{
    failing_visitor_context* state = (failing_visitor_context*)context;
    (void)entry;
    ++state->calls;
    return state->calls == state->fail_at
        ? FT_STATUS_CALLBACK_FAILURE
        : FT_STATUS_OK;
}

static void test_failure_atomicity_and_callback_sweeps(void)
{
    enum { count = 128, small_count = 8 };
    test_context context;
    ft_psq_policy_config config;
    ft_psq_policy policy;
    ft_priority_search_queue base;
    test_key new_key = make_key(1000, 1000);
    test_priority new_priority = make_priority(-1, 1000);
    test_value new_value = make_value(1000, NULL);
    test_key existing_key = make_key(63, 9999);
    test_priority replacement_priority = make_priority(-2, 2000);
    test_value replacement_value = make_value(2000, NULL);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_psq_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_priority_search_queue_init(&base, &policy), FT_STATUS_OK);
    for (int key_value = 0; key_value != count; ++key_value) {
        test_key key = make_key(key_value, key_value);
        test_priority priority = make_priority(key_value % 17, key_value);
        test_value value = make_value(-key_value, NULL);
        REQUIRE_STATUS(
            ft_priority_search_queue_set(&base, &key, &priority, &value, &base),
            FT_STATUS_OK);
    }
    REQUIRE(queue_valid(&base));

    {
        ft_priority_search_queue successful;
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base, &new_key, &new_priority, &new_value, &successful),
            FT_STATUS_OK);
        const size_t allocations = context.allocation_calls - before;
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa1, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &base,
                    &new_key,
                    &new_priority,
                    &new_value,
                    (ft_priority_search_queue*)output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged(output, expected, sizeof(output));
            REQUIRE(queue_valid(&base));
        }
    }
    {
        ft_priority_search_queue successful;
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base,
                &existing_key,
                &replacement_priority,
                &replacement_value,
                &successful),
            FT_STATUS_OK);
        const size_t allocations = context.allocation_calls - before;
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa2, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &base,
                    &existing_key,
                    &replacement_priority,
                    &replacement_value,
                    (ft_priority_search_queue*)output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
    }
    {
        ft_priority_search_queue successful;
        ft_priority_search_entry removed_entry = {0};
        bool removed = false;
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_try_remove(
                &base, &existing_key, &removed, &removed_entry, &successful),
            FT_STATUS_OK);
        REQUIRE(removed);
        const size_t allocations = context.allocation_calls - before;
        ft_priority_search_entry_dispose(&removed_entry);
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char queue_output[sizeof(ft_priority_search_queue)];
            unsigned char queue_expected[sizeof(queue_output)];
            unsigned char entry_output[sizeof(ft_priority_search_entry)];
            unsigned char entry_expected[sizeof(entry_output)];
            bool removed_output = true;
            (void)memset(queue_output, 0xa3, sizeof(queue_output));
            (void)memset(entry_output, 0x3a, sizeof(entry_output));
            (void)memcpy(queue_expected, queue_output, sizeof(queue_output));
            (void)memcpy(entry_expected, entry_output, sizeof(entry_output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_try_remove(
                    &base,
                    &existing_key,
                    &removed_output,
                    (ft_priority_search_entry*)entry_output,
                    (ft_priority_search_queue*)queue_output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            REQUIRE(removed_output);
            require_unchanged(queue_output, queue_expected, sizeof(queue_output));
            require_unchanged(entry_output, entry_expected, sizeof(entry_output));
        }
    }
    {
        ft_priority_search_queue successful;
        ft_priority_search_entry removed_entry = {0};
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_delete_minimum(
                &base, &removed_entry, &successful),
            FT_STATUS_OK);
        const size_t allocations = context.allocation_calls - before;
        ft_priority_search_entry_dispose(&removed_entry);
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char queue_output[sizeof(ft_priority_search_queue)];
            unsigned char queue_expected[sizeof(queue_output)];
            unsigned char entry_output[sizeof(ft_priority_search_entry)];
            unsigned char entry_expected[sizeof(entry_output)];
            (void)memset(queue_output, 0xa4, sizeof(queue_output));
            (void)memset(entry_output, 0x4a, sizeof(entry_output));
            (void)memcpy(queue_expected, queue_output, sizeof(queue_output));
            (void)memcpy(entry_expected, entry_output, sizeof(entry_output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_delete_minimum(
                    &base,
                    (ft_priority_search_entry*)entry_output,
                    (ft_priority_search_queue*)queue_output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged(queue_output, queue_expected, sizeof(queue_output));
            require_unchanged(entry_output, entry_expected, sizeof(entry_output));
        }
    }
    {
        test_key keys[small_count];
        test_priority priorities[small_count];
        test_value values[small_count];
        ft_priority_search_input inputs[small_count];
        ft_priority_search_queue successful;
        size_t allocations = 0;
        for (int index = 0; index != small_count; ++index) {
            keys[index] = make_key(small_count - index, index);
            priorities[index] = make_priority(index, index);
            values[index] = make_value(index, NULL);
            inputs[index] = (ft_priority_search_input){
                &keys[index], &priorities[index], &values[index]};
        }
        {
            const size_t before = context.allocation_calls;
            REQUIRE_STATUS(
                ft_priority_search_queue_from_array(
                    &successful, &policy, inputs, small_count),
                FT_STATUS_OK);
            allocations = context.allocation_calls - before;
            ft_priority_search_queue_dispose(&successful);
        }
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa5, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_from_array(
                    (ft_priority_search_queue*)output,
                    &policy,
                    inputs,
                    small_count),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= small_count; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa6, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_key_copy_at = context.key_copy_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_from_array(
                    (ft_priority_search_queue*)output,
                    &policy,
                    inputs,
                    small_count),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_copy_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= small_count; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa7, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_priority_copy_at = context.priority_copy_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_from_array(
                    (ft_priority_search_queue*)output,
                    &policy,
                    inputs,
                    small_count),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_priority_copy_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= small_count; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa8, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_value_copy_at = context.value_copy_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_from_array(
                    (ft_priority_search_queue*)output,
                    &policy,
                    inputs,
                    small_count),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_value_copy_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
    }
    {
        unsigned char output[sizeof(ft_priority_search_queue)];
        unsigned char expected[sizeof(output)];
        const size_t key_copies = context.key_copy_calls;
        (void)memset(output, 0xb1, sizeof(output));
        (void)memcpy(expected, output, sizeof(output));
        context.fail_priority_copy_at = context.priority_copy_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base,
                &existing_key,
                &replacement_priority,
                &replacement_value,
                (ft_priority_search_queue*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_priority_copy_at = 0;
        REQUIRE(context.key_copy_calls == key_copies);
        require_unchanged(output, expected, sizeof(output));
        context.fail_value_copy_at = context.value_copy_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base,
                &existing_key,
                &replacement_priority,
                &replacement_value,
                (ft_priority_search_queue*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_value_copy_at = 0;
        REQUIRE(context.key_copy_calls == key_copies);
        require_unchanged(output, expected, sizeof(output));
    }
    {
        test_priority stored_priority = make_priority(12, 12);
        test_value stored_value = make_value(12, NULL);
        test_key key = make_key(12, 9999);
        ft_priority_search_queue exact;
        unsigned char output[sizeof(ft_priority_search_queue)];
        unsigned char expected[sizeof(output)];
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base, &key, &stored_priority, &stored_value, &exact),
            FT_STATUS_OK);
        (void)memset(output, 0xb2, sizeof(output));
        (void)memcpy(expected, output, sizeof(output));
        context.fail_priority_equals_at = context.priority_equals_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &exact,
                &key,
                &stored_priority,
                &stored_value,
                (ft_priority_search_queue*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_priority_equals_at = 0;
        require_unchanged(output, expected, sizeof(output));
        context.fail_value_equals_at = context.value_equals_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &exact,
                &key,
                &stored_priority,
                &stored_value,
                (ft_priority_search_queue*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_value_equals_at = 0;
        require_unchanged(output, expected, sizeof(output));
        ft_priority_search_queue_dispose(&exact);
    }
    {
        unsigned char output[sizeof(ft_priority_search_queue)];
        unsigned char expected[sizeof(output)];
        (void)memset(output, 0xb3, sizeof(output));
        (void)memcpy(expected, output, sizeof(output));
        context.fail_key_compare_at = context.key_compare_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base,
                &new_key,
                &new_priority,
                &new_value,
                (ft_priority_search_queue*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_key_compare_at = 0;
        require_unchanged(output, expected, sizeof(output));
        context.fail_priority_compare_at = context.priority_compare_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base,
                &existing_key,
                &replacement_priority,
                &replacement_value,
                (ft_priority_search_queue*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_priority_compare_at = 0;
        require_unchanged(output, expected, sizeof(output));
    }
    {
        const void* root = ft_priority_search_queue_root_identity(&base);
        context.fail_allocation_at = context.allocation_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base, &new_key, &new_priority, &new_value, &base),
            FT_STATUS_NO_MEMORY);
        context.fail_allocation_at = 0;
        REQUIRE(ft_priority_search_queue_root_identity(&base) == root);
        REQUIRE(queue_valid(&base));
    }
    {
        bool valid = true;
        ft_priority_search_queue_statistics statistics = {77, 77, 77};
        const ft_priority_search_queue_statistics expected = statistics;
        context.fail_allocation_at = context.allocation_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_validate(&base, &valid, &statistics),
            FT_STATUS_NO_MEMORY);
        context.fail_allocation_at = 0;
        REQUIRE(valid);
        REQUIRE(memcmp(&statistics, &expected, sizeof(statistics)) == 0);
        {
            failing_visitor_context visitor = {0, 1};
            context.fail_allocation_at = context.allocation_calls + 1;
            REQUIRE_STATUS(
                ft_priority_search_queue_visit(&base, failing_visitor, &visitor),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            REQUIRE(visitor.calls == 0);
            REQUIRE_STATUS(
                ft_priority_search_queue_visit(&base, failing_visitor, &visitor),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(visitor.calls == 1);
        }
    }
    {
        size_t key_comparisons = 0;
        size_t priority_comparisons = 0;
        ft_priority_search_queue successful;
        const size_t before_key = context.key_compare_calls;
        const size_t before_priority = context.priority_compare_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base, &new_key, &new_priority, &new_value, &successful),
            FT_STATUS_OK);
        key_comparisons = context.key_compare_calls - before_key;
        priority_comparisons = context.priority_compare_calls - before_priority;
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= key_comparisons; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xc1, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_key_compare_at = context.key_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &base,
                    &new_key,
                    &new_priority,
                    &new_value,
                    (ft_priority_search_queue*)output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_compare_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= priority_comparisons; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xc2, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_priority_compare_at = context.priority_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_set(
                    &base,
                    &new_key,
                    &new_priority,
                    &new_value,
                    (ft_priority_search_queue*)output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_priority_compare_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
    }
    {
        size_t key_comparisons = 0;
        size_t priority_comparisons = 0;
        ft_priority_search_queue successful;
        const size_t before_key = context.key_compare_calls;
        const size_t before_priority = context.priority_compare_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_remove(&base, &existing_key, &successful),
            FT_STATUS_OK);
        key_comparisons = context.key_compare_calls - before_key;
        priority_comparisons = context.priority_compare_calls - before_priority;
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= key_comparisons; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xc3, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_key_compare_at = context.key_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_remove(
                    &base, &existing_key, (ft_priority_search_queue*)output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_compare_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= priority_comparisons; ++offset) {
            unsigned char output[sizeof(ft_priority_search_queue)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xc4, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_priority_compare_at = context.priority_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_remove(
                    &base, &existing_key, (ft_priority_search_queue*)output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_priority_compare_at = 0;
            require_unchanged(output, expected, sizeof(output));
        }
    }
    {
        size_t key_comparisons = 0;
        size_t priority_comparisons = 0;
        ft_priority_search_queue successful;
        ft_priority_search_entry removed_entry = {0};
        const size_t before_key = context.key_compare_calls;
        const size_t before_priority = context.priority_compare_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_delete_minimum(
                &base, &removed_entry, &successful),
            FT_STATUS_OK);
        key_comparisons = context.key_compare_calls - before_key;
        priority_comparisons = context.priority_compare_calls - before_priority;
        ft_priority_search_entry_dispose(&removed_entry);
        ft_priority_search_queue_dispose(&successful);
        for (size_t offset = 1; offset <= key_comparisons; ++offset) {
            unsigned char queue_output[sizeof(ft_priority_search_queue)];
            unsigned char queue_expected[sizeof(queue_output)];
            unsigned char entry_output[sizeof(ft_priority_search_entry)];
            unsigned char entry_expected[sizeof(entry_output)];
            (void)memset(queue_output, 0xc5, sizeof(queue_output));
            (void)memset(entry_output, 0x5c, sizeof(entry_output));
            (void)memcpy(queue_expected, queue_output, sizeof(queue_output));
            (void)memcpy(entry_expected, entry_output, sizeof(entry_output));
            context.fail_key_compare_at = context.key_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_delete_minimum(
                    &base,
                    (ft_priority_search_entry*)entry_output,
                    (ft_priority_search_queue*)queue_output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_compare_at = 0;
            require_unchanged(queue_output, queue_expected, sizeof(queue_output));
            require_unchanged(entry_output, entry_expected, sizeof(entry_output));
        }
        for (size_t offset = 1; offset <= priority_comparisons; ++offset) {
            unsigned char queue_output[sizeof(ft_priority_search_queue)];
            unsigned char queue_expected[sizeof(queue_output)];
            unsigned char entry_output[sizeof(ft_priority_search_entry)];
            unsigned char entry_expected[sizeof(entry_output)];
            (void)memset(queue_output, 0xc6, sizeof(queue_output));
            (void)memset(entry_output, 0x6c, sizeof(entry_output));
            (void)memcpy(queue_expected, queue_output, sizeof(queue_output));
            (void)memcpy(entry_expected, entry_output, sizeof(entry_output));
            context.fail_priority_compare_at = context.priority_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_delete_minimum(
                    &base,
                    (ft_priority_search_entry*)entry_output,
                    (ft_priority_search_queue*)queue_output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_priority_compare_at = 0;
            require_unchanged(queue_output, queue_expected, sizeof(queue_output));
            require_unchanged(entry_output, entry_expected, sizeof(entry_output));
        }
    }
    {
        test_key low = make_key(0, 0);
        test_key high = make_key(count - 1, 0);
        test_priority threshold = make_priority(100, 100);
        failing_visitor_context visitor = {0, SIZE_MAX};
        size_t key_comparisons = 0;
        size_t priority_comparisons = 0;
        const size_t before_key = context.key_compare_calls;
        const size_t before_priority = context.priority_compare_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_visit_at_most(
                &base, &low, &high, &threshold, failing_visitor, &visitor),
            FT_STATUS_OK);
        key_comparisons = context.key_compare_calls - before_key;
        priority_comparisons = context.priority_compare_calls - before_priority;
        for (size_t offset = 1; offset <= key_comparisons; ++offset) {
            visitor.calls = 0;
            context.fail_key_compare_at = context.key_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_visit_at_most(
                    &base, &low, &high, &threshold, failing_visitor, &visitor),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_compare_at = 0;
        }
        for (size_t offset = 1; offset <= priority_comparisons; ++offset) {
            visitor.calls = 0;
            context.fail_priority_compare_at = context.priority_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_visit_at_most(
                    &base, &low, &high, &threshold, failing_visitor, &visitor),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_priority_compare_at = 0;
        }
        context.fail_allocation_at = context.allocation_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_visit_at_most(
                &base, &low, &high, &threshold, failing_visitor, &visitor),
            FT_STATUS_NO_MEMORY);
        context.fail_allocation_at = 0;
    }
    {
        bool valid = false;
        ft_priority_search_queue_statistics statistics;
        size_t key_comparisons = 0;
        size_t priority_comparisons = 0;
        const size_t before_key = context.key_compare_calls;
        const size_t before_priority = context.priority_compare_calls;
        REQUIRE_STATUS(
            ft_priority_search_queue_validate(&base, &valid, &statistics),
            FT_STATUS_OK);
        REQUIRE(valid);
        key_comparisons = context.key_compare_calls - before_key;
        priority_comparisons = context.priority_compare_calls - before_priority;
        for (size_t offset = 1; offset <= key_comparisons; ++offset) {
            bool output_valid = true;
            ft_priority_search_queue_statistics output = {91, 92, 93};
            const ft_priority_search_queue_statistics expected = output;
            context.fail_key_compare_at = context.key_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_validate(&base, &output_valid, &output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_key_compare_at = 0;
            REQUIRE(output_valid);
            REQUIRE(memcmp(&output, &expected, sizeof(output)) == 0);
        }
        for (size_t offset = 1; offset <= priority_comparisons; ++offset) {
            bool output_valid = true;
            ft_priority_search_queue_statistics output = {94, 95, 96};
            const ft_priority_search_queue_statistics expected = output;
            context.fail_priority_compare_at = context.priority_compare_calls + offset;
            REQUIRE_STATUS(
                ft_priority_search_queue_validate(&base, &output_valid, &output),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_priority_compare_at = 0;
            REQUIRE(output_valid);
            REQUIRE(memcmp(&output, &expected, sizeof(output)) == 0);
        }
    }
    {
        ft_priority_search_queue updated;
        size_t shared = 77;
        bool added = true;
        unsigned char queue_output[sizeof(ft_priority_search_queue)];
        unsigned char queue_expected[sizeof(queue_output)];
        unsigned char entry_output[sizeof(ft_priority_search_entry)];
        unsigned char entry_expected[sizeof(entry_output)];
        bool removed = true;
        REQUIRE_STATUS(
            ft_priority_search_queue_set(
                &base, &new_key, &new_priority, &new_value, &updated),
            FT_STATUS_OK);
        context.fail_allocation_at = context.allocation_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_shared_node_count(&base, &updated, &shared),
            FT_STATUS_NO_MEMORY);
        context.fail_allocation_at = 0;
        REQUIRE(shared == 77);
        (void)memset(queue_output, 0xd1, sizeof(queue_output));
        (void)memcpy(queue_expected, queue_output, sizeof(queue_output));
        context.fail_key_compare_at = context.key_compare_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_try_add(
                &base,
                &new_key,
                &new_priority,
                &new_value,
                &added,
                (ft_priority_search_queue*)queue_output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_key_compare_at = 0;
        REQUIRE(added);
        require_unchanged(queue_output, queue_expected, sizeof(queue_output));
        (void)memset(queue_output, 0xd2, sizeof(queue_output));
        (void)memset(entry_output, 0x2d, sizeof(entry_output));
        (void)memcpy(queue_expected, queue_output, sizeof(queue_output));
        (void)memcpy(entry_expected, entry_output, sizeof(entry_output));
        context.fail_key_compare_at = context.key_compare_calls + 1;
        REQUIRE_STATUS(
            ft_priority_search_queue_try_remove(
                &base,
                &existing_key,
                &removed,
                (ft_priority_search_entry*)entry_output,
                (ft_priority_search_queue*)queue_output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_key_compare_at = 0;
        REQUIRE(removed);
        require_unchanged(queue_output, queue_expected, sizeof(queue_output));
        require_unchanged(entry_output, entry_expected, sizeof(entry_output));
        ft_priority_search_queue_dispose(&updated);
    }
    {
        ft_psq_policy_config invalid = config;
        ft_psq_policy invalid_policy;
        invalid.key.type_identity = NULL;
        REQUIRE_STATUS(
            ft_psq_policy_create(&invalid_policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
        invalid = config;
        invalid.priority.type_identity = NULL;
        REQUIRE_STATUS(
            ft_psq_policy_create(&invalid_policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
        invalid = config;
        invalid.value.type_identity = NULL;
        REQUIRE_STATUS(
            ft_psq_policy_create(&invalid_policy, &invalid),
            FT_STATUS_INVALID_ARGUMENT);
        invalid = config;
        invalid.key.type_identity = &g_other_key_type_identity;
        REQUIRE_STATUS(ft_psq_policy_create(&invalid_policy, &invalid), FT_STATUS_OK);
        ft_psq_policy_dispose(&invalid_policy);
    }
    REQUIRE(context.key_equals_calls == 0);
    ft_priority_search_queue_dispose(&base);
    ft_psq_policy_dispose(&policy);
    REQUIRE(context.successful_key_copies == context.key_destroy_calls);
    REQUIRE(context.successful_priority_copies == context.priority_destroy_calls);
    REQUIRE(context.successful_value_copies == context.value_destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static ft_status plain_key_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    const int first = ((const test_key*)left)->order;
    const int second = ((const test_key*)right)->order;
    (void)context;
    *comparison = (first > second) - (first < second);
    return FT_STATUS_OK;
}

static ft_status plain_priority_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    const int first = ((const test_priority*)left)->rank;
    const int second = ((const test_priority*)right)->rank;
    (void)context;
    *comparison = (first > second) - (first < second);
    return FT_STATUS_OK;
}

static ft_status plain_priority_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    (void)context;
    *equal = ((const test_priority*)left)->equality_class ==
        ((const test_priority*)right)->equality_class;
    return FT_STATUS_OK;
}

static ft_status plain_value_equals(
    const void* left,
    const void* right,
    bool* equal,
    void* context)
{
    const test_value* first = (const test_value*)left;
    const test_value* second = (const test_value*)right;
    (void)context;
    *equal = first->payload == second->payload && first->nullable == second->nullable;
    return FT_STATUS_OK;
}

typedef struct concurrent_context {
    const ft_priority_search_queue* queue;
    bool success;
} concurrent_context;

static ft_status concurrent_count(ft_priority_search_entry_ref entry, void* context)
{
    size_t* count = (size_t*)context;
    (void)entry;
    ++*count;
    return FT_STATUS_OK;
}

static void concurrent_worker(concurrent_context* context)
{
    context->success = true;
    for (int iteration = 0; iteration != 32 && context->success; ++iteration) {
        ft_priority_search_queue copy;
        ft_priority_search_entry minimum = {0};
        ft_priority_search_entry_ref minimum_ref;
        ft_priority_search_queue_statistics statistics;
        test_key key = make_key(iteration * 251 % 10000, 0);
        ft_priority_search_entry_ref entry_ref;
        size_t visited = 0;
        bool found = false;
        bool valid = false;
        context->success =
            ft_priority_search_queue_copy(context->queue, &copy) == FT_STATUS_OK &&
            ft_priority_search_queue_try_get_entry_ref(
                &copy, &key, &found, &entry_ref) == FT_STATUS_OK && found &&
            ft_priority_search_queue_try_get_minimum(
                &copy, &found, &minimum) == FT_STATUS_OK && found &&
            ft_priority_search_entry_get_ref(&minimum, &minimum_ref) == FT_STATUS_OK &&
            ((const test_key*)minimum_ref.key)->order == 0 &&
            ft_priority_search_queue_validate(
                &copy, &valid, &statistics) == FT_STATUS_OK && valid &&
            statistics.count == 10000 &&
            ft_priority_search_queue_visit(
                &copy, concurrent_count, &visited) == FT_STATUS_OK &&
            visited == 10000;
        ft_priority_search_entry_dispose(&minimum);
        ft_priority_search_queue_dispose(&copy);
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#elif defined(PSQ_TEST_HAS_THREADS)
static int concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#endif

static void test_concurrent_distinct_handle_readers(void)
{
    enum { count = 10000, thread_count = 8 };
    ft_psq_policy_config config;
    ft_psq_policy policy;
    ft_priority_search_queue queue;
    concurrent_context contexts[thread_count];
    ft_psq_policy_config_init(
        &config,
        sizeof(test_key),
        &g_key_type_identity,
        plain_key_compare,
        sizeof(test_priority),
        &g_priority_type_identity,
        plain_priority_compare,
        plain_priority_equals,
        sizeof(test_value),
        &g_value_type_identity,
        plain_value_equals);
    REQUIRE_STATUS(ft_psq_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_priority_search_queue_init(&queue, &policy), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        test_key key = make_key(index, index);
        test_priority priority = make_priority(index, index);
        test_value value = make_value(-index, NULL);
        REQUIRE_STATUS(
            ft_priority_search_queue_set(&queue, &key, &priority, &value, &queue),
            FT_STATUS_OK);
    }
    for (int index = 0; index != thread_count; ++index) {
        contexts[index].queue = &queue;
        contexts[index].success = false;
    }
#ifdef _WIN32
    {
        HANDLE threads[thread_count];
        for (DWORD index = 0; index != thread_count; ++index) {
            threads[index] = CreateThread(NULL, 0, concurrent_thread, &contexts[index], 0, NULL);
            REQUIRE(threads[index] != NULL);
        }
        REQUIRE(WaitForMultipleObjects(thread_count, threads, TRUE, INFINITE) == WAIT_OBJECT_0);
        for (int index = 0; index != thread_count; ++index) {
            REQUIRE(CloseHandle(threads[index]) != 0);
        }
    }
#elif defined(PSQ_TEST_HAS_THREADS)
    {
        thrd_t threads[thread_count];
        for (int index = 0; index != thread_count; ++index) {
            REQUIRE(thrd_create(&threads[index], concurrent_thread, &contexts[index]) == thrd_success);
        }
        for (int index = 0; index != thread_count; ++index) {
            int result = 0;
            REQUIRE(thrd_join(threads[index], &result) == thrd_success);
            REQUIRE(result == 0);
        }
    }
#else
    for (int index = 0; index != thread_count; ++index) {
        concurrent_worker(&contexts[index]);
    }
#endif
    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(contexts[index].success);
    }
    ft_priority_search_queue_dispose(&queue);
    ft_psq_policy_dispose(&policy);
}

typedef void (*test_fn)(void);

static void run_test(const char* name, test_fn test)
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
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    run_test("PSQ representatives no-ops and minimum", test_semantics_representatives_noops_and_minimum);
    run_test("PSQ rotations deletions stack safety and sharing", test_rotations_deletions_stack_safety_and_sharing);
    run_test("PSQ range threshold pruning equations", test_range_threshold_pruning_equations);
    run_test("PSQ twenty-thousand retained model", test_twenty_thousand_retained_model);
    run_test("PSQ failure atomicity and callback sweeps", test_failure_atomicity_and_callback_sweeps);
    run_test("PSQ concurrent distinct-handle readers", test_concurrent_distinct_handle_readers);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C priority search queue tests passed\n");
    return EXIT_SUCCESS;
}
