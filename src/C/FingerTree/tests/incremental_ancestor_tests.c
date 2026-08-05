/*
 * Tests for the append-only incremental level-ancestor arena.
 *
 * The obligations mirror the C# suite: model equivalence against a naive parent walk over randomized
 * branching histories, retained-version independence after branching, exact odd-block capacity at
 * the square boundaries, a conservative logarithmic hop-count envelope, boundary and empty cases,
 * failure atomicity under failing callbacks and allocations, and the operation-count guardrails the
 * statistics counters exist to support.
 */

#include <durable7/finger_tree/incremental_ancestor.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static const unsigned char g_value_type_identity = 0;

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

/* A cleanup-aware failure that runs the caller's teardown label instead of returning. */
#define REQUIRE_GOTO(expression, label) \
    do { \
        if (!(expression)) { \
            fail_at(__FILE__, __LINE__, #expression); \
            goto label; \
        } \
    } while (0)

typedef struct test_value {
    int value;
} test_value;

typedef struct test_context {
    size_t allocation_calls;
    size_t outstanding_allocations;
    size_t fail_allocation_at;
    size_t copy_calls;
    size_t live_values;
    size_t fail_copy_at;
} test_context;

static void* tracked_allocate(size_t size, void* context)
{
    test_context* state = (test_context*)context;
    void* allocation = NULL;
    ++state->allocation_calls;
    if (state->fail_allocation_at != 0 && state->allocation_calls == state->fail_allocation_at) {
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
    ++state->live_values;
    return FT_STATUS_OK;
}

static void tracked_destroy(void* value, void* context)
{
    test_context* state = (test_context*)context;
    (void)value;
    --state->live_values;
}

static void init_config(ft_incremental_ancestor_policy_config* config, test_context* context)
{
    ft_incremental_ancestor_policy_config_init(
        config,
        sizeof(test_value),
        &g_value_type_identity,
        context);
    config->copy = tracked_copy;
    config->destroy = tracked_destroy;
    config->allocator.allocate = tracked_allocate;
    config->allocator.deallocate = tracked_deallocate;
    config->allocator.context = context;
}

static test_value make_value(int value)
{
    test_value result;
    result.value = value;
    return result;
}

/* A deterministic xorshift generator; the tests must not depend on the platform's rand(). */
typedef struct xorshift {
    uint64_t state;
} xorshift;

static void xorshift_init(xorshift* random, uint64_t seed)
{
    random->state = seed | 1u;
}

static size_t xorshift_below(xorshift* random, size_t bound)
{
    random->state ^= random->state << 13;
    random->state ^= random->state >> 7;
    random->state ^= random->state << 17;
    return (size_t)(random->state % (uint64_t)bound);
}

static size_t ceiling_log2(size_t value)
{
    size_t power = 1;
    size_t result = 0;
    while (power < value) {
        power <<= 1;
        ++result;
    }
    return result;
}

/* Runs one ancestor query and reports the hops it actually needed.
 *
 * The hop count is read as the delta of total_ancestor_hop_count, which is maintained independently
 * of last_ancestor_hop_count, so the equality checked here pins the latter to the work this one
 * query performed. */
static bool query_with_hops(
    const ft_incremental_ancestor_arena* arena,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor,
    size_t* hops)
{
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    if (ft_incremental_ancestor_myers_arena_get_statistics(arena, &before) != FT_STATUS_OK) {
        return false;
    }
    if (ft_incremental_ancestor_arena_ancestor_at_depth(arena, node, depth, ancestor) !=
        FT_STATUS_OK) {
        return false;
    }
    if (ft_incremental_ancestor_myers_arena_get_statistics(arena, &after) != FT_STATUS_OK) {
        return false;
    }
    if (after.ancestor_query_count != before.ancestor_query_count + 1) {
        return false;
    }
    *hops = (size_t)(after.total_ancestor_hop_count - before.total_ancestor_hop_count);
    return after.last_ancestor_hop_count == *hops && after.maximum_ancestor_hop_count >= *hops;
}

static bool statistics_equal(
    const ft_incremental_ancestor_myers_statistics* left,
    const ft_incremental_ancestor_myers_statistics* right)
{
    /* Compared field by field: the record's padding bytes are indeterminate. */
    return left->published_node_count == right->published_node_count &&
        left->block_count == right->block_count &&
        left->allocated_slot_count == right->allocated_slot_count &&
        left->add_leaf_count == right->add_leaf_count &&
        left->ancestor_query_count == right->ancestor_query_count &&
        left->last_ancestor_hop_count == right->last_ancestor_hop_count &&
        left->maximum_ancestor_hop_count == right->maximum_ancestor_hop_count &&
        left->total_ancestor_hop_count == right->total_ancestor_hop_count;
}

static void test_empty_arena_holds_only_its_unlabelled_bottom_node(void)
{
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics statistics;
    ft_incremental_ancestor_node bottom = 0;
    ft_incremental_ancestor_node ancestor = 0;
    ft_incremental_ancestor_node parent = 0;
    ft_incremental_ancestor_depth depth = 0;
    const void* value_ref = NULL;
    size_t count = 1;
    test_value copy;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);

    REQUIRE_STATUS(ft_incremental_ancestor_arena_bottom(&arena, &bottom), FT_STATUS_OK);
    REQUIRE(bottom == FT_INCREMENTAL_ANCESTOR_BOTTOM);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &count),
        FT_STATUS_OK);
    REQUIRE(count == 0);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, bottom, &depth), FT_STATUS_OK);
    REQUIRE(depth == FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, bottom, -1, &ancestor),
        FT_STATUS_OK);
    REQUIRE(ancestor == bottom);

    /* The bottom node has neither a parent nor a value, and no other handle exists yet. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_parent(&arena, bottom, &parent),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_value_ref(&arena, bottom, &value_ref),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_value_copy(&arena, bottom, &copy),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, 7, &depth), FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_depth(&arena, SIZE_MAX, &depth),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, bottom, -2, &ancestor),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, bottom, 0, &ancestor),
        FT_STATUS_OUT_OF_RANGE);

    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics),
        FT_STATUS_OK);
    REQUIRE(statistics.published_node_count == 0);
    REQUIRE(statistics.block_count == 1);
    REQUIRE(statistics.allocated_slot_count == 1);
    REQUIRE(statistics.add_leaf_count == 0);
    REQUIRE(statistics.ancestor_query_count == 1);
    REQUIRE(statistics.total_ancestor_hop_count == 0);
    REQUIRE(statistics.maximum_ancestor_hop_count == 0);
    REQUIRE(statistics.last_ancestor_hop_count == 0);

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_singleton_leaf_hangs_directly_below_bottom(void)
{
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_node leaf = 0;
    ft_incremental_ancestor_node ancestor = 0;
    ft_incremental_ancestor_node parent = 0;
    ft_incremental_ancestor_depth depth = 0;
    const void* value_ref = NULL;
    const test_value payload = make_value(42);
    size_t count = 0;
    test_value copy;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(
            &arena,
            FT_INCREMENTAL_ANCESTOR_BOTTOM,
            &payload,
            &leaf),
        FT_STATUS_OK);

    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &count),
        FT_STATUS_OK);
    REQUIRE(count == 1);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, leaf, &depth), FT_STATUS_OK);
    REQUIRE(depth == 0);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_parent(&arena, leaf, &parent), FT_STATUS_OK);
    REQUIRE(parent == FT_INCREMENTAL_ANCESTOR_BOTTOM);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_value_ref(&arena, leaf, &value_ref), FT_STATUS_OK);
    REQUIRE(((const test_value*)value_ref)->value == 42);

    /* The borrow points into the arena; the copy is independently owned. */
    REQUIRE_STATUS(ft_incremental_ancestor_arena_value_copy(&arena, leaf, &copy), FT_STATUS_OK);
    REQUIRE(copy.value == 42);
    REQUIRE((const void*)&copy != value_ref);
    tracked_destroy(&copy, &context);

    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, leaf, 0, &ancestor),
        FT_STATUS_OK);
    REQUIRE(ancestor == leaf);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, leaf, -1, &ancestor),
        FT_STATUS_OK);
    REQUIRE(ancestor == FT_INCREMENTAL_ANCESTOR_BOTTOM);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, leaf, 1, &ancestor),
        FT_STATUS_OUT_OF_RANGE);

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* Compares level-ancestor answers on a deep, irregularly branched tree with a naive parent walk. */
static void test_branched_ancestor_queries_match_a_naive_parent_walk(void)
{
    enum { chain_length = 512, branch_count = 512, node_count = chain_length + branch_count };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics statistics;
    xorshift random;
    ft_incremental_ancestor_node* nodes = NULL;
    ft_incremental_ancestor_node* model_parent = NULL;
    ft_incremental_ancestor_depth* model_depth = NULL;
    int* model_value = NULL;
    uint64_t expected_query_count = 0;
    size_t published = 0;
    size_t index = 0;
    int step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    xorshift_init(&random, 0xA11CE);

    nodes = (ft_incremental_ancestor_node*)malloc((node_count + 1) * sizeof(*nodes));
    model_parent = (ft_incremental_ancestor_node*)malloc((node_count + 1) * sizeof(*model_parent));
    model_depth = (ft_incremental_ancestor_depth*)malloc((node_count + 1) * sizeof(*model_depth));
    model_value = (int*)malloc((node_count + 1) * sizeof(*model_value));
    REQUIRE_GOTO(
        nodes != NULL && model_parent != NULL && model_depth != NULL && model_value != NULL,
        cleanup);
    nodes[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    model_parent[FT_INCREMENTAL_ANCESTOR_BOTTOM] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    model_depth[FT_INCREMENTAL_ANCESTOR_BOTTOM] = FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH;
    model_value[FT_INCREMENTAL_ANCESTOR_BOTTOM] = 0;
    published = 1;

    {
        ft_incremental_ancestor_node parent = FT_INCREMENTAL_ANCESTOR_BOTTOM;
        for (step = 0; step != chain_length; ++step) {
            const test_value payload = make_value(step);
            ft_incremental_ancestor_node node = 0;
            REQUIRE_GOTO(
                ft_incremental_ancestor_arena_add_leaf(&arena, parent, &payload, &node) ==
                    FT_STATUS_OK,
                cleanup);
            nodes[published++] = node;
            model_parent[node] = parent;
            model_depth[node] = model_depth[parent] + 1;
            model_value[node] = step;
            parent = node;
        }
    }
    for (step = 0; step != branch_count; ++step) {
        const ft_incremental_ancestor_node parent = nodes[xorshift_below(&random, published)];
        const test_value payload = make_value(chain_length + step);
        ft_incremental_ancestor_node node = 0;
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_add_leaf(&arena, parent, &payload, &node) ==
                FT_STATUS_OK,
            cleanup);
        nodes[published++] = node;
        model_parent[node] = parent;
        model_depth[node] = model_depth[parent] + 1;
        model_value[node] = chain_length + step;
    }
    REQUIRE_GOTO(published == (size_t)node_count + 1, cleanup);

    {
        size_t count = 0;
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_published_node_count(&arena, &count) == FT_STATUS_OK &&
                count == (size_t)node_count,
            cleanup);
    }

    for (index = 1; index != published; ++index) {
        const ft_incremental_ancestor_node node = nodes[index];
        ft_incremental_ancestor_node expected_ancestor = node;
        ft_incremental_ancestor_depth target = model_depth[node];
        ft_incremental_ancestor_node observed = 0;
        ft_incremental_ancestor_depth depth = 0;
        const void* value_ref = NULL;

        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_depth(&arena, node, &depth) == FT_STATUS_OK &&
                depth == model_depth[node],
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_parent(&arena, node, &observed) == FT_STATUS_OK &&
                observed == model_parent[node],
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_value_ref(&arena, node, &value_ref) == FT_STATUS_OK &&
                ((const test_value*)value_ref)->value == model_value[node],
            cleanup);

        /* Naive oracle: walk parent links one level at a time. */
        while (target >= FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH) {
            REQUIRE_GOTO(
                ft_incremental_ancestor_arena_ancestor_at_depth(&arena, node, target, &observed) ==
                        FT_STATUS_OK &&
                    observed == expected_ancestor,
                cleanup);
            ++expected_query_count;
            if (target >= 0) {
                expected_ancestor = model_parent[expected_ancestor];
            }
            --target;
        }
    }

    REQUIRE_GOTO(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics) == FT_STATUS_OK,
        cleanup);
    REQUIRE_GOTO(statistics.ancestor_query_count == expected_query_count, cleanup);
    REQUIRE_GOTO(statistics.add_leaf_count == (uint64_t)node_count, cleanup);

cleanup:
    free(nodes);
    free(model_parent);
    free(model_depth);
    free(model_value);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* Verifies the odd-block directory allocates exactly at the labelled-node square boundaries. */
static void test_odd_block_statistics_match_the_exact_square_layout(void)
{
    enum { maximum_published_count = 4096 };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_node parent = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    size_t published_count = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);

    for (published_count = 0; published_count <= (size_t)maximum_published_count;
         ++published_count) {
        ft_incremental_ancestor_myers_statistics statistics;
        size_t root = 0;
        size_t expected_block_count = 0;
        size_t observed = 0;

        while ((root + 1) * (root + 1) <= published_count) {
            ++root;
        }
        expected_block_count = root + 1;

        REQUIRE_STATUS(
            ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics),
            FT_STATUS_OK);
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_published_node_count(&arena, &observed),
            FT_STATUS_OK);
        REQUIRE(observed == published_count);
        REQUIRE(statistics.published_node_count == published_count);
        REQUIRE(statistics.block_count == expected_block_count);
        REQUIRE(statistics.allocated_slot_count ==
            (uint64_t)expected_block_count * (uint64_t)expected_block_count);
        REQUIRE(statistics.add_leaf_count == (uint64_t)published_count);
        REQUIRE(statistics.ancestor_query_count == 0);
        REQUIRE(statistics.total_ancestor_hop_count == 0);

        if (published_count != (size_t)maximum_published_count) {
            const test_value payload = make_value((int)published_count);
            REQUIRE_STATUS(
                ft_incremental_ancestor_arena_add_leaf(&arena, parent, &payload, &parent),
                FT_STATUS_OK);
        }
    }

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* Guards the Myers backend against degenerating into a linear ancestor walk. */
static void test_deep_chain_queries_use_conservatively_logarithmic_hops(void)
{
    enum { node_count = 32768 };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics statistics;
    ft_incremental_ancestor_node* nodes_by_depth = NULL;
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    size_t bound = 0;
    int step = 0;
    ft_incremental_ancestor_depth target = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);

    nodes_by_depth =
        (ft_incremental_ancestor_node*)malloc((node_count + 1) * sizeof(*nodes_by_depth));
    REQUIRE_GOTO(nodes_by_depth != NULL, cleanup);
    nodes_by_depth[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    for (step = 0; step != node_count; ++step) {
        const test_value payload = make_value(step);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_add_leaf(&arena, tail, &payload, &tail) == FT_STATUS_OK,
            cleanup);
        nodes_by_depth[step + 1] = tail;
    }

    REQUIRE_GOTO(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics) == FT_STATUS_OK,
        cleanup);
    REQUIRE_GOTO(statistics.add_leaf_count == (uint64_t)node_count, cleanup);
    REQUIRE_GOTO(statistics.ancestor_query_count == 0, cleanup);
    REQUIRE_GOTO(statistics.maximum_ancestor_hop_count == 0, cleanup);
    REQUIRE_GOTO(statistics.total_ancestor_hop_count == 0, cleanup);

    for (target = node_count - 1; target >= FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH; --target) {
        ft_incremental_ancestor_node ancestor = 0;
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_ancestor_at_depth(&arena, tail, target, &ancestor) ==
                    FT_STATUS_OK &&
                ancestor == nodes_by_depth[(size_t)(target + 1)],
            cleanup);
    }

    REQUIRE_GOTO(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics) == FT_STATUS_OK,
        cleanup);
    bound = 4 * ceiling_log2((size_t)node_count + 1);
    REQUIRE_GOTO(statistics.ancestor_query_count == (uint64_t)node_count + 1, cleanup);
    REQUIRE_GOTO(statistics.maximum_ancestor_hop_count >= 1, cleanup);
    REQUIRE_GOTO(statistics.maximum_ancestor_hop_count <= bound, cleanup);
    REQUIRE_GOTO(statistics.total_ancestor_hop_count >= 1, cleanup);
    REQUIRE_GOTO(
        statistics.total_ancestor_hop_count <= ((uint64_t)node_count + 1) * (uint64_t)bound,
        cleanup);

cleanup:
    free(nodes_by_depth);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* An irregular branching arena: most additions extend the current frontier, the rest hang a fresh
 * branch off an arbitrary older node, so jump links coalesce over sibling branches instead of over
 * one straight chain. */
static void test_branched_queries_use_conservatively_logarithmic_hops(void)
{
    enum { chain_length = 64, branch_count = 1984, node_count = chain_length + branch_count };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics statistics;
    xorshift random;
    ft_incremental_ancestor_node* nodes = NULL;
    ft_incremental_ancestor_node* model_parent = NULL;
    ft_incremental_ancestor_depth* model_depth = NULL;
    ft_incremental_ancestor_node frontier = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_depth maximum_depth = FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH;
    size_t published = 0;
    size_t branch_point_count = 0;
    size_t index = 0;
    int step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    xorshift_init(&random, 0x5EED1234);

    nodes = (ft_incremental_ancestor_node*)malloc((node_count + 1) * sizeof(*nodes));
    model_parent = (ft_incremental_ancestor_node*)malloc((node_count + 1) * sizeof(*model_parent));
    model_depth = (ft_incremental_ancestor_depth*)malloc((node_count + 1) * sizeof(*model_depth));
    REQUIRE_GOTO(nodes != NULL && model_parent != NULL && model_depth != NULL, cleanup);
    nodes[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    model_parent[FT_INCREMENTAL_ANCESTOR_BOTTOM] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    model_depth[FT_INCREMENTAL_ANCESTOR_BOTTOM] = FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH;
    published = 1;

    for (step = 0; step != node_count; ++step) {
        const test_value payload = make_value(step);
        ft_incremental_ancestor_node parent = frontier;
        ft_incremental_ancestor_node node = 0;
        if (step >= chain_length && xorshift_below(&random, 8) == 0) {
            parent = nodes[xorshift_below(&random, published)];
            ++branch_point_count;
        }
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_add_leaf(&arena, parent, &payload, &node) ==
                FT_STATUS_OK,
            cleanup);
        nodes[published++] = node;
        model_parent[node] = parent;
        model_depth[node] = model_depth[parent] + 1;
        frontier = node;
    }
    REQUIRE_GOTO(branch_point_count > (size_t)branch_count / 16, cleanup);

    for (index = 1; index != published; ++index) {
        const ft_incremental_ancestor_node node = nodes[index];
        const ft_incremental_ancestor_depth node_depth = model_depth[node];
        /* Conservative envelope: logarithmic in this node's own depth, generously padded so the
         * test pins the asymptotic shape rather than the exact coalescing schedule. */
        const size_t envelope = 4 * ceiling_log2((size_t)node_depth + 2);
        ft_incremental_ancestor_node expected_ancestor = node;
        ft_incremental_ancestor_depth target = node_depth;
        ft_incremental_ancestor_depth depth = 0;

        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_depth(&arena, node, &depth) == FT_STATUS_OK &&
                depth == node_depth,
            cleanup);
        if (node_depth > maximum_depth) {
            maximum_depth = node_depth;
        }

        while (target >= FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH) {
            ft_incremental_ancestor_node observed = 0;
            size_t hops = 0;
            REQUIRE_GOTO(query_with_hops(&arena, node, target, &observed, &hops), cleanup);
            REQUIRE_GOTO(observed == expected_ancestor, cleanup);
            REQUIRE_GOTO(hops <= envelope, cleanup);
            if (target >= 0) {
                expected_ancestor = model_parent[expected_ancestor];
            }
            --target;
        }
    }

    REQUIRE_GOTO(maximum_depth >= (ft_incremental_ancestor_depth)chain_length, cleanup);
    REQUIRE_GOTO(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics) == FT_STATUS_OK,
        cleanup);
    REQUIRE_GOTO(
        statistics.maximum_ancestor_hop_count <= 4 * ceiling_log2((size_t)maximum_depth + 2),
        cleanup);

cleanup:
    free(nodes);
    free(model_parent);
    free(model_depth);
    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* The per-query counters must describe the most recent query rather than a running sum. */
static void test_the_last_hop_count_reports_only_the_most_recent_query(void)
{
    enum { chain_length = 1024 };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics statistics;
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_node ancestor = 0;
    ft_incremental_ancestor_node parent = 0;
    ft_incremental_ancestor_depth tail_depth = 0;
    size_t self_hops = 0;
    size_t parent_hops = 0;
    size_t bottom_hops = 0;
    size_t repeated_hops = 0;
    int step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    for (step = 0; step != chain_length; ++step) {
        const test_value payload = make_value(step);
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_add_leaf(&arena, tail, &payload, &tail),
            FT_STATUS_OK);
    }
    REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, tail, &tail_depth), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics),
        FT_STATUS_OK);
    REQUIRE(statistics.last_ancestor_hop_count == 0);

    /* A query for the node itself terminates before the first hop. */
    REQUIRE(query_with_hops(&arena, tail, tail_depth, &ancestor, &self_hops));
    REQUIRE(ancestor == tail);
    REQUIRE(self_hops == 0);

    /* A query for the immediate parent needs exactly one hop: the coalesced jump link of a node at
     * depth d covers more than one level, so only the parent link can be taken. */
    REQUIRE(query_with_hops(&arena, tail, tail_depth - 1, &ancestor, &parent_hops));
    REQUIRE_STATUS(ft_incremental_ancestor_arena_parent(&arena, tail, &parent), FT_STATUS_OK);
    REQUIRE(ancestor == parent);
    REQUIRE(parent_hops == 1);

    /* A full walk to the bottom node costs more than one hop. */
    REQUIRE(query_with_hops(&arena, tail, -1, &ancestor, &bottom_hops));
    REQUIRE(ancestor == FT_INCREMENTAL_ANCESTOR_BOTTOM);
    REQUIRE(bottom_hops > 1);

    /* Repeating the zero-hop query drives the counter back down. */
    REQUIRE(query_with_hops(&arena, tail, tail_depth, &ancestor, &repeated_hops));
    REQUIRE(ancestor == tail);
    REQUIRE(repeated_hops == 0);

    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_get_statistics(&arena, &statistics),
        FT_STATUS_OK);
    REQUIRE(statistics.last_ancestor_hop_count == 0);
    REQUIRE(statistics.maximum_ancestor_hop_count == bottom_hops);
    REQUIRE(statistics.total_ancestor_hop_count ==
        (uint64_t)(self_hops + parent_hops + bottom_hops + repeated_hops));
    REQUIRE(statistics.ancestor_query_count == 4);
    REQUIRE(statistics.add_leaf_count == (uint64_t)chain_length);

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* Published nodes stay immutable when old handles grow new branches. */
static void test_retained_versions_stay_independent_after_branching(void)
{
    enum { chain_length = 64, branch_length = 40, fork_index = 10 };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_node chain[chain_length + 1];
    ft_incremental_ancestor_node fork = 0;
    ft_incremental_ancestor_node branch_tail = 0;
    ft_incremental_ancestor_node ancestor = 0;
    size_t index = 0;
    size_t count = 0;
    int step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);

    chain[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    for (step = 0; step != chain_length; ++step) {
        const test_value payload = make_value(step);
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_add_leaf(
                &arena,
                chain[step],
                &payload,
                &chain[step + 1]),
            FT_STATUS_OK);
    }

    fork = chain[fork_index];
    branch_tail = fork;
    for (step = 0; step != branch_length; ++step) {
        const test_value payload = make_value(100 + step);
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_add_leaf(&arena, branch_tail, &payload, &branch_tail),
            FT_STATUS_OK);
    }

    /* The retained original chain answers exactly as it did before the branch existed. */
    for (index = 1; index <= (size_t)chain_length; ++index) {
        const ft_incremental_ancestor_node node = chain[index];
        ft_incremental_ancestor_depth depth = 0;
        ft_incremental_ancestor_node parent = 0;
        ft_incremental_ancestor_depth target = 0;
        const void* value_ref = NULL;

        REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, node, &depth), FT_STATUS_OK);
        REQUIRE(depth == (ft_incremental_ancestor_depth)index - 1);
        REQUIRE_STATUS(ft_incremental_ancestor_arena_parent(&arena, node, &parent), FT_STATUS_OK);
        REQUIRE(parent == chain[index - 1]);
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_value_ref(&arena, node, &value_ref),
            FT_STATUS_OK);
        REQUIRE(((const test_value*)value_ref)->value == (int)index - 1);
        for (target = -1; target <= depth; ++target) {
            REQUIRE_STATUS(
                ft_incremental_ancestor_arena_ancestor_at_depth(&arena, node, target, &ancestor),
                FT_STATUS_OK);
            REQUIRE(ancestor == chain[(size_t)(target + 1)]);
        }
    }

    /* The branch shares its prefix with the chain and diverges after the fork. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, branch_tail, 9, &ancestor),
        FT_STATUS_OK);
    REQUIRE(ancestor == fork);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, branch_tail, -1, &ancestor),
        FT_STATUS_OK);
    REQUIRE(ancestor == FT_INCREMENTAL_ANCESTOR_BOTTOM);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, branch_tail, 10, &ancestor),
        FT_STATUS_OK);
    REQUIRE(ancestor != chain[12]);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &count),
        FT_STATUS_OK);
    REQUIRE(count == (size_t)chain_length + branch_length);

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* A rejected addition or query must leave the arena, its counters, and its payload ownership
 * exactly as they were. */
static void test_failure_atomicity_and_lifetimes(void)
{
    enum { chain_length = 8 };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_myers_statistics before;
    ft_incremental_ancestor_myers_statistics after;
    ft_incremental_ancestor_node tail = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    ft_incremental_ancestor_node node = 0;
    ft_incremental_ancestor_depth depth = 0;
    const test_value payload = make_value(-1);
    size_t live_values = 0;
    size_t outstanding = 0;
    size_t count = 0;
    int step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    for (step = 0; step != chain_length; ++step) {
        const test_value item = make_value(step);
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_add_leaf(&arena, tail, &item, &tail),
            FT_STATUS_OK);
    }
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, tail, 3, &node),
        FT_STATUS_OK);

    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_get_statistics(&arena, &before), FT_STATUS_OK);
    live_values = context.live_values;
    outstanding = context.outstanding_allocations;
    /* Eight labelled nodes fill blocks 0..2 exactly, so the next addition must open block 3. That is
     * what makes the second injected allocation failure below land on the block rather than on the
     * value copy. */
    REQUIRE(before.block_count == 3);
    REQUIRE(before.allocated_slot_count == 9);

    /* Handles this arena never published, and depths outside the node's own range. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, SIZE_MAX, &payload, &node),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, 9999, &payload, &node),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, tail, -2, &node),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_ancestor_at_depth(&arena, tail, chain_length, &node),
        FT_STATUS_OUT_OF_RANGE);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, tail, NULL, &node),
        FT_STATUS_INVALID_ARGUMENT);

    /* A failing copy callback publishes nothing and leaks nothing. */
    context.fail_copy_at = context.copy_calls + 1;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, tail, &payload, &node),
        FT_STATUS_CALLBACK_FAILURE);
    context.fail_copy_at = 0;

    /* A failing value allocation, then a failing block allocation after the value was copied: the
     * ninth labelled node opens a new odd block, so the second allocation is the block itself. */
    context.fail_allocation_at = context.allocation_calls + 1;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, tail, &payload, &node),
        FT_STATUS_NO_MEMORY);
    context.fail_allocation_at = context.allocation_calls + 2;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, tail, &payload, &node),
        FT_STATUS_NO_MEMORY);
    context.fail_allocation_at = 0;

    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after), FT_STATUS_OK);
    REQUIRE(statistics_equal(&before, &after));
    REQUIRE(context.live_values == live_values);
    REQUIRE(context.outstanding_allocations == outstanding);

    /* The arena is still fully usable after the rejected calls. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, tail, &payload, &node),
        FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, node, &depth), FT_STATUS_OK);
    REQUIRE(depth == chain_length);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &count),
        FT_STATUS_OK);
    REQUIRE(count == (size_t)chain_length + 1);
    REQUIRE(context.live_values == live_values + 1);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_get_statistics(&arena, &after), FT_STATUS_OK);
    REQUIRE(after.block_count == 4);
    REQUIRE(after.allocated_slot_count == 16);

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

/* A minimal contract-faithful backend: one parent array plus a naive ancestor walk. Additions are
 * O(1) amortized and queries are O(depth), which the seam permits; a consumer must not depend on
 * anything faster. */
typedef struct parent_array_backend {
    ft_incremental_ancestor_node* parents;
    ft_incremental_ancestor_depth* depths;
    test_value* values;
    size_t count;
    size_t capacity;
    uint64_t add_leaf_count;
    uint64_t ancestor_query_count;
} parent_array_backend;

static ft_status parent_array_bottom(void* backend, ft_incremental_ancestor_node* bottom)
{
    (void)backend;
    *bottom = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    return FT_STATUS_OK;
}

static ft_status parent_array_published_node_count(void* backend, size_t* count)
{
    *count = ((const parent_array_backend*)backend)->count - 1;
    return FT_STATUS_OK;
}

static ft_status parent_array_add_leaf(
    void* backend,
    ft_incremental_ancestor_node parent,
    const void* value,
    ft_incremental_ancestor_node* leaf)
{
    parent_array_backend* arena = (parent_array_backend*)backend;
    if (parent >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (arena->count == arena->capacity) {
        return FT_STATUS_NO_MEMORY;
    }
    arena->parents[arena->count] = parent;
    arena->depths[arena->count] = arena->depths[parent] + 1;
    arena->values[arena->count] = *(const test_value*)value;
    *leaf = arena->count;
    arena->count = arena->count + 1;
    ++arena->add_leaf_count;
    return FT_STATUS_OK;
}

static ft_status parent_array_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth* depth)
{
    const parent_array_backend* arena = (const parent_array_backend*)backend;
    if (node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *depth = arena->depths[node];
    return FT_STATUS_OK;
}

static ft_status parent_array_parent(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_node* parent)
{
    const parent_array_backend* arena = (const parent_array_backend*)backend;
    if (node == FT_INCREMENTAL_ANCESTOR_BOTTOM || node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *parent = arena->parents[node];
    return FT_STATUS_OK;
}

static ft_status parent_array_ancestor_at_depth(
    void* backend,
    ft_incremental_ancestor_node node,
    ft_incremental_ancestor_depth depth,
    ft_incremental_ancestor_node* ancestor)
{
    parent_array_backend* arena = (parent_array_backend*)backend;
    if (node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (depth < FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH || depth > arena->depths[node]) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    while (arena->depths[node] > depth) {
        node = arena->parents[node];
    }
    ++arena->ancestor_query_count;
    *ancestor = node;
    return FT_STATUS_OK;
}

static ft_status parent_array_value_ref(
    void* backend,
    ft_incremental_ancestor_node node,
    const void** value_ref)
{
    const parent_array_backend* arena = (const parent_array_backend*)backend;
    if (node == FT_INCREMENTAL_ANCESTOR_BOTTOM || node >= arena->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *value_ref = &arena->values[node];
    return FT_STATUS_OK;
}

static void parent_array_destroy(void* backend)
{
    parent_array_backend* arena = (parent_array_backend*)backend;
    if (arena == NULL) {
        return;
    }
    free(arena->parents);
    free(arena->depths);
    free(arena->values);
    free(arena);
}

static const ft_incremental_ancestor_arena_vtable parent_array_vtable = {
    parent_array_bottom,
    parent_array_published_node_count,
    parent_array_add_leaf,
    parent_array_depth,
    parent_array_parent,
    parent_array_ancestor_at_depth,
    parent_array_value_ref,
    parent_array_destroy
};

static parent_array_backend* parent_array_create(size_t capacity)
{
    parent_array_backend* arena = (parent_array_backend*)malloc(sizeof(*arena));
    if (arena == NULL) {
        return NULL;
    }
    (void)memset(arena, 0, sizeof(*arena));
    arena->parents = (ft_incremental_ancestor_node*)malloc(capacity * sizeof(*arena->parents));
    arena->depths = (ft_incremental_ancestor_depth*)malloc(capacity * sizeof(*arena->depths));
    arena->values = (test_value*)malloc(capacity * sizeof(*arena->values));
    if (arena->parents == NULL || arena->depths == NULL || arena->values == NULL) {
        parent_array_destroy(arena);
        return NULL;
    }
    arena->capacity = capacity;
    arena->parents[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    arena->depths[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM_DEPTH;
    arena->values[0] = make_value(0);
    arena->count = 1;
    return arena;
}

/* Drives one randomized branching history through the shipped arena and a caller-supplied arena in
 * lockstep and requires identical handles, answers, and failure modes. */
static void test_a_caller_supplied_backend_matches_the_shipped_arena(void)
{
    enum { node_count = 2048, query_count = 8000 };
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena myers;
    ft_incremental_ancestor_arena custom;
    ft_incremental_ancestor_arena_config custom_config;
    parent_array_backend* backend = NULL;
    xorshift random;
    ft_incremental_ancestor_node* handles = NULL;
    size_t published = 0;
    size_t index = 0;
    int step = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&myers, &policy), FT_STATUS_OK);
    backend = parent_array_create(node_count + 1);
    if (backend == NULL) {
        ft_incremental_ancestor_arena_dispose(&myers);
        ft_incremental_ancestor_policy_dispose(&policy);
        REQUIRE(backend != NULL);
        return;
    }
    custom_config.vtable = &parent_array_vtable;
    custom_config.backend = backend;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_create(&custom, &policy, &custom_config),
        FT_STATUS_OK);
    xorshift_init(&random, 0x5EA1);

    handles = (ft_incremental_ancestor_node*)malloc((node_count + 1) * sizeof(*handles));
    REQUIRE_GOTO(handles != NULL, cleanup);
    handles[0] = FT_INCREMENTAL_ANCESTOR_BOTTOM;
    published = 1;

    for (step = 0; step != node_count; ++step) {
        const test_value payload = make_value(step);
        const ft_incremental_ancestor_node parent = (step % 3 == 0)
            ? handles[xorshift_below(&random, published)]
            : handles[published - 1];
        ft_incremental_ancestor_node myers_handle = 0;
        ft_incremental_ancestor_node custom_handle = 0;
        ft_incremental_ancestor_depth myers_depth = 0;
        ft_incremental_ancestor_depth custom_depth = 0;
        ft_incremental_ancestor_node myers_parent = 0;
        ft_incremental_ancestor_node custom_parent = 0;
        const void* myers_value = NULL;
        const void* custom_value = NULL;
        size_t myers_count = 0;
        size_t custom_count = 0;

        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_add_leaf(&myers, parent, &payload, &myers_handle) ==
                FT_STATUS_OK,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_add_leaf(&custom, parent, &payload, &custom_handle) ==
                FT_STATUS_OK,
            cleanup);
        REQUIRE_GOTO(myers_handle == custom_handle, cleanup);
        handles[published++] = myers_handle;

        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_published_node_count(&myers, &myers_count) ==
                    FT_STATUS_OK &&
                ft_incremental_ancestor_arena_published_node_count(&custom, &custom_count) ==
                    FT_STATUS_OK &&
                myers_count == custom_count,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_depth(&myers, myers_handle, &myers_depth) ==
                    FT_STATUS_OK &&
                ft_incremental_ancestor_arena_depth(&custom, custom_handle, &custom_depth) ==
                    FT_STATUS_OK &&
                myers_depth == custom_depth,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_parent(&myers, myers_handle, &myers_parent) ==
                    FT_STATUS_OK &&
                ft_incremental_ancestor_arena_parent(&custom, custom_handle, &custom_parent) ==
                    FT_STATUS_OK &&
                myers_parent == custom_parent,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_value_ref(&myers, myers_handle, &myers_value) ==
                    FT_STATUS_OK &&
                ft_incremental_ancestor_arena_value_ref(&custom, custom_handle, &custom_value) ==
                    FT_STATUS_OK &&
                ((const test_value*)myers_value)->value ==
                    ((const test_value*)custom_value)->value,
            cleanup);
    }

    for (index = 0; index != (size_t)query_count; ++index) {
        const ft_incremental_ancestor_node node = handles[xorshift_below(&random, published)];
        ft_incremental_ancestor_depth depth = 0;
        ft_incremental_ancestor_depth target = 0;
        ft_incremental_ancestor_node myers_ancestor = 0;
        ft_incremental_ancestor_node custom_ancestor = 0;

        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_depth(&myers, node, &depth) == FT_STATUS_OK,
            cleanup);
        target = (ft_incremental_ancestor_depth)xorshift_below(&random, (size_t)(depth + 2)) - 1;
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_ancestor_at_depth(&myers, node, target, &myers_ancestor) ==
                    FT_STATUS_OK &&
                ft_incremental_ancestor_arena_ancestor_at_depth(
                    &custom,
                    node,
                    target,
                    &custom_ancestor) == FT_STATUS_OK &&
                myers_ancestor == custom_ancestor,
            cleanup);
    }

    /* The failure modes must agree as well, so a consumer cannot depend on backend-specific
     * rejection behaviour. */
    {
        ft_incremental_ancestor_node scratch = 0;
        ft_incremental_ancestor_depth depth = 0;
        const void* value_ref = NULL;
        const test_value payload = make_value(0);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_add_leaf(&custom, SIZE_MAX, &payload, &scratch) ==
                FT_STATUS_OUT_OF_RANGE,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_depth(&custom, SIZE_MAX, &depth) ==
                FT_STATUS_OUT_OF_RANGE,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_parent(
                &custom,
                FT_INCREMENTAL_ANCESTOR_BOTTOM,
                &scratch) == FT_STATUS_OUT_OF_RANGE,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_value_ref(
                &custom,
                FT_INCREMENTAL_ANCESTOR_BOTTOM,
                &value_ref) == FT_STATUS_OUT_OF_RANGE,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_ancestor_at_depth(
                &custom,
                FT_INCREMENTAL_ANCESTOR_BOTTOM,
                -2,
                &scratch) == FT_STATUS_OUT_OF_RANGE,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_arena_ancestor_at_depth(
                &custom,
                FT_INCREMENTAL_ANCESTOR_BOTTOM,
                0,
                &scratch) == FT_STATUS_OUT_OF_RANGE,
            cleanup);
    }

    /* Only the shipped backend has a Myers representation to report. */
    {
        ft_incremental_ancestor_myers_statistics statistics;
        REQUIRE_GOTO(
            ft_incremental_ancestor_myers_arena_get_statistics(&custom, &statistics) ==
                FT_STATUS_INVALID_ARGUMENT,
            cleanup);
        REQUIRE_GOTO(
            ft_incremental_ancestor_myers_arena_get_statistics(&myers, &statistics) ==
                FT_STATUS_OK,
            cleanup);
        REQUIRE_GOTO(statistics.add_leaf_count == backend->add_leaf_count, cleanup);
        REQUIRE_GOTO(statistics.ancestor_query_count == backend->ancestor_query_count, cleanup);
        REQUIRE_GOTO(statistics.published_node_count == backend->count - 1, cleanup);
    }

cleanup:
    free(handles);
    ft_incremental_ancestor_arena_dispose(&custom);
    ft_incremental_ancestor_arena_dispose(&myers);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_handle_lifecycle_shares_rather_than_copies(void)
{
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy_config observed;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_policy alias;
    ft_incremental_ancestor_policy moved;
    ft_incremental_ancestor_policy other;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_arena second;
    ft_incremental_ancestor_arena relocated;
    ft_incremental_ancestor_node leaf = 0;
    ft_incremental_ancestor_depth depth = 0;
    const test_value payload = make_value(7);
    size_t count = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&other, &config), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_copy(&policy, &alias), FT_STATUS_OK);
    REQUIRE(ft_incremental_ancestor_policy_same(&policy, &alias));
    /* Independently recreating an equivalent configuration deliberately yields a new identity. */
    REQUIRE(!ft_incremental_ancestor_policy_same(&policy, &other));
    ft_incremental_ancestor_policy_move(&moved, &alias);
    REQUIRE(ft_incremental_ancestor_policy_same(&policy, &moved));
    REQUIRE(alias.rep == NULL);
    ft_incremental_ancestor_policy_dispose(&moved);
    ft_incremental_ancestor_policy_dispose(&other);

    REQUIRE_STATUS(
        ft_incremental_ancestor_policy_get_config(&policy, &observed),
        FT_STATUS_OK);
    REQUIRE(observed.value_size == sizeof(test_value));
    REQUIRE(observed.value_type_identity == &g_value_type_identity);
    REQUIRE(observed.destroy == tracked_destroy);

    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(ft_incremental_ancestor_arena_copy(&arena, &second), FT_STATUS_OK);
    REQUIRE(ft_incremental_ancestor_arena_same(&arena, &second));
    REQUIRE(ft_incremental_ancestor_arena_root_identity(&arena) ==
        ft_incremental_ancestor_arena_root_identity(&second));

    /* Both handles observe the same arena, so an addition through one is visible through the
     * other. */
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(
            &second,
            FT_INCREMENTAL_ANCESTOR_BOTTOM,
            &payload,
            &leaf),
        FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(&arena, &count),
        FT_STATUS_OK);
    REQUIRE(count == 1);

    ft_incremental_ancestor_arena_move(&relocated, &second);
    REQUIRE(second.rep == NULL);
    REQUIRE(ft_incremental_ancestor_arena_same(&arena, &relocated));
    ft_incremental_ancestor_arena_dispose(&relocated);

    /* The surviving handle keeps the arena and every published value alive. */
    REQUIRE_STATUS(ft_incremental_ancestor_arena_depth(&arena, leaf, &depth), FT_STATUS_OK);
    REQUIRE(depth == 0);
    REQUIRE(context.live_values == 1);

    {
        ft_incremental_ancestor_policy from_arena;
        REQUIRE_STATUS(
            ft_incremental_ancestor_arena_get_policy(&arena, &from_arena),
            FT_STATUS_OK);
        REQUIRE(ft_incremental_ancestor_policy_same(&policy, &from_arena));
        ft_incremental_ancestor_policy_dispose(&from_arena);
    }

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
}

static void test_invalid_arguments_are_rejected(void)
{
    test_context context;
    ft_incremental_ancestor_policy_config config;
    ft_incremental_ancestor_policy policy;
    ft_incremental_ancestor_arena arena;
    ft_incremental_ancestor_arena_config arena_config;
    ft_incremental_ancestor_arena_vtable vtable;
    ft_incremental_ancestor_node node = 0;

    (void)memset(&context, 0, sizeof(context));
    init_config(&config, &context);
    config.value_size = 0;
    REQUIRE_STATUS(
        ft_incremental_ancestor_policy_create(&policy, &config),
        FT_STATUS_INVALID_ARGUMENT);
    init_config(&config, &context);
    config.value_type_identity = NULL;
    REQUIRE_STATUS(
        ft_incremental_ancestor_policy_create(&policy, &config),
        FT_STATUS_INVALID_ARGUMENT);
    init_config(&config, &context);
    config.copy = NULL;
    REQUIRE_STATUS(
        ft_incremental_ancestor_policy_create(&policy, &config),
        FT_STATUS_INVALID_ARGUMENT);

    init_config(&config, &context);
    REQUIRE_STATUS(ft_incremental_ancestor_policy_create(&policy, &config), FT_STATUS_OK);

    /* A backend missing any required hook cannot satisfy the seam. */
    vtable = parent_array_vtable;
    vtable.ancestor_at_depth = NULL;
    arena_config.vtable = &vtable;
    arena_config.backend = NULL;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_create(&arena, &policy, &arena_config),
        FT_STATUS_INVALID_ARGUMENT);
    arena_config.vtable = NULL;
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_create(&arena, &policy, &arena_config),
        FT_STATUS_INVALID_ARGUMENT);

    REQUIRE_STATUS(
        ft_incremental_ancestor_myers_arena_create(&arena, NULL),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(ft_incremental_ancestor_myers_arena_create(&arena, &policy), FT_STATUS_OK);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_bottom(&arena, NULL),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_add_leaf(&arena, 0, NULL, &node),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE_STATUS(
        ft_incremental_ancestor_arena_published_node_count(NULL, NULL),
        FT_STATUS_INVALID_ARGUMENT);
    REQUIRE(ft_incremental_ancestor_arena_root_identity(NULL) == NULL);

    ft_incremental_ancestor_arena_dispose(&arena);
    ft_incremental_ancestor_policy_dispose(&policy);
    REQUIRE(context.outstanding_allocations == 0);
    REQUIRE(context.live_values == 0);
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
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    run_test("Ancestor arena empty and bottom", test_empty_arena_holds_only_its_unlabelled_bottom_node);
    run_test("Ancestor arena singleton leaf", test_singleton_leaf_hangs_directly_below_bottom);
    run_test("Ancestor arena naive parent-walk oracle", test_branched_ancestor_queries_match_a_naive_parent_walk);
    run_test("Ancestor arena odd-block square layout", test_odd_block_statistics_match_the_exact_square_layout);
    run_test("Ancestor arena deep-chain hop envelope", test_deep_chain_queries_use_conservatively_logarithmic_hops);
    run_test("Ancestor arena branched hop envelope", test_branched_queries_use_conservatively_logarithmic_hops);
    run_test("Ancestor arena per-query hop counters", test_the_last_hop_count_reports_only_the_most_recent_query);
    run_test("Ancestor arena retained-version independence", test_retained_versions_stay_independent_after_branching);
    run_test("Ancestor arena failure atomicity and lifetimes", test_failure_atomicity_and_lifetimes);
    run_test("Ancestor arena caller-supplied backend", test_a_caller_supplied_backend_matches_the_shipped_arena);
    run_test("Ancestor arena handle lifecycle", test_handle_lifecycle_shares_rather_than_copies);
    run_test("Ancestor arena invalid arguments", test_invalid_arguments_are_rejected);
    if (g_failures != 0) {
        (void)fprintf(stderr, "%d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)printf("all C incremental ancestor arena tests passed\n");
    return EXIT_SUCCESS;
}
