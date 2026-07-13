#include <tools/data_structures/finger_tree/rrb_vector.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef volatile LONG64 ft_rrb_ref_count;

static void ft_rrb_ref_init(ft_rrb_ref_count* count)
{
    *count = 1;
}

static void ft_rrb_ref_retain(ft_rrb_ref_count* count)
{
    (void)InterlockedIncrement64(count);
}

static bool ft_rrb_ref_release(ft_rrb_ref_count* count)
{
    return InterlockedDecrement64(count) == 0;
}
#else
#include <stdatomic.h>
typedef atomic_size_t ft_rrb_ref_count;

static void ft_rrb_ref_init(ft_rrb_ref_count* count)
{
    atomic_init(count, 1);
}

static void ft_rrb_ref_retain(ft_rrb_ref_count* count)
{
    (void)atomic_fetch_add_explicit(count, 1, memory_order_relaxed);
}

static bool ft_rrb_ref_release(ft_rrb_ref_count* count)
{
    return atomic_fetch_sub_explicit(count, 1, memory_order_acq_rel) == 1;
}
#endif

enum {
    FT_RRB_RADIX_BITS = 5,
    FT_RRB_BRANCH_FACTOR = 32,
    /* The base term is the greatest minimum height in the count domain; boundary-only
       concatenation may legally retain one additional level of slack. */
    FT_RRB_MAX_HEIGHT = (sizeof(size_t) * CHAR_BIT - 1) / FT_RRB_RADIX_BITS + 1
};

typedef enum ft_rrb_node_kind {
    FT_RRB_LEAF,
    FT_RRB_BRANCH
} ft_rrb_node_kind;

struct ft_rrb_node {
    ft_rrb_ref_count refs;
    ft_rrb_node_kind kind;
    unsigned height;
    size_t count;
    union {
        struct {
            unsigned char* values;
        } leaf;
        struct {
            size_t child_count;
            ft_rrb_node** children;
            size_t* sizes;
        } branch;
    } as;
};

struct ft_rrb_builder_rep {
    ft_rrb_node* prefix;
    ft_rrb_node** leaves;
    size_t leaf_count;
    size_t leaf_capacity;
    unsigned char* tail;
    size_t tail_count;
    size_t staged_count;
};

typedef struct ft_rrb_nodes {
    ft_rrb_node* values[2];
    size_t count;
} ft_rrb_nodes;

typedef struct ft_rrb_node_split {
    ft_rrb_node* left;
    ft_rrb_node* right;
} ft_rrb_node_split;

static void* ft_rrb_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size == 0 ? 1 : size);
}

static void ft_rrb_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_rrb_policy_valid(const ft_rrb_policy* policy)
{
    return policy != NULL &&
        policy->value.size != 0 &&
        policy->equal != NULL &&
        policy->allocator.allocate != NULL &&
        policy->allocator.deallocate != NULL;
}

static void* ft_rrb_allocate(const ft_rrb_policy* policy, size_t size)
{
    return policy->allocator.allocate(size == 0 ? 1 : size, policy->allocator.context);
}

static void ft_rrb_deallocate(const ft_rrb_policy* policy, void* allocation)
{
    if (allocation != NULL) {
        policy->allocator.deallocate(allocation, policy->allocator.context);
    }
}

static bool ft_rrb_add_overflow(size_t left, size_t right, size_t* result)
{
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool ft_rrb_multiply_overflow(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static void ft_rrb_value_copy(const ft_rrb_policy* policy, void* destination, const void* source)
{
    if (policy->value.copy != NULL) {
        policy->value.copy(destination, source, policy->value.context);
    } else {
        (void)memcpy(destination, source, policy->value.size);
    }
}

static void ft_rrb_value_destroy(const ft_rrb_policy* policy, void* value)
{
    if (policy->value.destroy != NULL) {
        policy->value.destroy(value, policy->value.context);
    }
}

static void* ft_rrb_leaf_value(const ft_rrb_policy* policy, const ft_rrb_node* leaf, size_t index)
{
    return leaf->as.leaf.values + index * policy->value.size;
}

static ft_rrb_node* ft_rrb_node_retain(ft_rrb_node* node)
{
    if (node != NULL) {
        ft_rrb_ref_retain(&node->refs);
    }
    return node;
}

static void ft_rrb_node_release(const ft_rrb_policy* policy, ft_rrb_node* node)
{
    if (node == NULL || !ft_rrb_ref_release(&node->refs)) {
        return;
    }

    if (node->kind == FT_RRB_LEAF) {
        for (size_t index = 0; index != node->count; ++index) {
            ft_rrb_value_destroy(policy, ft_rrb_leaf_value(policy, node, index));
        }
        ft_rrb_deallocate(policy, node->as.leaf.values);
    } else {
        for (size_t index = 0; index != node->as.branch.child_count; ++index) {
            ft_rrb_node_release(policy, node->as.branch.children[index]);
        }
        ft_rrb_deallocate(policy, node->as.branch.sizes);
        ft_rrb_deallocate(policy, node->as.branch.children);
    }
    ft_rrb_deallocate(policy, node);
}

static ft_status ft_rrb_leaf_adopt(
    const ft_rrb_policy* policy,
    unsigned char* values,
    size_t count,
    ft_rrb_node** result)
{
    ft_rrb_node* node = (ft_rrb_node*)ft_rrb_allocate(policy, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_rrb_ref_init(&node->refs);
    node->kind = FT_RRB_LEAF;
    node->height = 0;
    node->count = count;
    node->as.leaf.values = values;
    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_rrb_leaf_copy_range(
    const ft_rrb_policy* policy,
    const void* values,
    size_t count,
    ft_rrb_node** result)
{
    if (count == 0 || count > FT_RRB_BRANCH_FACTOR) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t byte_count = 0;
    if (ft_rrb_multiply_overflow(count, policy->value.size, &byte_count)) {
        return FT_STATUS_OVERFLOW;
    }
    unsigned char* copied = (unsigned char*)ft_rrb_allocate(policy, byte_count);
    if (copied == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t copied_count = 0;
    for (; copied_count != count; ++copied_count) {
        ft_rrb_value_copy(
            policy,
            copied + copied_count * policy->value.size,
            (const unsigned char*)values + copied_count * policy->value.size);
    }

    ft_status status = ft_rrb_leaf_adopt(policy, copied, count, result);
    if (status != FT_STATUS_OK) {
        while (copied_count != 0) {
            --copied_count;
            ft_rrb_value_destroy(policy, copied + copied_count * policy->value.size);
        }
        ft_rrb_deallocate(policy, copied);
    }
    return status;
}

static bool ft_rrb_regular_layout(ft_rrb_node* const* children, size_t child_count, unsigned height)
{
    if (child_count == 0 || height == 0 || height > FT_RRB_MAX_HEIGHT) {
        return false;
    }
    const unsigned shift = height * FT_RRB_RADIX_BITS;
    /* The legal slack height can exceed size_t's radix capacity. Such a branch is relaxed;
       never perform an oversized shift merely to decide whether a size table is required. */
    if (shift >= sizeof(size_t) * CHAR_BIT) {
        return false;
    }
    const size_t capacity = (size_t)1 << shift;
    for (size_t index = 0; index + 1 < child_count; ++index) {
        if (children[index]->count != capacity) {
            return false;
        }
    }
    return children[child_count - 1]->count <= capacity;
}

static ft_status ft_rrb_branch_create(
    const ft_rrb_policy* policy,
    ft_rrb_node* const* children,
    size_t child_count,
    ft_rrb_node** result)
{
    if (child_count == 0 || child_count > FT_RRB_BRANCH_FACTOR) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const unsigned child_height = children[0]->height;
    if (child_height >= FT_RRB_MAX_HEIGHT) {
        return FT_STATUS_OVERFLOW;
    }

    size_t count = 0;
    for (size_t index = 0; index != child_count; ++index) {
        if (children[index] == NULL || children[index]->height != child_height) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        if (ft_rrb_add_overflow(count, children[index]->count, &count)) {
            return FT_STATUS_OVERFLOW;
        }
    }

    size_t children_bytes = 0;
    if (ft_rrb_multiply_overflow(child_count, sizeof(ft_rrb_node*), &children_bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    ft_rrb_node* node = (ft_rrb_node*)ft_rrb_allocate(policy, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_rrb_node** copied_children = (ft_rrb_node**)ft_rrb_allocate(policy, children_bytes);
    if (copied_children == NULL) {
        ft_rrb_deallocate(policy, node);
        return FT_STATUS_NO_MEMORY;
    }

    const unsigned height = child_height + 1;
    const bool regular = ft_rrb_regular_layout(children, child_count, height);
    size_t* sizes = NULL;
    if (!regular) {
        size_t size_bytes = 0;
        if (ft_rrb_multiply_overflow(child_count, sizeof(size_t), &size_bytes)) {
            ft_rrb_deallocate(policy, copied_children);
            ft_rrb_deallocate(policy, node);
            return FT_STATUS_OVERFLOW;
        }
        sizes = (size_t*)ft_rrb_allocate(policy, size_bytes);
        if (sizes == NULL) {
            ft_rrb_deallocate(policy, copied_children);
            ft_rrb_deallocate(policy, node);
            return FT_STATUS_NO_MEMORY;
        }
    }

    size_t cumulative = 0;
    for (size_t index = 0; index != child_count; ++index) {
        copied_children[index] = ft_rrb_node_retain(children[index]);
        cumulative += children[index]->count;
        if (sizes != NULL) {
            sizes[index] = cumulative;
        }
    }
    ft_rrb_ref_init(&node->refs);
    node->kind = FT_RRB_BRANCH;
    node->height = height;
    node->count = count;
    node->as.branch.child_count = child_count;
    node->as.branch.children = copied_children;
    node->as.branch.sizes = sizes;
    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_rrb_find_child(
    const ft_rrb_node* branch,
    size_t index,
    size_t* child_index,
    size_t* before)
{
    if (branch->as.branch.sizes == NULL) {
        const unsigned shift = branch->height * FT_RRB_RADIX_BITS;
        const size_t selected = index >> shift;
        if (selected >= branch->as.branch.child_count) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        *child_index = selected;
        *before = selected << shift;
        return FT_STATUS_OK;
    }

    size_t low = 0;
    size_t high = branch->as.branch.child_count - 1;
    while (low < high) {
        const size_t middle = low + (high - low) / 2;
        if (index < branch->as.branch.sizes[middle]) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    *child_index = low;
    *before = low == 0 ? 0 : branch->as.branch.sizes[low - 1];
    return FT_STATUS_OK;
}

static const void* ft_rrb_node_at(
    const ft_rrb_policy* policy,
    const ft_rrb_node* root,
    size_t index)
{
    const ft_rrb_node* node = root;
    while (node->kind == FT_RRB_BRANCH) {
        size_t child_index = 0;
        size_t before = 0;
        if (ft_rrb_find_child(node, index, &child_index, &before) != FT_STATUS_OK) {
            return NULL;
        }
        index -= before;
        node = node->as.branch.children[child_index];
    }
    return ft_rrb_leaf_value(policy, node, index);
}

static ft_status ft_rrb_leaf_replace(
    const ft_rrb_policy* policy,
    const ft_rrb_node* leaf,
    size_t index,
    const void* value,
    ft_rrb_node** result)
{
    size_t byte_count = 0;
    if (ft_rrb_multiply_overflow(leaf->count, policy->value.size, &byte_count)) {
        return FT_STATUS_OVERFLOW;
    }
    unsigned char* values = (unsigned char*)ft_rrb_allocate(policy, byte_count);
    if (values == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    size_t copied = 0;
    for (; copied != leaf->count; ++copied) {
        const void* source = copied == index ? value : ft_rrb_leaf_value(policy, leaf, copied);
        ft_rrb_value_copy(policy, values + copied * policy->value.size, source);
    }
    ft_status status = ft_rrb_leaf_adopt(policy, values, leaf->count, result);
    if (status != FT_STATUS_OK) {
        while (copied != 0) {
            --copied;
            ft_rrb_value_destroy(policy, values + copied * policy->value.size);
        }
        ft_rrb_deallocate(policy, values);
    }
    return status;
}

static ft_status ft_rrb_node_set(
    const ft_rrb_policy* policy,
    ft_rrb_node* node,
    size_t index,
    const void* value,
    ft_rrb_node** result)
{
    if (node->kind == FT_RRB_LEAF) {
        const void* stored_value = ft_rrb_leaf_value(policy, node, index);
        if (stored_value == value || policy->equal(stored_value, value, policy->equal_context)) {
            *result = ft_rrb_node_retain(node);
            return FT_STATUS_OK;
        }
        return ft_rrb_leaf_replace(policy, node, index, value, result);
    }

    size_t child_index = 0;
    size_t before = 0;
    ft_status status = ft_rrb_find_child(node, index, &child_index, &before);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_rrb_node* child = NULL;
    status = ft_rrb_node_set(
        policy,
        node->as.branch.children[child_index],
        index - before,
        value,
        &child);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (child == node->as.branch.children[child_index]) {
        ft_rrb_node_release(policy, child);
        *result = ft_rrb_node_retain(node);
        return FT_STATUS_OK;
    }

    ft_rrb_node* children[FT_RRB_BRANCH_FACTOR];
    for (size_t position = 0; position != node->as.branch.child_count; ++position) {
        children[position] = position == child_index ? child : node->as.branch.children[position];
    }
    status = ft_rrb_branch_create(policy, children, node->as.branch.child_count, result);
    ft_rrb_node_release(policy, child);
    return status;
}

static ft_status ft_rrb_leaf_concat_segment(
    const ft_rrb_policy* policy,
    const ft_rrb_node* left,
    const ft_rrb_node* right,
    size_t start,
    size_t count,
    ft_rrb_node** result)
{
    size_t byte_count = 0;
    if (ft_rrb_multiply_overflow(count, policy->value.size, &byte_count)) {
        return FT_STATUS_OVERFLOW;
    }
    unsigned char* values = (unsigned char*)ft_rrb_allocate(policy, byte_count);
    if (values == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t copied = 0;
    for (; copied != count; ++copied) {
        const size_t source_index = start + copied;
        const void* source = source_index < left->count
            ? ft_rrb_leaf_value(policy, left, source_index)
            : ft_rrb_leaf_value(policy, right, source_index - left->count);
        ft_rrb_value_copy(policy, values + copied * policy->value.size, source);
    }
    ft_status status = ft_rrb_leaf_adopt(policy, values, count, result);
    if (status != FT_STATUS_OK) {
        while (copied != 0) {
            --copied;
            ft_rrb_value_destroy(policy, values + copied * policy->value.size);
        }
        ft_rrb_deallocate(policy, values);
    }
    return status;
}

static ft_status ft_rrb_partition(
    const ft_rrb_policy* policy,
    ft_rrb_node* const* children,
    size_t child_count,
    ft_rrb_nodes* result)
{
    result->count = 0;
    result->values[0] = NULL;
    result->values[1] = NULL;
    if (child_count <= FT_RRB_BRANCH_FACTOR) {
        ft_status status = ft_rrb_branch_create(policy, children, child_count, &result->values[0]);
        if (status == FT_STATUS_OK) {
            result->count = 1;
        }
        return status;
    }

    const size_t split = child_count / 2;
    ft_status status = ft_rrb_branch_create(policy, children, split, &result->values[0]);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_branch_create(policy, children + split, child_count - split, &result->values[1]);
    if (status != FT_STATUS_OK) {
        ft_rrb_node_release(policy, result->values[0]);
        result->values[0] = NULL;
        return status;
    }
    result->count = 2;
    return FT_STATUS_OK;
}

static void ft_rrb_nodes_release(const ft_rrb_policy* policy, ft_rrb_nodes* nodes)
{
    for (size_t index = 0; index != nodes->count; ++index) {
        ft_rrb_node_release(policy, nodes->values[index]);
        nodes->values[index] = NULL;
    }
    nodes->count = 0;
}

static ft_status ft_rrb_concat_same_height(
    const ft_rrb_policy* policy,
    ft_rrb_node* left,
    ft_rrb_node* right,
    ft_rrb_nodes* result);

static ft_status ft_rrb_concat_nodes(
    const ft_rrb_policy* policy,
    ft_rrb_node* left,
    ft_rrb_node* right,
    ft_rrb_nodes* result)
{
    if (left->height == right->height) {
        return ft_rrb_concat_same_height(policy, left, right, result);
    }

    ft_rrb_nodes boundary;
    ft_rrb_node* children[FT_RRB_BRANCH_FACTOR * 2];
    size_t child_count = 0;
    ft_status status = FT_STATUS_OK;
    if (left->height > right->height) {
        if (left->kind != FT_RRB_BRANCH) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        status = ft_rrb_concat_nodes(
            policy,
            left->as.branch.children[left->as.branch.child_count - 1],
            right,
            &boundary);
        if (status != FT_STATUS_OK) {
            return status;
        }
        for (size_t index = 0; index + 1 < left->as.branch.child_count; ++index) {
            children[child_count++] = left->as.branch.children[index];
        }
        for (size_t index = 0; index != boundary.count; ++index) {
            children[child_count++] = boundary.values[index];
        }
    } else {
        if (right->kind != FT_RRB_BRANCH) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        status = ft_rrb_concat_nodes(policy, left, right->as.branch.children[0], &boundary);
        if (status != FT_STATUS_OK) {
            return status;
        }
        for (size_t index = 0; index != boundary.count; ++index) {
            children[child_count++] = boundary.values[index];
        }
        for (size_t index = 1; index != right->as.branch.child_count; ++index) {
            children[child_count++] = right->as.branch.children[index];
        }
    }

    status = ft_rrb_partition(policy, children, child_count, result);
    ft_rrb_nodes_release(policy, &boundary);
    return status;
}

static ft_status ft_rrb_concat_same_height(
    const ft_rrb_policy* policy,
    ft_rrb_node* left,
    ft_rrb_node* right,
    ft_rrb_nodes* result)
{
    result->count = 0;
    result->values[0] = NULL;
    result->values[1] = NULL;
    if (left->kind == FT_RRB_LEAF) {
        if (right->kind != FT_RRB_LEAF) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
        if (left->count == FT_RRB_BRANCH_FACTOR && right->count == FT_RRB_BRANCH_FACTOR) {
            result->values[0] = ft_rrb_node_retain(left);
            result->values[1] = ft_rrb_node_retain(right);
            result->count = 2;
            return FT_STATUS_OK;
        }
        size_t total = 0;
        if (ft_rrb_add_overflow(left->count, right->count, &total)) {
            return FT_STATUS_OVERFLOW;
        }
        if (total <= FT_RRB_BRANCH_FACTOR) {
            ft_status status = ft_rrb_leaf_concat_segment(policy, left, right, 0, total, &result->values[0]);
            if (status == FT_STATUS_OK) {
                result->count = 1;
            }
            return status;
        }

        const size_t split = total / 2;
        ft_status status = ft_rrb_leaf_concat_segment(policy, left, right, 0, split, &result->values[0]);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = ft_rrb_leaf_concat_segment(
            policy,
            left,
            right,
            split,
            total - split,
            &result->values[1]);
        if (status != FT_STATUS_OK) {
            ft_rrb_node_release(policy, result->values[0]);
            result->values[0] = NULL;
            return status;
        }
        result->count = 2;
        return FT_STATUS_OK;
    }

    if (right->kind != FT_RRB_BRANCH) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_rrb_nodes boundary;
    ft_status status = ft_rrb_concat_same_height(
        policy,
        left->as.branch.children[left->as.branch.child_count - 1],
        right->as.branch.children[0],
        &boundary);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rrb_node* children[FT_RRB_BRANCH_FACTOR * 2];
    size_t child_count = 0;
    for (size_t index = 0; index + 1 < left->as.branch.child_count; ++index) {
        children[child_count++] = left->as.branch.children[index];
    }
    for (size_t index = 0; index != boundary.count; ++index) {
        children[child_count++] = boundary.values[index];
    }
    for (size_t index = 1; index != right->as.branch.child_count; ++index) {
        children[child_count++] = right->as.branch.children[index];
    }
    status = ft_rrb_partition(policy, children, child_count, result);
    ft_rrb_nodes_release(policy, &boundary);
    return status;
}

static ft_rrb_node* ft_rrb_normalize_root(const ft_rrb_policy* policy, ft_rrb_node* root)
{
    while (root != NULL && root->kind == FT_RRB_BRANCH && root->as.branch.child_count == 1) {
        ft_rrb_node* child = ft_rrb_node_retain(root->as.branch.children[0]);
        ft_rrb_node_release(policy, root);
        root = child;
    }
    return root;
}

static ft_status ft_rrb_build_same_height(
    const ft_rrb_policy* policy,
    ft_rrb_node* const* nodes,
    size_t count,
    ft_rrb_node** result)
{
    if (count == 0) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    return ft_rrb_branch_create(policy, nodes, count, result);
}

static ft_status ft_rrb_split_node(
    const ft_rrb_policy* policy,
    ft_rrb_node* node,
    size_t index,
    ft_rrb_node_split* result)
{
    result->left = NULL;
    result->right = NULL;
    if (index == 0) {
        result->right = ft_rrb_node_retain(node);
        return FT_STATUS_OK;
    }
    if (index == node->count) {
        result->left = ft_rrb_node_retain(node);
        return FT_STATUS_OK;
    }
    if (node->kind == FT_RRB_LEAF) {
        ft_status status = ft_rrb_leaf_copy_range(
            policy,
            node->as.leaf.values,
            index,
            &result->left);
        if (status != FT_STATUS_OK) {
            return status;
        }
        status = ft_rrb_leaf_copy_range(
            policy,
            node->as.leaf.values + index * policy->value.size,
            node->count - index,
            &result->right);
        if (status != FT_STATUS_OK) {
            ft_rrb_node_release(policy, result->left);
            result->left = NULL;
        }
        return status;
    }

    size_t child_index = 0;
    size_t before = 0;
    ft_status status = ft_rrb_find_child(node, index, &child_index, &before);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_rrb_node_split child;
    status = ft_rrb_split_node(
        policy,
        node->as.branch.children[child_index],
        index - before,
        &child);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rrb_node* left_nodes[FT_RRB_BRANCH_FACTOR];
    size_t left_count = 0;
    for (size_t position = 0; position != child_index; ++position) {
        left_nodes[left_count++] = node->as.branch.children[position];
    }
    if (child.left != NULL) {
        left_nodes[left_count++] = child.left;
    }

    ft_rrb_node* right_nodes[FT_RRB_BRANCH_FACTOR];
    size_t right_count = 0;
    if (child.right != NULL) {
        right_nodes[right_count++] = child.right;
    }
    for (size_t position = child_index + 1; position != node->as.branch.child_count; ++position) {
        right_nodes[right_count++] = node->as.branch.children[position];
    }

    status = ft_rrb_build_same_height(policy, left_nodes, left_count, &result->left);
    if (status == FT_STATUS_OK) {
        status = ft_rrb_build_same_height(policy, right_nodes, right_count, &result->right);
    }
    ft_rrb_node_release(policy, child.left);
    ft_rrb_node_release(policy, child.right);
    if (status != FT_STATUS_OK) {
        ft_rrb_node_release(policy, result->left);
        ft_rrb_node_release(policy, result->right);
        result->left = NULL;
        result->right = NULL;
    }
    return status;
}

static ft_status ft_rrb_build_level(
    const ft_rrb_policy* policy,
    ft_rrb_node* const* nodes,
    size_t node_count,
    ft_rrb_node** result)
{
    if (node_count == 0) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    if (node_count == 1) {
        *result = ft_rrb_node_retain(nodes[0]);
        return FT_STATUS_OK;
    }

    ft_rrb_node** current = (ft_rrb_node**)nodes;
    size_t current_count = node_count;
    bool current_owned = false;
    while (current_count > 1) {
        const size_t parent_count = current_count / FT_RRB_BRANCH_FACTOR +
            (current_count % FT_RRB_BRANCH_FACTOR == 0 ? 0 : 1);
        size_t bytes = 0;
        if (ft_rrb_multiply_overflow(parent_count, sizeof(ft_rrb_node*), &bytes)) {
            if (current_owned) {
                for (size_t index = 0; index != current_count; ++index) {
                    ft_rrb_node_release(policy, current[index]);
                }
                ft_rrb_deallocate(policy, current);
            }
            return FT_STATUS_OVERFLOW;
        }
        ft_rrb_node** parents = (ft_rrb_node**)ft_rrb_allocate(policy, bytes);
        if (parents == NULL) {
            if (current_owned) {
                for (size_t index = 0; index != current_count; ++index) {
                    ft_rrb_node_release(policy, current[index]);
                }
                ft_rrb_deallocate(policy, current);
            }
            return FT_STATUS_NO_MEMORY;
        }

        size_t created = 0;
        ft_status status = FT_STATUS_OK;
        for (size_t start = 0; start < current_count; start += FT_RRB_BRANCH_FACTOR) {
            const size_t remaining = current_count - start;
            const size_t group_count = remaining < FT_RRB_BRANCH_FACTOR ? remaining : FT_RRB_BRANCH_FACTOR;
            status = ft_rrb_branch_create(policy, current + start, group_count, &parents[created]);
            if (status != FT_STATUS_OK) {
                break;
            }
            ++created;
        }
        if (current_owned) {
            for (size_t index = 0; index != current_count; ++index) {
                ft_rrb_node_release(policy, current[index]);
            }
            ft_rrb_deallocate(policy, current);
        }
        if (status != FT_STATUS_OK) {
            for (size_t index = 0; index != created; ++index) {
                ft_rrb_node_release(policy, parents[index]);
            }
            ft_rrb_deallocate(policy, parents);
            return status;
        }
        current = parents;
        current_count = parent_count;
        current_owned = true;
    }

    *result = current[0];
    ft_rrb_deallocate(policy, current);
    return FT_STATUS_OK;
}

static void ft_rrb_publish_unary(
    const ft_rrb_vector* source,
    ft_rrb_vector* result,
    ft_rrb_node* root)
{
    ft_rrb_node* old = result == source ? result->root : NULL;
    result->policy = source->policy;
    result->root = root;
    if (old != NULL) {
        ft_rrb_node_release(source->policy, old);
    }
}

static void ft_rrb_publish_binary(
    const ft_rrb_vector* left,
    const ft_rrb_vector* right,
    ft_rrb_vector* result,
    ft_rrb_node* root)
{
    ft_rrb_node* old = result == left || result == right ? result->root : NULL;
    result->policy = left->policy;
    result->root = root;
    if (old != NULL) {
        ft_rrb_node_release(left->policy, old);
    }
}

void ft_rrb_policy_init(
    ft_rrb_policy* policy,
    const ft_value_type* value_type,
    ft_rrb_equal_fn equal,
    void* equal_context)
{
    if (policy == NULL) {
        return;
    }
    (void)memset(policy, 0, sizeof(*policy));
    if (value_type != NULL) {
        policy->value = *value_type;
    }
    policy->equal = equal;
    policy->equal_context = equal_context;
    policy->allocator.allocate = ft_rrb_default_allocate;
    policy->allocator.deallocate = ft_rrb_default_deallocate;
    policy->allocator.context = NULL;
}

ft_status ft_rrb_vector_init(ft_rrb_vector* vector, const ft_rrb_policy* policy)
{
    if (vector == NULL || !ft_rrb_policy_valid(policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    vector->policy = policy;
    vector->root = NULL;
    return FT_STATUS_OK;
}

ft_status ft_rrb_vector_from_array(
    ft_rrb_vector* vector,
    const ft_rrb_policy* policy,
    const void* values,
    size_t count)
{
    if (vector == NULL || !ft_rrb_policy_valid(policy) || (count != 0 && values == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        vector->policy = policy;
        vector->root = NULL;
        return FT_STATUS_OK;
    }
    size_t value_bytes = 0;
    if (ft_rrb_multiply_overflow(count, policy->value.size, &value_bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    (void)value_bytes;

    const size_t leaf_count = count / FT_RRB_BRANCH_FACTOR +
        (count % FT_RRB_BRANCH_FACTOR == 0 ? 0 : 1);
    size_t bytes = 0;
    if (ft_rrb_multiply_overflow(leaf_count, sizeof(ft_rrb_node*), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    ft_rrb_node** leaves = (ft_rrb_node**)ft_rrb_allocate(policy, bytes);
    if (leaves == NULL) {
        return FT_STATUS_NO_MEMORY;
    }

    size_t created = 0;
    ft_status status = FT_STATUS_OK;
    size_t offset = 0;
    while (offset < count) {
        const size_t remaining = count - offset;
        const size_t length = remaining < FT_RRB_BRANCH_FACTOR ? remaining : FT_RRB_BRANCH_FACTOR;
        status = ft_rrb_leaf_copy_range(
            policy,
            (const unsigned char*)values + offset * policy->value.size,
            length,
            &leaves[created]);
        if (status != FT_STATUS_OK) {
            break;
        }
        ++created;
        offset += length;
    }

    ft_rrb_node* root = NULL;
    if (status == FT_STATUS_OK) {
        status = ft_rrb_build_level(policy, leaves, created, &root);
    }
    for (size_t index = 0; index != created; ++index) {
        ft_rrb_node_release(policy, leaves[index]);
    }
    ft_rrb_deallocate(policy, leaves);
    if (status != FT_STATUS_OK) {
        return status;
    }
    vector->policy = policy;
    vector->root = root;
    return FT_STATUS_OK;
}

ft_status ft_rrb_vector_copy(const ft_rrb_vector* source, ft_rrb_vector* destination)
{
    if (source == NULL || destination == NULL || !ft_rrb_policy_valid(source->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    destination->policy = source->policy;
    destination->root = ft_rrb_node_retain(source->root);
    return FT_STATUS_OK;
}

void ft_rrb_vector_move(ft_rrb_vector* destination, ft_rrb_vector* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    source->policy = NULL;
    source->root = NULL;
}

void ft_rrb_vector_dispose(ft_rrb_vector* vector)
{
    if (vector == NULL) {
        return;
    }
    if (ft_rrb_policy_valid(vector->policy)) {
        ft_rrb_node_release(vector->policy, vector->root);
    }
    vector->policy = NULL;
    vector->root = NULL;
}

bool ft_rrb_vector_empty(const ft_rrb_vector* vector)
{
    return vector != NULL && ft_rrb_policy_valid(vector->policy) && vector->root == NULL;
}

size_t ft_rrb_vector_size(const ft_rrb_vector* vector)
{
    return vector != NULL && ft_rrb_policy_valid(vector->policy) && vector->root != NULL
        ? vector->root->count
        : 0;
}

unsigned ft_rrb_vector_height(const ft_rrb_vector* vector)
{
    return vector != NULL && ft_rrb_policy_valid(vector->policy) && vector->root != NULL
        ? vector->root->height
        : 0;
}

ft_status ft_rrb_vector_at(const ft_rrb_vector* vector, size_t index, void* destination)
{
    if (vector == NULL || destination == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->root == NULL || index >= vector->root->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    const void* value = ft_rrb_node_at(vector->policy, vector->root, index);
    if (value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_rrb_value_copy(vector->policy, destination, value);
    return FT_STATUS_OK;
}

ft_status ft_rrb_vector_front(const ft_rrb_vector* vector, void* destination)
{
    if (vector == NULL || destination == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return vector->root == NULL ? FT_STATUS_EMPTY : ft_rrb_vector_at(vector, 0, destination);
}

ft_status ft_rrb_vector_back(const ft_rrb_vector* vector, void* destination)
{
    if (vector == NULL || destination == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return vector->root == NULL
        ? FT_STATUS_EMPTY
        : ft_rrb_vector_at(vector, vector->root->count - 1, destination);
}

ft_status ft_rrb_vector_set(
    const ft_rrb_vector* vector,
    size_t index,
    const void* value,
    ft_rrb_vector* result)
{
    if (vector == NULL || result == NULL || value == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->root == NULL || index >= vector->root->count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    ft_rrb_node* root = NULL;
    ft_status status = ft_rrb_node_set(vector->policy, vector->root, index, value, &root);
    if (status == FT_STATUS_OK) {
        ft_rrb_publish_unary(vector, result, root);
    }
    return status;
}

ft_status ft_rrb_vector_concat(
    const ft_rrb_vector* left,
    const ft_rrb_vector* right,
    ft_rrb_vector* result)
{
    if (left == NULL || right == NULL || result == NULL ||
        !ft_rrb_policy_valid(left->policy) || left->policy != right->policy) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    size_t combined_count = 0;
    if (ft_rrb_add_overflow(ft_rrb_vector_size(left), ft_rrb_vector_size(right), &combined_count)) {
        return FT_STATUS_OVERFLOW;
    }
    (void)combined_count;

    if (left->root == NULL || right->root == NULL) {
        ft_rrb_node* root = ft_rrb_node_retain(left->root == NULL ? right->root : left->root);
        ft_rrb_publish_binary(left, right, result, root);
        return FT_STATUS_OK;
    }

    ft_rrb_nodes roots;
    ft_status status = ft_rrb_concat_nodes(left->policy, left->root, right->root, &roots);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_rrb_node* root = NULL;
    if (roots.count == 1) {
        root = roots.values[0];
        roots.values[0] = NULL;
        roots.count = 0;
    } else {
        status = ft_rrb_branch_create(left->policy, roots.values, roots.count, &root);
        ft_rrb_nodes_release(left->policy, &roots);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    root = ft_rrb_normalize_root(left->policy, root);
    ft_rrb_publish_binary(left, right, result, root);
    return FT_STATUS_OK;
}

ft_status ft_rrb_vector_split_at(
    const ft_rrb_vector* vector,
    size_t index,
    ft_rrb_split_result* result)
{
    if (vector == NULL || result == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t count = ft_rrb_vector_size(vector);
    if (index > count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    ft_rrb_node_split split;
    if (vector->root == NULL) {
        split.left = NULL;
        split.right = NULL;
    } else {
        ft_status status = ft_rrb_split_node(vector->policy, vector->root, index, &split);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    split.left = ft_rrb_normalize_root(vector->policy, split.left);
    split.right = ft_rrb_normalize_root(vector->policy, split.right);
    result->left.policy = vector->policy;
    result->left.root = split.left;
    result->right.policy = vector->policy;
    result->right.root = split.right;
    return FT_STATUS_OK;
}

static ft_status ft_rrb_vector_singleton(
    const ft_rrb_policy* policy,
    const void* value,
    ft_rrb_vector* vector)
{
    ft_rrb_node* root = NULL;
    ft_status status = ft_rrb_leaf_copy_range(policy, value, 1, &root);
    if (status == FT_STATUS_OK) {
        vector->policy = policy;
        vector->root = root;
    }
    return status;
}

ft_status ft_rrb_vector_push_front(
    const ft_rrb_vector* vector,
    const void* value,
    ft_rrb_vector* result)
{
    if (vector == NULL || value == NULL || result == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_rrb_vector singleton;
    ft_status status = ft_rrb_vector_singleton(vector->policy, value, &singleton);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_concat(&singleton, vector, result);
    ft_rrb_vector_dispose(&singleton);
    return status;
}

ft_status ft_rrb_vector_push_back(
    const ft_rrb_vector* vector,
    const void* value,
    ft_rrb_vector* result)
{
    if (vector == NULL || value == NULL || result == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_rrb_vector singleton;
    ft_status status = ft_rrb_vector_singleton(vector->policy, value, &singleton);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_rrb_vector_concat(vector, &singleton, result);
    ft_rrb_vector_dispose(&singleton);
    return status;
}

ft_status ft_rrb_vector_insert_range(
    const ft_rrb_vector* vector,
    size_t index,
    const void* values,
    size_t count,
    ft_rrb_vector* result)
{
    if (vector == NULL || result == NULL || !ft_rrb_policy_valid(vector->policy) ||
        (count != 0 && values == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t vector_count = ft_rrb_vector_size(vector);
    if (index > vector_count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    size_t combined_count = 0;
    if (ft_rrb_add_overflow(vector_count, count, &combined_count)) {
        return FT_STATUS_OVERFLOW;
    }
    (void)combined_count;
    if (count == 0) {
        ft_rrb_node* root = ft_rrb_node_retain(vector->root);
        ft_rrb_publish_unary(vector, result, root);
        return FT_STATUS_OK;
    }

    ft_rrb_vector middle;
    ft_status status = ft_rrb_vector_from_array(&middle, vector->policy, values, count);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_rrb_split_result split;
    status = ft_rrb_vector_split_at(vector, index, &split);
    if (status != FT_STATUS_OK) {
        ft_rrb_vector_dispose(&middle);
        return status;
    }

    ft_rrb_vector with_middle;
    status = ft_rrb_vector_concat(&split.left, &middle, &with_middle);
    if (status == FT_STATUS_OK) {
        ft_rrb_vector combined = { NULL, NULL };
        status = ft_rrb_vector_concat(&with_middle, &split.right, &combined);
        if (status == FT_STATUS_OK) {
            ft_rrb_node* root = combined.root;
            combined.root = NULL;
            ft_rrb_publish_unary(vector, result, root);
        }
        ft_rrb_vector_dispose(&combined);
        ft_rrb_vector_dispose(&with_middle);
    }
    ft_rrb_vector_dispose(&split.left);
    ft_rrb_vector_dispose(&split.right);
    ft_rrb_vector_dispose(&middle);
    return status;
}

ft_status ft_rrb_vector_remove_range(
    const ft_rrb_vector* vector,
    size_t index,
    size_t count,
    ft_rrb_vector* result)
{
    if (vector == NULL || result == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t vector_count = ft_rrb_vector_size(vector);
    if (count > vector_count || index > vector_count - count) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == 0) {
        ft_rrb_node* root = ft_rrb_node_retain(vector->root);
        ft_rrb_publish_unary(vector, result, root);
        return FT_STATUS_OK;
    }

    ft_rrb_split_result first;
    ft_status status = ft_rrb_vector_split_at(vector, index, &first);
    if (status != FT_STATUS_OK) {
        return status;
    }
    ft_rrb_split_result second;
    status = ft_rrb_vector_split_at(&first.right, count, &second);
    if (status == FT_STATUS_OK) {
        ft_rrb_vector combined = { NULL, NULL };
        status = ft_rrb_vector_concat(&first.left, &second.right, &combined);
        if (status == FT_STATUS_OK) {
            ft_rrb_node* root = combined.root;
            combined.root = NULL;
            ft_rrb_publish_unary(vector, result, root);
        }
        ft_rrb_vector_dispose(&combined);
        ft_rrb_vector_dispose(&second.left);
        ft_rrb_vector_dispose(&second.right);
    }
    ft_rrb_vector_dispose(&first.left);
    ft_rrb_vector_dispose(&first.right);
    return status;
}

static ft_status ft_rrb_vector_pop_at(
    const ft_rrb_vector* vector,
    size_t index,
    void* value,
    ft_rrb_vector* rest)
{
    size_t temporary_size = vector->policy->value.size;
    void* temporary = ft_rrb_allocate(vector->policy, temporary_size);
    if (temporary == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    const void* stored = ft_rrb_node_at(vector->policy, vector->root, index);
    ft_rrb_value_copy(vector->policy, temporary, stored);
    ft_status status = ft_rrb_vector_remove_range(vector, index, 1, rest);
    if (status == FT_STATUS_OK) {
        ft_rrb_value_copy(vector->policy, value, temporary);
    }
    ft_rrb_value_destroy(vector->policy, temporary);
    ft_rrb_deallocate(vector->policy, temporary);
    return status;
}

ft_status ft_rrb_vector_pop_front(
    const ft_rrb_vector* vector,
    void* value,
    ft_rrb_vector* rest)
{
    if (vector == NULL || value == NULL || rest == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->root == NULL) {
        return FT_STATUS_EMPTY;
    }
    return ft_rrb_vector_pop_at(vector, 0, value, rest);
}

ft_status ft_rrb_vector_pop_back(
    const ft_rrb_vector* vector,
    void* value,
    ft_rrb_vector* rest)
{
    if (vector == NULL || value == NULL || rest == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->root == NULL) {
        return FT_STATUS_EMPTY;
    }
    return ft_rrb_vector_pop_at(vector, vector->root->count - 1, value, rest);
}

static void ft_rrb_visit_node(
    const ft_rrb_policy* policy,
    const ft_rrb_node* node,
    ft_visit_fn visitor,
    void* context)
{
    if (node->kind == FT_RRB_LEAF) {
        for (size_t index = 0; index != node->count; ++index) {
            visitor(ft_rrb_leaf_value(policy, node, index), context);
        }
        return;
    }
    for (size_t index = 0; index != node->as.branch.child_count; ++index) {
        ft_rrb_visit_node(policy, node->as.branch.children[index], visitor, context);
    }
}

ft_status ft_rrb_vector_visit(const ft_rrb_vector* vector, ft_visit_fn visitor, void* context)
{
    if (vector == NULL || visitor == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->root != NULL) {
        ft_rrb_visit_node(vector->policy, vector->root, visitor, context);
    }
    return FT_STATUS_OK;
}

static void ft_rrb_visit_leaf_nodes(
    const ft_rrb_node* node,
    ft_rrb_leaf_visit_fn visitor,
    void* context)
{
    if (node->kind == FT_RRB_LEAF) {
        visitor(node, node->count, context);
        return;
    }
    for (size_t index = 0; index != node->as.branch.child_count; ++index) {
        ft_rrb_visit_leaf_nodes(node->as.branch.children[index], visitor, context);
    }
}

ft_status ft_rrb_vector_visit_leaves(
    const ft_rrb_vector* vector,
    ft_rrb_leaf_visit_fn visitor,
    void* context)
{
    if (vector == NULL || visitor == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (vector->root != NULL) {
        ft_rrb_visit_leaf_nodes(vector->root, visitor, context);
    }
    return FT_STATUS_OK;
}

const void* ft_rrb_vector_root_identity(const ft_rrb_vector* vector)
{
    return vector != NULL && ft_rrb_policy_valid(vector->policy) ? vector->root : NULL;
}

bool ft_rrb_vector_shares_root(const ft_rrb_vector* left, const ft_rrb_vector* right)
{
    return left != NULL && right != NULL &&
        ft_rrb_policy_valid(left->policy) &&
        left->policy == right->policy &&
        left->root == right->root;
}

typedef struct ft_rrb_validation_accumulator {
    ft_rrb_statistics statistics;
    bool valid;
} ft_rrb_validation_accumulator;

static void ft_rrb_validate_node(
    const ft_rrb_policy* policy,
    const ft_rrb_node* node,
    bool is_root,
    ft_rrb_validation_accumulator* accumulator,
    size_t* count,
    unsigned* height)
{
    (void)policy;
    if (node->kind == FT_RRB_LEAF) {
        ++accumulator->statistics.leaf_count;
        if (node->count < accumulator->statistics.minimum_leaf_length) {
            accumulator->statistics.minimum_leaf_length = node->count;
        }
        if (node->count > accumulator->statistics.maximum_leaf_length) {
            accumulator->statistics.maximum_leaf_length = node->count;
        }
        accumulator->valid = accumulator->valid &&
            node->count > 0 && node->count <= FT_RRB_BRANCH_FACTOR &&
            node->height == 0 && node->as.leaf.values != NULL;
        *count = node->count;
        *height = 0;
        return;
    }

    ++accumulator->statistics.branch_count;
    const size_t child_count = node->as.branch.child_count;
    if (child_count < accumulator->statistics.minimum_branching_factor) {
        accumulator->statistics.minimum_branching_factor = child_count;
    }
    if (child_count > accumulator->statistics.maximum_branching_factor) {
        accumulator->statistics.maximum_branching_factor = child_count;
    }
    if (node->as.branch.sizes == NULL) {
        ++accumulator->statistics.regular_branch_count;
    } else {
        ++accumulator->statistics.relaxed_branch_count;
    }
    if (child_count == 0 || child_count > FT_RRB_BRANCH_FACTOR ||
        (is_root && child_count == 1) || node->as.branch.children == NULL) {
        accumulator->valid = false;
        *count = node->count;
        *height = node->height;
        return;
    }

    size_t total = 0;
    unsigned child_height = 0;
    for (size_t index = 0; index != child_count; ++index) {
        size_t actual_count = 0;
        unsigned actual_height = 0;
        ft_rrb_validate_node(
            policy,
            node->as.branch.children[index],
            false,
            accumulator,
            &actual_count,
            &actual_height);
        if (index == 0) {
            child_height = actual_height;
        } else if (child_height != actual_height) {
            accumulator->valid = false;
        }
        if (ft_rrb_add_overflow(total, actual_count, &total)) {
            accumulator->valid = false;
        }
    }
    *height = child_height + 1;
    *count = total;
    accumulator->valid = accumulator->valid &&
        node->height == *height && node->height <= FT_RRB_MAX_HEIGHT && node->count == total;

    const bool regular = ft_rrb_regular_layout(node->as.branch.children, child_count, *height);
    if (regular != (node->as.branch.sizes == NULL)) {
        accumulator->valid = false;
    }
    if (!regular) {
        if (node->as.branch.sizes == NULL) {
            accumulator->valid = false;
        } else {
            size_t cumulative = 0;
            for (size_t index = 0; index != child_count; ++index) {
                if (ft_rrb_add_overflow(cumulative, node->as.branch.children[index]->count, &cumulative) ||
                    node->as.branch.sizes[index] != cumulative) {
                    accumulator->valid = false;
                }
            }
        }
    }
}

bool ft_rrb_vector_validate(const ft_rrb_vector* vector, ft_rrb_statistics* statistics)
{
    if (vector == NULL || statistics == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return false;
    }
    (void)memset(statistics, 0, sizeof(*statistics));
    if (vector->root == NULL) {
        return true;
    }

    ft_rrb_validation_accumulator accumulator;
    (void)memset(&accumulator, 0, sizeof(accumulator));
    accumulator.valid = true;
    accumulator.statistics.minimum_leaf_length = SIZE_MAX;
    accumulator.statistics.minimum_branching_factor = SIZE_MAX;
    size_t count = 0;
    unsigned height = 0;
    ft_rrb_validate_node(vector->policy, vector->root, true, &accumulator, &count, &height);
    accumulator.statistics.count = count;
    accumulator.statistics.height = height;
    if (accumulator.statistics.leaf_count == 0) {
        accumulator.statistics.minimum_leaf_length = 0;
    }
    if (accumulator.statistics.branch_count == 0) {
        accumulator.statistics.minimum_branching_factor = 0;
    }
    *statistics = accumulator.statistics;
    return accumulator.valid && count == vector->root->count && height == vector->root->height;
}

static void ft_rrb_builder_clear_staging(
    const ft_rrb_policy* policy,
    ft_rrb_builder_rep* rep)
{
    for (size_t index = 0; index != rep->leaf_count; ++index) {
        ft_rrb_node_release(policy, rep->leaves[index]);
    }
    ft_rrb_deallocate(policy, rep->leaves);
    rep->leaves = NULL;
    rep->leaf_count = 0;
    rep->leaf_capacity = 0;

    for (size_t index = 0; index != rep->tail_count; ++index) {
        ft_rrb_value_destroy(policy, rep->tail + index * policy->value.size);
    }
    ft_rrb_deallocate(policy, rep->tail);
    rep->tail = NULL;
    rep->tail_count = 0;
    rep->staged_count = 0;
}

static void ft_rrb_builder_rep_cleanup(
    const ft_rrb_policy* policy,
    ft_rrb_builder_rep* rep)
{
    ft_rrb_node_release(policy, rep->prefix);
    rep->prefix = NULL;
    ft_rrb_builder_clear_staging(policy, rep);
}

static ft_status ft_rrb_builder_ensure_leaf_capacity(
    const ft_rrb_policy* policy,
    ft_rrb_builder_rep* rep,
    size_t required)
{
    if (required <= rep->leaf_capacity) {
        return FT_STATUS_OK;
    }
    size_t capacity = rep->leaf_capacity == 0 ? 4 : rep->leaf_capacity;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    size_t bytes = 0;
    if (ft_rrb_multiply_overflow(capacity, sizeof(ft_rrb_node*), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    ft_rrb_node** leaves = (ft_rrb_node**)ft_rrb_allocate(policy, bytes);
    if (leaves == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    if (rep->leaf_count != 0) {
        (void)memcpy(leaves, rep->leaves, rep->leaf_count * sizeof(ft_rrb_node*));
    }
    ft_rrb_deallocate(policy, rep->leaves);
    rep->leaves = leaves;
    rep->leaf_capacity = capacity;
    return FT_STATUS_OK;
}

static ft_status ft_rrb_builder_ensure_tail(
    const ft_rrb_policy* policy,
    ft_rrb_builder_rep* rep)
{
    if (rep->tail != NULL) {
        return FT_STATUS_OK;
    }
    size_t bytes = 0;
    if (ft_rrb_multiply_overflow(FT_RRB_BRANCH_FACTOR, policy->value.size, &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    rep->tail = (unsigned char*)ft_rrb_allocate(policy, bytes);
    return rep->tail == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

static ft_status ft_rrb_builder_append_core(
    const ft_rrb_policy* policy,
    ft_rrb_builder_rep* rep,
    const void* value)
{
    size_t current_count = rep->prefix == NULL ? 0 : rep->prefix->count;
    if (ft_rrb_add_overflow(current_count, rep->staged_count, &current_count) ||
        current_count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ft_status status = ft_rrb_builder_ensure_tail(policy, rep);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (rep->tail_count + 1 == FT_RRB_BRANCH_FACTOR) {
        status = ft_rrb_builder_ensure_leaf_capacity(policy, rep, rep->leaf_count + 1);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }

    void* destination = rep->tail + rep->tail_count * policy->value.size;
    ft_rrb_value_copy(policy, destination, value);
    ++rep->tail_count;
    ++rep->staged_count;
    if (rep->tail_count == FT_RRB_BRANCH_FACTOR) {
        ft_rrb_node* leaf = NULL;
        status = ft_rrb_leaf_adopt(policy, rep->tail, FT_RRB_BRANCH_FACTOR, &leaf);
        if (status != FT_STATUS_OK) {
            --rep->tail_count;
            --rep->staged_count;
            ft_rrb_value_destroy(policy, destination);
            return status;
        }
        rep->leaves[rep->leaf_count++] = leaf;
        rep->tail = NULL;
        rep->tail_count = 0;
    }
    return FT_STATUS_OK;
}

static ft_status ft_rrb_builder_rep_clone(
    const ft_rrb_policy* policy,
    const ft_rrb_builder_rep* source,
    ft_rrb_builder_rep* destination)
{
    (void)memset(destination, 0, sizeof(*destination));
    destination->prefix = ft_rrb_node_retain(source->prefix);
    destination->staged_count = source->staged_count;
    if (source->leaf_count != 0) {
        size_t bytes = 0;
        if (ft_rrb_multiply_overflow(source->leaf_count, sizeof(ft_rrb_node*), &bytes)) {
            ft_rrb_builder_rep_cleanup(policy, destination);
            return FT_STATUS_OVERFLOW;
        }
        destination->leaves = (ft_rrb_node**)ft_rrb_allocate(policy, bytes);
        if (destination->leaves == NULL) {
            ft_rrb_builder_rep_cleanup(policy, destination);
            return FT_STATUS_NO_MEMORY;
        }
        destination->leaf_capacity = source->leaf_count;
        for (size_t index = 0; index != source->leaf_count; ++index) {
            destination->leaves[index] = ft_rrb_node_retain(source->leaves[index]);
            ++destination->leaf_count;
        }
    }
    if (source->tail_count != 0) {
        ft_status status = ft_rrb_builder_ensure_tail(policy, destination);
        if (status != FT_STATUS_OK) {
            ft_rrb_builder_rep_cleanup(policy, destination);
            return status;
        }
        for (size_t index = 0; index != source->tail_count; ++index) {
            ft_rrb_value_copy(
                policy,
                destination->tail + index * policy->value.size,
                source->tail + index * policy->value.size);
            ++destination->tail_count;
        }
    }
    return FT_STATUS_OK;
}

ft_status ft_rrb_builder_init(ft_rrb_builder* builder, const ft_rrb_policy* policy)
{
    if (builder == NULL || !ft_rrb_policy_valid(policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_rrb_builder_rep* rep = (ft_rrb_builder_rep*)ft_rrb_allocate(policy, sizeof(*rep));
    if (rep == NULL) {
        builder->policy = NULL;
        builder->rep = NULL;
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(rep, 0, sizeof(*rep));
    builder->policy = policy;
    builder->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_rrb_builder_init_from_vector(ft_rrb_builder* builder, const ft_rrb_vector* vector)
{
    if (builder == NULL || vector == NULL || !ft_rrb_policy_valid(vector->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_status status = ft_rrb_builder_init(builder, vector->policy);
    if (status == FT_STATUS_OK) {
        builder->rep->prefix = ft_rrb_node_retain(vector->root);
    }
    return status;
}

void ft_rrb_builder_dispose(ft_rrb_builder* builder)
{
    if (builder == NULL) {
        return;
    }
    if (builder->rep != NULL && ft_rrb_policy_valid(builder->policy)) {
        ft_rrb_builder_rep_cleanup(builder->policy, builder->rep);
        ft_rrb_deallocate(builder->policy, builder->rep);
    }
    builder->policy = NULL;
    builder->rep = NULL;
}

size_t ft_rrb_builder_size(const ft_rrb_builder* builder)
{
    if (builder == NULL || builder->rep == NULL || !ft_rrb_policy_valid(builder->policy)) {
        return 0;
    }
    const size_t prefix_count = builder->rep->prefix == NULL ? 0 : builder->rep->prefix->count;
    size_t result = 0;
    return ft_rrb_add_overflow(prefix_count, builder->rep->staged_count, &result) ? 0 : result;
}

ft_status ft_rrb_builder_append(ft_rrb_builder* builder, const void* value)
{
    if (builder == NULL || builder->rep == NULL || value == NULL ||
        !ft_rrb_policy_valid(builder->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_rrb_builder_append_core(builder->policy, builder->rep, value);
}

ft_status ft_rrb_builder_append_range(ft_rrb_builder* builder, const void* values, size_t count)
{
    if (builder == NULL || builder->rep == NULL || !ft_rrb_policy_valid(builder->policy) ||
        (count != 0 && values == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count == 0) {
        return FT_STATUS_OK;
    }
    size_t byte_count = 0;
    if (ft_rrb_multiply_overflow(count, builder->policy->value.size, &byte_count)) {
        return FT_STATUS_OVERFLOW;
    }
    (void)byte_count;

    ft_rrb_builder_rep temporary;
    ft_status status = ft_rrb_builder_rep_clone(builder->policy, builder->rep, &temporary);
    if (status != FT_STATUS_OK) {
        return status;
    }
    for (size_t index = 0; index != count; ++index) {
        status = ft_rrb_builder_append_core(
            builder->policy,
            &temporary,
            (const unsigned char*)values + index * builder->policy->value.size);
        if (status != FT_STATUS_OK) {
            ft_rrb_builder_rep_cleanup(builder->policy, &temporary);
            return status;
        }
    }
    ft_rrb_builder_rep_cleanup(builder->policy, builder->rep);
    *builder->rep = temporary;
    return FT_STATUS_OK;
}

ft_status ft_rrb_builder_to_vector(ft_rrb_builder* builder, ft_rrb_vector* result)
{
    if (builder == NULL || builder->rep == NULL || result == NULL ||
        !ft_rrb_policy_valid(builder->policy)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_rrb_builder_rep* rep = builder->rep;
    if (rep->staged_count == 0) {
        result->policy = builder->policy;
        result->root = ft_rrb_node_retain(rep->prefix);
        return FT_STATUS_OK;
    }

    const size_t node_count = rep->leaf_count + (rep->tail_count == 0 ? 0 : 1);
    size_t bytes = 0;
    if (ft_rrb_multiply_overflow(node_count, sizeof(ft_rrb_node*), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    ft_rrb_node** nodes = (ft_rrb_node**)ft_rrb_allocate(builder->policy, bytes);
    if (nodes == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    for (size_t index = 0; index != rep->leaf_count; ++index) {
        nodes[index] = rep->leaves[index];
    }

    ft_rrb_node* partial = NULL;
    ft_status status = FT_STATUS_OK;
    if (rep->tail_count != 0) {
        status = ft_rrb_leaf_copy_range(
            builder->policy,
            rep->tail,
            rep->tail_count,
            &partial);
        if (status == FT_STATUS_OK) {
            nodes[rep->leaf_count] = partial;
        }
    }

    ft_rrb_node* staged_root = NULL;
    if (status == FT_STATUS_OK) {
        status = ft_rrb_build_level(builder->policy, nodes, node_count, &staged_root);
    }
    ft_rrb_node_release(builder->policy, partial);
    ft_rrb_deallocate(builder->policy, nodes);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rrb_vector staged = { builder->policy, staged_root };
    ft_rrb_vector combined;
    if (rep->prefix == NULL) {
        combined = staged;
        staged.root = NULL;
    } else {
        const ft_rrb_vector prefix = { builder->policy, rep->prefix };
        status = ft_rrb_vector_concat(&prefix, &staged, &combined);
    }
    ft_rrb_vector_dispose(&staged);
    if (status != FT_STATUS_OK) {
        return status;
    }

    ft_rrb_node_release(builder->policy, rep->prefix);
    rep->prefix = ft_rrb_node_retain(combined.root);
    ft_rrb_builder_clear_staging(builder->policy, rep);
    result->policy = builder->policy;
    result->root = combined.root;
    combined.root = NULL;
    ft_rrb_vector_dispose(&combined);
    return FT_STATUS_OK;
}

void ft_rrb_builder_clear(ft_rrb_builder* builder)
{
    if (builder == NULL || builder->rep == NULL || !ft_rrb_policy_valid(builder->policy)) {
        return;
    }
    ft_rrb_builder_rep_cleanup(builder->policy, builder->rep);
}
