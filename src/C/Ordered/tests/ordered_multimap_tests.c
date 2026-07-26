/*
 * Tests for the insertion-ordered persistent multimap.
 */

#include <durable7/ordered/ordered_multimap.h>
#include <durable7/test_support/headless_test_process.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
    exit(EXIT_FAILURE); } } while (0)
#define CHECK_STATUS(expression) CHECK((expression) == D7_ORDERED_OK)

static uint32_t hash_int(const void* item, void* context)
{
    (void)context;
    return (uint32_t)*(const int*)item * UINT32_C(2654435761);
}

static bool equal_int(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

typedef struct visit_state {
    int pairs[10][2];
    size_t count;
} visit_state;

static void record_pair(const void* key, const void* value, void* raw_state)
{
    visit_state* state = (visit_state*)raw_state;
    state->pairs[state->count][0] = *(const int*)key;
    state->pairs[state->count][1] = *(const int*)value;
    ++state->count;
}

int main(void)
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    d7_ordered_policy policy;
    d7_ordered_policy_init(&policy, &int_type, hash_int, equal_int, NULL);
    d7_ordered_multimap map;
    CHECK_STATUS(d7_ordered_multimap_init(&map, &policy, &policy));

    const int b = 2, a = 1, c = 3;
    const int two = 20, nine = 90, one = 10, seven = 70, eight = 80;
    d7_ordered_multimap next;
#define ADD(key, value) do { \
    CHECK_STATUS(d7_ordered_multimap_add(&map, &(key), &(value), &next)); \
    d7_ordered_multimap_destroy(&map); \
    d7_ordered_multimap_move(&map, &next); \
} while (0)
    ADD(b, two);
    ADD(a, nine);
    ADD(b, one);
    ADD(c, seven);
    ADD(a, eight);
#undef ADD

    CHECK(d7_ordered_multimap_key_count(&map) == 3u);
    CHECK(d7_ordered_multimap_pair_count(&map) == 5);
    visit_state visited = {{{0}}, 0u};
    CHECK_STATUS(d7_ordered_multimap_visit(&map, record_pair, &visited));
    const int expected[5][2] = {{2,20},{2,10},{1,90},{1,80},{3,70}};
    CHECK(visited.count == 5u);
    for (size_t index = 0; index < 5u; ++index) {
        CHECK(visited.pairs[index][0] == expected[index][0]);
        CHECK(visited.pairs[index][1] == expected[index][1]);
    }
    CHECK(d7_ordered_multimap_debug_validate(&map));

    CHECK_STATUS(d7_ordered_multimap_add(&map, &b, &two, &next));
    CHECK(d7_ordered_multimap_debug_shares_groups(&map, &next));
    d7_ordered_multimap_destroy(&next);

    d7_ordered_multimap snapshot;
    CHECK_STATUS(d7_ordered_multimap_clone(&map, &snapshot));
    CHECK_STATUS(d7_ordered_multimap_remove(&map, &b, &two, &next));
    d7_ordered_multimap_destroy(&map);
    d7_ordered_multimap_move(&map, &next);
    CHECK_STATUS(d7_ordered_multimap_remove(&map, &b, &one, &next));
    d7_ordered_multimap_destroy(&map);
    d7_ordered_multimap_move(&map, &next);
    CHECK(!d7_ordered_multimap_contains_key(&map, &b));
    CHECK(d7_ordered_multimap_contains(&snapshot, &b, &two));
    CHECK_STATUS(d7_ordered_multimap_add(&map, &b, &two, &next));
    d7_ordered_multimap_destroy(&map);
    d7_ordered_multimap_move(&map, &next);
    visited.count = 0u;
    CHECK_STATUS(d7_ordered_multimap_visit(&map, record_pair, &visited));
    CHECK(visited.pairs[visited.count - 1u][0] == b);
    CHECK(d7_ordered_multimap_debug_validate(&map));

    d7_ordered_multimap_destroy(&snapshot);
    d7_ordered_multimap_destroy(&map);
    puts("[PASS] persistent ordered multimap");
    return EXIT_SUCCESS;
}
