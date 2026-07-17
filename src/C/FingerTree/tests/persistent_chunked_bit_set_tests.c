#include <tools/data_structures/finger_tree/persistent_chunked_bit_set.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
    exit(EXIT_FAILURE); } } while (0)
#define CHECK_STATUS(expression) CHECK((expression) == FT_STATUS_OK)

typedef struct visit_state {
    int32_t values[32];
    size_t count;
} visit_state;

static void record_bit(int32_t bit, void* raw_state)
{
    visit_state* state = (visit_state*)raw_state;
    state->values[state->count++] = bit;
}

static void require_values(
    const ft_persistent_chunked_bit_set* set,
    const int32_t* expected,
    size_t count)
{
    visit_state state = {{0}, 0u};
    CHECK_STATUS(ft_persistent_chunked_bit_set_visit(set, record_bit, &state));
    CHECK(state.count == count);
    for (size_t index = 0u; index < count; ++index) {
        CHECK(state.values[index] == expected[index]);
    }
}

int main(void)
{
    ft_persistent_chunked_bit_set empty;
    CHECK_STATUS(ft_persistent_chunked_bit_set_init(&empty));
    CHECK(ft_persistent_chunked_bit_set_empty(&empty));
    CHECK(!ft_persistent_chunked_bit_set_contains(&empty, -1));
    ft_persistent_chunked_bit_set invalid;
    CHECK(ft_persistent_chunked_bit_set_add(&empty, -1, &invalid)
        == FT_STATUS_INVALID_ARGUMENT);

    const int32_t source_values[] = {65, 0, 64, 63, 1, 130, 65, INT32_MAX};
    ft_persistent_chunked_bit_set set;
    CHECK_STATUS(ft_persistent_chunked_bit_set_from_array(
        &set, source_values, sizeof(source_values) / sizeof(source_values[0])));
    const int32_t expected[] = {0, 1, 63, 64, 65, 130, INT32_MAX};
    require_values(&set, expected, sizeof(expected) / sizeof(expected[0]));
    CHECK(ft_persistent_chunked_bit_set_count(&set) == 7u);
    CHECK(ft_persistent_chunked_bit_set_chunk_count(&set) == 4u);
    CHECK(ft_persistent_chunked_bit_set_debug_validate(&set));

    uint64_t rank = 0u;
    CHECK_STATUS(ft_persistent_chunked_bit_set_rank(&set, 64, &rank));
    CHECK(rank == 4u);
    CHECK_STATUS(ft_persistent_chunked_bit_set_rank(&set, 129, &rank));
    CHECK(rank == 5u);
    for (uint64_t index = 0u; index < 7u; ++index) {
        int32_t selected = -1;
        CHECK_STATUS(ft_persistent_chunked_bit_set_select(&set, index, &selected));
        CHECK(selected == expected[index]);
    }
    int32_t selected = -1;
    CHECK(ft_persistent_chunked_bit_set_select(&set, 7u, &selected)
        == FT_STATUS_OUT_OF_RANGE);

    ft_persistent_chunked_bit_set unchanged;
    CHECK_STATUS(ft_persistent_chunked_bit_set_add(&set, 65, &unchanged));
    CHECK(ft_persistent_chunked_bit_set_debug_shares_root(&set, &unchanged));
    ft_persistent_chunked_bit_set_dispose(&unchanged);

    ft_persistent_chunked_bit_set branch;
    CHECK_STATUS(ft_persistent_chunked_bit_set_remove(&set, 64, &branch));
    CHECK_STATUS(ft_persistent_chunked_bit_set_remove(&branch, 65, &branch));
    CHECK(!ft_persistent_chunked_bit_set_contains(&branch, 64));
    CHECK(ft_persistent_chunked_bit_set_contains(&set, 64));
    CHECK(ft_persistent_chunked_bit_set_debug_validate(&branch));

    const int32_t right_values[] = {1, 2, 64, 65};
    ft_persistent_chunked_bit_set right;
    CHECK_STATUS(ft_persistent_chunked_bit_set_from_array(
        &right, right_values, sizeof(right_values) / sizeof(right_values[0])));
    const int32_t left_values[] = {0, 1, 64, 130};
    ft_persistent_chunked_bit_set left;
    CHECK_STATUS(ft_persistent_chunked_bit_set_from_array(
        &left, left_values, sizeof(left_values) / sizeof(left_values[0])));
    ft_persistent_chunked_bit_set algebra;
    CHECK_STATUS(ft_persistent_chunked_bit_set_union(&left, &right, &algebra));
    const int32_t union_values[] = {0, 1, 2, 64, 65, 130};
    require_values(&algebra, union_values, 6u);
    ft_persistent_chunked_bit_set_dispose(&algebra);
    CHECK_STATUS(ft_persistent_chunked_bit_set_intersect(&left, &right, &algebra));
    const int32_t intersection_values[] = {1, 64};
    require_values(&algebra, intersection_values, 2u);
    ft_persistent_chunked_bit_set_dispose(&algebra);
    CHECK_STATUS(ft_persistent_chunked_bit_set_except(&left, &right, &algebra));
    const int32_t except_values[] = {0, 130};
    require_values(&algebra, except_values, 2u);
    ft_persistent_chunked_bit_set_dispose(&algebra);
    CHECK_STATUS(ft_persistent_chunked_bit_set_symmetric_except(&left, &right, &algebra));
    const int32_t symmetric_values[] = {0, 2, 65, 130};
    require_values(&algebra, symmetric_values, 4u);

    ft_persistent_chunked_bit_set_dispose(&algebra);
    ft_persistent_chunked_bit_set_dispose(&left);
    ft_persistent_chunked_bit_set_dispose(&right);
    ft_persistent_chunked_bit_set_dispose(&branch);
    ft_persistent_chunked_bit_set_dispose(&set);
    ft_persistent_chunked_bit_set_dispose(&empty);
    puts("[PASS] persistent chunked bit set");
    return EXIT_SUCCESS;
}
