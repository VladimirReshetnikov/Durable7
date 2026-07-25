#include <durable7/finger_tree/brodal_okasaki_heap.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_MSC_VER) || defined(__clang__)
#include <stdatomic.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
typedef volatile LONG64 ft_brodal_ref_count;

static void ft_brodal_ref_init(ft_brodal_ref_count* value)
{
    *value = 1;
}

static void ft_brodal_ref_retain(ft_brodal_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_brodal_ref_release(ft_brodal_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_brodal_ref_count;

static void ft_brodal_ref_init(ft_brodal_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_brodal_ref_retain(ft_brodal_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_brodal_ref_release(ft_brodal_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

typedef enum ft_brodal_release_kind {
    FT_BRODAL_RELEASE_TREE,
    FT_BRODAL_RELEASE_FOREST
} ft_brodal_release_kind;

typedef struct ft_brodal_release_header {
    ft_brodal_ref_count refs;
    ft_brodal_release_kind kind;
    struct ft_brodal_release_header* release_next;
} ft_brodal_release_header;

typedef struct ft_brodal_value {
    ft_brodal_ref_count refs;
    void* bytes;
} ft_brodal_value;

typedef struct ft_brodal_forest ft_brodal_forest;

struct ft_brodal_tree {
    ft_brodal_release_header header;
    unsigned rank;
    ft_brodal_value* value;
    ft_brodal_forest* children;
};

struct ft_brodal_forest {
    ft_brodal_release_header header;
    ft_brodal_tree* head;
    ft_brodal_forest* tail;
};

struct ft_brodal_policy_rep {
    ft_brodal_ref_count refs;
    ft_brodal_policy_config config;
};

typedef struct ft_brodal_visit_frame {
    ft_brodal_tree* tree;
    size_t depth;
} ft_brodal_visit_frame;

static void* ft_brodal_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_brodal_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_brodal_config_valid(const ft_brodal_policy_config* config)
{
    return config != NULL && config->value_size != 0 &&
        config->value_type_identity != NULL && config->compare != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL &&
        (config->destroy == NULL || config->copy != NULL);
}

static void* ft_brodal_allocate_config(
    const ft_brodal_policy_config* config,
    size_t size)
{
    return config->allocator.allocate(size, config->allocator.context);
}

static void ft_brodal_deallocate_config(
    const ft_brodal_policy_config* config,
    void* allocation)
{
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void* ft_brodal_allocate(const ft_brodal_policy_rep* policy, size_t size)
{
    return ft_brodal_allocate_config(&policy->config, size);
}

static void ft_brodal_deallocate(
    const ft_brodal_policy_rep* policy,
    void* allocation)
{
    ft_brodal_deallocate_config(&policy->config, allocation);
}

static bool ft_brodal_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool ft_brodal_add_overflows(size_t left, size_t right, size_t* result)
{
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static void ft_brodal_policy_retain(ft_brodal_policy_rep* policy)
{
    if (policy != NULL) {
        ft_brodal_ref_retain(&policy->refs);
    }
}

static void ft_brodal_policy_release(ft_brodal_policy_rep* policy)
{
    if (policy != NULL && ft_brodal_ref_release(&policy->refs)) {
        ft_brodal_deallocate_config(&policy->config, policy);
    }
}

static void ft_brodal_value_retain(ft_brodal_value* value)
{
    if (value != NULL) {
        ft_brodal_ref_retain(&value->refs);
    }
}

static void ft_brodal_value_release(
    const ft_brodal_policy_rep* policy,
    ft_brodal_value* value)
{
    if (value == NULL || !ft_brodal_ref_release(&value->refs)) {
        return;
    }
    if (policy->config.destroy != NULL) {
        policy->config.destroy(value->bytes, policy->config.callback_context);
    }
    ft_brodal_deallocate(policy, value->bytes);
    ft_brodal_deallocate(policy, value);
}

static ft_status ft_brodal_value_create(
    const ft_brodal_policy_rep* policy,
    const void* source,
    ft_brodal_value** result)
{
    ft_brodal_value* value = NULL;
    ft_status status = FT_STATUS_OK;
    if (source == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    value = (ft_brodal_value*)ft_brodal_allocate(policy, sizeof(*value));
    if (value == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    value->bytes = ft_brodal_allocate(policy, policy->config.value_size);
    if (value->bytes == NULL) {
        ft_brodal_deallocate(policy, value);
        return FT_STATUS_NO_MEMORY;
    }
    if (policy->config.copy == NULL) {
        (void)memcpy(value->bytes, source, policy->config.value_size);
    } else {
        status = policy->config.copy(
            value->bytes,
            source,
            policy->config.callback_context);
        if (status != FT_STATUS_OK) {
            ft_brodal_deallocate(policy, value->bytes);
            ft_brodal_deallocate(policy, value);
            return status;
        }
    }
    ft_brodal_ref_init(&value->refs);
    *result = value;
    return FT_STATUS_OK;
}

static void ft_brodal_tree_retain(ft_brodal_tree* tree)
{
    if (tree != NULL) {
        ft_brodal_ref_retain(&tree->header.refs);
    }
}

static void ft_brodal_forest_retain(ft_brodal_forest* forest)
{
    if (forest != NULL) {
        ft_brodal_ref_retain(&forest->header.refs);
    }
}

static void ft_brodal_release_graph(
    const ft_brodal_policy_rep* policy,
    ft_brodal_release_header* work)
{
    while (work != NULL) {
        ft_brodal_release_header* current = work;
        work = current->release_next;
        if (current->kind == FT_BRODAL_RELEASE_TREE) {
            ft_brodal_tree* tree = (ft_brodal_tree*)current;
            ft_brodal_forest* children = tree->children;
            ft_brodal_value_release(policy, tree->value);
            ft_brodal_deallocate(policy, tree);
            if (children != NULL && ft_brodal_ref_release(&children->header.refs)) {
                children->header.release_next = work;
                work = &children->header;
            }
        } else {
            ft_brodal_forest* forest = (ft_brodal_forest*)current;
            ft_brodal_tree* head = forest->head;
            ft_brodal_forest* tail = forest->tail;
            ft_brodal_deallocate(policy, forest);
            if (head != NULL && ft_brodal_ref_release(&head->header.refs)) {
                head->header.release_next = work;
                work = &head->header;
            }
            if (tail != NULL && ft_brodal_ref_release(&tail->header.refs)) {
                tail->header.release_next = work;
                work = &tail->header;
            }
        }
    }
}

static void ft_brodal_tree_release(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree* tree)
{
    if (tree != NULL && ft_brodal_ref_release(&tree->header.refs)) {
        tree->header.release_next = NULL;
        ft_brodal_release_graph(policy, &tree->header);
    }
}

static void ft_brodal_forest_release(
    const ft_brodal_policy_rep* policy,
    ft_brodal_forest* forest)
{
    if (forest != NULL && ft_brodal_ref_release(&forest->header.refs)) {
        forest->header.release_next = NULL;
        ft_brodal_release_graph(policy, &forest->header);
    }
}

static ft_status ft_brodal_tree_create(
    const ft_brodal_policy_rep* policy,
    unsigned rank,
    ft_brodal_value* value,
    ft_brodal_forest* children,
    ft_brodal_tree** result)
{
    ft_brodal_tree* tree = NULL;
    if (value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    tree = (ft_brodal_tree*)ft_brodal_allocate(policy, sizeof(*tree));
    if (tree == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_brodal_ref_init(&tree->header.refs);
    tree->header.kind = FT_BRODAL_RELEASE_TREE;
    tree->header.release_next = NULL;
    tree->rank = rank;
    tree->value = value;
    tree->children = children;
    ft_brodal_value_retain(value);
    ft_brodal_forest_retain(children);
    *result = tree;
    return FT_STATUS_OK;
}

static ft_status ft_brodal_forest_create(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree* head,
    ft_brodal_forest* tail,
    ft_brodal_forest** result)
{
    ft_brodal_forest* forest = NULL;
    if (head == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    forest = (ft_brodal_forest*)ft_brodal_allocate(policy, sizeof(*forest));
    if (forest == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_brodal_ref_init(&forest->header.refs);
    forest->header.kind = FT_BRODAL_RELEASE_FOREST;
    forest->header.release_next = NULL;
    forest->head = head;
    forest->tail = tail;
    ft_brodal_tree_retain(head);
    ft_brodal_forest_retain(tail);
    *result = forest;
    return FT_STATUS_OK;
}

static bool ft_brodal_heap_valid(const ft_brodal_heap* heap)
{
    return heap != NULL && heap->policy != NULL &&
        ((heap->root == NULL && heap->count == 0) ||
            (heap->root != NULL && heap->count != 0));
}

static ft_status ft_brodal_compare(
    const ft_brodal_policy_rep* policy,
    const void* left,
    const void* right,
    int* comparison)
{
    return policy->config.compare(
        left,
        right,
        comparison,
        policy->config.callback_context);
}

static ft_status ft_brodal_less_equal(
    const ft_brodal_policy_rep* policy,
    const void* left,
    const void* right,
    bool* result)
{
    int comparison = 0;
    ft_status status = ft_brodal_compare(policy, left, right, &comparison);
    if (status == FT_STATUS_OK) {
        *result = comparison <= 0;
    }
    return status;
}

static ft_status ft_brodal_prepend_owned(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree* tree,
    ft_brodal_forest** list)
{
    ft_brodal_forest* next = NULL;
    ft_status status = ft_brodal_forest_create(policy, tree, *list, &next);
    if (status == FT_STATUS_OK) {
        ft_brodal_forest_release(policy, *list);
        *list = next;
    }
    return status;
}

static ft_status ft_brodal_skew_link(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree* zero,
    ft_brodal_tree* first,
    ft_brodal_tree* second,
    ft_brodal_tree** result)
{
    ft_brodal_tree* winner = NULL;
    ft_brodal_tree* child_first = NULL;
    ft_brodal_tree* child_second = NULL;
    ft_brodal_forest* inner = NULL;
    ft_brodal_forest* children = NULL;
    bool first_le_zero = false;
    bool first_le_second = false;
    bool second_le_zero = false;
    bool second_le_first = false;
    ft_status status = FT_STATUS_OK;
    if (first->rank != second->rank || first->rank == UINT_MAX) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    status = ft_brodal_less_equal(
        policy, first->value->bytes, zero->value->bytes, &first_le_zero);
    if (status == FT_STATUS_OK && first_le_zero) {
        status = ft_brodal_less_equal(
            policy, first->value->bytes, second->value->bytes, &first_le_second);
    }
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (first_le_zero && first_le_second) {
        winner = first;
        child_first = zero;
        child_second = second;
    } else {
        status = ft_brodal_less_equal(
            policy, second->value->bytes, zero->value->bytes, &second_le_zero);
        if (status == FT_STATUS_OK && second_le_zero) {
            status = ft_brodal_less_equal(
                policy, second->value->bytes, first->value->bytes, &second_le_first);
        }
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (second_le_zero && second_le_first) {
            winner = second;
            child_first = zero;
            child_second = first;
        } else {
            winner = zero;
            child_first = first;
            child_second = second;
        }
    }
    status = ft_brodal_forest_create(
        policy, child_second, winner->children, &inner);
    if (status == FT_STATUS_OK) {
        status = ft_brodal_forest_create(policy, child_first, inner, &children);
    }
    if (status == FT_STATUS_OK) {
        status = ft_brodal_tree_create(
            policy, first->rank + 1, winner->value, children, result);
    }
    ft_brodal_forest_release(policy, children);
    ft_brodal_forest_release(policy, inner);
    return status;
}

static ft_status ft_brodal_link(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree* first,
    ft_brodal_tree* second,
    ft_brodal_tree** result)
{
    bool first_wins = false;
    ft_brodal_tree* winner = NULL;
    ft_brodal_tree* loser = NULL;
    ft_brodal_forest* children = NULL;
    ft_status status = FT_STATUS_OK;
    if (first->rank != second->rank || first->rank == UINT_MAX) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    status = ft_brodal_less_equal(
        policy, first->value->bytes, second->value->bytes, &first_wins);
    if (status != FT_STATUS_OK) {
        return status;
    }
    winner = first_wins ? first : second;
    loser = first_wins ? second : first;
    status = ft_brodal_forest_create(policy, loser, winner->children, &children);
    if (status == FT_STATUS_OK) {
        status = ft_brodal_tree_create(
            policy, first->rank + 1, winner->value, children, result);
    }
    ft_brodal_forest_release(policy, children);
    return status;
}

static ft_status ft_brodal_skew_insert(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree* tree,
    ft_brodal_forest* forest,
    ft_brodal_forest** result)
{
    ft_brodal_tree* linked = NULL;
    ft_status status = FT_STATUS_OK;
    if (forest != NULL && forest->tail != NULL &&
        forest->head->rank == forest->tail->head->rank) {
        status = ft_brodal_skew_link(
            policy, tree, forest->head, forest->tail->head, &linked);
        if (status == FT_STATUS_OK) {
            status = ft_brodal_forest_create(
                policy, linked, forest->tail->tail, result);
        }
        ft_brodal_tree_release(policy, linked);
        return status;
    }
    return ft_brodal_forest_create(policy, tree, forest, result);
}

static size_t ft_brodal_forest_length_bounded(
    const ft_brodal_forest* forest,
    size_t bound,
    bool* complete)
{
    size_t length = 0;
    while (forest != NULL && length <= bound) {
        ++length;
        forest = forest->tail;
    }
    *complete = forest == NULL;
    return length;
}

static ft_status ft_brodal_get_minimum_tree(
    const ft_brodal_policy_rep* policy,
    ft_brodal_forest* forest,
    size_t logical_bound,
    ft_brodal_tree** minimum,
    ft_brodal_forest** remainder)
{
    ft_brodal_forest** items = NULL;
    ft_brodal_forest* rebuilt = NULL;
    size_t length = 0;
    size_t bytes = 0;
    size_t minimum_index = 0;
    bool complete = false;
    ft_status status = FT_STATUS_OK;
    if (forest == NULL) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    length = ft_brodal_forest_length_bounded(forest, logical_bound, &complete);
    if (!complete || length == 0 ||
        ft_brodal_multiply_overflows(length, sizeof(*items), &bytes)) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    items = (ft_brodal_forest**)ft_brodal_allocate(policy, bytes);
    if (items == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    for (size_t index = 0; index != length; ++index) {
        items[index] = forest;
        forest = forest->tail;
    }
    for (size_t index = 1; index != length; ++index) {
        int comparison = 0;
        status = ft_brodal_compare(
            policy,
            items[index]->head->value->bytes,
            items[minimum_index]->head->value->bytes,
            &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (comparison < 0) {
            minimum_index = index;
        }
    }
    rebuilt = items[minimum_index]->tail;
    ft_brodal_forest_retain(rebuilt);
    for (size_t index = minimum_index; index != 0; --index) {
        status = ft_brodal_prepend_owned(policy, items[index - 1]->head, &rebuilt);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    *minimum = items[minimum_index]->head;
    *remainder = rebuilt;
    rebuilt = NULL;

cleanup:
    ft_brodal_forest_release(policy, rebuilt);
    ft_brodal_deallocate(policy, items);
    return status;
}

static ft_status ft_brodal_split_forest(
    const ft_brodal_policy_rep* policy,
    unsigned rank,
    ft_brodal_forest* source,
    ft_brodal_forest** zeros_result,
    ft_brodal_forest** trees_result,
    ft_brodal_forest** forest_result)
{
    ft_brodal_forest* zeros = NULL;
    ft_brodal_forest* trees = NULL;
    ft_brodal_forest* cursor_owned = NULL;
    ft_brodal_forest* cursor = source;
    ft_brodal_forest* remainder = NULL;
    ft_status status = FT_STATUS_OK;
    while (rank != 0) {
        ft_brodal_tree* first = NULL;
        ft_brodal_tree* second = NULL;
        ft_brodal_forest* rest = NULL;
        if (cursor == NULL) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            goto cleanup;
        }
        first = cursor->head;
        if (rank == 1 && cursor->tail == NULL) {
            status = ft_brodal_prepend_owned(policy, first, &trees);
            cursor = NULL;
            ft_brodal_forest_release(policy, cursor_owned);
            cursor_owned = NULL;
            break;
        }
        if (cursor->tail == NULL) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            goto cleanup;
        }
        second = cursor->tail->head;
        rest = cursor->tail->tail;
        if (rank == 1) {
            if (second->rank == 0) {
                status = ft_brodal_prepend_owned(policy, first, &zeros);
                if (status == FT_STATUS_OK) {
                    status = ft_brodal_prepend_owned(policy, second, &trees);
                }
                if (status != FT_STATUS_OK) {
                    goto cleanup;
                }
                cursor = rest;
                ft_brodal_forest_release(policy, cursor_owned);
                cursor_owned = NULL;
            } else {
                ft_brodal_forest* next_cursor = NULL;
                status = ft_brodal_prepend_owned(policy, first, &trees);
                if (status == FT_STATUS_OK) {
                    status = ft_brodal_forest_create(
                        policy, second, rest, &next_cursor);
                }
                if (status != FT_STATUS_OK) {
                    goto cleanup;
                }
                ft_brodal_forest_release(policy, cursor_owned);
                cursor_owned = next_cursor;
                cursor = next_cursor;
            }
            break;
        }
        if (first->rank == second->rank) {
            status = ft_brodal_prepend_owned(policy, second, &trees);
            if (status == FT_STATUS_OK) {
                status = ft_brodal_prepend_owned(policy, first, &trees);
            }
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            cursor = rest;
            ft_brodal_forest_release(policy, cursor_owned);
            cursor_owned = NULL;
            break;
        }
        if (first->rank == 0) {
            status = ft_brodal_prepend_owned(policy, first, &zeros);
            if (status == FT_STATUS_OK) {
                status = ft_brodal_prepend_owned(policy, second, &trees);
            }
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            cursor = rest;
            ft_brodal_forest_release(policy, cursor_owned);
            cursor_owned = NULL;
        } else {
            ft_brodal_forest* next_cursor = NULL;
            status = ft_brodal_prepend_owned(policy, first, &trees);
            if (status == FT_STATUS_OK) {
                status = ft_brodal_forest_create(
                    policy, second, rest, &next_cursor);
            }
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            ft_brodal_forest_release(policy, cursor_owned);
            cursor_owned = next_cursor;
            cursor = next_cursor;
        }
        --rank;
    }
    remainder = cursor;
    ft_brodal_forest_retain(remainder);
    *zeros_result = zeros;
    *trees_result = trees;
    *forest_result = remainder;
    zeros = NULL;
    trees = NULL;
    remainder = NULL;

cleanup:
    ft_brodal_forest_release(policy, remainder);
    ft_brodal_forest_release(policy, cursor_owned);
    ft_brodal_forest_release(policy, trees);
    ft_brodal_forest_release(policy, zeros);
    return status;
}

static ft_status ft_brodal_bucket_add(
    const ft_brodal_policy_rep* policy,
    ft_brodal_tree** buckets,
    size_t capacity,
    ft_brodal_tree* source)
{
    ft_brodal_tree* current = source;
    size_t rank = source->rank;
    ft_brodal_tree_retain(current);
    while (rank < capacity && buckets[rank] != NULL) {
        ft_brodal_tree* linked = NULL;
        ft_status status = ft_brodal_link(
            policy, buckets[rank], current, &linked);
        ft_brodal_tree_release(policy, buckets[rank]);
        buckets[rank] = NULL;
        ft_brodal_tree_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }
        current = linked;
        rank = current->rank;
    }
    if (rank >= capacity) {
        ft_brodal_tree_release(policy, current);
        return FT_STATUS_OVERFLOW;
    }
    buckets[rank] = current;
    return FT_STATUS_OK;
}

static ft_status ft_brodal_skew_meld(
    const ft_brodal_policy_rep* policy,
    ft_brodal_forest* left,
    ft_brodal_forest* right,
    size_t logical_bound,
    ft_brodal_forest** result)
{
    ft_brodal_tree** buckets = NULL;
    ft_brodal_forest* produced = NULL;
    size_t left_length = 0;
    size_t right_length = 0;
    size_t maximum_rank = 0;
    size_t capacity = 0;
    size_t bytes = 0;
    bool complete = false;
    ft_status status = FT_STATUS_OK;
    left_length = ft_brodal_forest_length_bounded(left, logical_bound, &complete);
    if (!complete) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    right_length = ft_brodal_forest_length_bounded(right, logical_bound, &complete);
    if (!complete) {
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    for (ft_brodal_forest* cursor = left; cursor != NULL; cursor = cursor->tail) {
        if (cursor->head->rank > maximum_rank) {
            maximum_rank = cursor->head->rank;
        }
    }
    for (ft_brodal_forest* cursor = right; cursor != NULL; cursor = cursor->tail) {
        if (cursor->head->rank > maximum_rank) {
            maximum_rank = cursor->head->rank;
        }
    }
    if (ft_brodal_add_overflows(left_length, right_length, &capacity) ||
        ft_brodal_add_overflows(capacity, maximum_rank, &capacity) ||
        ft_brodal_add_overflows(capacity, 2, &capacity) ||
        ft_brodal_multiply_overflows(capacity, sizeof(*buckets), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    if (capacity == 2) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    buckets = (ft_brodal_tree**)ft_brodal_allocate(policy, bytes);
    if (buckets == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(buckets, 0, bytes);
    for (ft_brodal_forest* cursor = left; cursor != NULL; cursor = cursor->tail) {
        status = ft_brodal_bucket_add(policy, buckets, capacity, cursor->head);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    for (ft_brodal_forest* cursor = right; cursor != NULL; cursor = cursor->tail) {
        status = ft_brodal_bucket_add(policy, buckets, capacity, cursor->head);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    for (size_t index = capacity; index != 0; --index) {
        ft_brodal_tree* tree = buckets[index - 1];
        if (tree != NULL) {
            status = ft_brodal_prepend_owned(policy, tree, &produced);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
        }
    }
    *result = produced;
    produced = NULL;

cleanup:
    ft_brodal_forest_release(policy, produced);
    if (buckets != NULL) {
        for (size_t index = 0; index != capacity; ++index) {
            ft_brodal_tree_release(policy, buckets[index]);
        }
    }
    ft_brodal_deallocate(policy, buckets);
    return status;
}

static ft_status ft_brodal_heap_adopt(
    ft_brodal_policy_rep* policy,
    ft_brodal_tree* root,
    size_t count,
    ft_brodal_heap* result)
{
    if (policy == NULL || result == NULL ||
        ((root == NULL) != (count == 0))) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_brodal_policy_retain(policy);
    result->policy = policy;
    result->root = root;
    result->count = count;
    return FT_STATUS_OK;
}

static void ft_brodal_publish_unary(
    const ft_brodal_heap* source,
    ft_brodal_heap* result,
    ft_brodal_heap produced)
{
    if (source == result) {
        ft_brodal_heap old = *result;
        *result = produced;
        ft_brodal_heap_dispose(&old);
    } else {
        *result = produced;
    }
}

static void ft_brodal_publish_binary(
    const ft_brodal_heap* left,
    const ft_brodal_heap* right,
    ft_brodal_heap* result,
    ft_brodal_heap produced)
{
    if (result == left || result == right) {
        ft_brodal_heap old = *result;
        *result = produced;
        ft_brodal_heap_dispose(&old);
    } else {
        *result = produced;
    }
}

void ft_brodal_policy_config_init(
    ft_brodal_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_brodal_compare_fn compare,
    void* callback_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->value_size = value_size;
    config->value_type_identity = value_type_identity;
    config->compare = compare;
    config->callback_context = callback_context;
    config->allocator.allocate = ft_brodal_default_allocate;
    config->allocator.deallocate = ft_brodal_default_deallocate;
}

ft_status ft_brodal_policy_create(
    ft_brodal_policy* policy,
    const ft_brodal_policy_config* config)
{
    ft_brodal_policy_rep* rep = NULL;
    if (policy == NULL || !ft_brodal_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_brodal_policy_rep*)ft_brodal_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_brodal_ref_init(&rep->refs);
    rep->config = *config;
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_brodal_policy_copy(
    const ft_brodal_policy* source,
    ft_brodal_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_brodal_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_brodal_policy_move(
    ft_brodal_policy* destination,
    ft_brodal_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_brodal_policy_dispose(ft_brodal_policy* policy)
{
    if (policy != NULL) {
        ft_brodal_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool ft_brodal_policy_same(
    const ft_brodal_policy* left,
    const ft_brodal_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL &&
        left->rep == right->rep;
}

ft_status ft_brodal_heap_init(
    ft_brodal_heap* heap,
    const ft_brodal_policy* policy)
{
    if (heap == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_brodal_heap_adopt(policy->rep, NULL, 0, heap);
}

ft_status ft_brodal_heap_copy(
    const ft_brodal_heap* source,
    ft_brodal_heap* destination)
{
    if (!ft_brodal_heap_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_brodal_policy_retain(source->policy);
    ft_brodal_tree_retain(source->root);
    destination->policy = source->policy;
    destination->root = source->root;
    destination->count = source->count;
    return FT_STATUS_OK;
}

void ft_brodal_heap_move(ft_brodal_heap* destination, ft_brodal_heap* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_brodal_heap_dispose(ft_brodal_heap* heap)
{
    if (heap == NULL) {
        return;
    }
    if (heap->policy != NULL) {
        ft_brodal_tree_release(heap->policy, heap->root);
        ft_brodal_policy_release(heap->policy);
    }
    (void)memset(heap, 0, sizeof(*heap));
}

bool ft_brodal_heap_empty(const ft_brodal_heap* heap)
{
    return ft_brodal_heap_valid(heap) && heap->root == NULL;
}

size_t ft_brodal_heap_size(const ft_brodal_heap* heap)
{
    return ft_brodal_heap_valid(heap) ? heap->count : 0;
}

ft_status ft_brodal_heap_try_get_minimum_ref(
    const ft_brodal_heap* heap,
    bool* found,
    const void** minimum_ref)
{
    if (!ft_brodal_heap_valid(heap) || found == NULL || minimum_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *found = heap->root != NULL;
    *minimum_ref = heap->root == NULL ? NULL : heap->root->value->bytes;
    return FT_STATUS_OK;
}

ft_status ft_brodal_heap_try_get_minimum_copy(
    const ft_brodal_heap* heap,
    bool* found,
    void* minimum)
{
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || found == NULL || minimum == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        *found = false;
        return FT_STATUS_OK;
    }
    if (heap->policy->config.copy == NULL) {
        (void)memcpy(minimum, heap->root->value->bytes, heap->policy->config.value_size);
    } else {
        status = heap->policy->config.copy(
            minimum,
            heap->root->value->bytes,
            heap->policy->config.callback_context);
    }
    if (status == FT_STATUS_OK) {
        *found = true;
    }
    return status;
}

ft_status ft_brodal_heap_insert(
    const ft_brodal_heap* heap,
    const void* value,
    ft_brodal_heap* result)
{
    ft_brodal_value* stored = NULL;
    ft_brodal_tree* zero = NULL;
    ft_brodal_tree* root = NULL;
    ft_brodal_forest* children = NULL;
    ft_brodal_heap produced = {0};
    size_t count = 0;
    bool new_wins = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    count = heap->count + 1;
    if (heap->root != NULL) {
        status = ft_brodal_less_equal(
            heap->policy, value, heap->root->value->bytes, &new_wins);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    status = ft_brodal_value_create(heap->policy, value, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (heap->root == NULL) {
        status = ft_brodal_tree_create(heap->policy, 0, stored, NULL, &root);
    } else if (new_wins) {
        status = ft_brodal_forest_create(heap->policy, heap->root, NULL, &children);
        if (status == FT_STATUS_OK) {
            status = ft_brodal_tree_create(heap->policy, 0, stored, children, &root);
        }
    } else {
        status = ft_brodal_tree_create(heap->policy, 0, stored, NULL, &zero);
        if (status == FT_STATUS_OK) {
            status = ft_brodal_skew_insert(
                heap->policy, zero, heap->root->children, &children);
        }
        if (status == FT_STATUS_OK) {
            status = ft_brodal_tree_create(
                heap->policy, 0, heap->root->value, children, &root);
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_brodal_heap_adopt(heap->policy, root, count, &produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
        ft_brodal_publish_unary(heap, result, produced);
    }
    ft_brodal_forest_release(heap->policy, children);
    ft_brodal_tree_release(heap->policy, zero);
    ft_brodal_tree_release(heap->policy, root);
    ft_brodal_value_release(heap->policy, stored);
    return status;
}

ft_status ft_brodal_heap_from_array(
    ft_brodal_heap* heap,
    const ft_brodal_policy* policy,
    const void* values,
    size_t count)
{
    ft_brodal_heap working;
    size_t bytes = 0;
    ft_status status = FT_STATUS_OK;
    if (heap == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && values == NULL) ||
        ft_brodal_multiply_overflows(count, policy->rep->config.value_size, &bytes)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)bytes;
    status = ft_brodal_heap_init(&working, policy);
    for (size_t index = 0; status == FT_STATUS_OK && index != count; ++index) {
        const unsigned char* value =
            (const unsigned char*)values + index * policy->rep->config.value_size;
        status = ft_brodal_heap_insert(&working, value, &working);
    }
    if (status == FT_STATUS_OK) {
        *heap = working;
    } else {
        ft_brodal_heap_dispose(&working);
    }
    return status;
}

ft_status ft_brodal_heap_meld(
    const ft_brodal_heap* left,
    const ft_brodal_heap* right,
    ft_brodal_heap* result)
{
    ft_brodal_tree* root = NULL;
    ft_brodal_forest* children = NULL;
    ft_brodal_heap produced = {0};
    size_t count = 0;
    bool left_wins = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(left) || !ft_brodal_heap_valid(right) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    if (ft_brodal_add_overflows(left->count, right->count, &count)) {
        return FT_STATUS_OVERFLOW;
    }
    if (left->root == NULL || right->root == NULL) {
        const ft_brodal_heap* selected = left->root == NULL ? right : left;
        status = ft_brodal_heap_copy(selected, &produced);
        if (status == FT_STATUS_OK) {
            ft_brodal_publish_binary(left, right, result, produced);
        }
        return status;
    }
    status = ft_brodal_less_equal(
        left->policy,
        left->root->value->bytes,
        right->root->value->bytes,
        &left_wins);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (left_wins) {
        status = ft_brodal_skew_insert(
            left->policy, right->root, left->root->children, &children);
        if (status == FT_STATUS_OK) {
            status = ft_brodal_tree_create(
                left->policy, 0, left->root->value, children, &root);
        }
    } else {
        status = ft_brodal_skew_insert(
            left->policy, left->root, right->root->children, &children);
        if (status == FT_STATUS_OK) {
            status = ft_brodal_tree_create(
                left->policy, 0, right->root->value, children, &root);
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_brodal_heap_adopt(left->policy, root, count, &produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
        ft_brodal_publish_binary(left, right, result, produced);
    }
    ft_brodal_tree_release(left->policy, root);
    ft_brodal_forest_release(left->policy, children);
    return status;
}

static ft_status ft_brodal_delete_nonempty(
    const ft_brodal_heap* heap,
    ft_brodal_heap* produced)
{
    ft_brodal_tree* minimum = NULL;
    ft_brodal_tree* root = NULL;
    ft_brodal_forest* remainder = NULL;
    ft_brodal_forest* zeros = NULL;
    ft_brodal_forest* trees = NULL;
    ft_brodal_forest* embedded = NULL;
    ft_brodal_forest* merged = NULL;
    ft_brodal_forest* all = NULL;
    ft_brodal_tree** zero_items = NULL;
    size_t zero_count = 0;
    size_t zero_bytes = 0;
    bool complete = false;
    ft_status status = FT_STATUS_OK;
    if (heap->root->children == NULL) {
        if (heap->count != 1) {
            return FT_STATUS_INCONSISTENT_POLICY;
        }
        return ft_brodal_heap_adopt(heap->policy, NULL, 0, produced);
    }
    status = ft_brodal_get_minimum_tree(
        heap->policy,
        heap->root->children,
        heap->count,
        &minimum,
        &remainder);
    if (status == FT_STATUS_OK) {
        status = ft_brodal_split_forest(
            heap->policy,
            minimum->rank,
            minimum->children,
            &zeros,
            &trees,
            &embedded);
    }
    if (status == FT_STATUS_OK) {
        status = ft_brodal_skew_meld(
            heap->policy, trees, remainder, heap->count, &merged);
    }
    if (status == FT_STATUS_OK) {
        status = ft_brodal_skew_meld(
            heap->policy, merged, embedded, heap->count, &all);
    }
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    zero_count = ft_brodal_forest_length_bounded(zeros, heap->count, &complete);
    if (!complete || ft_brodal_multiply_overflows(
            zero_count, sizeof(*zero_items), &zero_bytes)) {
        status = FT_STATUS_INCONSISTENT_POLICY;
        goto cleanup;
    }
    if (zero_count != 0) {
        zero_items = (ft_brodal_tree**)ft_brodal_allocate(heap->policy, zero_bytes);
        if (zero_items == NULL) {
            status = FT_STATUS_NO_MEMORY;
            goto cleanup;
        }
        {
            ft_brodal_forest* cursor = zeros;
            for (size_t index = 0; index != zero_count; ++index) {
                zero_items[index] = cursor->head;
                cursor = cursor->tail;
            }
        }
        for (size_t index = zero_count; index != 0; --index) {
            ft_brodal_forest* next = NULL;
            status = ft_brodal_skew_insert(
                heap->policy, zero_items[index - 1], all, &next);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            ft_brodal_forest_release(heap->policy, all);
            all = next;
        }
    }
    status = ft_brodal_tree_create(
        heap->policy, 0, minimum->value, all, &root);
    if (status == FT_STATUS_OK) {
        status = ft_brodal_heap_adopt(
            heap->policy, root, heap->count - 1, produced);
    }
    if (status == FT_STATUS_OK) {
        root = NULL;
    }

cleanup:
    ft_brodal_tree_release(heap->policy, root);
    ft_brodal_deallocate(heap->policy, zero_items);
    ft_brodal_forest_release(heap->policy, all);
    ft_brodal_forest_release(heap->policy, merged);
    ft_brodal_forest_release(heap->policy, embedded);
    ft_brodal_forest_release(heap->policy, trees);
    ft_brodal_forest_release(heap->policy, zeros);
    ft_brodal_forest_release(heap->policy, remainder);
    return status;
}

ft_status ft_brodal_heap_delete_minimum(
    const ft_brodal_heap* heap,
    ft_brodal_heap* result)
{
    ft_brodal_heap produced = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        return FT_STATUS_EMPTY;
    }
    status = ft_brodal_delete_nonempty(heap, &produced);
    if (status == FT_STATUS_OK) {
        ft_brodal_publish_unary(heap, result, produced);
    }
    return status;
}

ft_status ft_brodal_heap_try_delete_minimum(
    const ft_brodal_heap* heap,
    bool* removed,
    void* minimum,
    ft_brodal_heap* result)
{
    ft_brodal_heap produced = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || removed == NULL || minimum == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        status = ft_brodal_heap_copy(heap, &produced);
    } else {
        status = ft_brodal_delete_nonempty(heap, &produced);
        if (status == FT_STATUS_OK) {
            if (heap->policy->config.copy == NULL) {
                (void)memcpy(
                    minimum,
                    heap->root->value->bytes,
                    heap->policy->config.value_size);
            } else {
                status = heap->policy->config.copy(
                    minimum,
                    heap->root->value->bytes,
                    heap->policy->config.callback_context);
            }
        }
    }
    if (status == FT_STATUS_OK) {
        const bool did_remove = heap->root != NULL;
        ft_brodal_publish_unary(heap, result, produced);
        *removed = did_remove;
    } else {
        ft_brodal_heap_dispose(&produced);
    }
    return status;
}

static ft_status ft_brodal_allocate_visit_stack(
    const ft_brodal_heap* heap,
    ft_brodal_visit_frame** stack)
{
    size_t bytes = 0;
    if (heap->count == 0) {
        *stack = NULL;
        return FT_STATUS_OK;
    }
    if (ft_brodal_multiply_overflows(heap->count, sizeof(**stack), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *stack = (ft_brodal_visit_frame*)ft_brodal_allocate(heap->policy, bytes);
    return *stack == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

ft_status ft_brodal_heap_visit(
    const ft_brodal_heap* heap,
    ft_brodal_visit_fn visitor,
    void* context)
{
    ft_brodal_visit_frame* stack = NULL;
    size_t pending = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_brodal_allocate_visit_stack(heap, &stack);
    if (status != FT_STATUS_OK || heap->root == NULL) {
        return status;
    }
    stack[pending++] = (ft_brodal_visit_frame){heap->root, 1};
    while (pending != 0) {
        ft_brodal_tree* tree = stack[--pending].tree;
        status = visitor(tree->value->bytes, context);
        if (status != FT_STATUS_OK) {
            break;
        }
        for (ft_brodal_forest* children = tree->children;
             children != NULL;
             children = children->tail) {
            if (pending == heap->count) {
                status = FT_STATUS_INCONSISTENT_POLICY;
                break;
            }
            stack[pending++] = (ft_brodal_visit_frame){children->head, 0};
        }
        if (status != FT_STATUS_OK) {
            break;
        }
    }
    ft_brodal_deallocate(heap->policy, stack);
    return status;
}

ft_status ft_brodal_heap_visit_shape(
    const ft_brodal_heap* heap,
    ft_brodal_shape_visit_fn visitor,
    void* context)
{
    ft_brodal_visit_frame* stack = NULL;
    size_t pending = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_brodal_allocate_visit_stack(heap, &stack);
    if (status != FT_STATUS_OK || heap->root == NULL) {
        return status;
    }
    stack[pending++] = (ft_brodal_visit_frame){heap->root, 1};
    while (pending != 0) {
        ft_brodal_visit_frame frame = stack[--pending];
        ft_brodal_forest* children = frame.tree->children;
        size_t child_count = 0;
        bool complete = false;
        child_count = ft_brodal_forest_length_bounded(children, heap->count, &complete);
        if (!complete) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            break;
        }
        status = visitor(
            frame.tree,
            frame.tree->value->bytes,
            frame.tree->rank,
            child_count,
            frame.depth,
            context);
        if (status != FT_STATUS_OK) {
            break;
        }
        for (; children != NULL; children = children->tail) {
            if (pending == heap->count || frame.depth == SIZE_MAX) {
                status = FT_STATUS_INCONSISTENT_POLICY;
                break;
            }
            stack[pending++] =
                (ft_brodal_visit_frame){children->head, frame.depth + 1};
        }
        if (status != FT_STATUS_OK) {
            break;
        }
    }
    ft_brodal_deallocate(heap->policy, stack);
    return status;
}

const void* ft_brodal_heap_root_identity(const ft_brodal_heap* heap)
{
    return ft_brodal_heap_valid(heap) ? heap->root : NULL;
}

static bool ft_brodal_validate_skew_forest(
    const ft_brodal_forest* forest,
    size_t bound,
    size_t* length_result)
{
    size_t length = 0;
    unsigned previous = 0;
    bool has_previous = false;
    bool duplicate = false;
    while (forest != NULL) {
        const unsigned rank = forest->head->rank;
        if (length == bound || (has_previous && rank < previous)) {
            return false;
        }
        if (has_previous && rank == previous) {
            if (length != 1 || duplicate) {
                return false;
            }
            duplicate = true;
        }
        previous = rank;
        has_previous = true;
        ++length;
        forest = forest->tail;
    }
    *length_result = length;
    return true;
}

static bool ft_brodal_validate_fused_children(
    const ft_brodal_tree* tree,
    const ft_brodal_forest** embedded)
{
    unsigned rank = tree->rank;
    const ft_brodal_forest* forest = tree->children;
    while (rank != 0) {
        const ft_brodal_tree* first = NULL;
        const ft_brodal_tree* second = NULL;
        if (forest == NULL) {
            return false;
        }
        first = forest->head;
        if (rank == 1) {
            if (first->rank != 0) {
                return false;
            }
            if (forest->tail == NULL) {
                *embedded = NULL;
                return true;
            }
            *embedded = forest->tail->head->rank == 0
                ? forest->tail->tail
                : forest->tail;
            return true;
        }
        if (forest->tail == NULL) {
            return false;
        }
        second = forest->tail->head;
        if (first->rank == second->rank) {
            if (first->rank != rank - 1) {
                return false;
            }
            *embedded = forest->tail->tail;
            return true;
        }
        if (first->rank == 0) {
            if (second->rank != rank - 1) {
                return false;
            }
            forest = forest->tail->tail;
        } else {
            if (first->rank != rank - 1) {
                return false;
            }
            forest = forest->tail;
        }
        --rank;
    }
    *embedded = forest;
    return true;
}

ft_status ft_brodal_heap_validate(
    const ft_brodal_heap* heap,
    bool* valid,
    ft_brodal_heap_statistics* statistics)
{
    ft_brodal_visit_frame* stack = NULL;
    ft_brodal_heap_statistics local = {0, 0, 0, 0};
    size_t pending = 0;
    bool local_valid = true;
    ft_status status = FT_STATUS_OK;
    if (!ft_brodal_heap_valid(heap) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (heap->root == NULL) {
        *valid = true;
        *statistics = local;
        return FT_STATUS_OK;
    }
    if (heap->root->rank != 0 ||
        !ft_brodal_validate_skew_forest(
            heap->root->children, heap->count, &local.root_forest_length)) {
        *valid = false;
        *statistics = local;
        return FT_STATUS_OK;
    }
    status = ft_brodal_allocate_visit_stack(heap, &stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    stack[pending++] = (ft_brodal_visit_frame){heap->root, 1};
    while (pending != 0 && local_valid) {
        ft_brodal_visit_frame frame = stack[--pending];
        const ft_brodal_forest* embedded = NULL;
        size_t ignored_length = 0;
        if (local.count == heap->count ||
            !ft_brodal_validate_fused_children(frame.tree, &embedded) ||
            !ft_brodal_validate_skew_forest(
                embedded, heap->count, &ignored_length)) {
            local_valid = false;
            break;
        }
        ++local.count;
        if (frame.tree->rank > local.maximum_rank) {
            local.maximum_rank = frame.tree->rank;
        }
        if (frame.depth > local.maximum_depth) {
            local.maximum_depth = frame.depth;
        }
        {
            size_t child_steps = 0;
            for (ft_brodal_forest* child = frame.tree->children;
                 child != NULL;
                 child = child->tail) {
                bool ordered = false;
                if (child_steps == heap->count || pending == heap->count ||
                    frame.depth == SIZE_MAX) {
                    local_valid = false;
                    break;
                }
                status = ft_brodal_less_equal(
                    heap->policy,
                    frame.tree->value->bytes,
                    child->head->value->bytes,
                    &ordered);
                if (status != FT_STATUS_OK) {
                    goto cleanup;
                }
                if (!ordered) {
                    local_valid = false;
                    break;
                }
                stack[pending++] =
                    (ft_brodal_visit_frame){child->head, frame.depth + 1};
                ++child_steps;
            }
        }
    }
    if (local_valid && local.count != heap->count) {
        local_valid = false;
    }

cleanup:
    ft_brodal_deallocate(heap->policy, stack);
    if (status == FT_STATUS_OK) {
        *valid = local_valid;
        *statistics = local;
    }
    return status;
}
