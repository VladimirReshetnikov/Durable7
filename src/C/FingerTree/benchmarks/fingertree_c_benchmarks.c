#include <tools/data_structures/finger_tree/fingertree.h>
#include <tools/data_structures/finger_tree/rrb_vector.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int compare_ints(const void* left, const void* right, void* context)
{
    (void)context;
    const int left_value = *(const int*)left;
    const int right_value = *(const int*)right;
    return (left_value > right_value) - (left_value < right_value);
}

static double elapsed_ms(clock_t start, clock_t end)
{
    return 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
}

static bool equal_ints(const void* left, const void* right, void* context)
{
    (void)context;
    return *(const int*)left == *(const int*)right;
}

static int benchmark_rrb_vector(size_t count)
{
    int* values = (int*)malloc(count * sizeof(int));
    if (values == NULL) {
        return 1;
    }
    for (size_t index = 0; index != count; ++index) {
        values[index] = (int)index;
    }

    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_rrb_policy policy;
    ft_rrb_policy_init(&policy, &int_type, equal_ints, NULL);

    const clock_t build_start = clock();
    ft_rrb_vector vector;
    if (ft_rrb_vector_from_array(&vector, &policy, values, count) != FT_STATUS_OK) {
        free(values);
        return 1;
    }
    long long probe_sum = 0;
    for (size_t index = 0; index < count; index += 257) {
        int value = 0;
        if (ft_rrb_vector_at(&vector, index, &value) != FT_STATUS_OK) {
            ft_rrb_vector_dispose(&vector);
            free(values);
            return 1;
        }
        probe_sum += value;
    }
    ft_rrb_split_result split;
    if (ft_rrb_vector_split_at(&vector, count / 2, &split) != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&vector);
        free(values);
        return 1;
    }
    ft_rrb_vector joined;
    if (ft_rrb_vector_concat(&split.left, &split.right, &joined) != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&split.left);
        ft_rrb_vector_dispose(&split.right);
        ft_rrb_vector_dispose(&vector);
        free(values);
        return 1;
    }
    const clock_t build_end = clock();
    printf("rrb_build_index_split_concat,%zu,%.3f,%lld\n", count, elapsed_ms(build_start, build_end), probe_sum);

    const clock_t builder_start = clock();
    ft_rrb_builder builder;
    if (ft_rrb_builder_init(&builder, &policy) != FT_STATUS_OK ||
        ft_rrb_builder_append_range(&builder, values, count) != FT_STATUS_OK) {
        ft_rrb_builder_dispose(&builder);
        ft_rrb_vector_dispose(&joined);
        ft_rrb_vector_dispose(&split.left);
        ft_rrb_vector_dispose(&split.right);
        ft_rrb_vector_dispose(&vector);
        free(values);
        return 1;
    }
    ft_rrb_vector built;
    if (ft_rrb_builder_to_vector(&builder, &built) != FT_STATUS_OK) {
        ft_rrb_builder_dispose(&builder);
        ft_rrb_vector_dispose(&joined);
        ft_rrb_vector_dispose(&split.left);
        ft_rrb_vector_dispose(&split.right);
        ft_rrb_vector_dispose(&vector);
        free(values);
        return 1;
    }
    const clock_t builder_end = clock();
    printf("rrb_builder_freeze,%zu,%.3f,%zu\n", count, elapsed_ms(builder_start, builder_end), ft_rrb_vector_size(&built));

    ft_rrb_vector_dispose(&built);
    ft_rrb_builder_dispose(&builder);
    ft_rrb_vector_dispose(&joined);
    ft_rrb_vector_dispose(&split.left);
    ft_rrb_vector_dispose(&split.right);
    ft_rrb_vector_dispose(&vector);
    free(values);
    return 0;
}

static int benchmark_deque(size_t count)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_tree_policy policy;
    ft_tree_policy_init_size(&policy, &int_type);

    ft_persistent_deque deque;
    if (ft_persistent_deque_init(&deque, &policy) != FT_STATUS_OK) {
        return 1;
    }

    const clock_t start = clock();
    for (size_t index = 0; index != count; ++index) {
        const int value = (int)index;
        ft_persistent_deque next;
        if (ft_persistent_deque_push_back(&deque, &value, &next) != FT_STATUS_OK) {
            ft_persistent_deque_dispose(&deque);
            return 1;
        }

        ft_persistent_deque_dispose(&deque);
        deque = next;
    }

    long long probe_sum = 0;
    for (size_t index = 0; index < count; index += 257) {
        int value = 0;
        if (ft_persistent_deque_at(&deque, index, &value) != FT_STATUS_OK) {
            ft_persistent_deque_dispose(&deque);
            return 1;
        }

        probe_sum += value;
    }

    const clock_t end = clock();
    printf("deque_push_index,%zu,%.3f,%lld\n", count, elapsed_ms(start, end), probe_sum);
    ft_persistent_deque_dispose(&deque);
    return 0;
}

static int benchmark_rope(size_t count)
{
    int* values = (int*)malloc(count * sizeof(int));
    if (values == NULL) {
        return 1;
    }

    for (size_t index = 0; index != count; ++index) {
        values[index] = (int)index;
    }

    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));

    const clock_t start = clock();
    ft_rope rope;
    if (ft_rope_from_array(&rope, &int_type, values, count) != FT_STATUS_OK) {
        free(values);
        return 1;
    }

    ft_rope_split_result split;
    if (ft_rope_split_at(&rope, count / 2, &split) != FT_STATUS_OK) {
        ft_rope_dispose(&rope);
        free(values);
        return 1;
    }

    ft_rope joined;
    if (ft_rope_concat(&split.left, &split.right, &joined) != FT_STATUS_OK) {
        ft_rope_dispose(&split.left);
        ft_rope_dispose(&split.right);
        ft_rope_dispose(&rope);
        free(values);
        return 1;
    }

    const clock_t end = clock();
    printf("rope_build_split_concat,%zu,%.3f,%zu\n", count, elapsed_ms(start, end), ft_rope_size(&joined));
    ft_rope_dispose(&joined);
    ft_rope_dispose(&split.left);
    ft_rope_dispose(&split.right);
    ft_rope_dispose(&rope);
    free(values);
    return 0;
}

static int benchmark_sorted_set(size_t count)
{
    ft_value_type int_type;
    ft_value_type_init(&int_type, sizeof(int));
    ft_tree_policy policy;
    ft_tree_policy_init_size(&policy, &int_type);

    ft_sorted_set set;
    if (ft_sorted_set_init(&set, &policy, compare_ints, NULL) != FT_STATUS_OK) {
        return 1;
    }

    const clock_t start = clock();
    for (size_t index = 0; index != count; ++index) {
        const int value = (int)((count - index) % count);
        ft_sorted_set next;
        if (ft_sorted_set_add(&set, &value, &next) != FT_STATUS_OK) {
            ft_sorted_set_dispose(&set);
            return 1;
        }

        ft_sorted_set_dispose(&set);
        set = next;
    }

    const clock_t end = clock();
    printf("sorted_set_insert,%zu,%.3f,%zu\n", count, elapsed_ms(start, end), ft_sorted_set_size(&set));
    ft_sorted_set_dispose(&set);
    return 0;
}

int main(int argc, char** argv)
{
    size_t count = 10000;
    if (argc > 1) {
        count = (size_t)strtoull(argv[1], NULL, 10);
        if (count == 0) {
            count = 10000;
        }
    }

    printf("benchmark,count,elapsed_ms,check\n");
    if (benchmark_deque(count) != 0 || benchmark_rope(count) != 0 ||
        benchmark_rrb_vector(count) != 0 || benchmark_sorted_set(count / 2) != 0) {
        return 1;
    }

    return 0;
}
