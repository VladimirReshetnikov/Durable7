#include <tools/data_structures/finger_tree/fingertree.h>

#include <stdbool.h>
#include <stdio.h>

static int compare_ints(const void* left, const void* right, void* context)
{
    (void)context;
    const int left_value = *(const int*)left;
    const int right_value = *(const int*)right;
    return (left_value > right_value) - (left_value < right_value);
}

static void init_int_type(ft_value_type* type)
{
    ft_value_type_init(type, sizeof(int));
}

static int run_priority_queue(void)
{
    ft_value_type int_type;
    init_int_type(&int_type);

    ft_priority_queue queue;
    if (ft_priority_queue_init(&queue, &int_type, &int_type, compare_ints, NULL) != FT_STATUS_OK) {
        return 1;
    }

    const int values[] = {40, 10, 30, 20};
    const int priorities[] = {4, 1, 3, 2};
    for (size_t index = 0; index != 4; ++index) {
        ft_priority_queue next;
        if (ft_priority_queue_push(&queue, &values[index], &priorities[index], &next) != FT_STATUS_OK) {
            ft_priority_queue_dispose(&queue);
            return 1;
        }

        ft_priority_queue_dispose(&queue);
        queue = next;
    }

    printf("priority:");
    while (!ft_priority_queue_empty(&queue)) {
        bool found = false;
        int value = 0;
        int priority = 0;
        ft_priority_queue rest;
        if (ft_priority_queue_try_pop(&queue, &found, &value, &priority, &rest) != FT_STATUS_OK || !found) {
            ft_priority_queue_dispose(&queue);
            return 1;
        }

        printf(" %d@%d", value, priority);
        ft_priority_queue_dispose(&queue);
        queue = rest;
    }

    printf("\n");
    ft_priority_queue_dispose(&queue);
    return 0;
}

static int run_sorted_set(void)
{
    ft_value_type int_type;
    init_int_type(&int_type);
    ft_tree_policy policy;
    ft_tree_policy_init_size(&policy, &int_type);

    ft_sorted_set set;
    if (ft_sorted_set_init(&set, &policy, compare_ints, NULL) != FT_STATUS_OK) {
        return 1;
    }

    const int values[] = {5, 1, 3, 3, 2};
    for (size_t index = 0; index != 5; ++index) {
        ft_sorted_set next;
        if (ft_sorted_set_add(&set, &values[index], &next) != FT_STATUS_OK) {
            ft_sorted_set_dispose(&set);
            return 1;
        }

        ft_sorted_set_dispose(&set);
        set = next;
    }

    printf("sorted-set:");
    for (size_t index = 0; index != ft_sorted_set_size(&set); ++index) {
        int value = 0;
        if (ft_sorted_set_at(&set, index, &value) != FT_STATUS_OK) {
            ft_sorted_set_dispose(&set);
            return 1;
        }

        printf(" %d", value);
    }

    printf("\n");
    ft_sorted_set_dispose(&set);
    return 0;
}

static int run_intervals(void)
{
    ft_interval_tree_i64 intervals;
    if (ft_interval_tree_i64_init(&intervals) != FT_STATUS_OK) {
        return 1;
    }

    const ft_interval_i64 values[] = {{1, 3}, {5, 8}, {4, 6}};
    for (size_t index = 0; index != 3; ++index) {
        ft_interval_tree_i64 next;
        if (ft_interval_tree_i64_insert(&intervals, values[index], &next) != FT_STATUS_OK) {
            ft_interval_tree_i64_dispose(&intervals);
            return 1;
        }

        ft_interval_tree_i64_dispose(&intervals);
        intervals = next;
    }

    const ft_interval_i64 query = {6, 7};
    printf("interval-overlaps: %zu\n", ft_interval_tree_i64_count_overlaps(&intervals, query));
    ft_interval_tree_i64_dispose(&intervals);
    return 0;
}

static int run_rope(void)
{
    ft_text_rope rope;
    if (ft_text_rope_from_cstr("alpha\nbeta\n", &rope) != FT_STATUS_OK) {
        return 1;
    }

    ft_line_column location;
    if (ft_text_rope_line_column_of(&rope, 8, &location) != FT_STATUS_OK) {
        ft_text_rope_dispose(&rope);
        return 1;
    }

    printf("text: %zu chars, %zu lines, offset8=(%zu,%zu)\n",
        ft_text_rope_size(&rope),
        ft_text_rope_line_count(&rope),
        location.line,
        location.column);
    ft_text_rope_dispose(&rope);
    return 0;
}

int main(void)
{
    if (run_priority_queue() != 0 || run_sorted_set() != 0 || run_intervals() != 0 || run_rope() != 0) {
        return 1;
    }

    return 0;
}
