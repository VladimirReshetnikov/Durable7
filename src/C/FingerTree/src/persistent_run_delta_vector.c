/*
 * Implementation of the persistent run-delta vector.
 *
 * The two roots are ordinary RRB vectors whose element type is a pointer to a reference-counted
 * cell; cell address is the representative identity the cleanliness invariant needs, and it makes
 * the underlying vector's equal-replacement short circuit a pointer test. The maximal-run index is
 * a persistent weight-balanced tree keyed by run start, carrying subtree sizes so a run can be
 * selected by rank and located by any position it contains in O(log r), and enumerated in
 * Theta(r).
 */

#include <durable7/finger_tree/persistent_run_delta_vector.h>

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
typedef volatile LONG64 ft_run_delta_ref_count;

static void ft_run_delta_ref_init(ft_run_delta_ref_count* value)
{
    *value = 1;
}

static void ft_run_delta_ref_retain(ft_run_delta_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_run_delta_ref_release(ft_run_delta_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_run_delta_ref_count;

static void ft_run_delta_ref_init(ft_run_delta_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_run_delta_ref_retain(ft_run_delta_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_run_delta_ref_release(ft_run_delta_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

/* The run index's weight-balanced rebalancing thresholds, in the usual (delta, ratio) form. */
#define FT_RUN_DELTA_BALANCE_DELTA ((size_t)3)
#define FT_RUN_DELTA_BALANCE_RATIO ((size_t)2)

/* One reference-counted representative: the identity a clean position shares with its
 * checkpoint. */
typedef struct ft_run_delta_cell {
    ft_run_delta_ref_count refs;
    void* bytes;
} ft_run_delta_cell;

struct ft_run_delta_run_node {
    ft_run_delta_ref_count refs;
    size_t start;
    size_t end;
    size_t size;
    struct ft_run_delta_run_node* left;
    struct ft_run_delta_run_node* right;
};

struct ft_run_delta_policy_rep {
    ft_run_delta_ref_count refs;
    ft_run_delta_policy_config config;
    ft_rrb_policy rrb_policy;
};

/* --------------------------------------------------------------------------------------------
 * Allocation and policy plumbing.
 * -------------------------------------------------------------------------------------------- */

static void* ft_run_delta_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_run_delta_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_run_delta_config_valid(const ft_run_delta_policy_config* config)
{
    return config != NULL && config->value_size != 0 &&
        config->value_type_identity != NULL && config->equal != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL &&
        (config->destroy == NULL || config->copy != NULL);
}

static void* ft_run_delta_allocate_config(
    const ft_run_delta_policy_config* config,
    size_t size)
{
    return config->allocator.allocate(size == 0 ? 1 : size, config->allocator.context);
}

static void ft_run_delta_deallocate_config(
    const ft_run_delta_policy_config* config,
    void* allocation)
{
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void* ft_run_delta_allocate(const ft_run_delta_policy_rep* policy, size_t size)
{
    return ft_run_delta_allocate_config(&policy->config, size);
}

static void ft_run_delta_deallocate(
    const ft_run_delta_policy_rep* policy,
    void* allocation)
{
    ft_run_delta_deallocate_config(&policy->config, allocation);
}

static void ft_run_delta_policy_retain(ft_run_delta_policy_rep* policy)
{
    if (policy != NULL) {
        ft_run_delta_ref_retain(&policy->refs);
    }
}

static void ft_run_delta_policy_release(ft_run_delta_policy_rep* policy)
{
    if (policy != NULL && ft_run_delta_ref_release(&policy->refs)) {
        ft_run_delta_deallocate_config(&policy->config, policy);
    }
}

static ft_status ft_run_delta_equal(
    const ft_run_delta_policy_rep* policy,
    const void* left,
    const void* right,
    bool* equal)
{
    return policy->config.equal(left, right, equal, policy->config.callback_context);
}

/* --------------------------------------------------------------------------------------------
 * Cells.
 * -------------------------------------------------------------------------------------------- */

static void ft_run_delta_cell_retain(ft_run_delta_cell* cell)
{
    if (cell != NULL) {
        ft_run_delta_ref_retain(&cell->refs);
    }
}

static void ft_run_delta_cell_release(
    const ft_run_delta_policy_rep* policy,
    ft_run_delta_cell* cell)
{
    if (cell == NULL || !ft_run_delta_ref_release(&cell->refs)) {
        return;
    }
    if (policy->config.destroy != NULL) {
        policy->config.destroy(cell->bytes, policy->config.callback_context);
    }
    ft_run_delta_deallocate(policy, cell->bytes);
    ft_run_delta_deallocate(policy, cell);
}

static ft_status ft_run_delta_cell_create(
    const ft_run_delta_policy_rep* policy,
    const void* source,
    ft_run_delta_cell** result)
{
    ft_run_delta_cell* cell = (ft_run_delta_cell*)ft_run_delta_allocate(policy, sizeof(*cell));
    if (cell == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    cell->bytes = ft_run_delta_allocate(policy, policy->config.value_size);
    if (cell->bytes == NULL) {
        ft_run_delta_deallocate(policy, cell);
        return FT_STATUS_NO_MEMORY;
    }
    if (policy->config.copy == NULL) {
        (void)memcpy(cell->bytes, source, policy->config.value_size);
    } else {
        const ft_status status = policy->config.copy(
            cell->bytes,
            source,
            policy->config.callback_context);
        if (status != FT_STATUS_OK) {
            ft_run_delta_deallocate(policy, cell->bytes);
            ft_run_delta_deallocate(policy, cell);
            return status;
        }
    }
    ft_run_delta_ref_init(&cell->refs);
    *result = cell;
    return FT_STATUS_OK;
}

/* The RRB element type is a bare cell pointer, so copying an element retains it, destroying an
 * element releases it, and element equality is representative identity. */

static void ft_run_delta_cell_copy_hook(void* destination, const void* source, void* context)
{
    ft_run_delta_cell* const cell = *(ft_run_delta_cell* const*)source;
    (void)context;
    ft_run_delta_cell_retain(cell);
    *(ft_run_delta_cell**)destination = cell;
}

static void ft_run_delta_cell_destroy_hook(void* value, void* context)
{
    const ft_run_delta_policy_rep* const policy = (const ft_run_delta_policy_rep*)context;
    ft_run_delta_cell_release(policy, *(ft_run_delta_cell**)value);
}

static bool ft_run_delta_cell_equal_hook(const void* left, const void* right, void* context)
{
    (void)context;
    return *(ft_run_delta_cell* const*)left == *(ft_run_delta_cell* const*)right;
}

/* Reads the cell stored at a position, taking a reference the caller releases. */
static ft_status ft_run_delta_cell_at(
    const ft_rrb_vector* vector,
    size_t index,
    ft_run_delta_cell** result)
{
    ft_run_delta_cell* stored = NULL;
    const ft_status status = ft_rrb_vector_at(vector, index, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *result = stored;
    return FT_STATUS_OK;
}

/* --------------------------------------------------------------------------------------------
 * The persistent maximal-run index.
 * -------------------------------------------------------------------------------------------- */

static size_t ft_run_delta_node_size(const ft_run_delta_run_node* node)
{
    return node == NULL ? 0 : node->size;
}

static ft_run_delta_run_node* ft_run_delta_node_retain(ft_run_delta_run_node* node)
{
    if (node != NULL) {
        ft_run_delta_ref_retain(&node->refs);
    }
    return node;
}

static void ft_run_delta_node_release(
    const ft_run_delta_policy_rep* policy,
    ft_run_delta_run_node* node)
{
    while (node != NULL && ft_run_delta_ref_release(&node->refs)) {
        ft_run_delta_run_node* const left = node->left;
        ft_run_delta_run_node* const right = node->right;
        ft_run_delta_deallocate(policy, node);
        ft_run_delta_node_release(policy, left);
        node = right;
    }
}

/* Builds one node from owned child references, consuming them on success and on failure. */
static ft_status ft_run_delta_node_create(
    const ft_run_delta_policy_rep* policy,
    size_t start,
    size_t end,
    ft_run_delta_run_node* left,
    ft_run_delta_run_node* right,
    ft_run_delta_run_node** result)
{
    ft_run_delta_run_node* const node =
        (ft_run_delta_run_node*)ft_run_delta_allocate(policy, sizeof(*node));
    if (node == NULL) {
        ft_run_delta_node_release(policy, left);
        ft_run_delta_node_release(policy, right);
        return FT_STATUS_NO_MEMORY;
    }
    ft_run_delta_ref_init(&node->refs);
    node->start = start;
    node->end = end;
    node->left = left;
    node->right = right;
    node->size = 1 + ft_run_delta_node_size(left) + ft_run_delta_node_size(right);
    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_run_delta_node_rotate_left(
    const ft_run_delta_policy_rep* policy,
    size_t start,
    size_t end,
    ft_run_delta_run_node* left,
    ft_run_delta_run_node* right,
    ft_run_delta_run_node** result)
{
    ft_run_delta_run_node* const inner_left = ft_run_delta_node_retain(right->left);
    ft_run_delta_run_node* const inner_right = ft_run_delta_node_retain(right->right);
    const size_t right_start = right->start;
    const size_t right_end = right->end;
    ft_status status = FT_STATUS_OK;

    if (ft_run_delta_node_size(inner_left) <
        FT_RUN_DELTA_BALANCE_RATIO * ft_run_delta_node_size(inner_right)) {
        ft_run_delta_run_node* rebuilt = NULL;
        ft_run_delta_node_release(policy, right);
        status = ft_run_delta_node_create(policy, start, end, left, inner_left, &rebuilt);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, inner_right);
            return status;
        }
        return ft_run_delta_node_create(
            policy,
            right_start,
            right_end,
            rebuilt,
            inner_right,
            result);
    }

    {
        /* The inner-left subtree cannot be empty here: it outweighs a sibling that the caller's
         * weight test already proved nonempty. */
        const size_t pivot_start = inner_left->start;
        const size_t pivot_end = inner_left->end;
        ft_run_delta_run_node* const pivot_left = ft_run_delta_node_retain(inner_left->left);
        ft_run_delta_run_node* const pivot_right = ft_run_delta_node_retain(inner_left->right);
        ft_run_delta_run_node* rebuilt_left = NULL;
        ft_run_delta_run_node* rebuilt_right = NULL;
        ft_run_delta_node_release(policy, right);
        ft_run_delta_node_release(policy, inner_left);
        status = ft_run_delta_node_create(policy, start, end, left, pivot_left, &rebuilt_left);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, pivot_right);
            ft_run_delta_node_release(policy, inner_right);
            return status;
        }
        status = ft_run_delta_node_create(
            policy,
            right_start,
            right_end,
            pivot_right,
            inner_right,
            &rebuilt_right);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, rebuilt_left);
            return status;
        }
        return ft_run_delta_node_create(
            policy,
            pivot_start,
            pivot_end,
            rebuilt_left,
            rebuilt_right,
            result);
    }
}

static ft_status ft_run_delta_node_rotate_right(
    const ft_run_delta_policy_rep* policy,
    size_t start,
    size_t end,
    ft_run_delta_run_node* left,
    ft_run_delta_run_node* right,
    ft_run_delta_run_node** result)
{
    ft_run_delta_run_node* const inner_left = ft_run_delta_node_retain(left->left);
    ft_run_delta_run_node* const inner_right = ft_run_delta_node_retain(left->right);
    const size_t left_start = left->start;
    const size_t left_end = left->end;
    ft_status status = FT_STATUS_OK;

    if (ft_run_delta_node_size(inner_right) <
        FT_RUN_DELTA_BALANCE_RATIO * ft_run_delta_node_size(inner_left)) {
        ft_run_delta_run_node* rebuilt = NULL;
        ft_run_delta_node_release(policy, left);
        status = ft_run_delta_node_create(policy, start, end, inner_right, right, &rebuilt);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, inner_left);
            return status;
        }
        return ft_run_delta_node_create(
            policy,
            left_start,
            left_end,
            inner_left,
            rebuilt,
            result);
    }

    {
        const size_t pivot_start = inner_right->start;
        const size_t pivot_end = inner_right->end;
        ft_run_delta_run_node* const pivot_left = ft_run_delta_node_retain(inner_right->left);
        ft_run_delta_run_node* const pivot_right = ft_run_delta_node_retain(inner_right->right);
        ft_run_delta_run_node* rebuilt_left = NULL;
        ft_run_delta_run_node* rebuilt_right = NULL;
        ft_run_delta_node_release(policy, left);
        ft_run_delta_node_release(policy, inner_right);
        status = ft_run_delta_node_create(
            policy,
            left_start,
            left_end,
            inner_left,
            pivot_left,
            &rebuilt_left);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, pivot_right);
            ft_run_delta_node_release(policy, right);
            return status;
        }
        status = ft_run_delta_node_create(policy, start, end, pivot_right, right, &rebuilt_right);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, rebuilt_left);
            return status;
        }
        return ft_run_delta_node_create(
            policy,
            pivot_start,
            pivot_end,
            rebuilt_left,
            rebuilt_right,
            result);
    }
}

/* Builds one node from owned child references that may differ in weight by a single edit,
 * consuming them on success and on failure. */
static ft_status ft_run_delta_node_balance(
    const ft_run_delta_policy_rep* policy,
    size_t start,
    size_t end,
    ft_run_delta_run_node* left,
    ft_run_delta_run_node* right,
    ft_run_delta_run_node** result)
{
    const size_t left_size = ft_run_delta_node_size(left);
    const size_t right_size = ft_run_delta_node_size(right);
    if (left_size + right_size > 1) {
        if (right_size > FT_RUN_DELTA_BALANCE_DELTA * left_size) {
            return ft_run_delta_node_rotate_left(policy, start, end, left, right, result);
        }
        if (left_size > FT_RUN_DELTA_BALANCE_DELTA * right_size) {
            return ft_run_delta_node_rotate_right(policy, start, end, left, right, result);
        }
    }
    return ft_run_delta_node_create(policy, start, end, left, right, result);
}

/* Binds start to end, adding or replacing as needed. */
static ft_status ft_run_delta_node_set(
    const ft_run_delta_policy_rep* policy,
    const ft_run_delta_run_node* node,
    size_t start,
    size_t end,
    ft_run_delta_run_node** result)
{
    ft_run_delta_run_node* replaced = NULL;
    ft_status status = FT_STATUS_OK;
    if (node == NULL) {
        return ft_run_delta_node_create(policy, start, end, NULL, NULL, result);
    }
    if (start == node->start) {
        return ft_run_delta_node_create(
            policy,
            start,
            end,
            ft_run_delta_node_retain(node->left),
            ft_run_delta_node_retain(node->right),
            result);
    }
    if (start < node->start) {
        status = ft_run_delta_node_set(policy, node->left, start, end, &replaced);
        if (status != FT_STATUS_OK) {
            return status;
        }
        return ft_run_delta_node_balance(
            policy,
            node->start,
            node->end,
            replaced,
            ft_run_delta_node_retain(node->right),
            result);
    }
    status = ft_run_delta_node_set(policy, node->right, start, end, &replaced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_run_delta_node_balance(
        policy,
        node->start,
        node->end,
        ft_run_delta_node_retain(node->left),
        replaced,
        result);
}

static ft_status ft_run_delta_node_delete_maximum(
    const ft_run_delta_policy_rep* policy,
    const ft_run_delta_run_node* node,
    size_t* start,
    size_t* end,
    ft_run_delta_run_node** rest)
{
    ft_run_delta_run_node* remainder = NULL;
    ft_status status = FT_STATUS_OK;
    if (node->right == NULL) {
        *start = node->start;
        *end = node->end;
        *rest = ft_run_delta_node_retain(node->left);
        return FT_STATUS_OK;
    }
    status = ft_run_delta_node_delete_maximum(policy, node->right, start, end, &remainder);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_run_delta_node_balance(
        policy,
        node->start,
        node->end,
        ft_run_delta_node_retain(node->left),
        remainder,
        rest);
}

static ft_status ft_run_delta_node_delete_minimum(
    const ft_run_delta_policy_rep* policy,
    const ft_run_delta_run_node* node,
    size_t* start,
    size_t* end,
    ft_run_delta_run_node** rest)
{
    ft_run_delta_run_node* remainder = NULL;
    ft_status status = FT_STATUS_OK;
    if (node->left == NULL) {
        *start = node->start;
        *end = node->end;
        *rest = ft_run_delta_node_retain(node->right);
        return FT_STATUS_OK;
    }
    status = ft_run_delta_node_delete_minimum(policy, node->left, start, end, &remainder);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_run_delta_node_balance(
        policy,
        node->start,
        node->end,
        remainder,
        ft_run_delta_node_retain(node->right),
        rest);
}

/* Joins two subtrees whose keys are already separated, consuming both owned references. */
static ft_status ft_run_delta_node_glue(
    const ft_run_delta_policy_rep* policy,
    ft_run_delta_run_node* left,
    ft_run_delta_run_node* right,
    ft_run_delta_run_node** result)
{
    size_t start = 0;
    size_t end = 0;
    ft_run_delta_run_node* remainder = NULL;
    ft_status status = FT_STATUS_OK;
    if (left == NULL) {
        *result = right;
        return FT_STATUS_OK;
    }
    if (right == NULL) {
        *result = left;
        return FT_STATUS_OK;
    }
    if (ft_run_delta_node_size(left) > ft_run_delta_node_size(right)) {
        status = ft_run_delta_node_delete_maximum(policy, left, &start, &end, &remainder);
        if (status != FT_STATUS_OK) {
            ft_run_delta_node_release(policy, left);
            ft_run_delta_node_release(policy, right);
            return status;
        }
        ft_run_delta_node_release(policy, left);
        return ft_run_delta_node_balance(policy, start, end, remainder, right, result);
    }
    status = ft_run_delta_node_delete_minimum(policy, right, &start, &end, &remainder);
    if (status != FT_STATUS_OK) {
        ft_run_delta_node_release(policy, left);
        ft_run_delta_node_release(policy, right);
        return status;
    }
    ft_run_delta_node_release(policy, right);
    return ft_run_delta_node_balance(policy, start, end, left, remainder, result);
}

/* Unbinds start. An absent key leaves the index unchanged. */
static ft_status ft_run_delta_node_remove(
    const ft_run_delta_policy_rep* policy,
    const ft_run_delta_run_node* node,
    size_t start,
    ft_run_delta_run_node** result)
{
    ft_run_delta_run_node* replaced = NULL;
    ft_status status = FT_STATUS_OK;
    if (node == NULL) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    if (start == node->start) {
        return ft_run_delta_node_glue(
            policy,
            ft_run_delta_node_retain(node->left),
            ft_run_delta_node_retain(node->right),
            result);
    }
    if (start < node->start) {
        status = ft_run_delta_node_remove(policy, node->left, start, &replaced);
        if (status != FT_STATUS_OK) {
            return status;
        }
        return ft_run_delta_node_balance(
            policy,
            node->start,
            node->end,
            replaced,
            ft_run_delta_node_retain(node->right),
            result);
    }
    status = ft_run_delta_node_remove(policy, node->right, start, &replaced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_run_delta_node_balance(
        policy,
        node->start,
        node->end,
        ft_run_delta_node_retain(node->left),
        replaced,
        result);
}

/* The record with the greatest start not above the key. */
static const ft_run_delta_run_node* ft_run_delta_node_floor(
    const ft_run_delta_run_node* node,
    size_t key)
{
    const ft_run_delta_run_node* candidate = NULL;
    while (node != NULL) {
        if (node->start <= key) {
            candidate = node;
            node = node->right;
        } else {
            node = node->left;
        }
    }
    return candidate;
}

/* The record with the least start not below the key. */
static const ft_run_delta_run_node* ft_run_delta_node_ceiling(
    const ft_run_delta_run_node* node,
    size_t key)
{
    const ft_run_delta_run_node* candidate = NULL;
    while (node != NULL) {
        if (node->start >= key) {
            candidate = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    return candidate;
}

/* The record at a zero-based ascending-start rank. */
static const ft_run_delta_run_node* ft_run_delta_node_at(
    const ft_run_delta_run_node* node,
    size_t rank)
{
    while (node != NULL) {
        const size_t left_size = ft_run_delta_node_size(node->left);
        if (rank < left_size) {
            node = node->left;
        } else if (rank == left_size) {
            return node;
        } else {
            rank -= left_size + 1;
            node = node->right;
        }
    }
    return NULL;
}

/* The record covering the position, or null when the position is clean. */
static const ft_run_delta_run_node* ft_run_delta_node_containing(
    const ft_run_delta_run_node* node,
    size_t index)
{
    const ft_run_delta_run_node* const candidate = ft_run_delta_node_floor(node, index);
    return candidate != NULL && candidate->end > index ? candidate : NULL;
}

static ft_run_delta_run ft_run_delta_node_to_run(const ft_run_delta_run_node* node)
{
    ft_run_delta_run run;
    run.start = node->start;
    run.length = node->end - node->start;
    return run;
}

/* Whether every subtree records its own weight and honours the balance bound, which is what keeps
 * rank selection and containment lookup logarithmic. For the auditor only. */
static bool ft_run_delta_node_balanced(const ft_run_delta_run_node* node)
{
    size_t left_size = 0;
    size_t right_size = 0;
    if (node == NULL) {
        return true;
    }
    left_size = ft_run_delta_node_size(node->left);
    right_size = ft_run_delta_node_size(node->right);
    if (node->size != 1 + left_size + right_size) {
        return false;
    }
    if (left_size + right_size > 1 &&
        (right_size > FT_RUN_DELTA_BALANCE_DELTA * left_size ||
         left_size > FT_RUN_DELTA_BALANCE_DELTA * right_size)) {
        return false;
    }
    return ft_run_delta_node_balanced(node->left) && ft_run_delta_node_balanced(node->right);
}

static ft_status ft_run_delta_node_visit(
    const ft_run_delta_run_node* node,
    ft_run_delta_run_visit_fn visitor,
    void* context)
{
    while (node != NULL) {
        const ft_run_delta_run run = ft_run_delta_node_to_run(node);
        ft_status status = ft_run_delta_node_visit(node->left, visitor, context);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = visitor(&run, context);
        if (status != FT_STATUS_OK) {
            return status;
        }
        node = node->right;
    }
    return FT_STATUS_OK;
}

/* Records the position as dirty, merging the runs on both sides, extending one side, or inserting
 * a fresh singleton run. The result stays ordered, non-overlapping, non-adjacent, and maximal, and
 * at most two records change. */
static ft_status ft_run_delta_add_dirty_position(
    const ft_run_delta_policy_rep* policy,
    const ft_run_delta_run_node* runs,
    size_t index,
    ft_run_delta_run_node** result)
{
    const ft_run_delta_run_node* const left = ft_run_delta_node_floor(runs, index);
    const ft_run_delta_run_node* const right = ft_run_delta_node_ceiling(runs, index);
    const bool has_left = left != NULL && left->end == index;
    const bool has_right = right != NULL && right->start == index + 1;
    ft_run_delta_run_node* shortened = NULL;
    ft_status status = FT_STATUS_OK;

    if (has_left && has_right) {
        const size_t merged_start = left->start;
        const size_t merged_end = right->end;
        status = ft_run_delta_node_remove(policy, runs, right->start, &shortened);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = ft_run_delta_node_set(policy, shortened, merged_start, merged_end, result);
        ft_run_delta_node_release(policy, shortened);
        return status;
    }
    if (has_left) {
        return ft_run_delta_node_set(policy, runs, left->start, index + 1, result);
    }
    if (has_right) {
        const size_t extended_end = right->end;
        status = ft_run_delta_node_remove(policy, runs, right->start, &shortened);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = ft_run_delta_node_set(policy, shortened, index, extended_end, result);
        ft_run_delta_node_release(policy, shortened);
        return status;
    }
    return ft_run_delta_node_set(policy, runs, index, index + 1, result);
}

/* Clears the position, deleting, shrinking at either edge, or splitting its unique containing
 * run. */
static ft_status ft_run_delta_remove_dirty_position(
    const ft_run_delta_policy_rep* policy,
    const ft_run_delta_run_node* runs,
    size_t index,
    ft_run_delta_run_node** result)
{
    const ft_run_delta_run_node* const containing = ft_run_delta_node_containing(runs, index);
    size_t start = 0;
    size_t end = 0;
    ft_run_delta_run_node* shortened = NULL;
    ft_status status = FT_STATUS_OK;

    if (containing == NULL) {
        /* The cleared position is absent from the run index, so the maintained invariant would
         * already have been broken. */
        return FT_STATUS_INCONSISTENT_POLICY;
    }
    start = containing->start;
    end = containing->end;
    if (start == index && end == index + 1) {
        return ft_run_delta_node_remove(policy, runs, start, result);
    }
    if (start == index) {
        status = ft_run_delta_node_remove(policy, runs, start, &shortened);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = ft_run_delta_node_set(policy, shortened, index + 1, end, result);
        ft_run_delta_node_release(policy, shortened);
        return status;
    }
    if (end == index + 1) {
        return ft_run_delta_node_set(policy, runs, start, index, result);
    }
    status = ft_run_delta_node_set(policy, runs, start, index, &shortened);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_run_delta_node_set(policy, shortened, index + 1, end, result);
    ft_run_delta_node_release(policy, shortened);
    return status;
}

/* --------------------------------------------------------------------------------------------
 * Version plumbing.
 * -------------------------------------------------------------------------------------------- */

static void ft_run_delta_vector_zero(ft_run_delta_vector* vector)
{
    vector->policy = NULL;
    vector->current.policy = NULL;
    vector->current.root = NULL;
    vector->checkpoint.policy = NULL;
    vector->checkpoint.root = NULL;
    vector->runs = NULL;
    vector->dirty_count = 0;
}

static bool ft_run_delta_vector_valid(const ft_run_delta_vector* vector)
{
    return vector != NULL && vector->policy != NULL;
}

static void ft_run_delta_vector_dispose_contents(ft_run_delta_vector* vector)
{
    ft_rrb_vector_dispose(&vector->current);
    ft_rrb_vector_dispose(&vector->checkpoint);
    ft_run_delta_node_release(vector->policy, vector->runs);
    ft_run_delta_policy_release(vector->policy);
    ft_run_delta_vector_zero(vector);
}

/* Publishes a fully built successor, supporting a result that aliases the operand. */
static void ft_run_delta_vector_publish(
    const ft_run_delta_vector* source,
    ft_run_delta_vector* result,
    ft_run_delta_vector* produced)
{
    ft_run_delta_vector previous;
    const bool aliases = result == source;
    ft_run_delta_vector_zero(&previous);
    if (aliases) {
        previous = *result;
    }
    *result = *produced;
    ft_run_delta_vector_zero(produced);
    if (aliases) {
        ft_run_delta_vector_dispose_contents(&previous);
    }
}

static ft_status ft_run_delta_vector_clone(
    const ft_run_delta_vector* source,
    ft_run_delta_vector* destination)
{
    ft_status status = FT_STATUS_OK;
    ft_run_delta_vector_zero(destination);
    status = ft_rrb_vector_copy(&source->current, &destination->current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_copy(&source->checkpoint, &destination->checkpoint);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&destination->current);
        ft_run_delta_vector_zero(destination);
        return status;
    }
    destination->runs = ft_run_delta_node_retain(source->runs);
    destination->dirty_count = source->dirty_count;
    destination->policy = source->policy;
    ft_run_delta_policy_retain(destination->policy);
    return FT_STATUS_OK;
}

/* Produces the receiver's own contents, which is what a documented no-op returns. */
static ft_status ft_run_delta_vector_return_receiver(
    const ft_run_delta_vector* vector,
    ft_run_delta_vector* result)
{
    ft_run_delta_vector produced;
    ft_status status = FT_STATUS_OK;
    if (result == vector) {
        return FT_STATUS_OK;
    }
    status = ft_run_delta_vector_clone(vector, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *result = produced;
    return FT_STATUS_OK;
}

/* Produces the clean version canonicalized onto one shared root. */
static ft_status ft_run_delta_vector_publish_clean(
    const ft_run_delta_vector* vector,
    const ft_rrb_vector* root,
    ft_run_delta_vector* result)
{
    ft_run_delta_vector produced;
    ft_status status = FT_STATUS_OK;
    ft_run_delta_vector_zero(&produced);
    status = ft_rrb_vector_copy(root, &produced.current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_copy(root, &produced.checkpoint);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.current);
        return status;
    }
    produced.policy = vector->policy;
    ft_run_delta_policy_retain(produced.policy);
    ft_run_delta_vector_publish(vector, result, &produced);
    return FT_STATUS_OK;
}

/* Splices the source's slice over the destination's, sharing every untouched subtree. Four splits
 * and two boundary-spine concatenations, so O(log n) regardless of the run's length. */
static ft_status ft_run_delta_replace_range(
    const ft_rrb_vector* destination,
    const ft_rrb_vector* source,
    ft_run_delta_run run,
    ft_rrb_vector* result)
{
    ft_rrb_split_result destination_split;
    ft_rrb_split_result tail_split;
    ft_rrb_split_result source_split;
    ft_rrb_split_result middle_split;
    ft_rrb_vector prefix;
    ft_status status = FT_STATUS_OK;

    if (run.start == 0 && run.length == ft_rrb_vector_size(destination)) {
        return ft_rrb_vector_copy(source, result);
    }

    status = ft_rrb_vector_split_at(destination, run.start, &destination_split);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_split_at(&destination_split.right, run.length, &tail_split);
    ft_rrb_vector_dispose(&destination_split.right);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&destination_split.left);
        return status;
    }
    ft_rrb_vector_dispose(&tail_split.left);

    status = ft_rrb_vector_split_at(source, run.start, &source_split);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&destination_split.left);
        ft_rrb_vector_dispose(&tail_split.right);
        return status;
    }
    ft_rrb_vector_dispose(&source_split.left);
    status = ft_rrb_vector_split_at(&source_split.right, run.length, &middle_split);
    ft_rrb_vector_dispose(&source_split.right);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&destination_split.left);
        ft_rrb_vector_dispose(&tail_split.right);
        return status;
    }
    ft_rrb_vector_dispose(&middle_split.right);

    status = ft_rrb_vector_concat(&destination_split.left, &middle_split.left, &prefix);
    ft_rrb_vector_dispose(&destination_split.left);
    ft_rrb_vector_dispose(&middle_split.left);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&tail_split.right);
        return status;
    }
    status = ft_rrb_vector_concat(&prefix, &tail_split.right, result);
    ft_rrb_vector_dispose(&prefix);
    ft_rrb_vector_dispose(&tail_split.right);
    return status;
}

/* --------------------------------------------------------------------------------------------
 * Run descriptors.
 * -------------------------------------------------------------------------------------------- */

size_t ft_run_delta_run_end_exclusive(const ft_run_delta_run* run)
{
    return run == NULL ? 0 : run->start + run->length;
}

bool ft_run_delta_run_contains(const ft_run_delta_run* run, size_t index)
{
    return run != NULL && index >= run->start && index - run->start < run->length;
}

/* --------------------------------------------------------------------------------------------
 * Policies.
 * -------------------------------------------------------------------------------------------- */

void ft_run_delta_policy_config_init(
    ft_run_delta_policy_config* config,
    size_t value_size,
    const void* value_type_identity,
    ft_run_delta_equal_fn equal,
    void* callback_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->value_size = value_size;
    config->value_type_identity = value_type_identity;
    config->equal = equal;
    config->callback_context = callback_context;
    config->allocator.allocate = ft_run_delta_default_allocate;
    config->allocator.deallocate = ft_run_delta_default_deallocate;
    config->allocator.context = NULL;
}

ft_status ft_run_delta_policy_create(
    ft_run_delta_policy* policy,
    const ft_run_delta_policy_config* config)
{
    ft_run_delta_policy_rep* rep = NULL;
    ft_value_type cell_type;
    if (policy == NULL || !ft_run_delta_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_run_delta_policy_rep*)ft_run_delta_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_run_delta_ref_init(&rep->refs);
    rep->config = *config;

    (void)memset(&cell_type, 0, sizeof(cell_type));
    cell_type.size = sizeof(ft_run_delta_cell*);
    cell_type.copy = ft_run_delta_cell_copy_hook;
    cell_type.destroy = ft_run_delta_cell_destroy_hook;
    cell_type.context = rep;
    ft_rrb_policy_init(&rep->rrb_policy, &cell_type, ft_run_delta_cell_equal_hook, NULL);
    rep->rrb_policy.allocator.allocate = config->allocator.allocate;
    rep->rrb_policy.allocator.deallocate = config->allocator.deallocate;
    rep->rrb_policy.allocator.context = config->allocator.context;

    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_run_delta_policy_copy(
    const ft_run_delta_policy* source,
    ft_run_delta_policy* destination)
{
    if (source == NULL || destination == NULL || source->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_run_delta_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_run_delta_policy_move(
    ft_run_delta_policy* destination,
    ft_run_delta_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_run_delta_policy_dispose(ft_run_delta_policy* policy)
{
    if (policy == NULL) {
        return;
    }
    ft_run_delta_policy_release(policy->rep);
    policy->rep = NULL;
}

bool ft_run_delta_policy_same(
    const ft_run_delta_policy* left,
    const ft_run_delta_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL && left->rep == right->rep;
}

/* --------------------------------------------------------------------------------------------
 * Construction and lifetime.
 * -------------------------------------------------------------------------------------------- */

ft_status ft_run_delta_vector_init(
    ft_run_delta_vector* vector,
    const ft_run_delta_policy* policy)
{
    ft_run_delta_vector produced;
    ft_status status = FT_STATUS_OK;
    if (vector == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_run_delta_vector_zero(&produced);
    status = ft_rrb_vector_init(&produced.current, &policy->rep->rrb_policy);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_init(&produced.checkpoint, &policy->rep->rrb_policy);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.current);
        return status;
    }
    produced.policy = policy->rep;
    ft_run_delta_policy_retain(produced.policy);
    *vector = produced;
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_from_array(
    ft_run_delta_vector* vector,
    const ft_run_delta_policy* policy,
    const void* values,
    size_t count)
{
    ft_run_delta_policy_rep* rep = NULL;
    ft_run_delta_cell** cells = NULL;
    ft_run_delta_vector produced;
    size_t created = 0;
    ft_status status = FT_STATUS_OK;

    if (vector == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && values == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return ft_run_delta_vector_init(vector, policy);
    }
    rep = policy->rep;
    if (count > SIZE_MAX / sizeof(ft_run_delta_cell*)) {
        return FT_STATUS_OVERFLOW;
    }
    cells = (ft_run_delta_cell**)ft_run_delta_allocate(
        rep,
        count * sizeof(ft_run_delta_cell*));
    if (cells == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    while (created != count) {
        status = ft_run_delta_cell_create(
            rep,
            (const unsigned char*)values + created * rep->config.value_size,
            &cells[created]);
        if (status != FT_STATUS_OK) {
            break;
        }
        ++created;
    }

    ft_run_delta_vector_zero(&produced);
    if (status == FT_STATUS_OK) {
        status = ft_rrb_vector_from_array(&produced.current, &rep->rrb_policy, cells, count);
    }
    if (status == FT_STATUS_OK) {
        status = ft_rrb_vector_copy(&produced.current, &produced.checkpoint);
        if (status != FT_STATUS_OK) {
            ft_rrb_vector_dispose(&produced.current);
            ft_run_delta_vector_zero(&produced);
        }
    }
    while (created != 0) {
        --created;
        ft_run_delta_cell_release(rep, cells[created]);
    }
    ft_run_delta_deallocate(rep, cells);
    if (status != FT_STATUS_OK) {
        return status;
    }
    produced.policy = rep;
    ft_run_delta_policy_retain(rep);
    *vector = produced;
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_copy(
    const ft_run_delta_vector* source,
    ft_run_delta_vector* destination)
{
    ft_run_delta_vector produced;
    ft_status status = FT_STATUS_OK;
    if (!ft_run_delta_vector_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    status = ft_run_delta_vector_clone(source, &produced);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *destination = produced;
    return FT_STATUS_OK;
}

void ft_run_delta_vector_move(
    ft_run_delta_vector* destination,
    ft_run_delta_vector* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    ft_run_delta_vector_zero(source);
}

void ft_run_delta_vector_dispose(ft_run_delta_vector* vector)
{
    if (vector == NULL || vector->policy == NULL) {
        return;
    }
    ft_run_delta_vector_dispose_contents(vector);
}

ft_status ft_run_delta_vector_get_policy(
    const ft_run_delta_vector* vector,
    ft_run_delta_policy* policy)
{
    if (!ft_run_delta_vector_valid(vector) || policy == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_run_delta_policy_retain(vector->policy);
    policy->rep = vector->policy;
    return FT_STATUS_OK;
}

/* --------------------------------------------------------------------------------------------
 * Queries.
 * -------------------------------------------------------------------------------------------- */

bool ft_run_delta_vector_empty(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_size(vector) == 0;
}

size_t ft_run_delta_vector_size(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_valid(vector) ? ft_rrb_vector_size(&vector->current) : 0;
}

size_t ft_run_delta_vector_dirty_count(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_valid(vector) ? vector->dirty_count : 0;
}

size_t ft_run_delta_vector_dirty_run_count(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_valid(vector) ? ft_run_delta_node_size(vector->runs) : 0;
}

bool ft_run_delta_vector_has_changes(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_dirty_count(vector) != 0;
}

static ft_status ft_run_delta_vector_read_ref(
    const ft_run_delta_vector* vector,
    const ft_rrb_vector* root,
    size_t index,
    const void** value_ref)
{
    ft_run_delta_cell* cell = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_run_delta_vector_valid(vector) || value_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_run_delta_cell_at(root, index, &cell);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *value_ref = cell->bytes;
    ft_run_delta_cell_release(vector->policy, cell);
    return FT_STATUS_OK;
}

static ft_status ft_run_delta_vector_read_copy(
    const ft_run_delta_vector* vector,
    const ft_rrb_vector* root,
    size_t index,
    void* value)
{
    ft_run_delta_cell* cell = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_run_delta_vector_valid(vector) || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_run_delta_cell_at(root, index, &cell);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (vector->policy->config.copy == NULL) {
        (void)memcpy(value, cell->bytes, vector->policy->config.value_size);
    } else {
        status = vector->policy->config.copy(
            value,
            cell->bytes,
            vector->policy->config.callback_context);
    }
    ft_run_delta_cell_release(vector->policy, cell);
    return status;
}

ft_status ft_run_delta_vector_at_ref(
    const ft_run_delta_vector* vector,
    size_t index,
    const void** value_ref)
{
    if (!ft_run_delta_vector_valid(vector)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_run_delta_vector_read_ref(vector, &vector->current, index, value_ref);
}

ft_status ft_run_delta_vector_at_copy(
    const ft_run_delta_vector* vector,
    size_t index,
    void* value)
{
    if (!ft_run_delta_vector_valid(vector)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_run_delta_vector_read_copy(vector, &vector->current, index, value);
}

ft_status ft_run_delta_vector_checkpoint_at_ref(
    const ft_run_delta_vector* vector,
    size_t index,
    const void** value_ref)
{
    if (!ft_run_delta_vector_valid(vector)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_run_delta_vector_read_ref(vector, &vector->checkpoint, index, value_ref);
}

ft_status ft_run_delta_vector_checkpoint_at_copy(
    const ft_run_delta_vector* vector,
    size_t index,
    void* value)
{
    if (!ft_run_delta_vector_valid(vector)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_run_delta_vector_read_copy(vector, &vector->checkpoint, index, value);
}

ft_status ft_run_delta_vector_is_dirty(
    const ft_run_delta_vector* vector,
    size_t index,
    bool* dirty)
{
    if (!ft_run_delta_vector_valid(vector) || dirty == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_rrb_vector_size(&vector->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *dirty = ft_run_delta_node_containing(vector->runs, index) != NULL;
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_try_get_dirty_run_containing(
    const ft_run_delta_vector* vector,
    size_t index,
    bool* found,
    ft_run_delta_run* run)
{
    const ft_run_delta_run_node* node = NULL;
    if (!ft_run_delta_vector_valid(vector) || found == NULL || run == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_rrb_vector_size(&vector->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    node = ft_run_delta_node_containing(vector->runs, index);
    *found = node != NULL;
    if (node != NULL) {
        *run = ft_run_delta_node_to_run(node);
    }
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_dirty_run_at(
    const ft_run_delta_vector* vector,
    size_t rank,
    ft_run_delta_run* run)
{
    const ft_run_delta_run_node* node = NULL;
    if (!ft_run_delta_vector_valid(vector) || run == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    node = ft_run_delta_node_at(vector->runs, rank);
    if (node == NULL) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    *run = ft_run_delta_node_to_run(node);
    return FT_STATUS_OK;
}

/* --------------------------------------------------------------------------------------------
 * Point edits.
 * -------------------------------------------------------------------------------------------- */

/* Publishes the successor in which the position holds the replacement cell, whose reference this
 * consumes and whose dirtiness the caller has already decided. */
static ft_status ft_run_delta_vector_replace_cell(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_cell* replacement,
    bool remains_dirty,
    ft_run_delta_vector* result)
{
    ft_run_delta_policy_rep* const rep = vector->policy;
    const bool was_dirty = ft_run_delta_node_containing(vector->runs, index) != NULL;
    ft_run_delta_run_node* next_runs = NULL;
    ft_run_delta_vector produced;
    size_t next_dirty_count = vector->dirty_count;
    ft_status status = FT_STATUS_OK;

    if (!was_dirty && remains_dirty) {
        status = ft_run_delta_add_dirty_position(rep, vector->runs, index, &next_runs);
        ++next_dirty_count;
    } else if (was_dirty && !remains_dirty) {
        status = ft_run_delta_remove_dirty_position(rep, vector->runs, index, &next_runs);
        --next_dirty_count;
    } else {
        next_runs = ft_run_delta_node_retain(vector->runs);
    }
    if (status != FT_STATUS_OK) {
        ft_run_delta_cell_release(rep, replacement);
        return status;
    }

    ft_run_delta_vector_zero(&produced);
    if (next_runs == NULL) {
        status = ft_rrb_vector_copy(&vector->checkpoint, &produced.current);
    } else {
        status = ft_rrb_vector_set(&vector->current, index, &replacement, &produced.current);
    }
    ft_run_delta_cell_release(rep, replacement);
    if (status != FT_STATUS_OK) {
        ft_run_delta_node_release(rep, next_runs);
        return status;
    }
    status = ft_rrb_vector_copy(&vector->checkpoint, &produced.checkpoint);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.current);
        ft_run_delta_node_release(rep, next_runs);
        return status;
    }
    produced.runs = next_runs;
    produced.dirty_count = next_dirty_count;
    produced.policy = rep;
    ft_run_delta_policy_retain(rep);
    ft_run_delta_vector_publish(vector, result, &produced);
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_set(
    const ft_run_delta_vector* vector,
    size_t index,
    const void* value,
    ft_run_delta_vector* result)
{
    ft_run_delta_policy_rep* rep = NULL;
    ft_run_delta_cell* current_cell = NULL;
    ft_run_delta_cell* checkpoint_cell = NULL;
    ft_run_delta_cell* replacement = NULL;
    bool equal = false;
    bool remains_dirty = false;
    ft_status status = FT_STATUS_OK;

    if (!ft_run_delta_vector_valid(vector) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_rrb_vector_size(&vector->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    rep = vector->policy;

    status = ft_run_delta_cell_at(&vector->current, index, &current_cell);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_run_delta_equal(rep, current_cell->bytes, value, &equal);
    ft_run_delta_cell_release(rep, current_cell);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (equal) {
        return ft_run_delta_vector_return_receiver(vector, result);
    }

    status = ft_run_delta_cell_at(&vector->checkpoint, index, &checkpoint_cell);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_run_delta_equal(rep, checkpoint_cell->bytes, value, &equal);
    if (status != FT_STATUS_OK) {
        ft_run_delta_cell_release(rep, checkpoint_cell);
        return status;
    }
    remains_dirty = !equal;
    if (remains_dirty) {
        status = ft_run_delta_cell_create(rep, value, &replacement);
        ft_run_delta_cell_release(rep, checkpoint_cell);
        if (status != FT_STATUS_OK) {
            return status;
        }
    } else {
        replacement = checkpoint_cell;
    }
    return ft_run_delta_vector_replace_cell(vector, index, replacement, remains_dirty, result);
}

ft_status ft_run_delta_vector_reset(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_vector* result)
{
    ft_run_delta_policy_rep* rep = NULL;
    ft_run_delta_cell* current_cell = NULL;
    ft_run_delta_cell* checkpoint_cell = NULL;
    bool equal = false;
    ft_status status = FT_STATUS_OK;

    if (!ft_run_delta_vector_valid(vector) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_rrb_vector_size(&vector->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    rep = vector->policy;

    status = ft_run_delta_cell_at(&vector->current, index, &current_cell);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_run_delta_cell_at(&vector->checkpoint, index, &checkpoint_cell);
    if (status != FT_STATUS_OK) {
        ft_run_delta_cell_release(rep, current_cell);
        return status;
    }
    status = ft_run_delta_equal(rep, current_cell->bytes, checkpoint_cell->bytes, &equal);
    ft_run_delta_cell_release(rep, current_cell);
    if (status != FT_STATUS_OK) {
        ft_run_delta_cell_release(rep, checkpoint_cell);
        return status;
    }
    if (equal) {
        ft_run_delta_cell_release(rep, checkpoint_cell);
        return ft_run_delta_vector_return_receiver(vector, result);
    }
    return ft_run_delta_vector_replace_cell(vector, index, checkpoint_cell, false, result);
}

/* --------------------------------------------------------------------------------------------
 * Whole-version and selected-run operations.
 * -------------------------------------------------------------------------------------------- */

ft_status ft_run_delta_vector_checkpoint(
    const ft_run_delta_vector* vector,
    ft_run_delta_vector* result)
{
    if (!ft_run_delta_vector_valid(vector) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->dirty_count == 0) {
        return ft_run_delta_vector_return_receiver(vector, result);
    }
    return ft_run_delta_vector_publish_clean(vector, &vector->current, result);
}

ft_status ft_run_delta_vector_rollback(
    const ft_run_delta_vector* vector,
    ft_run_delta_vector* result)
{
    if (!ft_run_delta_vector_valid(vector) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->dirty_count == 0) {
        return ft_run_delta_vector_return_receiver(vector, result);
    }
    return ft_run_delta_vector_publish_clean(vector, &vector->checkpoint, result);
}

/* Splices one indexed run's current values into the checkpoint, leaving every other run alone.
 * Comparison-free; the run must be a record of this version's index. */
static ft_status ft_run_delta_vector_accept_run(
    const ft_run_delta_vector* vector,
    ft_run_delta_run run,
    ft_run_delta_vector* result)
{
    ft_run_delta_policy_rep* const rep = vector->policy;
    ft_run_delta_vector produced;
    ft_status status = FT_STATUS_OK;

    if (ft_run_delta_node_size(vector->runs) == 1) {
        return ft_run_delta_vector_checkpoint(vector, result);
    }
    ft_run_delta_vector_zero(&produced);
    status = ft_run_delta_replace_range(
        &vector->checkpoint,
        &vector->current,
        run,
        &produced.checkpoint);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_copy(&vector->current, &produced.current);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.checkpoint);
        return status;
    }
    status = ft_run_delta_node_remove(rep, vector->runs, run.start, &produced.runs);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.checkpoint);
        ft_rrb_vector_dispose(&produced.current);
        return status;
    }
    produced.dirty_count = vector->dirty_count - run.length;
    produced.policy = rep;
    ft_run_delta_policy_retain(rep);
    ft_run_delta_vector_publish(vector, result, &produced);
    return FT_STATUS_OK;
}

/* Splices one indexed run's checkpoint values back over the current root, leaving every other run
 * alone. Comparison-free; the run must be a record of this version's index. */
static ft_status ft_run_delta_vector_revert_run(
    const ft_run_delta_vector* vector,
    ft_run_delta_run run,
    ft_run_delta_vector* result)
{
    ft_run_delta_policy_rep* const rep = vector->policy;
    ft_run_delta_vector produced;
    ft_status status = FT_STATUS_OK;

    if (ft_run_delta_node_size(vector->runs) == 1) {
        return ft_run_delta_vector_rollback(vector, result);
    }
    ft_run_delta_vector_zero(&produced);
    status = ft_run_delta_replace_range(
        &vector->current,
        &vector->checkpoint,
        run,
        &produced.current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_copy(&vector->checkpoint, &produced.checkpoint);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.current);
        return status;
    }
    status = ft_run_delta_node_remove(rep, vector->runs, run.start, &produced.runs);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&produced.checkpoint);
        ft_rrb_vector_dispose(&produced.current);
        return status;
    }
    produced.dirty_count = vector->dirty_count - run.length;
    produced.policy = rep;
    ft_run_delta_policy_retain(rep);
    ft_run_delta_vector_publish(vector, result, &produced);
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_accept_dirty_run_at(
    const ft_run_delta_vector* vector,
    size_t rank,
    ft_run_delta_vector* result)
{
    ft_run_delta_run run;
    ft_status status = FT_STATUS_OK;
    if (result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_run_delta_vector_dirty_run_at(vector, rank, &run);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_run_delta_vector_accept_run(vector, run, result);
}

ft_status ft_run_delta_vector_revert_dirty_run_at(
    const ft_run_delta_vector* vector,
    size_t rank,
    ft_run_delta_vector* result)
{
    ft_run_delta_run run;
    ft_status status = FT_STATUS_OK;
    if (result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_run_delta_vector_dirty_run_at(vector, rank, &run);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_run_delta_vector_revert_run(vector, run, result);
}

ft_status ft_run_delta_vector_accept_dirty_run_containing(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_vector* result)
{
    const ft_run_delta_run_node* node = NULL;
    if (!ft_run_delta_vector_valid(vector) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_rrb_vector_size(&vector->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    node = ft_run_delta_node_containing(vector->runs, index);
    if (node == NULL) {
        return ft_run_delta_vector_return_receiver(vector, result);
    }
    return ft_run_delta_vector_accept_run(vector, ft_run_delta_node_to_run(node), result);
}

ft_status ft_run_delta_vector_revert_dirty_run_containing(
    const ft_run_delta_vector* vector,
    size_t index,
    ft_run_delta_vector* result)
{
    const ft_run_delta_run_node* node = NULL;
    if (!ft_run_delta_vector_valid(vector) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_rrb_vector_size(&vector->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    node = ft_run_delta_node_containing(vector->runs, index);
    if (node == NULL) {
        return ft_run_delta_vector_return_receiver(vector, result);
    }
    return ft_run_delta_vector_revert_run(vector, ft_run_delta_node_to_run(node), result);
}

/* --------------------------------------------------------------------------------------------
 * Traversal.
 * -------------------------------------------------------------------------------------------- */

typedef struct ft_run_delta_visit_state {
    ft_run_delta_visit_fn visitor;
    void* context;
    ft_status status;
} ft_run_delta_visit_state;

/* The underlying leaf walk cannot itself be stopped, so once a visitor reports failure no further
 * visitor call is made and that first status is what propagates. */
static void ft_run_delta_visit_trampoline(const void* value, void* context)
{
    ft_run_delta_visit_state* const state = (ft_run_delta_visit_state*)context;
    const ft_run_delta_cell* const cell = *(const ft_run_delta_cell* const*)value;
    if (state->status != FT_STATUS_OK) {
        return;
    }
    state->status = state->visitor(cell->bytes, state->context);
}

ft_status ft_run_delta_vector_visit(
    const ft_run_delta_vector* vector,
    ft_run_delta_visit_fn visitor,
    void* context)
{
    ft_run_delta_visit_state state;
    ft_status status = FT_STATUS_OK;
    if (!ft_run_delta_vector_valid(vector) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    state.visitor = visitor;
    state.context = context;
    state.status = FT_STATUS_OK;
    status = ft_rrb_vector_visit(&vector->current, ft_run_delta_visit_trampoline, &state);
    return status != FT_STATUS_OK ? status : state.status;
}

ft_status ft_run_delta_vector_visit_dirty_runs(
    const ft_run_delta_vector* vector,
    ft_run_delta_run_visit_fn visitor,
    void* context)
{
    if (!ft_run_delta_vector_valid(vector) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_run_delta_node_visit(vector->runs, visitor, context);
}

/* --------------------------------------------------------------------------------------------
 * Identity and validation.
 * -------------------------------------------------------------------------------------------- */

const void* ft_run_delta_vector_current_root_identity(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_valid(vector)
        ? ft_rrb_vector_root_identity(&vector->current)
        : NULL;
}

const void* ft_run_delta_vector_checkpoint_root_identity(const ft_run_delta_vector* vector)
{
    return ft_run_delta_vector_valid(vector)
        ? ft_rrb_vector_root_identity(&vector->checkpoint)
        : NULL;
}

bool ft_run_delta_vector_shares_current_root(
    const ft_run_delta_vector* left,
    const ft_run_delta_vector* right)
{
    return ft_run_delta_vector_valid(left) && ft_run_delta_vector_valid(right) &&
        ft_rrb_vector_shares_root(&left->current, &right->current);
}

bool ft_run_delta_vector_shares_checkpoint_root(
    const ft_run_delta_vector* left,
    const ft_run_delta_vector* right)
{
    return ft_run_delta_vector_valid(left) && ft_run_delta_vector_valid(right) &&
        ft_rrb_vector_shares_root(&left->checkpoint, &right->checkpoint);
}

typedef struct ft_run_delta_audit_state {
    size_t count;
    size_t visited;
    size_t counted_dirty;
    size_t previous_end;
    bool has_previous;
    bool valid;
} ft_run_delta_audit_state;

static ft_status ft_run_delta_audit_run(const ft_run_delta_run* run, void* context)
{
    ft_run_delta_audit_state* const state = (ft_run_delta_audit_state*)context;
    const size_t end = run->start + run->length;
    ++state->visited;
    if (run->length == 0 || end < run->start || end > state->count ||
        (state->has_previous && run->start <= state->previous_end)) {
        state->valid = false;
        return FT_STATUS_OK;
    }
    state->counted_dirty += run->length;
    state->previous_end = end;
    state->has_previous = true;
    return FT_STATUS_OK;
}

ft_status ft_run_delta_vector_validate(
    const ft_run_delta_vector* vector,
    bool* valid,
    ft_run_delta_statistics* statistics)
{
    ft_run_delta_audit_state audit;
    ft_rrb_statistics root_statistics;
    ft_run_delta_statistics produced;
    size_t index = 0;
    bool structurally_valid = true;

    if (!ft_run_delta_vector_valid(vector) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    produced.count = ft_rrb_vector_size(&vector->current);
    produced.dirty_count = vector->dirty_count;
    produced.dirty_run_count = ft_run_delta_node_size(vector->runs);

    structurally_valid = produced.count == ft_rrb_vector_size(&vector->checkpoint) &&
        produced.dirty_count <= produced.count &&
        ft_rrb_vector_validate(&vector->current, &root_statistics) &&
        ft_rrb_vector_validate(&vector->checkpoint, &root_statistics);
    if (structurally_valid && vector->runs == NULL) {
        structurally_valid = produced.dirty_count == 0 &&
            ft_rrb_vector_shares_root(&vector->current, &vector->checkpoint);
    }

    if (structurally_valid) {
        structurally_valid = ft_run_delta_node_balanced(vector->runs);
    }

    if (structurally_valid) {
        audit.count = produced.count;
        audit.visited = 0;
        audit.counted_dirty = 0;
        audit.previous_end = 0;
        audit.has_previous = false;
        audit.valid = true;
        (void)ft_run_delta_node_visit(vector->runs, ft_run_delta_audit_run, &audit);
        structurally_valid = audit.valid && audit.visited == produced.dirty_run_count &&
            audit.counted_dirty == produced.dirty_count;
    }

    for (index = 0; structurally_valid && index != produced.count; ++index) {
        ft_run_delta_cell* current_cell = NULL;
        ft_run_delta_cell* checkpoint_cell = NULL;
        const bool dirty = ft_run_delta_node_containing(vector->runs, index) != NULL;
        bool equal = false;
        ft_status status = ft_run_delta_cell_at(&vector->current, index, &current_cell);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = ft_run_delta_cell_at(&vector->checkpoint, index, &checkpoint_cell);
        if (status != FT_STATUS_OK) {
            ft_run_delta_cell_release(vector->policy, current_cell);
            return status;
        }
        if (!dirty) {
            structurally_valid = current_cell == checkpoint_cell;
        } else {
            status = ft_run_delta_equal(
                vector->policy,
                current_cell->bytes,
                checkpoint_cell->bytes,
                &equal);
            if (status != FT_STATUS_OK) {
                ft_run_delta_cell_release(vector->policy, current_cell);
                ft_run_delta_cell_release(vector->policy, checkpoint_cell);
                return status;
            }
            structurally_valid = !equal;
        }
        ft_run_delta_cell_release(vector->policy, current_cell);
        ft_run_delta_cell_release(vector->policy, checkpoint_cell);
    }

    *valid = structurally_valid;
    *statistics = produced;
    return FT_STATUS_OK;
}
