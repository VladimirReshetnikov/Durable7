/*
 * Tests for the canonical sorted set.
 *
 * The central property is history independence: sets reaching the same contents by different
 * sequences of edits must be structurally identical.
 */

#include <durable7/finger_tree/canonical_sorted_set.h>
#include <durable7/test_support/headless_test_process.h>

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
#define CANONICAL_TEST_HAS_THREADS 1
#include <threads.h>
#endif
#endif

static int g_failures = 0;
static const unsigned char g_test_value_type_identity = 0;
static const unsigned char g_unrelated_value_type_identity = 0;

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

typedef struct test_value {
    int key;
    int representative;
    const void* nullable_payload;
} test_value;

typedef struct test_context {
    size_t allocation_calls;
    size_t deallocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t successful_copies;
    size_t destroy_calls;
    size_t compare_calls;
    size_t rank_hash_calls;
    size_t fail_copy_at;
    size_t fail_compare_at;
    size_t fail_rank_hash_at;
    bool absolute_comparison;
    bool constant_rank_hash;
    bool forced_rank_hash;
    uint64_t forced_rank_hash_value;
} test_context;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* allocation = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 && state->allocation_calls == state->fail_allocation_at) {
        return NULL;
    }
    allocation = malloc(size == 0 ? 1 : size);
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
    *(test_value*)destination = *(const test_value*)source;
    ++state->successful_copies;
    return FT_STATUS_OK;
}

static void tracked_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    ++state->destroy_calls;
}

static int absolute_int(int value)
{
    return value < 0 ? -value : value;
}

static ft_status tracked_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    test_context* state = (test_context*)context;
    int left_key = ((const test_value*)left)->key;
    int right_key = ((const test_value*)right)->key;
    ++state->compare_calls;
    if (state->fail_compare_at != 0 && state->compare_calls == state->fail_compare_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (state->absolute_comparison) {
        left_key = absolute_int(left_key);
        right_key = absolute_int(right_key);
    }
    *comparison = (left_key > right_key) - (left_key < right_key);
    return FT_STATUS_OK;
}

static ft_status tracked_rank_hash(const void* value, uint64_t* rank_hash, void* context)
{
    test_context* state = (test_context*)context;
    int key = ((const test_value*)value)->key;
    ++state->rank_hash_calls;
    if (state->fail_rank_hash_at != 0 && state->rank_hash_calls == state->fail_rank_hash_at) {
        return FT_STATUS_CALLBACK_FAILURE;
    }
    if (state->forced_rank_hash) {
        *rank_hash = state->forced_rank_hash_value;
    } else if (state->constant_rank_hash) {
        *rank_hash = 0;
    } else {
        if (state->absolute_comparison) {
            key = absolute_int(key);
        }
        *rank_hash = (uint64_t)(uint32_t)key;
    }
    return FT_STATUS_OK;
}

static void init_config(ft_canonical_policy_config* config, test_context* context)
{
    ft_canonical_policy_config_init(
        config,
        sizeof(test_value),
        &g_test_value_type_identity,
        tracked_compare,
        tracked_rank_hash,
        context);
    config->copy = tracked_copy;
    config->destroy = tracked_destroy;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static test_value make_value(int key, int representative)
{
    test_value value;
    value.key = key;
    value.representative = representative;
    value.nullable_payload = NULL;
    return value;
}

typedef struct shape_entry {
    int key;
    size_t left_count;
    size_t right_count;
} shape_entry;

typedef struct shape_collector {
    shape_entry* entries;
    size_t capacity;
    size_t count;
} shape_collector;

static ft_status collect_shape(
    const void* value,
    size_t left_count,
    size_t right_count,
    void* context)
{
    shape_collector* collector = (shape_collector*)context;
    if (collector->count >= collector->capacity) {
        return FT_STATUS_OVERFLOW;
    }
    collector->entries[collector->count].key = ((const test_value*)value)->key;
    collector->entries[collector->count].left_count = left_count;
    collector->entries[collector->count].right_count = right_count;
    ++collector->count;
    return FT_STATUS_OK;
}

static bool shapes_equal(
    const ft_canonical_sorted_set* left,
    const ft_canonical_sorted_set* right)
{
    const size_t count = ft_canonical_sorted_set_size(left);
    shape_entry* left_entries = count == 0 ? NULL : (shape_entry*)malloc(count * sizeof(*left_entries));
    shape_entry* right_entries = count == 0 ? NULL : (shape_entry*)malloc(count * sizeof(*right_entries));
    shape_collector left_collector = { left_entries, count, 0 };
    shape_collector right_collector = { right_entries, count, 0 };
    bool result = false;
    if (ft_canonical_sorted_set_size(right) != count ||
        (count != 0 && (left_entries == NULL || right_entries == NULL))) {
        free(right_entries);
        free(left_entries);
        return false;
    }
    result = ft_canonical_sorted_set_visit_shape(left, collect_shape, &left_collector) == FT_STATUS_OK &&
        ft_canonical_sorted_set_visit_shape(right, collect_shape, &right_collector) == FT_STATUS_OK &&
        left_collector.count == count && right_collector.count == count;
    for (size_t index = 0; result && index != count; ++index) {
        result = left_entries[index].key == right_entries[index].key &&
            left_entries[index].left_count == right_entries[index].left_count &&
            left_entries[index].right_count == right_entries[index].right_count;
    }
    free(right_entries);
    free(left_entries);
    return result;
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

static void shuffle(test_value* values, size_t count, uint64_t* state)
{
    size_t index = count;
    while (index > 1) {
        size_t other = (size_t)(next_random(state) % index);
        test_value temporary = values[index - 1];
        values[index - 1] = values[other];
        values[other] = temporary;
        --index;
    }
}

static void test_crypto_vectors_and_priority(void)
{
    test_context context;
    ft_canonical_policy_config config;
    ft_canonical_policy keyed;
    ft_canonical_policy keyed_copy;
    ft_canonical_policy seeded;
    ft_canonical_policy random_first;
    ft_canonical_policy random_second;
    ft_zip_tree_rank rank;
    ft_zip_tree_rank second_rank;
    unsigned char key[32];
    unsigned char retained_key[32];
    test_value value = make_value(0, 0);
    size_t index = 0;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    for (index = 0; index != sizeof(key); ++index) {
        key[index] = (unsigned char)index;
    }
    (void)memcpy(retained_key, key, sizeof(key));
    context.forced_rank_hash = true;
    context.forced_rank_hash_value = UINT64_C(0x0102030405060708);
    REQUIRE_STATUS(ft_canonical_policy_create_keyed(&keyed, &config, key, sizeof(key)), FT_STATUS_OK);
    (void)memset(key, 0xff, sizeof(key));
    REQUIRE_STATUS(ft_canonical_policy_rank_for(&keyed, &value, &rank), FT_STATUS_OK);
    REQUIRE(rank.geometric == 1);
    REQUIRE(rank.secondary == UINT64_C(0x197520638af1f7a6));
    REQUIRE(rank.content == UINT64_C(0xf31ebe0ab983ff0f));
    REQUIRE_STATUS(
        ft_canonical_policy_create_keyed(&keyed_copy, &config, retained_key, sizeof(retained_key)),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_policy_rank_for(&keyed_copy, &value, &second_rank), FT_STATUS_OK);
    REQUIRE(rank.geometric == second_rank.geometric);
    REQUIRE(rank.secondary == second_rank.secondary);
    REQUIRE(rank.content == second_rank.content);
    ft_canonical_policy_dispose(&keyed_copy);
    ft_canonical_policy_dispose(&keyed);

    context.forced_rank_hash_value = UINT64_C(0xfedcba9876543210);
    REQUIRE_STATUS(
        ft_canonical_policy_create_seeded(&seeded, &config, UINT64_C(0x0123456789abcdef)),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_policy_rank_for(&seeded, &value, &rank), FT_STATUS_OK);
    REQUIRE(rank.geometric == 0);
    REQUIRE(rank.secondary == UINT64_C(0x9efdaef03f68c6bd));
    REQUIRE(rank.content == UINT64_C(0x4da75484837a7798));
    REQUIRE(ft_canonical_policy_has_public_seed(&seeded));
    {
        uint64_t seed = 0;
        REQUIRE_STATUS(ft_canonical_policy_public_seed(&seeded, &seed), FT_STATUS_OK);
        REQUIRE(seed == UINT64_C(0x0123456789abcdef));
    }
    ft_canonical_policy_dispose(&seeded);

    context.forced_rank_hash_value = UINT64_C(0x1020304050607080);
    REQUIRE_STATUS(ft_canonical_policy_create_random(&random_first, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_policy_create_random(&random_second, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_policy_rank_for(&random_first, &value, &rank), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_policy_rank_for(&random_second, &value, &second_rank), FT_STATUS_OK);
    /* The independently random keys collide on secondary/content with probability at most 2^-128. */
    REQUIRE(rank.secondary != second_rank.secondary || rank.content != second_rank.content);
    ft_canonical_policy_dispose(&random_second);
    ft_canonical_policy_dispose(&random_first);
    REQUIRE(context.outstanding_allocations == 0);

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&seeded, &config, UINT64_C(0x771122)), FT_STATUS_OK);
    {
        bool found_pair = false;
        test_value candidates[512];
        ft_zip_tree_rank ranks[512];
        test_value negative_value = make_value(0, 0);
        test_value positive_value = make_value(0, 0);
        ft_zip_tree_rank negative_rank = { 0, 0, 0 };
        ft_zip_tree_rank positive_rank = { 0, 0, 0 };
        for (int candidate_index = 0; candidate_index != 512; ++candidate_index) {
            candidates[candidate_index] = make_value(candidate_index, candidate_index);
            REQUIRE_STATUS(
                ft_canonical_policy_rank_for(
                    &seeded,
                    &candidates[candidate_index],
                    &ranks[candidate_index]),
                FT_STATUS_OK);
        }
        for (int left = 0; left != 512 && !found_pair; ++left) {
            for (int right = 0; right != left; ++right) {
                if (ranks[left].geometric == ranks[right].geometric &&
                    ((ranks[left].secondary >> 63) != (ranks[right].secondary >> 63))) {
                    if ((ranks[left].secondary >> 63) != 0) {
                        negative_value = candidates[left];
                        negative_rank = ranks[left];
                        positive_value = candidates[right];
                        positive_rank = ranks[right];
                    } else {
                        negative_value = candidates[right];
                        negative_rank = ranks[right];
                        positive_value = candidates[left];
                        positive_rank = ranks[left];
                    }
                    found_pair = true;
                    break;
                }
            }
        }
        REQUIRE(found_pair);
        REQUIRE((negative_rank.secondary >> 63) == 1);
        REQUIRE((positive_rank.secondary >> 63) == 0);
        REQUIRE(negative_rank.secondary > positive_rank.secondary);
        {
            test_value pair[2] = { positive_value, negative_value };
            ft_canonical_sorted_set set;
            shape_entry entries[2];
            shape_collector collector = { entries, 2, 0 };
            REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&set, &seeded, pair, 2), FT_STATUS_OK);
            REQUIRE_STATUS(ft_canonical_sorted_set_visit_shape(&set, collect_shape, &collector), FT_STATUS_OK);
            REQUIRE(collector.count == 2);
            REQUIRE(entries[0].key == negative_value.key);
            ft_canonical_sorted_set_dispose(&set);
        }
    }
    ft_canonical_policy_dispose(&seeded);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_canonical_topology_and_representatives(void)
{
    enum { count = 512 };
    test_context context;
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    test_value values[count];
    uint64_t random = UINT64_C(0x5eed1234);
    ft_canonical_sorted_set baseline;
    ft_canonical_sorted_set incremental;
    ft_canonical_sorted_set bulk;
    uint64_t baseline_hash = 0;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, UINT64_C(0x123456789abcdef0)), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_value(index - count / 2, index);
    }
    REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&baseline, &policy, values, count), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_content_hash(&baseline, &baseline_hash), FT_STATUS_OK);
    for (int trial = 0; trial != 10; ++trial) {
        shuffle(values, count, &random);
        REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&bulk, &policy, values, count), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_sorted_set_init(&incremental, &policy), FT_STATUS_OK);
        for (int index = 0; index != count; ++index) {
            REQUIRE_STATUS(
                ft_canonical_sorted_set_add(&incremental, &values[index], &incremental),
                FT_STATUS_OK);
        }
        REQUIRE(shapes_equal(&baseline, &bulk));
        REQUIRE(shapes_equal(&baseline, &incremental));
        {
            uint64_t digest = 0;
            REQUIRE_STATUS(ft_canonical_sorted_set_content_hash(&incremental, &digest), FT_STATUS_OK);
            REQUIRE(digest == baseline_hash);
        }
        for (int index = 0; index != 50; ++index) {
            REQUIRE_STATUS(
                ft_canonical_sorted_set_remove(&incremental, &values[index], &incremental),
                FT_STATUS_OK);
        }
        for (int index = 49; index >= 0; --index) {
            REQUIRE_STATUS(
                ft_canonical_sorted_set_add(&incremental, &values[index], &incremental),
                FT_STATUS_OK);
        }
        REQUIRE(shapes_equal(&baseline, &incremental));
        ft_canonical_sorted_set_dispose(&incremental);
        ft_canonical_sorted_set_dispose(&bulk);
    }
    {
        test_value duplicates[5] = {
            make_value(7, 100), make_value(2, 20), make_value(7, 101),
            make_value(2, 21), make_value(5, 50)
        };
        test_value probe = make_value(7, 0);
        const void* found_value = NULL;
        bool found = false;
        ft_canonical_sorted_set representatives;
        REQUIRE_STATUS(
            ft_canonical_sorted_set_from_array(&representatives, &policy, duplicates, 5),
            FT_STATUS_OK);
        REQUIRE(ft_canonical_sorted_set_size(&representatives) == 3);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_try_get_ref(&representatives, &probe, &found, &found_value),
            FT_STATUS_OK);
        REQUIRE(found);
        REQUIRE(((const test_value*)found_value)->representative == 100);
        REQUIRE(((const test_value*)found_value)->nullable_payload == NULL);
        ft_canonical_sorted_set_dispose(&representatives);
    }
    {
        bool valid = false;
        ft_canonical_sorted_set_statistics statistics;
        REQUIRE_STATUS(ft_canonical_sorted_set_validate(&baseline, &valid, &statistics), FT_STATUS_OK);
        REQUIRE(valid);
        REQUIRE(statistics.count == count);
        REQUIRE(statistics.height == ft_canonical_sorted_set_height(&baseline));
    }
    ft_canonical_sorted_set_dispose(&baseline);
    ft_canonical_policy_dispose(&policy);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_deep_collisions_and_stack_safe_lifecycle(void)
{
    enum { count = 4096 };
    test_context context;
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    test_value* values = (test_value*)malloc(count * sizeof(*values));
    ft_canonical_sorted_set forward;
    ft_canonical_sorted_set reverse;
    ft_canonical_sorted_set removed;
    ft_canonical_sorted_set restored;
    uint64_t forward_hash = 0;
    uint64_t restored_hash = 0;
    bool valid = false;
    ft_canonical_sorted_set_statistics statistics;
    REQUIRE(values != NULL);
    (void)memset(&context, 0, sizeof(context));
    context.constant_rank_hash = true;
    init_config(&config, &context);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, 1), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_value(index, index);
    }
    REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&forward, &policy, values, count), FT_STATUS_OK);
    for (int index = 0; index != count / 2; ++index) {
        test_value temporary = values[index];
        values[index] = values[count - 1 - index];
        values[count - 1 - index] = temporary;
    }
    REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&reverse, &policy, values, count), FT_STATUS_OK);
    REQUIRE(ft_canonical_sorted_set_height(&forward) == count);
    REQUIRE(shapes_equal(&forward, &reverse));
    REQUIRE_STATUS(ft_canonical_sorted_set_validate(&forward, &valid, &statistics), FT_STATUS_OK);
    REQUIRE(valid);
    REQUIRE(statistics.priority_collision_count == count - 1);
    {
        test_value last = make_value(count - 1, count - 1);
        REQUIRE_STATUS(ft_canonical_sorted_set_remove(&forward, &last, &removed), FT_STATUS_OK);
        REQUIRE(ft_canonical_sorted_set_height(&removed) == count - 1);
        REQUIRE_STATUS(ft_canonical_sorted_set_add(&removed, &last, &restored), FT_STATUS_OK);
    }
    REQUIRE_STATUS(ft_canonical_sorted_set_content_hash(&forward, &forward_hash), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_content_hash(&restored, &restored_hash), FT_STATUS_OK);
    REQUIRE(forward_hash == restored_hash);
    REQUIRE(shapes_equal(&forward, &restored));
    ft_canonical_sorted_set_dispose(&restored);
    ft_canonical_sorted_set_dispose(&removed);
    ft_canonical_sorted_set_dispose(&reverse);
    ft_canonical_sorted_set_dispose(&forward);
    ft_canonical_policy_dispose(&policy);
    free(values);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static void test_interior_removal_merge_seam(void)
{
    /*
     * Regression for the removal-merge scratch overflow. ft_canonical_merge zips
     * the right spine of a removed node's left child against the left spine of
     * its right child, so its scratch path can reach roughly 2 * height steps -
     * not the single root-to-leaf height. A prior revision sized that buffer for
     * one path (height + 1), so removing an interior node of a tree taller than
     * three wrote past the buffer. Build a tall pseudo-random treap and drain it
     * in a scrambled order (which repeatedly removes near-root nodes with long
     * merge seams), validating structure throughout. Under AddressSanitizer the
     * prior sizing fails here; the corrected two-spine sizing plus the in-merge
     * capacity guard keep it clean.
     */
    enum { count = 8192 };
    test_context context;
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    ft_canonical_sorted_set set;
    test_value* values = (test_value*)malloc(count * sizeof(*values));
    uint64_t random = UINT64_C(0x5eed5eed0badf00d);
    size_t remaining = count;
    bool valid = false;
    ft_canonical_sorted_set_statistics statistics;
    REQUIRE(values != NULL);
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(
        ft_canonical_policy_create_seeded(&policy, &config, UINT64_C(0xa11ce5ee0badf00d)),
        FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_value(index, index);
    }
    REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&set, &policy, values, count), FT_STATUS_OK);
    /*
     * Any tree of 8192 nodes is at least 14 levels tall, so removal seams for
     * near-root interior nodes far exceed the old height + 1 scratch bound.
     */
    REQUIRE(ft_canonical_sorted_set_height(&set) >= 14);
    shuffle(values, count, &random);
    for (size_t index = 0; index != count; ++index) {
        REQUIRE_STATUS(ft_canonical_sorted_set_remove(&set, &values[index], &set), FT_STATUS_OK);
        --remaining;
        REQUIRE(ft_canonical_sorted_set_size(&set) == remaining);
        if (index % 256 == 0) {
            REQUIRE_STATUS(ft_canonical_sorted_set_validate(&set, &valid, &statistics), FT_STATUS_OK);
            REQUIRE(valid);
            REQUIRE(statistics.count == remaining);
        }
    }
    REQUIRE(ft_canonical_sorted_set_size(&set) == 0);
    ft_canonical_sorted_set_dispose(&set);
    ft_canonical_policy_dispose(&policy);
    free(values);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

typedef struct key_collector {
    int* keys;
    size_t capacity;
    size_t count;
} key_collector;

static ft_status collect_key(const void* value, void* context)
{
    key_collector* collector = (key_collector*)context;
    if (collector->count >= collector->capacity) {
        return FT_STATUS_OVERFLOW;
    }
    collector->keys[collector->count++] = ((const test_value*)value)->key;
    return FT_STATUS_OK;
}

static bool set_matches_keys(
    const ft_canonical_sorted_set* set,
    const int* expected,
    size_t count)
{
    int* actual = count == 0 ? NULL : (int*)malloc(count * sizeof(*actual));
    key_collector collector = { actual, count, 0 };
    bool result = false;
    if (ft_canonical_sorted_set_size(set) != count || (count != 0 && actual == NULL)) {
        free(actual);
        return false;
    }
    result = ft_canonical_sorted_set_visit(set, collect_key, &collector) == FT_STATUS_OK &&
        collector.count == count && (count == 0 || memcmp(actual, expected, count * sizeof(*actual)) == 0);
    free(actual);
    return result;
}

static bool set_matches_bitmap(
    const ft_canonical_sorted_set* set,
    const bool* present,
    size_t range)
{
    size_t count = 0;
    size_t index = 0;
    int* expected = NULL;
    size_t output = 0;
    for (index = 0; index != range; ++index) {
        if (present[index]) {
            ++count;
        }
    }
    expected = count == 0 ? NULL : (int*)malloc(count * sizeof(*expected));
    if (count != 0 && expected == NULL) {
        return false;
    }
    for (index = 0; index != range; ++index) {
        if (present[index]) {
            expected[output++] = (int)index - (int)(range / 2);
        }
    }
    {
        bool result = set_matches_keys(set, expected, count);
        free(expected);
        return result;
    }
}

typedef struct retained_snapshot {
    ft_canonical_sorted_set set;
    bool* present;
} retained_snapshot;

static void test_randomized_histories_and_snapshots(void)
{
    enum { range = 2001, operations = 10000, maximum_snapshots = 16 };
    test_context context;
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    ft_canonical_sorted_set set;
    bool present[range];
    retained_snapshot snapshots[maximum_snapshots];
    size_t snapshot_count = 0;
    uint64_t random = UINT64_C(0xd1ff12345678);
    (void)memset(&context, 0, sizeof(context));
    (void)memset(present, 0, sizeof(present));
    (void)memset(snapshots, 0, sizeof(snapshots));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, UINT64_C(0xddc0ffee15ebeef)), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_init(&set, &policy), FT_STATUS_OK);
    for (int operation = 0; operation != operations; ++operation) {
        const int key = (int)(next_random(&random) % range) - range / 2;
        const size_t model_index = (size_t)(key + range / 2);
        test_value value = make_value(key, operation);
        if ((next_random(&random) % 5) < 3) {
            REQUIRE_STATUS(ft_canonical_sorted_set_add(&set, &value, &set), FT_STATUS_OK);
            present[model_index] = true;
        } else {
            REQUIRE_STATUS(ft_canonical_sorted_set_remove(&set, &value, &set), FT_STATUS_OK);
            present[model_index] = false;
        }
        if (operation % 503 == 0) {
            bool valid = false;
            ft_canonical_sorted_set_statistics statistics;
            REQUIRE(set_matches_bitmap(&set, present, range));
            REQUIRE_STATUS(ft_canonical_sorted_set_validate(&set, &valid, &statistics), FT_STATUS_OK);
            REQUIRE(valid);
            REQUIRE(statistics.count == ft_canonical_sorted_set_size(&set));
        }
        if (operation % 701 == 0 && snapshot_count < maximum_snapshots) {
            snapshots[snapshot_count].present = (bool*)malloc(sizeof(present));
            REQUIRE(snapshots[snapshot_count].present != NULL);
            (void)memcpy(snapshots[snapshot_count].present, present, sizeof(present));
            REQUIRE_STATUS(ft_canonical_sorted_set_copy(&set, &snapshots[snapshot_count].set), FT_STATUS_OK);
            ++snapshot_count;
        }
    }
    REQUIRE(set_matches_bitmap(&set, present, range));
    for (size_t index = 0; index != snapshot_count; ++index) {
        REQUIRE(set_matches_bitmap(&snapshots[index].set, snapshots[index].present, range));
        ft_canonical_sorted_set_dispose(&snapshots[index].set);
        free(snapshots[index].present);
    }
    ft_canonical_sorted_set_dispose(&set);
    ft_canonical_policy_dispose(&policy);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);
}

static ft_status set_from_ints(
    ft_canonical_sorted_set* set,
    const ft_canonical_policy* policy,
    const int* keys,
    size_t count)
{
    test_value* values = count == 0 ? NULL : (test_value*)malloc(count * sizeof(*values));
    ft_status status = FT_STATUS_OK;
    if (count != 0 && values == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    for (size_t index = 0; index != count; ++index) {
        values[index] = make_value(keys[index], (int)index);
    }
    status = ft_canonical_sorted_set_from_array(set, policy, values, count);
    free(values);
    return status;
}

static void test_algebra_relations_aliasing_and_sharing(void)
{
    static const int left_keys[] = { 1, 2, 4, 8 };
    static const int right_keys[] = { 2, 3, 4, 5 };
    static const int union_keys[] = { 1, 2, 3, 4, 5, 8 };
    static const int intersect_keys[] = { 2, 4 };
    static const int except_keys[] = { 1, 8 };
    test_context context;
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    ft_canonical_policy policy_copy;
    ft_canonical_policy distinct_policy;
    ft_canonical_sorted_set left;
    ft_canonical_sorted_set right;
    ft_canonical_sorted_set result;
    bool relation = false;
    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, 99), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_policy_copy(&policy, &policy_copy), FT_STATUS_OK);
    REQUIRE(ft_canonical_policy_same(&policy, &policy_copy));
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&distinct_policy, &config, 99), FT_STATUS_OK);
    REQUIRE(!ft_canonical_policy_same(&policy, &distinct_policy));
    REQUIRE_STATUS(set_from_ints(&left, &policy_copy, left_keys, 4), FT_STATUS_OK);
    REQUIRE_STATUS(set_from_ints(&right, &policy, right_keys, 4), FT_STATUS_OK);
    ft_canonical_policy_dispose(&policy_copy);
    REQUIRE_STATUS(ft_canonical_sorted_set_union(&left, &right, &result), FT_STATUS_OK);
    REQUIRE(set_matches_keys(&result, union_keys, 6));
    ft_canonical_sorted_set_dispose(&result);
    REQUIRE_STATUS(ft_canonical_sorted_set_intersect(&left, &right, &result), FT_STATUS_OK);
    REQUIRE(set_matches_keys(&result, intersect_keys, 2));
    ft_canonical_sorted_set_dispose(&result);
    REQUIRE_STATUS(ft_canonical_sorted_set_except(&left, &right, &result), FT_STATUS_OK);
    REQUIRE(set_matches_keys(&result, except_keys, 2));
    ft_canonical_sorted_set_dispose(&result);

    REQUIRE_STATUS(ft_canonical_sorted_set_union(&left, &right, &left), FT_STATUS_OK);
    REQUIRE(set_matches_keys(&left, union_keys, 6));
    ft_canonical_sorted_set_dispose(&left);
    REQUIRE_STATUS(set_from_ints(&left, &policy, left_keys, 4), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_intersect(&left, &right, &right), FT_STATUS_OK);
    REQUIRE(set_matches_keys(&right, intersect_keys, 2));
    ft_canonical_sorted_set_dispose(&right);
    REQUIRE_STATUS(set_from_ints(&right, &policy, right_keys, 4), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_except(&left, &right, &left), FT_STATUS_OK);
    REQUIRE(set_matches_keys(&left, except_keys, 2));
    ft_canonical_sorted_set_dispose(&left);

    REQUIRE_STATUS(set_from_ints(&left, &policy, left_keys, 4), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_is_subset(&left, &left, &relation), FT_STATUS_OK);
    REQUIRE(relation);
    REQUIRE_STATUS(ft_canonical_sorted_set_is_proper_subset(&left, &left, &relation), FT_STATUS_OK);
    REQUIRE(!relation);
    REQUIRE_STATUS(ft_canonical_sorted_set_is_superset(&left, &right, &relation), FT_STATUS_OK);
    REQUIRE(!relation);
    REQUIRE_STATUS(ft_canonical_sorted_set_is_proper_superset(&left, &right, &relation), FT_STATUS_OK);
    REQUIRE(!relation);
    REQUIRE_STATUS(ft_canonical_sorted_set_overlaps(&left, &right, &relation), FT_STATUS_OK);
    REQUIRE(relation);

    {
        ft_canonical_sorted_set incompatible;
        REQUIRE_STATUS(set_from_ints(&incompatible, &distinct_policy, left_keys, 4), FT_STATUS_OK);
        REQUIRE(shapes_equal(&left, &incompatible));
        REQUIRE_STATUS(ft_canonical_sorted_set_equals(&left, &incompatible, &relation), FT_STATUS_OK);
        REQUIRE(relation);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_union(&left, &incompatible, &result),
            FT_STATUS_INCOMPATIBLE_POLICY);
        ft_canonical_sorted_set_dispose(&incompatible);
    }

    {
        enum { count = 2000 };
        test_value* values = (test_value*)malloc(count * sizeof(*values));
        ft_canonical_sorted_set large;
        ft_canonical_sorted_set added;
        ft_canonical_sorted_set removed;
        size_t shared = 0;
        test_value add_value = make_value(count, count);
        test_value remove_value = make_value(count / 2, count / 2);
        REQUIRE(values != NULL);
        for (int index = 0; index != count; ++index) {
            values[index] = make_value(index, index);
        }
        REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&large, &policy, values, count), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_sorted_set_add(&large, &add_value, &added), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_sorted_set_remove(&large, &remove_value, &removed), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_sorted_set_shared_node_count(&large, &added, &shared), FT_STATUS_OK);
        REQUIRE(shared >= 1800);
        REQUIRE_STATUS(ft_canonical_sorted_set_shared_node_count(&large, &removed, &shared), FT_STATUS_OK);
        REQUIRE(shared >= 1799);
        ft_canonical_sorted_set_dispose(&removed);
        ft_canonical_sorted_set_dispose(&added);
        ft_canonical_sorted_set_dispose(&large);
        free(values);
    }

    ft_canonical_sorted_set_dispose(&right);
    ft_canonical_sorted_set_dispose(&left);
    ft_canonical_policy_dispose(&distinct_policy);
    ft_canonical_policy_dispose(&policy);
    REQUIRE(context.successful_copies == context.destroy_calls);
    REQUIRE(context.outstanding_allocations == 0);

    {
        test_context insensitive_context;
        test_context sensitive_context;
        ft_canonical_policy_config insensitive_config;
        ft_canonical_policy_config sensitive_config;
        ft_canonical_policy insensitive_policy;
        ft_canonical_policy sensitive_policy;
        ft_canonical_sorted_set insensitive;
        ft_canonical_sorted_set sensitive;
        const int insensitive_keys[] = { 1 };
        const int sensitive_keys[] = { -1, 1 };
        (void)memset(&insensitive_context, 0, sizeof(insensitive_context));
        (void)memset(&sensitive_context, 0, sizeof(sensitive_context));
        insensitive_context.absolute_comparison = true;
        init_config(&insensitive_config, &insensitive_context);
        init_config(&sensitive_config, &sensitive_context);
        REQUIRE_STATUS(ft_canonical_policy_create_seeded(&insensitive_policy, &insensitive_config, 11), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_policy_create_seeded(&sensitive_policy, &sensitive_config, 97), FT_STATUS_OK);
        REQUIRE_STATUS(set_from_ints(&insensitive, &insensitive_policy, insensitive_keys, 1), FT_STATUS_OK);
        REQUIRE_STATUS(set_from_ints(&sensitive, &sensitive_policy, sensitive_keys, 2), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_sorted_set_equals(&insensitive, &sensitive, &relation), FT_STATUS_OK);
        REQUIRE(relation);
        REQUIRE_STATUS(ft_canonical_sorted_set_equals(&sensitive, &insensitive, &relation), FT_STATUS_OK);
        REQUIRE(!relation);
        REQUIRE_STATUS(ft_canonical_sorted_set_is_subset(&insensitive, &sensitive, &relation), FT_STATUS_OK);
        REQUIRE(relation);
        REQUIRE_STATUS(ft_canonical_sorted_set_is_proper_superset(&sensitive, &insensitive, &relation), FT_STATUS_OK);
        REQUIRE(relation);
        ft_canonical_sorted_set_dispose(&sensitive);
        ft_canonical_sorted_set_dispose(&insensitive);
        ft_canonical_policy_dispose(&sensitive_policy);
        ft_canonical_policy_dispose(&insensitive_policy);
        REQUIRE(insensitive_context.outstanding_allocations == 0);
        REQUIRE(sensitive_context.outstanding_allocations == 0);
    }

    {
        test_context compatible_context;
        test_context unrelated_context;
        ft_canonical_policy_config compatible_config;
        ft_canonical_policy_config unrelated_config;
        ft_canonical_policy compatible_policy;
        ft_canonical_policy unrelated_policy;
        ft_canonical_sorted_set compatible;
        ft_canonical_sorted_set unrelated;
        const int keys[] = { 1, 2, 3 };
        size_t compatible_copy_calls = 0;
        (void)memset(&compatible_context, 0, sizeof(compatible_context));
        (void)memset(&unrelated_context, 0, sizeof(unrelated_context));
        init_config(&compatible_config, &compatible_context);
        init_config(&unrelated_config, &unrelated_context);
        unrelated_config.value_type_identity = &g_unrelated_value_type_identity;
        REQUIRE_STATUS(
            ft_canonical_policy_create_seeded(&compatible_policy, &compatible_config, 1),
            FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_canonical_policy_create_seeded(&unrelated_policy, &unrelated_config, 2),
            FT_STATUS_OK);
        REQUIRE_STATUS(set_from_ints(&compatible, &compatible_policy, keys, 3), FT_STATUS_OK);
        REQUIRE_STATUS(set_from_ints(&unrelated, &unrelated_policy, keys, 3), FT_STATUS_OK);
        compatible_copy_calls = compatible_context.copy_calls;

        relation = true;
        REQUIRE_STATUS(
            ft_canonical_sorted_set_equals(&compatible, &unrelated, &relation),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(relation);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_is_subset(&compatible, &unrelated, &relation),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(relation);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_is_proper_subset(&compatible, &unrelated, &relation),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(relation);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_is_superset(&compatible, &unrelated, &relation),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(relation);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_is_proper_superset(&compatible, &unrelated, &relation),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(relation);
        REQUIRE_STATUS(
            ft_canonical_sorted_set_overlaps(&compatible, &unrelated, &relation),
            FT_STATUS_INCOMPATIBLE_POLICY);
        REQUIRE(relation);
        REQUIRE(compatible_context.copy_calls == compatible_copy_calls);

        ft_canonical_sorted_set_dispose(&unrelated);
        ft_canonical_sorted_set_dispose(&compatible);
        ft_canonical_policy_dispose(&unrelated_policy);
        ft_canonical_policy_dispose(&compatible_policy);
        REQUIRE(compatible_context.successful_copies == compatible_context.destroy_calls);
        REQUIRE(unrelated_context.successful_copies == unrelated_context.destroy_calls);
        REQUIRE(compatible_context.outstanding_allocations == 0);
        REQUIRE(unrelated_context.outstanding_allocations == 0);
    }
}

typedef ft_status (*relation_operation)(
    const ft_canonical_sorted_set*,
    const ft_canonical_sorted_set*,
    bool*);

static void test_allocation_and_callback_failure_atomicity(void)
{
    {
        bool reached_success = false;
        for (size_t fail_at_index = 1; fail_at_index != 8; ++fail_at_index) {
            test_context context;
            ft_canonical_policy_config config;
            ft_canonical_policy policy = { NULL };
            ft_status status;
            (void)memset(&context, 0, sizeof(context));
            init_config(&config, &context);
            context.fail_allocation_at = fail_at_index;
            status = ft_canonical_policy_create_seeded(&policy, &config, 42);
            if (status == FT_STATUS_OK) {
                reached_success = true;
                ft_canonical_policy_dispose(&policy);
                REQUIRE(context.outstanding_allocations == 0);
                break;
            }
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(policy.rep == NULL);
            REQUIRE(context.outstanding_allocations == 0);
        }
        REQUIRE(reached_success);
    }

    {
        enum { count = 128 };
        test_context context;
        ft_canonical_policy_config config;
        ft_canonical_policy policy;
        ft_canonical_sorted_set base;
        test_value values[count];
        test_value added_value = make_value(1000, 1000);
        const void* original_root = NULL;
        size_t baseline_outstanding = 0;
        bool reached_success = false;
        (void)memset(&context, 0, sizeof(context));
        init_config(&config, &context);
        REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, 42), FT_STATUS_OK);
        for (int index = 0; index != count; ++index) {
            values[index] = make_value(index, index);
        }
        REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&base, &policy, values, count), FT_STATUS_OK);
        original_root = ft_canonical_sorted_set_root_identity(&base);
        baseline_outstanding = context.outstanding_allocations;
        for (size_t failure_ordinal = 1; failure_ordinal != 128; ++failure_ordinal) {
            ft_canonical_sorted_set result = { NULL, NULL };
            ft_status status;
            context.fail_allocation_at = context.allocation_calls + failure_ordinal;
            status = ft_canonical_sorted_set_add(&base, &added_value, &result);
            if (status == FT_STATUS_OK) {
                reached_success = true;
                ft_canonical_sorted_set_dispose(&result);
                break;
            }
            REQUIRE(status == FT_STATUS_NO_MEMORY);
            REQUIRE(result.policy == NULL && result.root == NULL);
            REQUIRE(ft_canonical_sorted_set_root_identity(&base) == original_root);
            REQUIRE(ft_canonical_sorted_set_size(&base) == count);
            REQUIRE(context.outstanding_allocations == baseline_outstanding);
        }
        REQUIRE(reached_success);
        context.fail_allocation_at = 0;

        {
            ft_canonical_sorted_set result = { NULL, NULL };
            context.fail_compare_at = context.compare_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_add(&base, &added_value, &result),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(result.policy == NULL && result.root == NULL);
            REQUIRE(ft_canonical_sorted_set_root_identity(&base) == original_root);
            context.fail_compare_at = 0;
        }
        {
            ft_canonical_sorted_set result = { NULL, NULL };
            context.fail_rank_hash_at = context.rank_hash_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_add(&base, &added_value, &result),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(result.policy == NULL && result.root == NULL);
            REQUIRE(ft_canonical_sorted_set_root_identity(&base) == original_root);
            context.fail_rank_hash_at = 0;
        }
        {
            context.fail_copy_at = context.copy_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_add(&base, &added_value, &base),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(ft_canonical_sorted_set_root_identity(&base) == original_root);
            REQUIRE(ft_canonical_sorted_set_size(&base) == count);
            context.fail_copy_at = 0;
        }
        {
            test_value remove_value = make_value(64, 64);
            context.fail_compare_at = context.compare_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_remove(&base, &remove_value, &base),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(ft_canonical_sorted_set_root_identity(&base) == original_root);
            REQUIRE(ft_canonical_sorted_set_size(&base) == count);
            context.fail_compare_at = 0;
        }
        {
            ft_canonical_sorted_set result = { NULL, NULL };
            context.fail_copy_at = context.copy_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_from_array(&result, &policy, values, count),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(result.policy == NULL && result.root == NULL);
            context.fail_copy_at = 0;
        }
        {
            ft_canonical_sorted_set result = { NULL, NULL };
            context.fail_compare_at = context.compare_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_from_array(&result, &policy, values, count),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(result.policy == NULL && result.root == NULL);
            context.fail_compare_at = 0;
        }
        {
            uint64_t digest = UINT64_C(0xdeadbeef);
            context.fail_allocation_at = context.allocation_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_content_hash(&base, &digest),
                FT_STATUS_NO_MEMORY);
            REQUIRE(digest == UINT64_C(0xdeadbeef));
            context.fail_allocation_at = 0;
            REQUIRE_STATUS(ft_canonical_sorted_set_content_hash(&base, &digest), FT_STATUS_OK);
        }
        {
            bool valid = true;
            ft_canonical_sorted_set_statistics statistics = { 91, 92, 93, 94 };
            context.fail_rank_hash_at = context.rank_hash_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_validate(&base, &valid, &statistics),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(valid);
            REQUIRE(statistics.count == 91 && statistics.height == 92);
            context.fail_rank_hash_at = 0;
        }
        {
            const int right_keys[] = { 1000, 1001, 1002 };
            ft_canonical_sorted_set right;
            REQUIRE_STATUS(set_from_ints(&right, &policy, right_keys, 3), FT_STATUS_OK);
            context.fail_copy_at = context.copy_calls + 1;
            REQUIRE_STATUS(
                ft_canonical_sorted_set_union(&base, &right, &base),
                FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(ft_canonical_sorted_set_root_identity(&base) == original_root);
            REQUIRE(ft_canonical_sorted_set_size(&base) == count);
            context.fail_copy_at = 0;
            ft_canonical_sorted_set_dispose(&right);
        }

        ft_canonical_sorted_set_dispose(&base);
        ft_canonical_policy_dispose(&policy);
        REQUIRE(context.successful_copies == context.destroy_calls);
        REQUIRE(context.outstanding_allocations == 0);
    }

    {
        static relation_operation operations[] = {
            ft_canonical_sorted_set_equals,
            ft_canonical_sorted_set_is_subset,
            ft_canonical_sorted_set_is_proper_subset,
            ft_canonical_sorted_set_is_superset,
            ft_canonical_sorted_set_is_proper_superset,
            ft_canonical_sorted_set_overlaps
        };
        const int left_keys[] = { 1, 2, 4 };
        const int right_keys[] = { 2, 3, 5 };
        test_context left_context;
        test_context right_context;
        ft_canonical_policy_config left_config;
        ft_canonical_policy_config right_config;
        ft_canonical_policy left_policy;
        ft_canonical_policy right_policy;
        ft_canonical_sorted_set left;
        ft_canonical_sorted_set right;
        (void)memset(&left_context, 0, sizeof(left_context));
        (void)memset(&right_context, 0, sizeof(right_context));
        init_config(&left_config, &left_context);
        init_config(&right_config, &right_context);
        REQUIRE_STATUS(ft_canonical_policy_create_seeded(&left_policy, &left_config, 1), FT_STATUS_OK);
        REQUIRE_STATUS(ft_canonical_policy_create_seeded(&right_policy, &right_config, 2), FT_STATUS_OK);
        REQUIRE_STATUS(set_from_ints(&left, &left_policy, left_keys, 3), FT_STATUS_OK);
        REQUIRE_STATUS(set_from_ints(&right, &right_policy, right_keys, 3), FT_STATUS_OK);
        for (size_t index = 0; index != sizeof(operations) / sizeof(operations[0]); ++index) {
            bool output = true;
            left_context.fail_copy_at = left_context.copy_calls + 1;
            REQUIRE_STATUS(operations[index](&left, &right, &output), FT_STATUS_CALLBACK_FAILURE);
            REQUIRE(output);
            left_context.fail_copy_at = 0;
        }
        ft_canonical_sorted_set_dispose(&right);
        ft_canonical_sorted_set_dispose(&left);
        ft_canonical_policy_dispose(&right_policy);
        ft_canonical_policy_dispose(&left_policy);
        REQUIRE(left_context.outstanding_allocations == 0);
        REQUIRE(right_context.outstanding_allocations == 0);
    }
}

static ft_status plain_compare(
    const void* left,
    const void* right,
    int* comparison,
    void* context)
{
    const int left_key = ((const test_value*)left)->key;
    const int right_key = ((const test_value*)right)->key;
    (void)context;
    *comparison = (left_key > right_key) - (left_key < right_key);
    return FT_STATUS_OK;
}

static ft_status plain_rank_hash(const void* value, uint64_t* rank_hash, void* context)
{
    (void)context;
    *rank_hash = (uint64_t)(uint32_t)((const test_value*)value)->key;
    return FT_STATUS_OK;
}

static void test_cursor_rank_policy_and_persistent_edits(void)
{
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    ft_canonical_sorted_set set;
    ft_canonical_sorted_set snapshot;
    ft_canonical_sorted_set_cursor cursor;
    ft_canonical_sorted_set_cursor copy;
    test_value values[] = {
        { 30, 300, NULL },
        { 10, 100, NULL },
        { 20, 200, NULL }
    };
    test_value query = make_value(20, 999);
    test_value absent = make_value(25, 250);
    test_value inserted = make_value(25, 251);
    const void* value_ref = NULL;
    bool found = false;
    bool at_boundary = false;

    ft_canonical_policy_config_init(
        &config,
        sizeof(test_value),
        &g_test_value_type_identity,
        plain_compare,
        plain_rank_hash,
        NULL);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, 4815162342ULL), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&set, &policy, values, 3), FT_STATUS_OK);

    REQUIRE_STATUS(
        ft_canonical_sorted_set_get_cursor_at_item(&set, &query, &found, &cursor),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(ft_canonical_sorted_set_cursor_position(&cursor) == 1);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_try_peek_next_ref(&cursor, &found, &value_ref),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(((const test_value*)value_ref)->representative == 200);

    REQUIRE_STATUS(ft_canonical_sorted_set_cursor_copy(&cursor, &copy), FT_STATUS_OK);
    ft_canonical_sorted_set_dispose(&set);
    ft_canonical_policy_dispose(&policy);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_try_peek_next_ref(&copy, &found, &value_ref),
        FT_STATUS_OK);
    REQUIRE(found && ((const test_value*)value_ref)->key == 20);

    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_add(&cursor, &inserted, &cursor),
        FT_STATUS_OK);
    REQUIRE(ft_canonical_sorted_set_cursor_position(&cursor) == 3);
    REQUIRE(ft_canonical_sorted_set_cursor_size(&cursor) == 4);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_try_peek_previous_ref(&cursor, &found, &value_ref),
        FT_STATUS_OK);
    REQUIRE(found && ((const test_value*)value_ref)->representative == 251);
    REQUIRE_STATUS(ft_canonical_sorted_set_cursor_snapshot(&cursor, &snapshot), FT_STATUS_OK);
    REQUIRE(ft_canonical_sorted_set_size(&snapshot) == 4);

    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_delete_previous(&cursor, &cursor),
        FT_STATUS_OK);
    REQUIRE(ft_canonical_sorted_set_cursor_position(&cursor) == 2);
    REQUIRE(ft_canonical_sorted_set_cursor_size(&cursor) == 3);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_try_peek_next_ref(&cursor, &found, &value_ref),
        FT_STATUS_OK);
    REQUIRE(found && ((const test_value*)value_ref)->key == 30);

    ft_canonical_sorted_set_cursor_dispose(&cursor);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_get_cursor_at_item(&snapshot, &absent, &found, &cursor),
        FT_STATUS_OK);
    REQUIRE(found);
    REQUIRE(ft_canonical_sorted_set_cursor_position(&cursor) == 2);
    REQUIRE_STATUS(ft_canonical_sorted_set_cursor_seek_rank(&cursor, 0, &cursor), FT_STATUS_OK);
    REQUIRE_STATUS(ft_canonical_sorted_set_cursor_is_at_start(&cursor, &at_boundary), FT_STATUS_OK);
    REQUIRE(at_boundary);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_move_previous(&cursor, &cursor),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_canonical_sorted_set_cursor_seek_rank(&cursor, 5, &cursor),
        FT_STATUS_OUT_OF_RANGE);

    ft_canonical_sorted_set_cursor_dispose(&copy);
    ft_canonical_sorted_set_cursor_dispose(&cursor);
    ft_canonical_sorted_set_dispose(&snapshot);
}

typedef struct concurrent_context {
    const ft_canonical_sorted_set* set;
    bool success;
    uint64_t digest;
} concurrent_context;

static void concurrent_worker(concurrent_context* context)
{
    context->success = true;
    context->digest = 0;
    for (int iteration = 0; iteration != 128; ++iteration) {
        test_value probe = make_value((iteration * 73) % 10000, iteration);
        bool found = false;
        uint64_t digest = 0;
        ft_canonical_sorted_set copy;
        if (ft_canonical_sorted_set_content_hash(context->set, &digest) != FT_STATUS_OK ||
            ft_canonical_sorted_set_contains(context->set, &probe, &found) != FT_STATUS_OK || !found ||
            ft_canonical_sorted_set_copy(context->set, &copy) != FT_STATUS_OK) {
            context->success = false;
            return;
        }
        ft_canonical_sorted_set_dispose(&copy);
        if (iteration == 0) {
            context->digest = digest;
        } else if (context->digest != digest) {
            context->success = false;
            return;
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#elif defined(CANONICAL_TEST_HAS_THREADS)
static int concurrent_thread(void* parameter)
{
    concurrent_worker((concurrent_context*)parameter);
    return 0;
}
#endif

static void test_concurrent_digest_copy_and_readers(void)
{
    enum { count = 10000, thread_count = 8 };
    ft_canonical_policy_config config;
    ft_canonical_policy policy;
    ft_canonical_sorted_set set;
    test_value* values = (test_value*)malloc(count * sizeof(*values));
    concurrent_context contexts[thread_count];
    REQUIRE(values != NULL);
    ft_canonical_policy_config_init(
        &config,
        sizeof(test_value),
        &g_test_value_type_identity,
        plain_compare,
        plain_rank_hash,
        NULL);
    REQUIRE_STATUS(ft_canonical_policy_create_seeded(&policy, &config, 31337), FT_STATUS_OK);
    for (int index = 0; index != count; ++index) {
        values[index] = make_value(index, index);
    }
    REQUIRE_STATUS(ft_canonical_sorted_set_from_array(&set, &policy, values, count), FT_STATUS_OK);
    for (int index = 0; index != thread_count; ++index) {
        contexts[index].set = &set;
        contexts[index].success = false;
        contexts[index].digest = 0;
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
            CloseHandle(threads[index]);
        }
    }
#elif defined(CANONICAL_TEST_HAS_THREADS)
    {
        thrd_t threads[thread_count];
        for (int index = 0; index != thread_count; ++index) {
            REQUIRE(thrd_create(&threads[index], concurrent_thread, &contexts[index]) == thrd_success);
        }
        for (int index = 0; index != thread_count; ++index) {
            REQUIRE(thrd_join(threads[index], NULL) == thrd_success);
        }
    }
#else
    for (int index = 0; index != thread_count; ++index) {
        concurrent_worker(&contexts[index]);
    }
#endif
    for (int index = 0; index != thread_count; ++index) {
        REQUIRE(contexts[index].success);
        REQUIRE(contexts[index].digest == contexts[0].digest);
    }
    ft_canonical_sorted_set_dispose(&set);
    ft_canonical_policy_dispose(&policy);
    free(values);
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
    run_test("canonical crypto vectors and unsigned priority", test_crypto_vectors_and_priority);
    run_test("canonical topology and representatives", test_canonical_topology_and_representatives);
    run_test("canonical deep collisions and lifecycle", test_deep_collisions_and_stack_safe_lifecycle);
    run_test("canonical interior removal merge seam", test_interior_removal_merge_seam);
    run_test("canonical randomized histories and snapshots", test_randomized_histories_and_snapshots);
    run_test("canonical algebra relations aliasing and sharing", test_algebra_relations_aliasing_and_sharing);
    run_test("canonical allocation and callback atomicity", test_allocation_and_callback_failure_atomicity);
    run_test("canonical cursor rank policy and persistent edits", test_cursor_rank_policy_and_persistent_edits);
    run_test("canonical concurrent digest copy and readers", test_concurrent_digest_copy_and_readers);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C canonical sorted-set tests passed\n");
    return EXIT_SUCCESS;
}
