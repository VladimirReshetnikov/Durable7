#include <Tools/DataStructures/Hamt/persistent_directed_graph.h>
#include <Tools/DataStructures/Hamt/persistent_indexed_map.h>
#include <Tools/DataStructures/Hamt/persistent_map_patch.h>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
    exit(EXIT_FAILURE); } } while (0)
#define CHECK_STATUS(expression) CHECK((expression) == TDS_HAMT_OK)

static uint32_t int_hash(const void* raw, void* context)
{
    (void)context;
    return (uint32_t)*(const int*)raw * UINT32_C(2654435761);
}

static bool int_equal(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

static void* int_retain(const void* raw, void* context)
{
    (void)context;
    int* copy = (int*)malloc(sizeof(*copy));
    if (copy != NULL) {
        *copy = *(const int*)raw;
    }
    return copy;
}

static void int_release(void* raw, void* context)
{
    (void)context;
    free(raw);
}

static tds_hamt_set_policy int_set_policy(void)
{
    const tds_hamt_set_policy policy = {
        int_hash, int_equal, int_retain, int_release, NULL };
    return policy;
}

static tds_hamt_policy int_map_policy(void)
{
    const tds_hamt_policy policy = {
        int_hash, int_equal, int_equal,
        int_retain, int_retain, int_release, int_release, NULL };
    return policy;
}

static void map_set(tds_hamt_map* map, int key, int value)
{
    tds_hamt_map next;
    CHECK_STATUS(tds_hamt_map_set(map, &key, &value, &next));
    tds_hamt_map_destroy(map);
    *map = next;
}

static void test_map_patch(void)
{
    const tds_hamt_policy policy = int_map_policy();
    tds_hamt_map source = tds_hamt_map_create(&policy);
    map_set(&source, 1, 10);
    map_set(&source, 2, 20);
    tds_hamt_map middle = tds_hamt_map_clone(&source);
    tds_hamt_map next;
    const int one = 1;
    CHECK_STATUS(tds_hamt_map_remove(&middle, &one, &next));
    tds_hamt_map_destroy(&middle);
    middle = next;
    map_set(&middle, 2, 22);
    map_set(&middle, 3, 30);
    tds_hamt_map target = tds_hamt_map_clone(&middle);
    map_set(&target, 3, 33);
    map_set(&target, 4, 40);

    tds_hamt_map_patch first;
    tds_hamt_map_patch second;
    CHECK_STATUS(tds_hamt_map_patch_between(&source, &middle, &first));
    CHECK_STATUS(tds_hamt_map_patch_between(&middle, &target, &second));
    CHECK(tds_hamt_map_patch_count(&first) == 3u);
    CHECK(tds_hamt_map_patch_debug_validate(&first));
    tds_hamt_map applied;
    CHECK_STATUS(tds_hamt_map_patch_apply(&first, &source, &applied));
    CHECK(tds_hamt_map_equals(&applied, &middle));
    tds_hamt_map_destroy(&applied);

    tds_hamt_map_patch composed;
    CHECK_STATUS(tds_hamt_map_patch_compose(&first, &second, &composed));
    CHECK_STATUS(tds_hamt_map_patch_apply(&composed, &source, &applied));
    CHECK(tds_hamt_map_equals(&applied, &target));
    tds_hamt_map_destroy(&applied);
    tds_hamt_map_patch inverse;
    CHECK_STATUS(tds_hamt_map_patch_invert(&composed, &inverse));
    CHECK_STATUS(tds_hamt_map_patch_apply(&inverse, &target, &applied));
    CHECK(tds_hamt_map_equals(&applied, &source));
    tds_hamt_map_destroy(&applied);

    const int conflict_key = 2;
    tds_hamt_map conflicting = tds_hamt_map_clone(&source);
    map_set(&conflicting, conflict_key, 99);
    bool did_apply = true;
    const void* reported = NULL;
    CHECK_STATUS(tds_hamt_map_patch_try_apply(
        &first, &conflicting, &did_apply, &reported, &applied));
    CHECK(!did_apply && reported != NULL);
    CHECK(tds_hamt_map_equals(&applied, &conflicting));
    tds_hamt_map_destroy(&applied);
    tds_hamt_map_destroy(&conflicting);

    const int five = 5;
    const int fifty = 50;
    const tds_hamt_map_patch_entry no_op = {
        &five, {true, &fifty}, {true, &fifty} };
    tds_hamt_map_patch unchanged;
    CHECK_STATUS(tds_hamt_map_patch_add(&first, &no_op, &unchanged));
    CHECK(tds_hamt_map_patch_debug_shares_root(&first, &unchanged));
    tds_hamt_map_patch_destroy(&unchanged);

    tds_hamt_map_patch_destroy(&inverse);
    tds_hamt_map_patch_destroy(&composed);
    tds_hamt_map_patch_destroy(&second);
    tds_hamt_map_patch_destroy(&first);
    tds_hamt_map_destroy(&target);
    tds_hamt_map_destroy(&middle);
    tds_hamt_map_destroy(&source);
}

static void test_directed_graph(void)
{
    const tds_hamt_set_policy policy = int_set_policy();
    tds_hamt_directed_graph graph;
    CHECK_STATUS(tds_hamt_directed_graph_init(&graph, &policy));
    const int one = 1, two = 2, three = 3, nine = 9;
    CHECK_STATUS(tds_hamt_directed_graph_add_vertex(&graph, &nine, &graph));
    CHECK_STATUS(tds_hamt_directed_graph_add_edge(&graph, &one, &two, &graph));
    CHECK_STATUS(tds_hamt_directed_graph_add_edge(&graph, &one, &three, &graph));
    CHECK_STATUS(tds_hamt_directed_graph_add_edge(&graph, &two, &three, &graph));
    CHECK(tds_hamt_directed_graph_vertex_count(&graph) == 4u);
    CHECK(tds_hamt_directed_graph_edge_count(&graph) == 3);
    CHECK(tds_hamt_directed_graph_out_degree(&graph, &one) == 2u);
    CHECK(tds_hamt_directed_graph_in_degree(&graph, &three) == 2u);
    CHECK(tds_hamt_directed_graph_debug_validate(&graph));

    tds_hamt_directed_graph reversed;
    CHECK_STATUS(tds_hamt_directed_graph_reverse(&graph, &reversed));
    CHECK(tds_hamt_directed_graph_contains_edge(&reversed, &two, &one));
    tds_hamt_directed_graph restored;
    CHECK_STATUS(tds_hamt_directed_graph_reverse(&reversed, &restored));
    CHECK(tds_hamt_directed_graph_debug_shares_roots(&graph, &restored));

    tds_hamt_directed_graph branch;
    CHECK_STATUS(tds_hamt_directed_graph_remove_vertex(&graph, &two, &branch));
    CHECK(!tds_hamt_directed_graph_contains_vertex(&branch, &two));
    CHECK(tds_hamt_directed_graph_contains_edge(&graph, &one, &two));
    CHECK(tds_hamt_directed_graph_debug_validate(&branch));
    tds_hamt_directed_graph_destroy(&branch);
    tds_hamt_directed_graph_destroy(&restored);
    tds_hamt_directed_graph_destroy(&reversed);
    tds_hamt_directed_graph_destroy(&graph);
}

typedef struct selector_context {
    int values[2];
    int calls;
} selector_context;

static tds_hamt_status select_parity(
    const void* key,
    const void* value,
    void* raw_context,
    const void** index_key)
{
    (void)key;
    selector_context* context = (selector_context*)raw_context;
    ++context->calls;
    *index_key = &context->values[*(const int*)value & 1];
    return TDS_HAMT_OK;
}

static void test_indexed_map(void)
{
    const tds_hamt_set_policy policy = int_set_policy();
    selector_context selector = {{0, 1}, 0};
    tds_hamt_indexed_map map;
    CHECK_STATUS(tds_hamt_indexed_map_init(
        &map, &policy, &policy, &policy, select_parity, &selector));
    const int one = 1, two = 2, three = 3;
    const int ten = 10, twelve = 12, eleven = 11, thirteen = 13;
    CHECK_STATUS(tds_hamt_indexed_map_add(&map, &one, &ten, &map));
    CHECK_STATUS(tds_hamt_indexed_map_add(&map, &two, &twelve, &map));
    CHECK_STATUS(tds_hamt_indexed_map_add(&map, &three, &eleven, &map));
    CHECK(tds_hamt_indexed_map_count(&map) == 3u);
    CHECK(tds_hamt_indexed_map_count_by_index(&map, &selector.values[0]) == 2u);
    CHECK(tds_hamt_indexed_map_debug_validate(&map));

    const int calls = selector.calls;
    tds_hamt_indexed_map unchanged;
    CHECK_STATUS(tds_hamt_indexed_map_set(&map, &one, &ten, &unchanged));
    CHECK(tds_hamt_indexed_map_debug_shares_roots(&map, &unchanged));
    CHECK(selector.calls == calls);
    tds_hamt_indexed_map_destroy(&unchanged);

    tds_hamt_indexed_map branch;
    CHECK_STATUS(tds_hamt_indexed_map_set(&map, &one, &thirteen, &branch));
    CHECK(tds_hamt_indexed_map_count_by_index(&branch, &selector.values[0]) == 1u);
    CHECK(tds_hamt_indexed_map_count_by_index(&branch, &selector.values[1]) == 2u);
    const void* value = NULL;
    CHECK(tds_hamt_indexed_map_try_get(&map, &one, &value));
    CHECK(*(const int*)value == ten);
    CHECK(tds_hamt_indexed_map_debug_validate(&branch));
    CHECK_STATUS(tds_hamt_indexed_map_remove(&branch, &two, &branch));
    CHECK(!tds_hamt_indexed_map_contains_key(&branch, &two));
    tds_hamt_indexed_map_destroy(&branch);
    tds_hamt_indexed_map_destroy(&map);
}

int main(void)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    test_map_patch();
    puts("[PASS] persistent map patch");
    test_directed_graph();
    puts("[PASS] persistent directed graph");
    test_indexed_map();
    puts("[PASS] persistent indexed map");
    puts("3 test(s) passed");
    return EXIT_SUCCESS;
}
