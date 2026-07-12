#include <tools/data_structures/finger_tree/brodal_okasaki_heap.h>
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
#define BRODAL_TEST_HAS_THREADS 1
#include <threads.h>
#endif
#endif

static int g_failures = 0;
static const unsigned char g_item_type_identity = 0;

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

typedef struct test_item {
    int priority;
    int representative;
    const void* nullable_payload;
} test_item;

typedef struct test_context {
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t successful_copies;
    size_t destroy_calls;
    size_t fail_copy_at;
    size_t compare_calls;
    size_t fail_compare_at;
} test_context;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* allocation = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 &&
        state->allocation_calls == state->fail_allocation_at) {
        return NULL;
    }
    allocation = malloc(size);
    if (allocation != NULL) {
        ++state->outstanding_allocations;
    }
    return allocation;
}

static void tracked_deallocate(void* allocation, void* context)
{
    test_context* state = (test_context*)context;
    if (allocation != NULL) {
        ++state->deallocation_calls;
        --state->outstanding_allocations;
        free(allocation);
    }
}

static ft_status tracked_copy(void* destination, const void* source, void* context)
{
    test_context* state = (test_context*)context;
    ++state->copy_calls;
    if (state->fail_copy_at != 0 && state->copy_calls == state->fail_copy_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *(test_item*)destination = *(const test_item*)source;
    ++state->successful_copies;
    return FT_STATUS_OK;
}

static void tracked_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->destroy_calls;
}

static ft_status tracked_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    test_context* state = (test_context*)context;
    const int left_priority = ((const test_item*)left)->priority;
    const int right_priority = ((const test_item*)right)->priority;
    ++state->compare_calls;
    if (state->fail_compare_at != 0 &&
        state->compare_calls == state->fail_compare_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    *comparison = (left_priority > right_priority) -
        (left_priority < right_priority);
    return FT_STATUS_OK;
}

static void init_config(ft_brodal_policy_config* config, test_context* context)
{
    ft_brodal_policy_config_init(
        config,
        sizeof(test_item),
        &g_item_type_identity,
        tracked_compare,
        context);
    config->copy = tracked_copy;
    config->destroy = tracked_destroy;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static test_item make_item(int priority, int representative)
{
    test_item item;
    item.priority = priority;
    item.representative = representative;
    item.nullable_payload = NULL;
    return item;
}

static bool heap_valid(const ft_brodal_heap* heap)
{
    bool valid = false;
    ft_brodal_heap_statistics statistics;
    return ft_brodal_heap_validate(heap, &valid, &statistics) == FT_STATUS_OK &&
        valid && statistics.count == ft_brodal_heap_size(heap);
}

typedef struct visit_counter {
    size_t count;
} visit_counter;

static ft_status count_visit(const void* value, void* context)
{
    visit_counter* counter = (visit_counter*)context;
    (void)value;
    ++counter->count;
    return FT_STATUS_OK;
}

static unsigned ceiling_log2(size_t value)
{
    unsigned result = 0;
    size_t power = 1;
    while (power < value && power <= SIZE_MAX / 2) {
        power *= 2;
        ++result;
    }
    return result;
}

static void test_basic_bounds_representatives_and_sharing(void)
{
    enum { count = 4096 };
    test_context context;
    ft_brodal_policy_config config;
    ft_brodal_policy policy;
    ft_brodal_policy policy_copy;
    ft_brodal_policy incompatible_policy;
    ft_brodal_heap ascending;
    ft_brodal_heap descending;
    ft_brodal_heap equal;
    ft_brodal_heap empty;
    ft_brodal_heap result;
    test_item* values = (test_item*)malloc(count * sizeof(*values));
    test_item inserted = make_item(-1, 9000);
    const void* root_identity = NULL;
    size_t comparisons = 0;
    bool found = false;
    const void* minimum = NULL;
    visit_counter visited = {0};
    REQUIRE(values != NULL);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_brodal_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_brodal_policy_copy(&policy, &policy_copy), FT_STATUS_OK);
    REQUIRE(ft_brodal_policy_same(&policy, &policy_copy));
    REQUIRE_STATUS(ft_brodal_policy_create(&incompatible_policy, &config), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_item(index, index);
    }
    REQUIRE_STATUS(ft_brodal_heap_from_array(&ascending, &policy, values, count), FT_STATUS_OK);
    for (int index = 0; index != count / 2; ++index) {
        test_item temporary = values[index];
        values[index] = values[count - 1 - index];
        values[count - 1 - index] = temporary;
    }
    REQUIRE_STATUS(ft_brodal_heap_from_array(&descending, &policy, values, count), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_item(7, index);
    }
    REQUIRE_STATUS(ft_brodal_heap_from_array(&equal, &policy_copy, values, count), FT_STATUS_OK);
    ft_brodal_policy_dispose(&policy_copy);
    REQUIRE(heap_valid(&ascending));
    REQUIRE(heap_valid(&descending));
    REQUIRE(heap_valid(&equal));

    comparisons = context.compare_calls;
    REQUIRE_STATUS(ft_brodal_heap_try_get_minimum_ref(&ascending, &found, &minimum), FT_STATUS_OK);
    REQUIRE(found && ((const test_item*)minimum)->priority == 0);
    REQUIRE(context.compare_calls == comparisons);
    REQUIRE_STATUS(ft_brodal_heap_visit(&ascending, count_visit, &visited), FT_STATUS_OK);
    REQUIRE(visited.count == count);
    REQUIRE(context.compare_calls == comparisons);

    comparisons = context.compare_calls;
    REQUIRE_STATUS(ft_brodal_heap_insert(&ascending, &inserted, &result), FT_STATUS_OK);
    REQUIRE(context.compare_calls - comparisons >= 1);
    REQUIRE(context.compare_calls - comparisons <= 5);
    REQUIRE(heap_valid(&result));
    ft_brodal_heap_dispose(&result);

    REQUIRE_STATUS(ft_brodal_heap_init(&empty, &policy), FT_STATUS_OK);
    root_identity = ft_brodal_heap_root_identity(&ascending);
    comparisons = context.compare_calls;
    REQUIRE_STATUS(ft_brodal_heap_meld(&ascending, &empty, &result), FT_STATUS_OK);
    REQUIRE(ft_brodal_heap_root_identity(&result) == root_identity);
    REQUIRE(context.compare_calls == comparisons);
    ft_brodal_heap_dispose(&result);
    comparisons = context.compare_calls;
    REQUIRE_STATUS(ft_brodal_heap_meld(&ascending, &descending, &result), FT_STATUS_OK);
    REQUIRE(context.compare_calls - comparisons >= 1);
    REQUIRE(context.compare_calls - comparisons <= 5);
    REQUIRE(ft_brodal_heap_size(&result) == count * 2);
    REQUIRE(heap_valid(&result));
    ft_brodal_heap_dispose(&result);
    REQUIRE_STATUS(
        ft_brodal_heap_meld(&ascending, &(ft_brodal_heap){incompatible_policy.rep, NULL, 0}, &result),
        FT_STATUS_INCOMPATIBLE_POLICY);

    {
        ft_brodal_heap self;
        REQUIRE_STATUS(ft_brodal_heap_meld(&equal, &equal, &self), FT_STATUS_OK);
        REQUIRE(ft_brodal_heap_size(&self) == count * 2);
        REQUIRE(heap_valid(&self));
        ft_brodal_heap_dispose(&self);
    }
    {
        ft_brodal_heap reduced;
        const size_t before = context.compare_calls;
        REQUIRE_STATUS(ft_brodal_heap_delete_minimum(&ascending, &reduced), FT_STATUS_OK);
        REQUIRE(context.compare_calls - before <=
            (size_t)(32 * ceiling_log2(count + 1) + 8));
        REQUIRE(ft_brodal_heap_size(&reduced) == count - 1);
        REQUIRE(heap_valid(&reduced));
        ft_brodal_heap_dispose(&reduced);
    }
    {
        bool removed = false;
        test_item removed_item = make_item(123, 123);
        REQUIRE_STATUS(
            ft_brodal_heap_try_delete_minimum(&ascending, &removed, &removed_item, &result),
            FT_STATUS_OK);
        REQUIRE(removed && removed_item.priority == 0 && removed_item.representative == 0);
        tracked_destroy(&removed_item, &context);
        ft_brodal_heap_dispose(&result);
        removed = true;
        REQUIRE_STATUS(
            ft_brodal_heap_try_delete_minimum(&empty, &removed, &removed_item, &result),
            FT_STATUS_OK);
        REQUIRE(!removed);
        REQUIRE(ft_brodal_heap_root_identity(&result) == NULL);
        ft_brodal_heap_dispose(&result);
    }

    ft_brodal_heap_dispose(&empty);
    ft_brodal_heap_dispose(&equal);
    ft_brodal_heap_dispose(&descending);
    ft_brodal_heap_dispose(&ascending);
    ft_brodal_policy_dispose(&incompatible_policy);
    ft_brodal_policy_dispose(&policy);
    free(values);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef struct model_version {
    ft_brodal_heap heap;
    test_item* values;
    size_t count;
} model_version;

static uint64_t next_random(uint64_t* state)
{
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static bool model_append(model_version* version, test_item item)
{
    test_item* values = (test_item*)realloc(
        version->values, (version->count + 1) * sizeof(*values));
    if (values == NULL) {
        return false;
    }
    version->values = values;
    version->values[version->count++] = item;
    return true;
}

static bool model_remove_id(model_version* version, int representative)
{
    for (size_t index = 0; index != version->count; ++index) {
        if (version->values[index].representative == representative) {
            version->values[index] = version->values[version->count - 1];
            --version->count;
            return true;
        }
    }
    return false;
}

static int model_minimum_priority(const model_version* version)
{
    int minimum = version->values[0].priority;
    for (size_t index = 1; index != version->count; ++index) {
        if (version->values[index].priority < minimum) {
            minimum = version->values[index].priority;
        }
    }
    return minimum;
}

static bool model_clone(const model_version* source, model_version* destination)
{
    (void)memset(destination, 0, sizeof(*destination));
    if (ft_brodal_heap_copy(&source->heap, &destination->heap) != FT_STATUS_OK) {
        return false;
    }
    if (source->count != 0) {
        destination->values = (test_item*)malloc(source->count * sizeof(*destination->values));
        if (destination->values == NULL) {
            ft_brodal_heap_dispose(&destination->heap);
            return false;
        }
        (void)memcpy(
            destination->values,
            source->values,
            source->count * sizeof(*destination->values));
    }
    destination->count = source->count;
    return true;
}

static void model_dispose(model_version* version)
{
    ft_brodal_heap_dispose(&version->heap);
    free(version->values);
    (void)memset(version, 0, sizeof(*version));
}

static void test_randomized_retained_history(void)
{
    enum { operation_count = 10000, snapshot_capacity = 64, maximum_count = 2048 };
    test_context context;
    ft_brodal_policy_config config;
    ft_brodal_policy policy;
    model_version current;
    model_version snapshots[snapshot_capacity];
    size_t snapshot_count = 0;
    uint64_t random = UINT64_C(0x5b0da1c0ffee);
    int next_id = 0;
    (void)memset(&context, 0, sizeof(context));
    (void)memset(&current, 0, sizeof(current));
    (void)memset(snapshots, 0, sizeof(snapshots));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_brodal_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_brodal_heap_init(&current.heap, &policy), FT_STATUS_OK);
    for (int step = 0; step != operation_count; ++step) {
        const uint64_t choice = next_random(&random) % 100;
        if (current.count == 0 || (choice < 63 && current.count < maximum_count)) {
            test_item item = make_item((int)(next_random(&random) % 257) - 128, next_id++);
            REQUIRE_STATUS(ft_brodal_heap_insert(&current.heap, &item, &current.heap), FT_STATUS_OK);
            REQUIRE(model_append(&current, item));
        } else if (choice < 88 || snapshot_count == 0) {
            bool found = false;
            const void* minimum = NULL;
            REQUIRE_STATUS(
                ft_brodal_heap_try_get_minimum_ref(&current.heap, &found, &minimum),
                FT_STATUS_OK);
            REQUIRE(found);
            REQUIRE(((const test_item*)minimum)->priority == model_minimum_priority(&current));
            {
                const int id = ((const test_item*)minimum)->representative;
                REQUIRE_STATUS(
                    ft_brodal_heap_delete_minimum(&current.heap, &current.heap),
                    FT_STATUS_OK);
                REQUIRE(model_remove_id(&current, id));
            }
        } else {
            model_version* partner = &snapshots[next_random(&random) % snapshot_count];
            if (current.count + partner->count <= maximum_count) {
                const size_t old_count = current.count;
                test_item* merged_values = (test_item*)realloc(
                    current.values,
                    (current.count + partner->count) * sizeof(*merged_values));
                REQUIRE(merged_values != NULL || partner->count == 0);
                current.values = merged_values;
                if (partner->count != 0) {
                    (void)memcpy(
                        current.values + old_count,
                        partner->values,
                        partner->count * sizeof(*partner->values));
                }
                current.count += partner->count;
                REQUIRE_STATUS(
                    ft_brodal_heap_meld(&current.heap, &partner->heap, &current.heap),
                    FT_STATUS_OK);
            }
        }
        REQUIRE(ft_brodal_heap_size(&current.heap) == current.count);
        if (current.count != 0) {
            bool found = false;
            const void* minimum = NULL;
            REQUIRE_STATUS(
                ft_brodal_heap_try_get_minimum_ref(&current.heap, &found, &minimum),
                FT_STATUS_OK);
            REQUIRE(found && ((const test_item*)minimum)->priority ==
                model_minimum_priority(&current));
        }
        if ((step & 127) == 0) {
            REQUIRE(heap_valid(&current.heap));
        }
        if (step % 173 == 0) {
            const size_t slot = snapshot_count < snapshot_capacity
                ? snapshot_count++
                : (size_t)(next_random(&random) % snapshot_capacity);
            if (slot < snapshot_count - 1 || snapshot_count == snapshot_capacity) {
                model_dispose(&snapshots[slot]);
            }
            REQUIRE(model_clone(&current, &snapshots[slot]));
        }
    }
    for (size_t index = 0; index != snapshot_count; ++index) {
        REQUIRE(ft_brodal_heap_size(&snapshots[index].heap) == snapshots[index].count);
        REQUIRE(heap_valid(&snapshots[index].heap));
        if (snapshots[index].count != 0) {
            bool found = false;
            const void* minimum = NULL;
            REQUIRE_STATUS(
                ft_brodal_heap_try_get_minimum_ref(
                    &snapshots[index].heap, &found, &minimum),
                FT_STATUS_OK);
            REQUIRE(found && ((const test_item*)minimum)->priority ==
                model_minimum_priority(&snapshots[index]));
        }
        model_dispose(&snapshots[index]);
    }
    model_dispose(&current);
    ft_brodal_policy_dispose(&policy);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void require_unchanged_bytes(
    const unsigned char* left,
    const unsigned char* right,
    size_t size)
{
    if (memcmp(left, right, size) != 0) {
        fail_at(__FILE__, __LINE__, "failure output remained byte-identical");
    }
}

static void test_failure_atomicity_and_lifetimes(void)
{
    enum { count = 256 };
    test_context context;
    ft_brodal_policy_config config;
    ft_brodal_policy policy;
    ft_brodal_heap base;
    ft_brodal_heap other;
    test_item values[count];
    test_item item = make_item(-7, 9999);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_brodal_policy_create(&policy, &config), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_item(index, index);
    }
    REQUIRE_STATUS(ft_brodal_heap_from_array(&base, &policy, values, count), FT_STATUS_OK);
    REQUIRE_STATUS(ft_brodal_heap_from_array(&other, &policy, values, count), FT_STATUS_OK);

    {
        ft_brodal_heap successful;
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(ft_brodal_heap_insert(&base, &item, &successful), FT_STATUS_OK);
        const size_t allocations = context.allocation_calls - before;
        ft_brodal_heap_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char output[sizeof(ft_brodal_heap)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0xa5, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_brodal_heap_insert(&base, &item, (ft_brodal_heap*)output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged_bytes(output, expected, sizeof(output));
            REQUIRE(heap_valid(&base));
        }
    }
    {
        ft_brodal_heap successful;
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(ft_brodal_heap_meld(&base, &other, &successful), FT_STATUS_OK);
        const size_t allocations = context.allocation_calls - before;
        ft_brodal_heap_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char output[sizeof(ft_brodal_heap)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0x5a, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_brodal_heap_meld(&base, &other, (ft_brodal_heap*)output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged_bytes(output, expected, sizeof(output));
        }
    }
    {
        ft_brodal_heap successful;
        const size_t before = context.allocation_calls;
        REQUIRE_STATUS(ft_brodal_heap_delete_minimum(&base, &successful), FT_STATUS_OK);
        const size_t allocations = context.allocation_calls - before;
        ft_brodal_heap_dispose(&successful);
        for (size_t offset = 1; offset <= allocations; ++offset) {
            unsigned char output[sizeof(ft_brodal_heap)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0x3c, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_brodal_heap_delete_minimum(&base, (ft_brodal_heap*)output),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged_bytes(output, expected, sizeof(output));
            REQUIRE(heap_valid(&base));
        }
    }
    {
        unsigned char output[sizeof(ft_brodal_heap)];
        unsigned char expected[sizeof(output)];
        bool removed = false;
        test_item minimum = make_item(123, 456);
        const test_item expected_minimum = minimum;
        (void)memset(output, 0xc3, sizeof(output));
        (void)memcpy(expected, output, sizeof(output));
        context.fail_copy_at = context.copy_calls + 1;
        REQUIRE_STATUS(
            ft_brodal_heap_try_delete_minimum(
                &base, &removed, &minimum, (ft_brodal_heap*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_copy_at = 0;
        REQUIRE(!removed);
        REQUIRE(memcmp(&minimum, &expected_minimum, sizeof(minimum)) == 0);
        require_unchanged_bytes(output, expected, sizeof(output));
        REQUIRE(heap_valid(&base));
    }
    {
        unsigned char output[sizeof(ft_brodal_heap)];
        unsigned char expected[sizeof(output)];
        (void)memset(output, 0x7e, sizeof(output));
        (void)memcpy(expected, output, sizeof(output));
        context.fail_compare_at = context.compare_calls + 1;
        REQUIRE_STATUS(
            ft_brodal_heap_insert(&base, &item, (ft_brodal_heap*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_compare_at = 0;
        require_unchanged_bytes(output, expected, sizeof(output));
        context.fail_copy_at = context.copy_calls + 1;
        REQUIRE_STATUS(
            ft_brodal_heap_insert(&base, &item, (ft_brodal_heap*)output),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_copy_at = 0;
        require_unchanged_bytes(output, expected, sizeof(output));
    }
    {
        {
            ft_brodal_heap successful;
            const size_t before = context.compare_calls;
            REQUIRE_STATUS(ft_brodal_heap_insert(&base, &item, &successful), FT_STATUS_OK);
            const size_t comparisons = context.compare_calls - before;
            ft_brodal_heap_dispose(&successful);
            for (size_t offset = 1; offset <= comparisons; ++offset) {
                unsigned char output[sizeof(ft_brodal_heap)];
                unsigned char expected[sizeof(output)];
                (void)memset(output, 0x19, sizeof(output));
                (void)memcpy(expected, output, sizeof(output));
                context.fail_compare_at = context.compare_calls + offset;
                REQUIRE_STATUS(
                    ft_brodal_heap_insert(&base, &item, (ft_brodal_heap*)output),
                    FT_STATUS_CALLBACK_FAILURE);
                context.fail_compare_at = 0;
                require_unchanged_bytes(output, expected, sizeof(output));
            }
        }
        {
            ft_brodal_heap successful;
            const size_t before = context.compare_calls;
            REQUIRE_STATUS(ft_brodal_heap_meld(&base, &other, &successful), FT_STATUS_OK);
            const size_t comparisons = context.compare_calls - before;
            ft_brodal_heap_dispose(&successful);
            for (size_t offset = 1; offset <= comparisons; ++offset) {
                unsigned char output[sizeof(ft_brodal_heap)];
                unsigned char expected[sizeof(output)];
                (void)memset(output, 0x29, sizeof(output));
                (void)memcpy(expected, output, sizeof(output));
                context.fail_compare_at = context.compare_calls + offset;
                REQUIRE_STATUS(
                    ft_brodal_heap_meld(&base, &other, (ft_brodal_heap*)output),
                    FT_STATUS_CALLBACK_FAILURE);
                context.fail_compare_at = 0;
                require_unchanged_bytes(output, expected, sizeof(output));
            }
        }
        {
            ft_brodal_heap successful;
            const size_t before = context.compare_calls;
            REQUIRE_STATUS(ft_brodal_heap_delete_minimum(&base, &successful), FT_STATUS_OK);
            const size_t comparisons = context.compare_calls - before;
            ft_brodal_heap_dispose(&successful);
            for (size_t offset = 1; offset <= comparisons; ++offset) {
                unsigned char output[sizeof(ft_brodal_heap)];
                unsigned char expected[sizeof(output)];
                (void)memset(output, 0x39, sizeof(output));
                (void)memcpy(expected, output, sizeof(output));
                context.fail_compare_at = context.compare_calls + offset;
                REQUIRE_STATUS(
                    ft_brodal_heap_delete_minimum(&base, (ft_brodal_heap*)output),
                    FT_STATUS_CALLBACK_FAILURE);
                context.fail_compare_at = 0;
                require_unchanged_bytes(output, expected, sizeof(output));
                REQUIRE(heap_valid(&base));
            }
        }
    }
    {
        enum { small_count = 8 };
        test_item small[small_count];
        ft_brodal_heap successful;
        size_t allocation_count = 0;
        size_t comparison_count = 0;
        for (int index = 0; index != small_count; ++index) {
            small[index] = make_item(small_count - index, 20000 + index);
        }
        {
            const size_t before_allocations = context.allocation_calls;
            const size_t before_comparisons = context.compare_calls;
            REQUIRE_STATUS(
                ft_brodal_heap_from_array(&successful, &policy, small, small_count),
                FT_STATUS_OK);
            allocation_count = context.allocation_calls - before_allocations;
            comparison_count = context.compare_calls - before_comparisons;
            ft_brodal_heap_dispose(&successful);
        }
        for (size_t offset = 1; offset <= allocation_count; ++offset) {
            unsigned char output[sizeof(ft_brodal_heap)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0x49, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_allocation_at = context.allocation_calls + offset;
            REQUIRE_STATUS(
                ft_brodal_heap_from_array(
                    (ft_brodal_heap*)output, &policy, small, small_count),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            require_unchanged_bytes(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= small_count; ++offset) {
            unsigned char output[sizeof(ft_brodal_heap)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0x59, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_copy_at = context.copy_calls + offset;
            REQUIRE_STATUS(
                ft_brodal_heap_from_array(
                    (ft_brodal_heap*)output, &policy, small, small_count),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_copy_at = 0;
            require_unchanged_bytes(output, expected, sizeof(output));
        }
        for (size_t offset = 1; offset <= comparison_count; ++offset) {
            unsigned char output[sizeof(ft_brodal_heap)];
            unsigned char expected[sizeof(output)];
            (void)memset(output, 0x69, sizeof(output));
            (void)memcpy(expected, output, sizeof(output));
            context.fail_compare_at = context.compare_calls + offset;
            REQUIRE_STATUS(
                ft_brodal_heap_from_array(
                    (ft_brodal_heap*)output, &policy, small, small_count),
                FT_STATUS_CALLBACK_FAILURE);
            context.fail_compare_at = 0;
            require_unchanged_bytes(output, expected, sizeof(output));
        }
    }
    {
        const void* root = ft_brodal_heap_root_identity(&base);
        const size_t size = ft_brodal_heap_size(&base);
        context.fail_allocation_at = context.allocation_calls + 1;
        REQUIRE_STATUS(ft_brodal_heap_insert(&base, &item, &base), FT_STATUS_NO_MEMORY);
        context.fail_allocation_at = 0;
        REQUIRE(ft_brodal_heap_root_identity(&base) == root);
        REQUIRE(ft_brodal_heap_size(&base) == size);
    }
    {
        bool valid = true;
        ft_brodal_heap_statistics statistics = {88, 88, 88, 88};
        const ft_brodal_heap_statistics expected = statistics;
        context.fail_allocation_at = context.allocation_calls + 1;
        REQUIRE_STATUS(
            ft_brodal_heap_validate(&base, &valid, &statistics),
            FT_STATUS_NO_MEMORY);
        context.fail_allocation_at = 0;
        REQUIRE(valid);
        REQUIRE(memcmp(&statistics, &expected, sizeof(statistics)) == 0);
        {
            visit_counter visitor = {0};
            context.fail_allocation_at = context.allocation_calls + 1;
            REQUIRE_STATUS(
                ft_brodal_heap_visit(&base, count_visit, &visitor),
                FT_STATUS_NO_MEMORY);
            context.fail_allocation_at = 0;
            REQUIRE(visitor.count == 0);
        }
    }
    {
        bool valid = true;
        ft_brodal_heap_statistics statistics = {77, 77, 77, 77};
        const ft_brodal_heap_statistics expected = statistics;
        context.fail_compare_at = context.compare_calls + 1;
        REQUIRE_STATUS(
            ft_brodal_heap_validate(&base, &valid, &statistics),
            FT_STATUS_CALLBACK_FAILURE);
        context.fail_compare_at = 0;
        REQUIRE(valid);
        REQUIRE(memcmp(&statistics, &expected, sizeof(statistics)) == 0);
    }

    ft_brodal_heap_dispose(&other);
    ft_brodal_heap_dispose(&base);
    ft_brodal_policy_dispose(&policy);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static ft_status plain_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    const test_item* first = (const test_item*)left;
    const test_item* second = (const test_item*)right;
    (void)context;
    *comparison = (first->priority > second->priority) -
        (first->priority < second->priority);
    return FT_STATUS_OK;
}

typedef struct concurrent_context {
    const ft_brodal_heap* source;
    bool success;
} concurrent_context;

static void concurrent_worker(concurrent_context* context)
{
    context->success = true;
    for (int iteration = 0; iteration != 64 && context->success; ++iteration) {
        ft_brodal_heap copy;
        bool valid = false;
        bool found = false;
        const void* minimum = NULL;
        ft_brodal_heap_statistics statistics;
        context->success = ft_brodal_heap_copy(context->source, &copy) == FT_STATUS_OK &&
            ft_brodal_heap_try_get_minimum_ref(&copy, &found, &minimum) == FT_STATUS_OK &&
            found && ((const test_item*)minimum)->priority == 0 &&
            ft_brodal_heap_validate(&copy, &valid, &statistics) == FT_STATUS_OK &&
            valid && statistics.count == ft_brodal_heap_size(&copy);
        ft_brodal_heap_dispose(&copy);
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#elif defined(BRODAL_TEST_HAS_THREADS)
static int concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#endif

static void test_concurrent_readers(void)
{
    enum { count = 10000, thread_count = 8 };
    ft_brodal_policy_config config;
    ft_brodal_policy policy;
    ft_brodal_heap heap;
    test_item* values = (test_item*)malloc(count * sizeof(*values));
    concurrent_context contexts[thread_count];
    REQUIRE(values != NULL);
    ft_brodal_policy_config_init(
        &config,
        sizeof(test_item),
        &g_item_type_identity,
        plain_compare,
        NULL);
    REQUIRE_STATUS(ft_brodal_policy_create(&policy, &config), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_item(index, index);
    }
    REQUIRE_STATUS(ft_brodal_heap_from_array(&heap, &policy, values, count), FT_STATUS_OK);
    for (int index = 0; index != thread_count; ++index) {
        contexts[index].source = &heap;
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
#elif defined(BRODAL_TEST_HAS_THREADS)
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
    ft_brodal_heap_dispose(&heap);
    ft_brodal_policy_dispose(&policy);
    free(values);
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
    run_test("Brodal bounds representatives and sharing", test_basic_bounds_representatives_and_sharing);
    run_test("Brodal randomized retained history", test_randomized_retained_history);
    run_test("Brodal failure atomicity and lifetimes", test_failure_atomicity_and_lifetimes);
    run_test("Brodal concurrent readers", test_concurrent_readers);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C Brodal-Okasaki heap tests passed\n");
    return EXIT_SUCCESS;
}
