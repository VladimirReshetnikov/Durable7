/*
 * Tests for the persistent ancestral connection forest: sparse singleton semantics, union by size
 * without path compression, the branch-local first-connection query, and the representation audit.
 */

#include <durable7/hamt/persistent_ancestral_connection_forest.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
    exit(EXIT_FAILURE); } } while (0)
#define CHECK_STATUS(expression) CHECK((expression) == D7_HAMT_OK)

typedef d7_hamt_ancestral_connection_forest d7_forest;
typedef d7_hamt_ancestral_connection_version d7_version;
typedef d7_hamt_ancestral_connection_forest_statistics d7_forest_statistics;

#ifdef D7_HAMT_TESTING
void d7_hamt_ancestral_connection_forest_test_fail_allocations_after(
    size_t successful_allocations);
void d7_hamt_ancestral_connection_forest_test_reset_allocator(void);
#endif

static void forest_init(d7_forest* forest, int32_t vertex_count)
{
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_init(forest, vertex_count));
}

static void forest_link(const d7_forest* source, int32_t left, int32_t right, d7_forest* result)
{
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_link(source, left, right, result));
}

static void forest_destroy(d7_forest* forest)
{
    d7_hamt_ancestral_connection_forest_destroy(forest);
}

static const d7_version* forest_version(const d7_forest* forest)
{
    return d7_hamt_ancestral_connection_forest_version_ref(forest);
}

static int32_t forest_find(const d7_forest* forest, int32_t vertex)
{
    int32_t representative = -1;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_find(forest, vertex, &representative));
    return representative;
}

static bool forest_connected(const d7_forest* forest, int32_t left, int32_t right)
{
    bool connected = false;
    CHECK_STATUS(
        d7_hamt_ancestral_connection_forest_connected(forest, left, right, &connected));
    return connected;
}

static int32_t forest_component_size(const d7_forest* forest, int32_t vertex)
{
    int32_t size = -1;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_component_size(forest, vertex, &size));
    return size;
}

static const d7_version* first_connected(const d7_forest* forest, int32_t left, int32_t right)
{
    const d7_version* version = NULL;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_first_connected_ref(
        forest, left, right, &version));
    return version;
}

static bool same_version(const d7_version* left, const d7_version* right)
{
    return d7_hamt_ancestral_connection_version_same(left, right);
}

static d7_forest_statistics forest_validate(const d7_forest* forest)
{
    bool valid = false;
    d7_forest_statistics statistics;
    CHECK_STATUS(
        d7_hamt_ancestral_connection_forest_debug_validate(forest, &valid, &statistics));
    CHECK(valid);
    return statistics;
}

static void test_init_produces_singleton_components_and_root_history(void)
{
    d7_forest root;
    forest_init(&root, 5);

    CHECK(d7_hamt_ancestral_connection_forest_vertex_count(&root) == 5);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&root) == 5);
    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&root)) == 0u);
    CHECK(d7_hamt_ancestral_connection_version_parent_ref(forest_version(&root)) == NULL);
    CHECK(same_version(
        forest_version(&root),
        d7_hamt_ancestral_connection_version_root_ref(forest_version(&root))));
    for (int32_t vertex = 0; vertex < 5; ++vertex) {
        CHECK(forest_find(&root, vertex) == vertex);
        CHECK(forest_connected(&root, vertex, vertex));
        CHECK(same_version(first_connected(&root, vertex, vertex), forest_version(&root)));
    }

    CHECK(!forest_connected(&root, 0, 1));
    CHECK(first_connected(&root, 0, 1) == NULL);
    const d7_forest_statistics statistics = forest_validate(&root);
    CHECK(statistics.stored_cell_count == 0u);
    CHECK(statistics.component_count == 5);
    CHECK(statistics.maximum_parent_path_length == 0u);
    CHECK(statistics.history_depth == 0u);

    /* A singleton is the absence of a cell, so an empty universe is legal and construction never
     * touches the connectivity index. */
    d7_forest empty;
    forest_init(&empty, 0);
    CHECK(d7_hamt_ancestral_connection_forest_vertex_count(&empty) == 0);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&empty) == 0);
    (void)forest_validate(&empty);
    forest_destroy(&empty);

    d7_forest negative;
    CHECK(d7_hamt_ancestral_connection_forest_init(&negative, -1)
        == D7_HAMT_INVALID_ARGUMENT);
    forest_destroy(&root);
}

static void test_link_records_the_first_connection_version(void)
{
    d7_forest root;
    d7_forest first;
    d7_forest second;
    d7_forest bridge;
    d7_forest attach;
    forest_init(&root, 6);
    forest_link(&root, 0, 1, &first);
    forest_link(&first, 2, 3, &second);
    forest_link(&second, 0, 2, &bridge);
    forest_link(&bridge, 4, 0, &attach);

    CHECK(d7_hamt_ancestral_connection_forest_component_count(&attach) == 2);
    CHECK(same_version(first_connected(&attach, 0, 1), forest_version(&first)));
    CHECK(same_version(first_connected(&attach, 2, 3), forest_version(&second)));
    CHECK(same_version(first_connected(&attach, 0, 3), forest_version(&bridge)));
    CHECK(same_version(first_connected(&attach, 1, 2), forest_version(&bridge)));
    CHECK(same_version(first_connected(&attach, 4, 1), forest_version(&attach)));
    CHECK(same_version(first_connected(&attach, 4, 4), forest_version(&root)));
    CHECK(first_connected(&attach, 0, 5) == NULL);

    CHECK(forest_find(&attach, 0) == 0);
    CHECK(forest_find(&attach, 4) == 0);

    /* Every retained version keeps its own connectivity, untouched by later descendants. */
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&root) == 6);
    CHECK(!forest_connected(&root, 0, 1));
    CHECK(!forest_connected(&first, 0, 2));
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&first) == 5);
    (void)forest_validate(&root);
    (void)forest_validate(&first);
    (void)forest_validate(&second);
    (void)forest_validate(&bridge);
    (void)forest_validate(&attach);

    forest_destroy(&attach);
    forest_destroy(&bridge);
    forest_destroy(&second);
    forest_destroy(&first);
    forest_destroy(&root);
}

static void test_redundant_links_create_versions_and_share_connectivity_state(void)
{
    d7_forest root;
    d7_forest linked;
    d7_forest reverse_duplicate;
    d7_forest sibling_duplicate;
    d7_forest self_duplicate;
    forest_init(&root, 4);
    forest_link(&root, 0, 1, &linked);
    forest_link(&linked, 1, 0, &reverse_duplicate);
    forest_link(&linked, 1, 0, &sibling_duplicate);
    forest_link(&reverse_duplicate, 0, 0, &self_duplicate);

    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&linked)) == 1u);
    CHECK(d7_hamt_ancestral_connection_version_depth(
        forest_version(&reverse_duplicate)) == 2u);
    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&self_duplicate)) == 3u);
    CHECK(same_version(
        d7_hamt_ancestral_connection_version_parent_ref(forest_version(&reverse_duplicate)),
        forest_version(&linked)));
    CHECK(same_version(
        d7_hamt_ancestral_connection_version_parent_ref(forest_version(&sibling_duplicate)),
        forest_version(&linked)));
    CHECK(same_version(
        d7_hamt_ancestral_connection_version_parent_ref(forest_version(&self_duplicate)),
        forest_version(&reverse_duplicate)));
    CHECK(!same_version(forest_version(&linked), forest_version(&reverse_duplicate)));
    CHECK(!same_version(
        forest_version(&reverse_duplicate), forest_version(&sibling_duplicate)));
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&linked)
        == d7_hamt_ancestral_connection_forest_component_count(&reverse_duplicate));
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&linked)
        == d7_hamt_ancestral_connection_forest_component_count(&self_duplicate));
    CHECK(d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(
        &linked, &reverse_duplicate));
    CHECK(d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(
        &reverse_duplicate, &self_duplicate));
    CHECK(!d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(&root, &linked));
    CHECK(d7_hamt_ancestral_connection_forest_debug_root_identity(&linked)
        == d7_hamt_ancestral_connection_forest_debug_root_identity(&self_duplicate));
    CHECK(same_version(first_connected(&self_duplicate, 0, 1), forest_version(&linked)));
    CHECK(same_version(first_connected(&self_duplicate, 2, 2), forest_version(&root)));
    (void)forest_validate(&self_duplicate);

    /* A result may alias its source: the successor is computed in full before it is published. */
    d7_forest rolling;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_clone(&self_duplicate, &rolling));
    forest_link(&rolling, 2, 3, &rolling);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&rolling) == 2);
    CHECK(forest_connected(&rolling, 2, 3));
    CHECK(!forest_connected(&self_duplicate, 2, 3));
    (void)forest_validate(&rolling);
    forest_destroy(&rolling);

    forest_destroy(&self_duplicate);
    forest_destroy(&sibling_duplicate);
    forest_destroy(&reverse_duplicate);
    forest_destroy(&linked);
    forest_destroy(&root);
}

static void test_sibling_branches_keep_equal_depth_version_tags_distinct(void)
{
    d7_forest root;
    d7_forest common;
    d7_forest left;
    d7_forest right;
    d7_forest left_later;
    d7_forest right_later;
    forest_init(&root, 5);
    forest_link(&root, 0, 1, &common);
    forest_link(&common, 0, 2, &left);
    forest_link(&common, 0, 3, &right);
    forest_link(&left, 2, 4, &left_later);
    forest_link(&right, 3, 4, &right_later);

    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&left))
        == d7_hamt_ancestral_connection_version_depth(forest_version(&right)));
    CHECK(!same_version(forest_version(&left), forest_version(&right)));
    CHECK(same_version(first_connected(&left, 0, 1), forest_version(&common)));
    CHECK(same_version(first_connected(&right, 0, 1), forest_version(&common)));
    CHECK(same_version(first_connected(&left_later, 1, 2), forest_version(&left)));
    CHECK(same_version(first_connected(&right_later, 1, 3), forest_version(&right)));
    CHECK(same_version(first_connected(&left_later, 0, 4), forest_version(&left_later)));
    CHECK(same_version(first_connected(&right_later, 0, 4), forest_version(&right_later)));
    CHECK(!forest_connected(&left_later, 0, 3));
    CHECK(!forest_connected(&right_later, 0, 2));
    CHECK(same_version(
        d7_hamt_ancestral_connection_version_root_ref(forest_version(&left_later)),
        forest_version(&root)));
    CHECK(same_version(
        d7_hamt_ancestral_connection_version_root_ref(forest_version(&right_later)),
        forest_version(&root)));
    (void)forest_validate(&left_later);
    (void)forest_validate(&right_later);

    forest_destroy(&right_later);
    forest_destroy(&left_later);
    forest_destroy(&right);
    forest_destroy(&left);
    forest_destroy(&common);
    forest_destroy(&root);
}

static void test_first_connected_uses_pair_lca_rather_than_latest_component_birth(void)
{
    d7_forest root;
    d7_forest ab;
    d7_forest cd;
    d7_forest abcd;
    d7_forest ef;
    d7_forest gh;
    d7_forest efgh;
    d7_forest all;
    forest_init(&root, 8);
    forest_link(&root, 0, 1, &ab);
    forest_link(&ab, 2, 3, &cd);
    forest_link(&cd, 1, 3, &abcd);
    forest_link(&abcd, 4, 5, &ef);
    forest_link(&ef, 6, 7, &gh);
    forest_link(&gh, 4, 6, &efgh);
    forest_link(&efgh, 0, 4, &all);

    CHECK(same_version(first_connected(&all, 0, 1), forest_version(&ab)));
    CHECK(same_version(first_connected(&all, 2, 3), forest_version(&cd)));
    CHECK(same_version(first_connected(&all, 0, 2), forest_version(&abcd)));
    CHECK(same_version(first_connected(&all, 4, 5), forest_version(&ef)));
    CHECK(same_version(first_connected(&all, 6, 7), forest_version(&gh)));
    CHECK(same_version(first_connected(&all, 5, 7), forest_version(&efgh)));
    CHECK(same_version(first_connected(&all, 1, 6), forest_version(&all)));
    (void)forest_validate(&all);

    forest_destroy(&all);
    forest_destroy(&efgh);
    forest_destroy(&gh);
    forest_destroy(&ef);
    forest_destroy(&abcd);
    forest_destroy(&cd);
    forest_destroy(&ab);
    forest_destroy(&root);
}

static void test_first_connected_handles_one_endpoint_at_lca_after_it_becomes_a_child(void)
{
    d7_forest root;
    d7_forest pair;
    d7_forest other_pair;
    d7_forest larger;
    d7_forest merged;
    forest_init(&root, 5);
    forest_link(&root, 0, 1, &pair);           /* 1 -> 0 */
    forest_link(&pair, 2, 3, &other_pair);     /* 3 -> 2 */
    forest_link(&other_pair, 2, 4, &larger);
    forest_link(&larger, 2, 0, &merged);       /* the component rooted at 0 becomes a child of 2 */

    CHECK(forest_find(&merged, 0) == 2);
    CHECK(same_version(first_connected(&merged, 0, 1), forest_version(&pair)));
    CHECK(same_version(first_connected(&merged, 0, 2), forest_version(&merged)));
    CHECK(same_version(first_connected(&merged, 1, 4), forest_version(&merged)));
    (void)forest_validate(&merged);

    forest_destroy(&merged);
    forest_destroy(&larger);
    forest_destroy(&other_pair);
    forest_destroy(&pair);
    forest_destroy(&root);
}

static void test_balanced_unions_respect_height_and_retain_witnesses(void)
{
    enum { COUNT = 1024 };
    d7_forest root;
    d7_forest current;
    forest_init(&root, COUNT);
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_clone(&root, &current));

    d7_version first_pair;
    d7_version final_bridge;
    first_pair.rep = NULL;
    final_bridge.rep = NULL;
    for (int32_t width = 1; width < COUNT; width *= 2) {
        for (int32_t start = 0; start < COUNT; start += width * 2) {
            forest_link(&current, start, start + width, &current);
            if (start == 0 && width == 1) {
                CHECK_STATUS(d7_hamt_ancestral_connection_forest_copy_version(
                    &current, &first_pair));
            }
            if (start == 0 && width == COUNT / 2) {
                CHECK_STATUS(d7_hamt_ancestral_connection_forest_copy_version(
                    &current, &final_bridge));
            }
        }
    }

    CHECK(d7_hamt_ancestral_connection_forest_component_count(&current) == 1);
    CHECK(forest_find(&current, COUNT - 1) == 0);
    CHECK(same_version(first_connected(&current, 0, 1), &first_pair));
    CHECK(same_version(first_connected(&current, 0, COUNT - 1), &final_bridge));
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&root) == COUNT);

    const d7_forest_statistics statistics = forest_validate(&current);
    CHECK(statistics.stored_cell_count == (size_t)COUNT);
    CHECK(statistics.component_count == 1);
    CHECK(statistics.maximum_parent_path_length == 10u);
    CHECK(statistics.history_depth == (uint64_t)(COUNT - 1));

    d7_hamt_ancestral_connection_version_destroy(&final_bridge);
    d7_hamt_ancestral_connection_version_destroy(&first_pair);
    forest_destroy(&current);
    forest_destroy(&root);
}

enum { MODEL_VERTEX_COUNT = 12, MODEL_OPERATION_COUNT = 350 };

typedef struct model_version {
    d7_forest actual;
    int32_t classes[MODEL_VERTEX_COUNT];
    size_t parent;
    bool has_parent;
} model_version;

static const d7_version* first_connected_by_scan(
    const model_version* states,
    size_t index,
    int32_t left,
    int32_t right)
{
    size_t lineage[MODEL_OPERATION_COUNT + 1];
    size_t length = 0u;
    for (size_t current = index;; current = states[current].parent) {
        lineage[length++] = current;
        if (!states[current].has_parent) {
            break;
        }
    }
    while (length > 0u) {
        const model_version* const candidate = &states[lineage[--length]];
        if (candidate->classes[left] == candidate->classes[right]) {
            return forest_version(&candidate->actual);
        }
    }
    return NULL;
}

static uint64_t model_next(uint64_t* state)
{
    *state = *state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return *state >> 33;
}

static void test_random_branching_histories_match_naive_ancestor_scan(void)
{
    static model_version states[MODEL_OPERATION_COUNT + 1];
    uint64_t random_state = UINT64_C(0x5EED2026);

    forest_init(&states[0].actual, MODEL_VERTEX_COUNT);
    for (int32_t vertex = 0; vertex < MODEL_VERTEX_COUNT; ++vertex) {
        states[0].classes[vertex] = vertex;
    }
    states[0].parent = 0u;
    states[0].has_parent = false;
    size_t state_count = 1u;

    for (size_t operation = 0u; operation < MODEL_OPERATION_COUNT; ++operation) {
        const size_t parent_index = (size_t)(model_next(&random_state) % state_count);
        const int32_t left =
            (int32_t)(model_next(&random_state) % (uint64_t)MODEL_VERTEX_COUNT);
        const int32_t right =
            (int32_t)(model_next(&random_state) % (uint64_t)MODEL_VERTEX_COUNT);

        const size_t index = state_count;
        for (int32_t vertex = 0; vertex < MODEL_VERTEX_COUNT; ++vertex) {
            states[index].classes[vertex] = states[parent_index].classes[vertex];
        }
        const int32_t left_class = states[index].classes[left];
        const int32_t right_class = states[index].classes[right];
        if (left_class != right_class) {
            for (int32_t vertex = 0; vertex < MODEL_VERTEX_COUNT; ++vertex) {
                if (states[index].classes[vertex] == right_class) {
                    states[index].classes[vertex] = left_class;
                }
            }
        }

        forest_link(&states[parent_index].actual, left, right, &states[index].actual);
        states[index].parent = parent_index;
        states[index].has_parent = true;
        ++state_count;

        CHECK(same_version(
            d7_hamt_ancestral_connection_version_parent_ref(
                forest_version(&states[index].actual)),
            forest_version(&states[parent_index].actual)));
        CHECK(d7_hamt_ancestral_connection_version_depth(
                forest_version(&states[index].actual))
            == d7_hamt_ancestral_connection_version_depth(
                forest_version(&states[parent_index].actual)) + 1u);

        int32_t distinct = 0;
        for (int32_t vertex = 0; vertex < MODEL_VERTEX_COUNT; ++vertex) {
            bool seen = false;
            for (int32_t earlier = 0; earlier < vertex; ++earlier) {
                seen = seen || states[index].classes[earlier] == states[index].classes[vertex];
            }
            distinct += seen ? 0 : 1;
        }
        CHECK(d7_hamt_ancestral_connection_forest_component_count(&states[index].actual)
            == distinct);

        for (int32_t query_left = 0; query_left < MODEL_VERTEX_COUNT; ++query_left) {
            for (int32_t query_right = 0; query_right < MODEL_VERTEX_COUNT; ++query_right) {
                const bool expected = states[index].classes[query_left]
                    == states[index].classes[query_right];
                CHECK(forest_connected(&states[index].actual, query_left, query_right)
                    == expected);
                const d7_version* const actual =
                    first_connected(&states[index].actual, query_left, query_right);
                const d7_version* const modelled =
                    first_connected_by_scan(states, index, query_left, query_right);
                CHECK(actual == NULL ? modelled == NULL : same_version(actual, modelled));
            }
        }

        if (operation % 25u == 0u) {
            (void)forest_validate(&states[index].actual);
        }
    }

    /* Recheck retained versions after every sibling branch has been constructed. */
    for (int repeat = 0; repeat < 80; ++repeat) {
        const size_t index = (size_t)(model_next(&random_state) % state_count);
        const int32_t left =
            (int32_t)(model_next(&random_state) % (uint64_t)MODEL_VERTEX_COUNT);
        const int32_t right =
            (int32_t)(model_next(&random_state) % (uint64_t)MODEL_VERTEX_COUNT);
        const d7_version* const actual = first_connected(&states[index].actual, left, right);
        const d7_version* const modelled = first_connected_by_scan(states, index, left, right);
        CHECK(actual == NULL ? modelled == NULL : same_version(actual, modelled));
    }

    for (size_t index = state_count; index > 0u; --index) {
        forest_destroy(&states[index - 1u].actual);
    }
}

static void test_long_redundant_history_keeps_the_successful_union_witness(void)
{
    d7_forest root;
    d7_forest first;
    d7_forest current;
    forest_init(&root, 3);
    forest_link(&root, 0, 1, &first);
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_clone(&first, &current));
    for (int repeat = 0; repeat < 20000; ++repeat) {
        forest_link(&current, 2, 2, &current);
    }

    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&current)) == 20001u);
    CHECK(same_version(first_connected(&current, 0, 1), forest_version(&first)));
    CHECK(first_connected(&current, 0, 2) == NULL);
    CHECK(d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(&first, &current));

    /* Releasing the tip must unwind a history that deep without recursing. */
    forest_destroy(&current);
    forest_destroy(&first);
    forest_destroy(&root);
}

static void test_huge_sparse_universe_stores_only_touched_vertices(void)
{
    d7_forest root;
    d7_forest linked;
    forest_init(&root, INT32_MAX);
    const d7_forest_statistics empty_statistics = forest_validate(&root);
    CHECK(empty_statistics.stored_cell_count == 0u);

    forest_link(&root, 0, INT32_MAX - 1, &linked);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&linked) == INT32_MAX - 1);
    CHECK(forest_connected(&linked, 0, INT32_MAX - 1));
    CHECK(!forest_connected(&linked, 0, INT32_MAX - 2));
    CHECK(forest_find(&linked, INT32_MAX - 2) == INT32_MAX - 2);
    CHECK(same_version(
        first_connected(&linked, 0, INT32_MAX - 1), forest_version(&linked)));
    const d7_forest_statistics statistics = forest_validate(&linked);
    CHECK(statistics.stored_cell_count == 2u);
    CHECK(statistics.maximum_parent_path_length == 1u);

    forest_destroy(&linked);
    forest_destroy(&root);
}

static void test_operations_reject_vertices_outside_the_universe(void)
{
    d7_forest root;
    forest_init(&root, 3);
    int32_t representative = 7;
    bool connected = true;
    int32_t size = 7;
    d7_version sentinel;
    sentinel.rep = NULL;
    const d7_version* version = &sentinel;
    d7_forest result;
    result.version.rep = NULL;

    CHECK(d7_hamt_ancestral_connection_forest_find(&root, -1, &representative)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_ancestral_connection_forest_find(&root, 3, &representative)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(representative == 7);
    CHECK(d7_hamt_ancestral_connection_forest_connected(&root, -1, 0, &connected)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_ancestral_connection_forest_connected(&root, 0, 3, &connected)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(connected);
    CHECK(d7_hamt_ancestral_connection_forest_link(&root, -1, 0, &result)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_ancestral_connection_forest_link(&root, 0, 3, &result)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(result.version.rep == NULL);
    CHECK(d7_hamt_ancestral_connection_forest_first_connected_ref(&root, -1, 0, &version)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_ancestral_connection_forest_first_connected_ref(&root, 0, 3, &version)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(version == &sentinel);
    CHECK(d7_hamt_ancestral_connection_forest_component_size(&root, -1, &size)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(d7_hamt_ancestral_connection_forest_component_size(&root, 3, &size)
        == D7_HAMT_INVALID_ARGUMENT);
    CHECK(size == 7);

    /* A rejected operation publishes nothing. */
    CHECK(same_version(
        forest_version(&root),
        d7_hamt_ancestral_connection_version_root_ref(forest_version(&root))));
    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&root)) == 0u);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&root) == 3);
    (void)forest_validate(&root);
    forest_destroy(&root);
}

/* Sums one cached size per distinct current root, which must recover the whole universe. */
static int32_t total_component_size(const d7_forest* forest)
{
    const int32_t vertex_count = d7_hamt_ancestral_connection_forest_vertex_count(forest);
    int32_t total = 0;
    int32_t roots = 0;
    for (int32_t vertex = 0; vertex < vertex_count; ++vertex) {
        const int32_t root = forest_find(forest, vertex);
        if (root == vertex) {
            ++roots;
            total += forest_component_size(forest, vertex);
        }
    }
    CHECK(roots == d7_hamt_ancestral_connection_forest_component_count(forest));
    return total;
}

static void test_component_size_reports_the_cached_union_size(void)
{
    d7_forest root;
    d7_forest pair;
    d7_forest triple;
    d7_forest quad;
    forest_init(&root, 6);
    for (int32_t vertex = 0; vertex < 6; ++vertex) {
        CHECK(forest_component_size(&root, vertex) == 1);
    }
    CHECK(total_component_size(&root) == 6);

    /* A chain of unions grows exactly the touched component. */
    forest_link(&root, 0, 1, &pair);
    forest_link(&pair, 1, 2, &triple);
    forest_link(&triple, 2, 3, &quad);
    CHECK(forest_component_size(&pair, 0) == 2);
    CHECK(forest_component_size(&pair, 1) == 2);
    CHECK(forest_component_size(&pair, 5) == 1);
    CHECK(forest_component_size(&triple, 2) == 3);
    for (int32_t vertex = 0; vertex < 4; ++vertex) {
        CHECK(forest_component_size(&quad, vertex) == 4);
    }
    CHECK(forest_component_size(&quad, 4) == 1);
    CHECK(total_component_size(&quad) == 6);

    /* A redundant link changes nothing observable about sizes. */
    d7_forest redundant;
    forest_link(&quad, 3, 0, &redundant);
    for (int32_t vertex = 0; vertex < 6; ++vertex) {
        CHECK(forest_component_size(&redundant, vertex)
            == forest_component_size(&quad, vertex));
    }
    CHECK(total_component_size(&redundant) == 6);

    /* Retained versions keep their own sizes across a branching history. */
    d7_forest branch;
    forest_link(&quad, 4, 5, &branch);
    CHECK(forest_component_size(&branch, 4) == 2);
    CHECK(forest_component_size(&quad, 4) == 1);
    CHECK(forest_component_size(&pair, 2) == 1);
    CHECK(forest_component_size(&root, 0) == 1);
    CHECK(total_component_size(&branch) == 6);
    CHECK(total_component_size(&pair) == 6);

    d7_forest merged;
    forest_link(&branch, 5, 0, &merged);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&merged) == 1);
    for (int32_t vertex = 0; vertex < 6; ++vertex) {
        CHECK(forest_component_size(&merged, vertex) == 6);
    }
    CHECK(total_component_size(&merged) == 6);
    (void)forest_validate(&merged);

    /* A huge sparse universe still reports singletons without storing them. */
    d7_forest sparse_root;
    d7_forest sparse;
    forest_init(&sparse_root, INT32_MAX);
    forest_link(&sparse_root, 0, INT32_MAX - 1, &sparse);
    CHECK(forest_component_size(&sparse, 0) == 2);
    CHECK(forest_component_size(&sparse, INT32_MAX - 1) == 2);
    CHECK(forest_component_size(&sparse, INT32_MAX - 2) == 1);

    forest_destroy(&sparse);
    forest_destroy(&sparse_root);
    forest_destroy(&merged);
    forest_destroy(&branch);
    forest_destroy(&redundant);
    forest_destroy(&quad);
    forest_destroy(&triple);
    forest_destroy(&pair);
    forest_destroy(&root);
}

static void test_separate_forests_have_separate_version_trees(void)
{
    d7_forest left_root;
    d7_forest right_root;
    d7_forest left;
    d7_forest right;
    forest_init(&left_root, 2);
    forest_init(&right_root, 2);
    forest_link(&left_root, 0, 1, &left);
    forest_link(&right_root, 0, 1, &right);

    CHECK(!same_version(forest_version(&left_root), forest_version(&right_root)));
    CHECK(!same_version(forest_version(&left), forest_version(&right)));
    CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&left))
        == d7_hamt_ancestral_connection_version_depth(forest_version(&right)));
    CHECK(same_version(first_connected(&left, 0, 1), forest_version(&left)));
    CHECK(same_version(first_connected(&right, 0, 1), forest_version(&right)));
    CHECK(!same_version(first_connected(&left, 0, 1), first_connected(&right, 0, 1)));

    forest_destroy(&right);
    forest_destroy(&left);
    forest_destroy(&right_root);
    forest_destroy(&left_root);
}

static void test_successful_union_rebuilds_only_the_touched_cells(void)
{
    d7_forest root;
    d7_forest first;
    d7_forest second;
    forest_init(&root, 16);
    forest_link(&root, 0, 1, &first);
    forest_link(&first, 2, 3, &second);

    /* A successful union publishes a new connectivity index; a redundant one does not. */
    CHECK(!d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(&first, &second));
    d7_forest redundant;
    forest_link(&second, 0, 1, &redundant);
    CHECK(d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(
        &second, &redundant));
    forest_destroy(&redundant);
    forest_link(&second, 3, 2, &redundant);
    CHECK(d7_hamt_ancestral_connection_forest_debug_shares_connectivity_root(
        &second, &redundant));
    forest_destroy(&redundant);

    /* Union by size: on a tie the first endpoint's root stays the representative. */
    CHECK(forest_find(&second, 1) == 0);
    CHECK(forest_find(&second, 3) == 2);
    d7_forest merged;
    forest_link(&second, 3, 1, &merged);
    CHECK(forest_find(&merged, 0) == 2);
    CHECK(d7_hamt_ancestral_connection_forest_component_count(&merged) == 13);
    (void)forest_validate(&merged);

    forest_destroy(&merged);
    forest_destroy(&second);
    forest_destroy(&first);
    forest_destroy(&root);
}

static void test_version_handles_copy_move_and_borrow(void)
{
    d7_forest root;
    d7_forest first;
    d7_forest second;
    forest_init(&root, 4);
    forest_link(&root, 0, 1, &first);
    forest_link(&first, 2, 3, &second);

    d7_version owned;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_copy_version(&second, &owned));
    CHECK(same_version(&owned, forest_version(&second)));

    d7_version relocated;
    d7_hamt_ancestral_connection_version_move(&relocated, &owned);
    CHECK(owned.rep == NULL);
    CHECK(same_version(&relocated, forest_version(&second)));

    d7_version parent;
    CHECK(d7_hamt_ancestral_connection_version_try_copy_parent(&relocated, &parent));
    CHECK(same_version(&parent, forest_version(&first)));
    d7_version grandparent;
    CHECK(d7_hamt_ancestral_connection_version_try_copy_parent(&parent, &grandparent));
    CHECK(same_version(&grandparent, forest_version(&root)));
    d7_version none;
    none.rep = NULL;
    CHECK(!d7_hamt_ancestral_connection_version_try_copy_parent(&grandparent, &none));
    CHECK(none.rep == NULL);

    d7_version history_root;
    CHECK_STATUS(
        d7_hamt_ancestral_connection_version_copy_root(&relocated, &history_root));
    CHECK(same_version(&history_root, forest_version(&root)));

    /* An owned token outlives every forest handle that produced it. */
    bool connected = false;
    d7_version witness;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_first_connected_copy(
        &second, 0, 1, &connected, &witness));
    CHECK(connected);
    CHECK(same_version(&witness, forest_version(&first)));
    bool disconnected = true;
    d7_version absent;
    absent.rep = NULL;
    CHECK_STATUS(d7_hamt_ancestral_connection_forest_first_connected_copy(
        &second, 0, 2, &disconnected, &absent));
    CHECK(!disconnected);
    CHECK(absent.rep == NULL);

    forest_destroy(&second);
    forest_destroy(&first);
    forest_destroy(&root);
    CHECK(d7_hamt_ancestral_connection_version_depth(&witness) == 1u);
    CHECK(same_version(
        d7_hamt_ancestral_connection_version_root_ref(&witness), &history_root));

    d7_hamt_ancestral_connection_version_destroy(&witness);
    d7_hamt_ancestral_connection_version_destroy(&history_root);
    d7_hamt_ancestral_connection_version_destroy(&grandparent);
    d7_hamt_ancestral_connection_version_destroy(&parent);
    d7_hamt_ancestral_connection_version_destroy(&relocated);
}

#ifdef D7_HAMT_TESTING
static void test_failed_allocations_leave_the_source_untouched(void)
{
    d7_forest root;
    d7_forest first;
    d7_forest second;
    forest_init(&root, 6);
    forest_link(&root, 0, 1, &first);
    forest_link(&first, 2, 3, &second);
    const void* const identity =
        d7_hamt_ancestral_connection_forest_debug_root_identity(&second);

    bool observed_failure = false;
    for (size_t budget = 0u; budget < 10u; ++budget) {
        d7_forest attempt;
        attempt.version.rep = NULL;
        d7_hamt_ancestral_connection_forest_test_fail_allocations_after(budget);
        const d7_hamt_status status =
            d7_hamt_ancestral_connection_forest_link(&second, 1, 3, &attempt);
        d7_hamt_ancestral_connection_forest_test_reset_allocator();
        if (status == D7_HAMT_OK) {
            CHECK(d7_hamt_ancestral_connection_forest_component_count(&attempt) == 3);
            (void)forest_validate(&attempt);
            forest_destroy(&attempt);
        } else {
            observed_failure = true;
            CHECK(status == D7_HAMT_OUT_OF_MEMORY);
            CHECK(attempt.version.rep == NULL);
        }

        /* The source keeps its own version, counts, and connectivity index either way. */
        CHECK(d7_hamt_ancestral_connection_forest_component_count(&second) == 4);
        CHECK(d7_hamt_ancestral_connection_version_depth(forest_version(&second)) == 2u);
        CHECK(d7_hamt_ancestral_connection_forest_debug_root_identity(&second) == identity);
        CHECK(!forest_connected(&second, 1, 3));
        (void)forest_validate(&second);
    }
    CHECK(observed_failure);

    forest_destroy(&second);
    forest_destroy(&first);
    forest_destroy(&root);
}
#endif

int main(void)
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    int passed = 0;
    test_init_produces_singleton_components_and_root_history();
    puts("[PASS] init produces singleton components and a root history");
    ++passed;
    test_link_records_the_first_connection_version();
    puts("[PASS] link records the first connection version");
    ++passed;
    test_redundant_links_create_versions_and_share_connectivity_state();
    puts("[PASS] redundant links create versions and share connectivity state");
    ++passed;
    test_sibling_branches_keep_equal_depth_version_tags_distinct();
    puts("[PASS] sibling branches keep equal-depth version tags distinct");
    ++passed;
    test_first_connected_uses_pair_lca_rather_than_latest_component_birth();
    puts("[PASS] first connected uses the pair LCA rather than the latest component birth");
    ++passed;
    test_first_connected_handles_one_endpoint_at_lca_after_it_becomes_a_child();
    puts("[PASS] first connected handles an endpoint at an LCA that later becomes a child");
    ++passed;
    test_balanced_unions_respect_height_and_retain_witnesses();
    puts("[PASS] balanced unions respect the union-by-size height and retain witnesses");
    ++passed;
    test_random_branching_histories_match_naive_ancestor_scan();
    puts("[PASS] random branching histories match the naive ancestor scan");
    ++passed;
    test_long_redundant_history_keeps_the_successful_union_witness();
    puts("[PASS] a long redundant history keeps the successful-union witness");
    ++passed;
    test_huge_sparse_universe_stores_only_touched_vertices();
    puts("[PASS] a huge sparse universe stores only the vertices a union touched");
    ++passed;
    test_operations_reject_vertices_outside_the_universe();
    puts("[PASS] operations reject vertices outside the universe");
    ++passed;
    test_component_size_reports_the_cached_union_size();
    puts("[PASS] component size reports the cached union size");
    ++passed;
    test_separate_forests_have_separate_version_trees();
    puts("[PASS] separate forests have separate version trees");
    ++passed;
    test_successful_union_rebuilds_only_the_touched_cells();
    puts("[PASS] a successful union rebuilds only the touched cells");
    ++passed;
    test_version_handles_copy_move_and_borrow();
    puts("[PASS] version handles copy, move, and borrow");
    ++passed;
#ifdef D7_HAMT_TESTING
    test_failed_allocations_leave_the_source_untouched();
    puts("[PASS] failed allocations leave the source untouched");
    ++passed;
#endif
    printf("%d test(s) passed\n", passed);
    return EXIT_SUCCESS;
}
