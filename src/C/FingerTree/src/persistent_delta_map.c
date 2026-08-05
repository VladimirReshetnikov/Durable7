/*
 * Implementation of the checkpoint-differential persistent ordered map.
 *
 * The ordered core is a weight-balanced (Adams') binary search tree of reference-counted nodes,
 * path-copied on every edit. One node type serves both root kinds: a state node carries a value box
 * and no endpoints, a change-index node carries the before and after endpoint boxes, where a null
 * box is the absent endpoint. Keys and values live in separately reference-counted boxes so a
 * representative can be retained across versions and across the state and change roots without
 * being copied again.
 */

#include <durable7/finger_tree/persistent_delta_map.h>

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
typedef volatile LONG64 ft_delta_map_ref_count;

static void ft_delta_map_ref_init(ft_delta_map_ref_count* value)
{
    *value = 1;
}

static void ft_delta_map_ref_retain(ft_delta_map_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_delta_map_ref_release(ft_delta_map_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_delta_map_ref_count;

static void ft_delta_map_ref_init(ft_delta_map_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_delta_map_ref_retain(ft_delta_map_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_delta_map_ref_release(ft_delta_map_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

/* Adams' weight-balance parameters: a subtree may be at most FT_DELTA_MAP_DELTA times heavier than
 * its sibling, and FT_DELTA_MAP_GAMMA decides single versus double rotation. */
#define FT_DELTA_MAP_DELTA 3u
#define FT_DELTA_MAP_GAMMA 2u

typedef struct ft_delta_map_box {
    ft_delta_map_ref_count refs;
    void* bytes;
} ft_delta_map_box;

struct ft_delta_map_node {
    ft_delta_map_ref_count refs;
    size_t count;
    ft_delta_map_box* key;
    ft_delta_map_box* value;
    ft_delta_map_box* before;
    ft_delta_map_box* after;
    struct ft_delta_map_node* left;
    struct ft_delta_map_node* right;
};

struct ft_delta_map_policy_rep {
    ft_delta_map_ref_count refs;
    ft_delta_map_policy_config config;
};

/* The payload of one node, so a rebalance can rebuild a node without knowing its root kind. */
typedef struct ft_delta_map_payload {
    ft_delta_map_box* key;
    ft_delta_map_box* value;
    ft_delta_map_box* before;
    ft_delta_map_box* after;
} ft_delta_map_payload;

typedef ft_status (*ft_delta_map_node_visit_fn)(
    const ft_delta_map_node* node,
    void* context);

static void* ft_delta_map_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_delta_map_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_delta_map_config_valid(const ft_delta_map_policy_config* config)
{
    return config != NULL && config->key_size != 0 && config->value_size != 0 &&
        config->key_type_identity != NULL && config->value_type_identity != NULL &&
        config->compare_key != NULL && config->value_equal != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL &&
        (config->key_destroy == NULL || config->key_copy != NULL) &&
        (config->value_destroy == NULL || config->value_copy != NULL);
}

static void* ft_delta_map_allocate_config(
    const ft_delta_map_policy_config* config,
    size_t size)
{
    return config->allocator.allocate(size, config->allocator.context);
}

static void ft_delta_map_deallocate_config(
    const ft_delta_map_policy_config* config,
    void* allocation)
{
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void* ft_delta_map_allocate(
    const ft_delta_map_policy_rep* policy,
    size_t size)
{
    return ft_delta_map_allocate_config(&policy->config, size);
}

static void ft_delta_map_deallocate(
    const ft_delta_map_policy_rep* policy,
    void* allocation)
{
    ft_delta_map_deallocate_config(&policy->config, allocation);
}

static void ft_delta_map_policy_retain(ft_delta_map_policy_rep* policy)
{
    if (policy != NULL) {
        ft_delta_map_ref_retain(&policy->refs);
    }
}

static void ft_delta_map_policy_release(ft_delta_map_policy_rep* policy)
{
    if (policy != NULL && ft_delta_map_ref_release(&policy->refs)) {
        ft_delta_map_deallocate_config(&policy->config, policy);
    }
}

static ft_status ft_delta_map_compare(
    const ft_delta_map_policy_rep* policy,
    const void* left,
    const void* right,
    int* comparison)
{
    return policy->config.compare_key(
        left,
        right,
        comparison,
        policy->config.callback_context);
}

static ft_status ft_delta_map_values_equal(
    const ft_delta_map_policy_rep* policy,
    const void* left,
    const void* right,
    bool* equal)
{
    return policy->config.value_equal(
        left,
        right,
        equal,
        policy->config.callback_context);
}

/* Box lifetime. A box owns one constructed key or value; the copy hook builds it and the matching
 * destroy hook tears it down exactly once, when the last node referring to it goes away. */

static void ft_delta_map_box_retain(ft_delta_map_box* box)
{
    if (box != NULL) {
        ft_delta_map_ref_retain(&box->refs);
    }
}

static void ft_delta_map_box_release(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_box* box,
    ft_delta_map_destroy_fn destroy)
{
    if (box == NULL || !ft_delta_map_ref_release(&box->refs)) {
        return;
    }
    if (destroy != NULL) {
        destroy(box->bytes, policy->config.callback_context);
    }
    ft_delta_map_deallocate(policy, box->bytes);
    ft_delta_map_deallocate(policy, box);
}

static void ft_delta_map_key_release(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_box* box)
{
    ft_delta_map_box_release(policy, box, policy->config.key_destroy);
}

static void ft_delta_map_value_release(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_box* box)
{
    ft_delta_map_box_release(policy, box, policy->config.value_destroy);
}

static ft_status ft_delta_map_box_create(
    const ft_delta_map_policy_rep* policy,
    const void* source,
    size_t size,
    ft_delta_map_copy_fn copy,
    ft_delta_map_box** result)
{
    ft_delta_map_box* box = NULL;
    ft_status status = FT_STATUS_OK;
    box = (ft_delta_map_box*)ft_delta_map_allocate(policy, sizeof(*box));
    if (box == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    box->bytes = ft_delta_map_allocate(policy, size);
    if (box->bytes == NULL) {
        ft_delta_map_deallocate(policy, box);
        return FT_STATUS_NO_MEMORY;
    }
    if (copy == NULL) {
        (void)memcpy(box->bytes, source, size);
    } else {
        status = copy(box->bytes, source, policy->config.callback_context);
        if (status != FT_STATUS_OK) {
            ft_delta_map_deallocate(policy, box->bytes);
            ft_delta_map_deallocate(policy, box);
            return status;
        }
    }
    ft_delta_map_ref_init(&box->refs);
    *result = box;
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_key_box_create(
    const ft_delta_map_policy_rep* policy,
    const void* source,
    ft_delta_map_box** result)
{
    return ft_delta_map_box_create(
        policy,
        source,
        policy->config.key_size,
        policy->config.key_copy,
        result);
}

static ft_status ft_delta_map_value_box_create(
    const ft_delta_map_policy_rep* policy,
    const void* source,
    ft_delta_map_box** result)
{
    return ft_delta_map_box_create(
        policy,
        source,
        policy->config.value_size,
        policy->config.value_copy,
        result);
}

/* Node lifetime. Release is recursive over a balanced tree, so its depth is logarithmic in the
 * node count. */

static size_t ft_delta_map_node_size(const ft_delta_map_node* node)
{
    return node == NULL ? 0u : node->count;
}

static void ft_delta_map_node_retain(ft_delta_map_node* node)
{
    if (node != NULL) {
        ft_delta_map_ref_retain(&node->refs);
    }
}

static void ft_delta_map_node_release(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* node)
{
    if (node == NULL || !ft_delta_map_ref_release(&node->refs)) {
        return;
    }
    ft_delta_map_node_release(policy, node->left);
    ft_delta_map_node_release(policy, node->right);
    ft_delta_map_key_release(policy, node->key);
    ft_delta_map_value_release(policy, node->value);
    ft_delta_map_value_release(policy, node->before);
    ft_delta_map_value_release(policy, node->after);
    ft_delta_map_deallocate(policy, node);
}

static ft_delta_map_payload ft_delta_map_payload_of(const ft_delta_map_node* node)
{
    ft_delta_map_payload payload;
    payload.key = node->key;
    payload.value = node->value;
    payload.before = node->before;
    payload.after = node->after;
    return payload;
}

static ft_status ft_delta_map_node_create(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_payload* payload,
    ft_delta_map_node* left,
    ft_delta_map_node* right,
    ft_delta_map_node** result)
{
    ft_delta_map_node* node =
        (ft_delta_map_node*)ft_delta_map_allocate(policy, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_delta_map_ref_init(&node->refs);
    node->count = 1u + ft_delta_map_node_size(left) + ft_delta_map_node_size(right);
    node->key = payload->key;
    node->value = payload->value;
    node->before = payload->before;
    node->after = payload->after;
    node->left = left;
    node->right = right;
    ft_delta_map_box_retain(node->key);
    ft_delta_map_box_retain(node->value);
    ft_delta_map_box_retain(node->before);
    ft_delta_map_box_retain(node->after);
    ft_delta_map_node_retain(left);
    ft_delta_map_node_retain(right);
    *result = node;
    return FT_STATUS_OK;
}

/* Rebuilds a node whose children differ from balance by at most one element, rotating when the
 * weight ratio is exceeded. Every intermediate node is released on any failure, so a failed
 * rebalance allocates nothing net and leaves the operands untouched. */
static ft_status ft_delta_map_balance(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_payload* payload,
    ft_delta_map_node* left,
    ft_delta_map_node* right,
    ft_delta_map_node** result)
{
    const size_t left_size = ft_delta_map_node_size(left);
    const size_t right_size = ft_delta_map_node_size(right);
    ft_status status = FT_STATUS_OK;
    if (left_size + right_size > 1u &&
        right_size > (size_t)FT_DELTA_MAP_DELTA * left_size) {
        ft_delta_map_node* inner = right->left;
        ft_delta_map_node* outer = right->right;
        const ft_delta_map_payload pivot = ft_delta_map_payload_of(right);
        if (ft_delta_map_node_size(inner) <
            (size_t)FT_DELTA_MAP_GAMMA * ft_delta_map_node_size(outer)) {
            ft_delta_map_node* rebuilt = NULL;
            status = ft_delta_map_node_create(policy, payload, left, inner, &rebuilt);
            if (status != FT_STATUS_OK) {
                return status;
            }
            status = ft_delta_map_node_create(policy, &pivot, rebuilt, outer, result);
            ft_delta_map_node_release(policy, rebuilt);
            return status;
        } else {
            const ft_delta_map_payload middle = ft_delta_map_payload_of(inner);
            ft_delta_map_node* rebuilt_left = NULL;
            ft_delta_map_node* rebuilt_right = NULL;
            status = ft_delta_map_node_create(
                policy, payload, left, inner->left, &rebuilt_left);
            if (status != FT_STATUS_OK) {
                return status;
            }
            status = ft_delta_map_node_create(
                policy, &pivot, inner->right, outer, &rebuilt_right);
            if (status != FT_STATUS_OK) {
                ft_delta_map_node_release(policy, rebuilt_left);
                return status;
            }
            status = ft_delta_map_node_create(
                policy, &middle, rebuilt_left, rebuilt_right, result);
            ft_delta_map_node_release(policy, rebuilt_left);
            ft_delta_map_node_release(policy, rebuilt_right);
            return status;
        }
    }
    if (left_size + right_size > 1u &&
        left_size > (size_t)FT_DELTA_MAP_DELTA * right_size) {
        ft_delta_map_node* inner = left->right;
        ft_delta_map_node* outer = left->left;
        const ft_delta_map_payload pivot = ft_delta_map_payload_of(left);
        if (ft_delta_map_node_size(inner) <
            (size_t)FT_DELTA_MAP_GAMMA * ft_delta_map_node_size(outer)) {
            ft_delta_map_node* rebuilt = NULL;
            status = ft_delta_map_node_create(policy, payload, inner, right, &rebuilt);
            if (status != FT_STATUS_OK) {
                return status;
            }
            status = ft_delta_map_node_create(policy, &pivot, outer, rebuilt, result);
            ft_delta_map_node_release(policy, rebuilt);
            return status;
        } else {
            const ft_delta_map_payload middle = ft_delta_map_payload_of(inner);
            ft_delta_map_node* rebuilt_left = NULL;
            ft_delta_map_node* rebuilt_right = NULL;
            status = ft_delta_map_node_create(
                policy, &pivot, outer, inner->left, &rebuilt_left);
            if (status != FT_STATUS_OK) {
                return status;
            }
            status = ft_delta_map_node_create(
                policy, payload, inner->right, right, &rebuilt_right);
            if (status != FT_STATUS_OK) {
                ft_delta_map_node_release(policy, rebuilt_left);
                return status;
            }
            status = ft_delta_map_node_create(
                policy, &middle, rebuilt_left, rebuilt_right, result);
            ft_delta_map_node_release(policy, rebuilt_left);
            ft_delta_map_node_release(policy, rebuilt_right);
            return status;
        }
    }
    return ft_delta_map_node_create(policy, payload, left, right, result);
}

/* Ordered-core searches. Every one propagates a failing key comparison unchanged. */

static ft_status ft_delta_map_find(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* node,
    const void* key,
    ft_delta_map_node** found)
{
    *found = NULL;
    while (node != NULL) {
        int comparison = 0;
        const ft_status status =
            ft_delta_map_compare(policy, key, node->key->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison == 0) {
            *found = node;
            return FT_STATUS_OK;
        }
        node = comparison < 0 ? node->left : node->right;
    }
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_find_rank(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* node,
    const void* key,
    bool* found,
    size_t* index)
{
    size_t rank = 0;
    *found = false;
    while (node != NULL) {
        int comparison = 0;
        const ft_status status =
            ft_delta_map_compare(policy, key, node->key->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison == 0) {
            *found = true;
            *index = rank + ft_delta_map_node_size(node->left);
            return FT_STATUS_OK;
        }
        if (comparison < 0) {
            node = node->left;
        } else {
            rank += ft_delta_map_node_size(node->left) + 1u;
            node = node->right;
        }
    }
    return FT_STATUS_OK;
}

static const ft_delta_map_node* ft_delta_map_node_at(
    const ft_delta_map_node* node,
    size_t index)
{
    while (node != NULL) {
        const size_t left_size = ft_delta_map_node_size(node->left);
        if (index < left_size) {
            node = node->left;
        } else if (index == left_size) {
            return node;
        } else {
            index -= left_size + 1u;
            node = node->right;
        }
    }
    return NULL;
}

static const ft_delta_map_node* ft_delta_map_extreme(
    const ft_delta_map_node* node,
    bool least)
{
    if (node == NULL) {
        return NULL;
    }
    while (least ? node->left != NULL : node->right != NULL) {
        node = least ? node->left : node->right;
    }
    return node;
}

/* One descent answering floor, ceiling, lower, and higher: below selects the greatest key on the
 * requested side, and strict excludes an exactly equal key. */
static ft_status ft_delta_map_neighbor(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_node* node,
    const void* key,
    bool below,
    bool strict,
    const ft_delta_map_node** found)
{
    *found = NULL;
    while (node != NULL) {
        int comparison = 0;
        const ft_status status =
            ft_delta_map_compare(policy, node->key->bytes, key, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison == 0 && !strict) {
            *found = node;
            return FT_STATUS_OK;
        }
        if (below) {
            if (comparison < 0) {
                *found = node;
                node = node->right;
            } else {
                node = node->left;
            }
        } else {
            if (comparison > 0) {
                *found = node;
                node = node->left;
            } else {
                node = node->right;
            }
        }
    }
    return FT_STATUS_OK;
}

/* Ordered-core edits. */

static ft_status ft_delta_map_node_set(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* node,
    const ft_delta_map_payload* entry,
    ft_delta_map_node** result)
{
    int comparison = 0;
    ft_status status = FT_STATUS_OK;
    ft_delta_map_node* rebuilt = NULL;
    if (node == NULL) {
        return ft_delta_map_node_create(policy, entry, NULL, NULL, result);
    }
    status = ft_delta_map_compare(
        policy, entry->key->bytes, node->key->bytes, &comparison);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (comparison == 0) {
        return ft_delta_map_node_create(policy, entry, node->left, node->right, result);
    }
    if (comparison < 0) {
        status = ft_delta_map_node_set(policy, node->left, entry, &rebuilt);
        if (status != FT_STATUS_OK) {
            return status;
        }
        {
            const ft_delta_map_payload payload = ft_delta_map_payload_of(node);
            status = ft_delta_map_balance(policy, &payload, rebuilt, node->right, result);
        }
    } else {
        status = ft_delta_map_node_set(policy, node->right, entry, &rebuilt);
        if (status != FT_STATUS_OK) {
            return status;
        }
        {
            const ft_delta_map_payload payload = ft_delta_map_payload_of(node);
            status = ft_delta_map_balance(policy, &payload, node->left, rebuilt, result);
        }
    }
    ft_delta_map_node_release(policy, rebuilt);
    return status;
}

static ft_status ft_delta_map_delete_least(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* node,
    ft_delta_map_node** result)
{
    ft_delta_map_node* rebuilt = NULL;
    ft_status status = FT_STATUS_OK;
    if (node->left == NULL) {
        ft_delta_map_node_retain(node->right);
        *result = node->right;
        return FT_STATUS_OK;
    }
    status = ft_delta_map_delete_least(policy, node->left, &rebuilt);
    if (status != FT_STATUS_OK) {
        return status;
    }
    {
        const ft_delta_map_payload payload = ft_delta_map_payload_of(node);
        status = ft_delta_map_balance(policy, &payload, rebuilt, node->right, result);
    }
    ft_delta_map_node_release(policy, rebuilt);
    return status;
}

static ft_status ft_delta_map_glue(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* left,
    ft_delta_map_node* right,
    ft_delta_map_node** result)
{
    const ft_delta_map_node* successor = NULL;
    ft_delta_map_node* rebuilt = NULL;
    ft_status status = FT_STATUS_OK;
    if (left == NULL) {
        ft_delta_map_node_retain(right);
        *result = right;
        return FT_STATUS_OK;
    }
    if (right == NULL) {
        ft_delta_map_node_retain(left);
        *result = left;
        return FT_STATUS_OK;
    }
    successor = ft_delta_map_extreme(right, true);
    status = ft_delta_map_delete_least(policy, right, &rebuilt);
    if (status != FT_STATUS_OK) {
        return status;
    }
    {
        const ft_delta_map_payload payload = ft_delta_map_payload_of(successor);
        status = ft_delta_map_balance(policy, &payload, left, rebuilt, result);
    }
    ft_delta_map_node_release(policy, rebuilt);
    return status;
}

static ft_status ft_delta_map_node_remove(
    const ft_delta_map_policy_rep* policy,
    ft_delta_map_node* node,
    const void* key,
    ft_delta_map_node** result)
{
    int comparison = 0;
    ft_status status = FT_STATUS_OK;
    ft_delta_map_node* rebuilt = NULL;
    if (node == NULL) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    status = ft_delta_map_compare(policy, key, node->key->bytes, &comparison);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (comparison == 0) {
        return ft_delta_map_glue(policy, node->left, node->right, result);
    }
    if (comparison < 0) {
        status = ft_delta_map_node_remove(policy, node->left, key, &rebuilt);
        if (status != FT_STATUS_OK) {
            return status;
        }
        {
            const ft_delta_map_payload payload = ft_delta_map_payload_of(node);
            status = ft_delta_map_balance(policy, &payload, rebuilt, node->right, result);
        }
    } else {
        status = ft_delta_map_node_remove(policy, node->right, key, &rebuilt);
        if (status != FT_STATUS_OK) {
            return status;
        }
        {
            const ft_delta_map_payload payload = ft_delta_map_payload_of(node);
            status = ft_delta_map_balance(policy, &payload, node->left, rebuilt, result);
        }
    }
    ft_delta_map_node_release(policy, rebuilt);
    return status;
}

/* Traversal. */

static ft_status ft_delta_map_visit_nodes(
    const ft_delta_map_node* node,
    ft_delta_map_node_visit_fn visitor,
    void* context)
{
    ft_status status = FT_STATUS_OK;
    if (node == NULL) {
        return FT_STATUS_OK;
    }
    status = ft_delta_map_visit_nodes(node->left, visitor, context);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = visitor(node, context);
    if (status != FT_STATUS_OK) {
        return status;
    }
    return ft_delta_map_visit_nodes(node->right, visitor, context);
}

typedef struct ft_delta_map_range_state {
    const ft_delta_map_policy_rep* policy;
    const void* low;
    const void* high;
    ft_delta_map_node_visit_fn visitor;
    void* context;
} ft_delta_map_range_state;

/* Prunes at every node, so the walk touches only the boundary paths plus the in-range nodes rather
 * than filtering the whole root. */
static ft_status ft_delta_map_visit_range_nodes(
    const ft_delta_map_node* node,
    ft_delta_map_range_state* state)
{
    int from_low = 0;
    int to_high = 0;
    ft_status status = FT_STATUS_OK;
    if (node == NULL) {
        return FT_STATUS_OK;
    }
    status = ft_delta_map_compare(state->policy, state->low, node->key->bytes, &from_low);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_compare(state->policy, node->key->bytes, state->high, &to_high);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (from_low <= 0) {
        status = ft_delta_map_visit_range_nodes(node->left, state);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (to_high <= 0) {
            status = state->visitor(node, state->context);
            if (status != FT_STATUS_OK) {
                return status;
            }
        }
    }
    if (to_high <= 0) {
        return ft_delta_map_visit_range_nodes(node->right, state);
    }
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_visit_range_core(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_node* root,
    const void* low,
    const void* high,
    ft_delta_map_node_visit_fn visitor,
    void* context)
{
    ft_delta_map_range_state state;
    int comparison = 0;
    const ft_status status = ft_delta_map_compare(policy, low, high, &comparison);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (comparison > 0) {
        return FT_STATUS_OK;
    }
    state.policy = policy;
    state.low = low;
    state.high = high;
    state.visitor = visitor;
    state.context = context;
    return ft_delta_map_visit_range_nodes(root, &state);
}

/* Handle plumbing. */

static bool ft_delta_map_valid(const ft_delta_map* map)
{
    return map != NULL && map->policy != NULL;
}

static void ft_delta_map_change_of(
    const ft_delta_map_node* node,
    ft_delta_map_change* change)
{
    change->key = node->key->bytes;
    change->before.has_value = node->before != NULL;
    change->before.value = node->before == NULL ? NULL : node->before->bytes;
    change->after.has_value = node->after != NULL;
    change->after.value = node->after == NULL ? NULL : node->after->bytes;
}

static ft_delta_map ft_delta_map_retain_all(const ft_delta_map* map)
{
    ft_delta_map produced;
    produced.policy = map->policy;
    produced.current = map->current;
    produced.checkpoint = map->checkpoint;
    produced.changes = map->changes;
    ft_delta_map_policy_retain(produced.policy);
    ft_delta_map_node_retain(produced.current);
    ft_delta_map_node_retain(produced.checkpoint);
    ft_delta_map_node_retain(produced.changes);
    return produced;
}

static void ft_delta_map_publish(
    const ft_delta_map* source,
    ft_delta_map* result,
    ft_delta_map produced)
{
    if (source == result) {
        ft_delta_map old = *result;
        *result = produced;
        ft_delta_map_dispose(&old);
    } else {
        *result = produced;
    }
}

void ft_delta_map_policy_config_init(
    ft_delta_map_policy_config* config,
    size_t key_size,
    const void* key_type_identity,
    ft_delta_map_compare_fn compare_key,
    size_t value_size,
    const void* value_type_identity,
    ft_delta_map_equal_fn value_equal,
    void* callback_context)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->key_size = key_size;
    config->key_type_identity = key_type_identity;
    config->compare_key = compare_key;
    config->value_size = value_size;
    config->value_type_identity = value_type_identity;
    config->value_equal = value_equal;
    config->callback_context = callback_context;
    config->allocator.allocate = ft_delta_map_default_allocate;
    config->allocator.deallocate = ft_delta_map_default_deallocate;
}

ft_status ft_delta_map_policy_create(
    ft_delta_map_policy* policy,
    const ft_delta_map_policy_config* config)
{
    ft_delta_map_policy_rep* rep = NULL;
    if (policy == NULL || !ft_delta_map_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_delta_map_policy_rep*)ft_delta_map_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_delta_map_ref_init(&rep->refs);
    rep->config = *config;
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_policy_copy(
    const ft_delta_map_policy* source,
    ft_delta_map_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_delta_map_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_delta_map_policy_move(
    ft_delta_map_policy* destination,
    ft_delta_map_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_delta_map_policy_dispose(ft_delta_map_policy* policy)
{
    if (policy != NULL) {
        ft_delta_map_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool ft_delta_map_policy_same(
    const ft_delta_map_policy* left,
    const ft_delta_map_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL &&
        left->rep == right->rep;
}

ft_status ft_delta_map_init(ft_delta_map* map, const ft_delta_map_policy* policy)
{
    if (map == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_delta_map_policy_retain(policy->rep);
    map->policy = policy->rep;
    map->current = NULL;
    map->checkpoint = NULL;
    map->changes = NULL;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_from_entries(
    ft_delta_map* map,
    const ft_delta_map_policy* policy,
    const void* keys,
    const void* values,
    size_t count)
{
    ft_delta_map_policy_rep* rep = NULL;
    ft_delta_map_node* root = NULL;
    ft_status status = FT_STATUS_OK;
    size_t index = 0;
    if (map == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && (keys == NULL || values == NULL))) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = policy->rep;
    for (index = 0; index != count; ++index) {
        const void* key = (const void*)((const unsigned char*)keys + index * rep->config.key_size);
        const void* value =
            (const void*)((const unsigned char*)values + index * rep->config.value_size);
        ft_delta_map_payload entry;
        ft_delta_map_node* rebuilt = NULL;
        entry.key = NULL;
        entry.value = NULL;
        entry.before = NULL;
        entry.after = NULL;
        status = ft_delta_map_key_box_create(rep, key, &entry.key);
        if (status != FT_STATUS_OK) {
            break;
        }
        status = ft_delta_map_value_box_create(rep, value, &entry.value);
        if (status != FT_STATUS_OK) {
            ft_delta_map_key_release(rep, entry.key);
            break;
        }
        status = ft_delta_map_node_set(rep, root, &entry, &rebuilt);
        ft_delta_map_key_release(rep, entry.key);
        ft_delta_map_value_release(rep, entry.value);
        if (status != FT_STATUS_OK) {
            break;
        }
        ft_delta_map_node_release(rep, root);
        root = rebuilt;
    }
    if (status != FT_STATUS_OK) {
        ft_delta_map_node_release(rep, root);
        return status;
    }
    ft_delta_map_policy_retain(rep);
    ft_delta_map_node_retain(root);
    map->policy = rep;
    map->current = root;
    map->checkpoint = root;
    map->changes = NULL;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_copy(const ft_delta_map* source, ft_delta_map* destination)
{
    if (!ft_delta_map_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    *destination = ft_delta_map_retain_all(source);
    return FT_STATUS_OK;
}

void ft_delta_map_move(ft_delta_map* destination, ft_delta_map* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_delta_map_dispose(ft_delta_map* map)
{
    if (map == NULL) {
        return;
    }
    if (map->policy != NULL) {
        ft_delta_map_node_release(map->policy, map->current);
        ft_delta_map_node_release(map->policy, map->checkpoint);
        ft_delta_map_node_release(map->policy, map->changes);
        ft_delta_map_policy_release(map->policy);
    }
    (void)memset(map, 0, sizeof(*map));
}

ft_status ft_delta_map_get_policy(const ft_delta_map* map, ft_delta_map_policy* policy)
{
    if (!ft_delta_map_valid(map) || policy == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_delta_map_policy_retain(map->policy);
    policy->rep = map->policy;
    return FT_STATUS_OK;
}

bool ft_delta_map_empty(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) && map->current == NULL;
}

size_t ft_delta_map_size(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) ? ft_delta_map_node_size(map->current) : 0u;
}

size_t ft_delta_map_checkpoint_size(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) ? ft_delta_map_node_size(map->checkpoint) : 0u;
}

size_t ft_delta_map_change_count(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) ? ft_delta_map_node_size(map->changes) : 0u;
}

bool ft_delta_map_has_changes(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) && map->changes != NULL;
}

ft_status ft_delta_map_contains_key(
    const ft_delta_map* map,
    const void* key,
    bool* found)
{
    ft_delta_map_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_delta_map_find(map->policy, map->current, key, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    *found = node != NULL;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_index_of_key(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    size_t* index)
{
    bool located = false;
    size_t rank = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || found == NULL || index == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_delta_map_find_rank(map->policy, map->current, key, &located, &rank);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (located) {
        *index = rank;
    }
    *found = located;
    return FT_STATUS_OK;
}

static void ft_delta_map_publish_entry_ref(
    const ft_delta_map_node* node,
    const void** key_ref,
    const void** value_ref)
{
    if (key_ref != NULL) {
        *key_ref = node->key->bytes;
    }
    if (value_ref != NULL) {
        *value_ref = node->value->bytes;
    }
}

/* Constructs both requested copies or neither, so a failing second copy destroys the first. */
static ft_status ft_delta_map_publish_entry_copy(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_node* node,
    void* key_out,
    void* value_out)
{
    ft_status status = FT_STATUS_OK;
    if (key_out != NULL) {
        if (policy->config.key_copy == NULL) {
            (void)memcpy(key_out, node->key->bytes, policy->config.key_size);
        } else {
            status = policy->config.key_copy(
                key_out, node->key->bytes, policy->config.callback_context);
            if (status != FT_STATUS_OK) {
                return status;
            }
        }
    }
    if (value_out != NULL) {
        if (policy->config.value_copy == NULL) {
            (void)memcpy(value_out, node->value->bytes, policy->config.value_size);
        } else {
            status = policy->config.value_copy(
                value_out, node->value->bytes, policy->config.callback_context);
            if (status != FT_STATUS_OK) {
                if (key_out != NULL && policy->config.key_destroy != NULL) {
                    policy->config.key_destroy(key_out, policy->config.callback_context);
                }
                return status;
            }
        }
    }
    return FT_STATUS_OK;
}

typedef enum ft_delta_map_seek {
    FT_DELTA_MAP_SEEK_EXACT,
    FT_DELTA_MAP_SEEK_FLOOR,
    FT_DELTA_MAP_SEEK_CEILING,
    FT_DELTA_MAP_SEEK_LOWER,
    FT_DELTA_MAP_SEEK_HIGHER
} ft_delta_map_seek;

static ft_status ft_delta_map_seek_node(
    const ft_delta_map* map,
    const void* key,
    ft_delta_map_seek seek,
    const ft_delta_map_node** found)
{
    if (seek == FT_DELTA_MAP_SEEK_EXACT) {
        ft_delta_map_node* node = NULL;
        const ft_status status = ft_delta_map_find(map->policy, map->current, key, &node);
        *found = node;
        return status;
    }
    return ft_delta_map_neighbor(
        map->policy,
        map->current,
        key,
        seek == FT_DELTA_MAP_SEEK_FLOOR || seek == FT_DELTA_MAP_SEEK_LOWER,
        seek == FT_DELTA_MAP_SEEK_LOWER || seek == FT_DELTA_MAP_SEEK_HIGHER,
        found);
}

static ft_status ft_delta_map_seek_ref(
    const ft_delta_map* map,
    const void* key,
    ft_delta_map_seek seek,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    const ft_delta_map_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_delta_map_seek_node(map, key, seek, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (node != NULL) {
        ft_delta_map_publish_entry_ref(node, key_ref, value_ref);
    }
    *found = node != NULL;
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_seek_copy(
    const ft_delta_map* map,
    const void* key,
    ft_delta_map_seek seek,
    bool* found,
    void* key_out,
    void* value_out)
{
    const ft_delta_map_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_delta_map_seek_node(map, key, seek, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (node != NULL) {
        status = ft_delta_map_publish_entry_copy(map->policy, node, key_out, value_out);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    *found = node != NULL;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_try_get_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_seek_ref(
        map, key, FT_DELTA_MAP_SEEK_EXACT, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_get_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_seek_copy(
        map, key, FT_DELTA_MAP_SEEK_EXACT, found, key_out, value_out);
}

ft_status ft_delta_map_entry_at_ref(
    const ft_delta_map* map,
    size_t index,
    const void** key_ref,
    const void** value_ref)
{
    const ft_delta_map_node* node = NULL;
    if (!ft_delta_map_valid(map)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_delta_map_node_size(map->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    node = ft_delta_map_node_at(map->current, index);
    ft_delta_map_publish_entry_ref(node, key_ref, value_ref);
    return FT_STATUS_OK;
}

ft_status ft_delta_map_entry_at_copy(
    const ft_delta_map* map,
    size_t index,
    void* key_out,
    void* value_out)
{
    const ft_delta_map_node* node = NULL;
    if (!ft_delta_map_valid(map)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_delta_map_node_size(map->current)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    node = ft_delta_map_node_at(map->current, index);
    return ft_delta_map_publish_entry_copy(map->policy, node, key_out, value_out);
}

static ft_status ft_delta_map_extreme_ref(
    const ft_delta_map* map,
    bool least,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    const ft_delta_map_node* node = NULL;
    if (!ft_delta_map_valid(map) || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    node = ft_delta_map_extreme(map->current, least);
    if (node != NULL) {
        ft_delta_map_publish_entry_ref(node, key_ref, value_ref);
    }
    *found = node != NULL;
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_extreme_copy(
    const ft_delta_map* map,
    bool least,
    bool* found,
    void* key_out,
    void* value_out)
{
    const ft_delta_map_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    node = ft_delta_map_extreme(map->current, least);
    if (node != NULL) {
        status = ft_delta_map_publish_entry_copy(map->policy, node, key_out, value_out);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    *found = node != NULL;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_try_min_entry_ref(
    const ft_delta_map* map,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_extreme_ref(map, true, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_min_entry_copy(
    const ft_delta_map* map,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_extreme_copy(map, true, found, key_out, value_out);
}

ft_status ft_delta_map_try_max_entry_ref(
    const ft_delta_map* map,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_extreme_ref(map, false, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_max_entry_copy(
    const ft_delta_map* map,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_extreme_copy(map, false, found, key_out, value_out);
}

ft_status ft_delta_map_try_floor_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_seek_ref(
        map, key, FT_DELTA_MAP_SEEK_FLOOR, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_floor_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_seek_copy(
        map, key, FT_DELTA_MAP_SEEK_FLOOR, found, key_out, value_out);
}

ft_status ft_delta_map_try_ceiling_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_seek_ref(
        map, key, FT_DELTA_MAP_SEEK_CEILING, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_ceiling_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_seek_copy(
        map, key, FT_DELTA_MAP_SEEK_CEILING, found, key_out, value_out);
}

ft_status ft_delta_map_try_lower_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_seek_ref(
        map, key, FT_DELTA_MAP_SEEK_LOWER, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_lower_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_seek_copy(
        map, key, FT_DELTA_MAP_SEEK_LOWER, found, key_out, value_out);
}

ft_status ft_delta_map_try_higher_entry_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    const void** key_ref,
    const void** value_ref)
{
    return ft_delta_map_seek_ref(
        map, key, FT_DELTA_MAP_SEEK_HIGHER, found, key_ref, value_ref);
}

ft_status ft_delta_map_try_higher_entry_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    void* value_out)
{
    return ft_delta_map_seek_copy(
        map, key, FT_DELTA_MAP_SEEK_HIGHER, found, key_out, value_out);
}

typedef struct ft_delta_map_entry_relay {
    ft_delta_map_entry_visit_fn visitor;
    void* context;
} ft_delta_map_entry_relay;

static ft_status ft_delta_map_relay_entry(const ft_delta_map_node* node, void* context)
{
    ft_delta_map_entry_relay* relay = (ft_delta_map_entry_relay*)context;
    return relay->visitor(node->key->bytes, node->value->bytes, relay->context);
}

typedef struct ft_delta_map_change_relay {
    ft_delta_map_change_visit_fn visitor;
    void* context;
} ft_delta_map_change_relay;

static ft_status ft_delta_map_relay_change(const ft_delta_map_node* node, void* context)
{
    ft_delta_map_change_relay* relay = (ft_delta_map_change_relay*)context;
    ft_delta_map_change change;
    ft_delta_map_change_of(node, &change);
    return relay->visitor(&change, relay->context);
}

ft_status ft_delta_map_visit(
    const ft_delta_map* map,
    ft_delta_map_entry_visit_fn visitor,
    void* context)
{
    ft_delta_map_entry_relay relay;
    if (!ft_delta_map_valid(map) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    relay.visitor = visitor;
    relay.context = context;
    return ft_delta_map_visit_nodes(map->current, ft_delta_map_relay_entry, &relay);
}

ft_status ft_delta_map_visit_checkpoint(
    const ft_delta_map* map,
    ft_delta_map_entry_visit_fn visitor,
    void* context)
{
    ft_delta_map_entry_relay relay;
    if (!ft_delta_map_valid(map) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    relay.visitor = visitor;
    relay.context = context;
    return ft_delta_map_visit_nodes(map->checkpoint, ft_delta_map_relay_entry, &relay);
}

ft_status ft_delta_map_visit_range(
    const ft_delta_map* map,
    const void* low,
    const void* high,
    ft_delta_map_entry_visit_fn visitor,
    void* context)
{
    ft_delta_map_entry_relay relay;
    if (!ft_delta_map_valid(map) || low == NULL || high == NULL || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    relay.visitor = visitor;
    relay.context = context;
    return ft_delta_map_visit_range_core(
        map->policy, map->current, low, high, ft_delta_map_relay_entry, &relay);
}

ft_status ft_delta_map_try_get_change_ref(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    ft_delta_map_change* change)
{
    ft_delta_map_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || found == NULL || change == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_delta_map_find(map->policy, map->changes, key, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (node != NULL) {
        ft_delta_map_change_of(node, change);
    }
    *found = node != NULL;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_try_get_change_copy(
    const ft_delta_map* map,
    const void* key,
    bool* found,
    void* key_out,
    bool* before_has_value,
    void* before_out,
    bool* after_has_value,
    void* after_out)
{
    ft_delta_map_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    bool key_constructed = false;
    bool before_constructed = false;
    if (!ft_delta_map_valid(map) || key == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_delta_map_find(map->policy, map->changes, key, &node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (node == NULL) {
        *found = false;
        return FT_STATUS_OK;
    }
    if (key_out != NULL) {
        if (map->policy->config.key_copy == NULL) {
            (void)memcpy(key_out, node->key->bytes, map->policy->config.key_size);
        } else {
            status = map->policy->config.key_copy(
                key_out, node->key->bytes, map->policy->config.callback_context);
            if (status != FT_STATUS_OK) {
                return status;
            }
        }
        key_constructed = true;
    }
    if (before_out != NULL && node->before != NULL) {
        if (map->policy->config.value_copy == NULL) {
            (void)memcpy(before_out, node->before->bytes, map->policy->config.value_size);
        } else {
            status = map->policy->config.value_copy(
                before_out, node->before->bytes, map->policy->config.callback_context);
            if (status != FT_STATUS_OK) {
                if (key_constructed && map->policy->config.key_destroy != NULL) {
                    map->policy->config.key_destroy(
                        key_out, map->policy->config.callback_context);
                }
                return status;
            }
        }
        before_constructed = true;
    }
    if (after_out != NULL && node->after != NULL) {
        if (map->policy->config.value_copy == NULL) {
            (void)memcpy(after_out, node->after->bytes, map->policy->config.value_size);
        } else {
            status = map->policy->config.value_copy(
                after_out, node->after->bytes, map->policy->config.callback_context);
            if (status != FT_STATUS_OK) {
                if (before_constructed && map->policy->config.value_destroy != NULL) {
                    map->policy->config.value_destroy(
                        before_out, map->policy->config.callback_context);
                }
                if (key_constructed && map->policy->config.key_destroy != NULL) {
                    map->policy->config.key_destroy(
                        key_out, map->policy->config.callback_context);
                }
                return status;
            }
        }
    }
    if (before_has_value != NULL) {
        *before_has_value = node->before != NULL;
    }
    if (after_has_value != NULL) {
        *after_has_value = node->after != NULL;
    }
    *found = true;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_change_kind_of(
    const ft_delta_map_change* change,
    ft_delta_map_change_kind* kind)
{
    if (change == NULL || kind == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (change->before.has_value) {
        *kind = change->after.has_value
            ? FT_DELTA_MAP_CHANGE_UPDATED
            : FT_DELTA_MAP_CHANGE_REMOVED;
        return FT_STATUS_OK;
    }
    if (!change->after.has_value) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *kind = FT_DELTA_MAP_CHANGE_ADDED;
    return FT_STATUS_OK;
}

ft_status ft_delta_map_visit_changes(
    const ft_delta_map* map,
    ft_delta_map_change_visit_fn visitor,
    void* context)
{
    ft_delta_map_change_relay relay;
    if (!ft_delta_map_valid(map) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    relay.visitor = visitor;
    relay.context = context;
    return ft_delta_map_visit_nodes(map->changes, ft_delta_map_relay_change, &relay);
}

ft_status ft_delta_map_visit_changes_in_range(
    const ft_delta_map* map,
    const void* low,
    const void* high,
    ft_delta_map_change_visit_fn visitor,
    void* context)
{
    ft_delta_map_change_relay relay;
    if (!ft_delta_map_valid(map) || low == NULL || high == NULL || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    relay.visitor = visitor;
    relay.context = context;
    return ft_delta_map_visit_range_core(
        map->policy, map->changes, low, high, ft_delta_map_relay_change, &relay);
}

/* Successor construction. The current root snaps back to the exact checkpoint root whenever the
 * change index empties, which is what keeps clean versions canonical. */
static ft_delta_map ft_delta_map_adopt_successor(
    const ft_delta_map* map,
    ft_delta_map_node* next_current,
    ft_delta_map_node* next_changes)
{
    ft_delta_map produced;
    if (next_changes == NULL) {
        ft_delta_map_node_release(map->policy, next_current);
        next_current = map->checkpoint;
        ft_delta_map_node_retain(next_current);
    }
    produced.policy = map->policy;
    produced.current = next_current;
    produced.checkpoint = map->checkpoint;
    produced.changes = next_changes;
    ft_delta_map_policy_retain(produced.policy);
    ft_delta_map_node_retain(produced.checkpoint);
    return produced;
}

ft_status ft_delta_map_set_item(
    const ft_delta_map* map,
    const void* key,
    const void* value,
    ft_delta_map* result)
{
    ft_delta_map_policy_rep* policy = NULL;
    ft_delta_map_node* current_node = NULL;
    ft_delta_map_node* change_node = NULL;
    ft_delta_map_box* representative = NULL;
    ft_delta_map_box* after = NULL;
    ft_delta_map_box* before = NULL;
    ft_delta_map_node* next_current = NULL;
    ft_delta_map_node* next_changes = NULL;
    bool representative_owned = false;
    bool cancels = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    policy = map->policy;
    status = ft_delta_map_find(policy, map->current, key, &current_node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (current_node != NULL) {
        bool equal = false;
        status = ft_delta_map_values_equal(
            policy, current_node->value->bytes, value, &equal);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (equal) {
            ft_delta_map_publish(map, result, ft_delta_map_retain_all(map));
            return FT_STATUS_OK;
        }
    }
    status = ft_delta_map_find(policy, map->changes, key, &change_node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    /* A class present at the checkpoint keeps its baseline representative; a still-active addition
     * episode keeps its first representative; only a genuinely new class takes the supplied key. */
    if (current_node != NULL) {
        representative = current_node->key;
    } else if (change_node != NULL) {
        representative = change_node->key;
    } else {
        status = ft_delta_map_key_box_create(policy, key, &representative);
        if (status != FT_STATUS_OK) {
            return status;
        }
        representative_owned = true;
    }
    /* The first effective write captures the checkpoint-relative before endpoint; later writes
     * preserve it. Without a record the current value is the checkpoint value. */
    before = change_node != NULL
        ? change_node->before
        : (current_node != NULL ? current_node->value : NULL);
    status = ft_delta_map_value_box_create(policy, value, &after);
    if (status != FT_STATUS_OK) {
        goto failed;
    }
    {
        ft_delta_map_payload entry;
        entry.key = representative;
        entry.value = after;
        entry.before = NULL;
        entry.after = NULL;
        status = ft_delta_map_node_set(policy, map->current, &entry, &next_current);
        if (status != FT_STATUS_OK) {
            goto failed;
        }
    }
    if (before != NULL) {
        status = ft_delta_map_values_equal(policy, before->bytes, value, &cancels);
        if (status != FT_STATUS_OK) {
            goto failed;
        }
    }
    if (cancels) {
        status = ft_delta_map_node_remove(
            policy, map->changes, representative->bytes, &next_changes);
    } else {
        ft_delta_map_payload record;
        record.key = representative;
        record.value = NULL;
        record.before = before;
        record.after = after;
        status = ft_delta_map_node_set(policy, map->changes, &record, &next_changes);
    }
    if (status != FT_STATUS_OK) {
        goto failed;
    }
    {
        const ft_delta_map produced =
            ft_delta_map_adopt_successor(map, next_current, next_changes);
        ft_delta_map_value_release(policy, after);
        if (representative_owned) {
            ft_delta_map_key_release(policy, representative);
        }
        ft_delta_map_publish(map, result, produced);
    }
    return FT_STATUS_OK;

failed:
    ft_delta_map_node_release(policy, next_current);
    ft_delta_map_node_release(policy, next_changes);
    ft_delta_map_value_release(policy, after);
    if (representative_owned) {
        ft_delta_map_key_release(policy, representative);
    }
    return status;
}

ft_status ft_delta_map_set_items(
    const ft_delta_map* map,
    const void* keys,
    const void* values,
    size_t count,
    ft_delta_map* result)
{
    ft_delta_map working;
    ft_status status = FT_STATUS_OK;
    size_t index = 0;
    if (!ft_delta_map_valid(map) || result == NULL ||
        (count != 0 && (keys == NULL || values == NULL))) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    working = ft_delta_map_retain_all(map);
    for (index = 0; index != count; ++index) {
        const void* key =
            (const void*)((const unsigned char*)keys + index * map->policy->config.key_size);
        const void* value =
            (const void*)((const unsigned char*)values + index * map->policy->config.value_size);
        status = ft_delta_map_set_item(&working, key, value, &working);
        if (status != FT_STATUS_OK) {
            ft_delta_map_dispose(&working);
            return status;
        }
    }
    ft_delta_map_publish(map, result, working);
    return FT_STATUS_OK;
}

ft_status ft_delta_map_remove(
    const ft_delta_map* map,
    const void* key,
    ft_delta_map* result)
{
    ft_delta_map_policy_rep* policy = NULL;
    ft_delta_map_node* current_node = NULL;
    ft_delta_map_node* change_node = NULL;
    ft_delta_map_box* representative = NULL;
    ft_delta_map_box* before = NULL;
    ft_delta_map_node* next_current = NULL;
    ft_delta_map_node* next_changes = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || key == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    policy = map->policy;
    status = ft_delta_map_find(policy, map->current, key, &current_node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (current_node == NULL) {
        ft_delta_map_publish(map, result, ft_delta_map_retain_all(map));
        return FT_STATUS_OK;
    }
    status = ft_delta_map_find(policy, map->changes, key, &change_node);
    if (status != FT_STATUS_OK) {
        return status;
    }
    representative = change_node != NULL ? change_node->key : current_node->key;
    before = change_node != NULL ? change_node->before : current_node->value;
    status = ft_delta_map_node_remove(policy, map->current, key, &next_current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    /* Removing a class added since the checkpoint cancels its record rather than recording a
     * removal, because its before endpoint is absent. */
    if (before == NULL) {
        status = ft_delta_map_node_remove(
            policy, map->changes, representative->bytes, &next_changes);
    } else {
        ft_delta_map_payload record;
        record.key = representative;
        record.value = NULL;
        record.before = before;
        record.after = NULL;
        status = ft_delta_map_node_set(policy, map->changes, &record, &next_changes);
    }
    if (status != FT_STATUS_OK) {
        ft_delta_map_node_release(policy, next_current);
        return status;
    }
    ft_delta_map_publish(
        map, result, ft_delta_map_adopt_successor(map, next_current, next_changes));
    return FT_STATUS_OK;
}

static bool ft_delta_map_clean(const ft_delta_map* map)
{
    return map->changes == NULL && map->current == map->checkpoint;
}

ft_status ft_delta_map_checkpoint(const ft_delta_map* map, ft_delta_map* result)
{
    ft_delta_map produced;
    if (!ft_delta_map_valid(map) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_delta_map_clean(map)) {
        ft_delta_map_publish(map, result, ft_delta_map_retain_all(map));
        return FT_STATUS_OK;
    }
    produced.policy = map->policy;
    produced.current = map->current;
    produced.checkpoint = map->current;
    produced.changes = NULL;
    ft_delta_map_policy_retain(produced.policy);
    ft_delta_map_node_retain(produced.current);
    ft_delta_map_node_retain(produced.checkpoint);
    ft_delta_map_publish(map, result, produced);
    return FT_STATUS_OK;
}

ft_status ft_delta_map_rollback(const ft_delta_map* map, ft_delta_map* result)
{
    ft_delta_map produced;
    if (!ft_delta_map_valid(map) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_delta_map_clean(map)) {
        ft_delta_map_publish(map, result, ft_delta_map_retain_all(map));
        return FT_STATUS_OK;
    }
    produced.policy = map->policy;
    produced.current = map->checkpoint;
    produced.checkpoint = map->checkpoint;
    produced.changes = NULL;
    ft_delta_map_policy_retain(produced.policy);
    ft_delta_map_node_retain(produced.current);
    ft_delta_map_node_retain(produced.checkpoint);
    ft_delta_map_publish(map, result, produced);
    return FT_STATUS_OK;
}

const void* ft_delta_map_current_identity(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) ? (const void*)map->current : NULL;
}

const void* ft_delta_map_checkpoint_identity(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) ? (const void*)map->checkpoint : NULL;
}

const void* ft_delta_map_changes_identity(const ft_delta_map* map)
{
    return ft_delta_map_valid(map) ? (const void*)map->changes : NULL;
}

bool ft_delta_map_shares_storage(const ft_delta_map* left, const ft_delta_map* right)
{
    return ft_delta_map_valid(left) && ft_delta_map_valid(right) &&
        left->policy == right->policy && left->current == right->current &&
        left->checkpoint == right->checkpoint && left->changes == right->changes;
}

/* Validation. */

typedef struct ft_delta_map_order_state {
    const ft_delta_map_policy_rep* policy;
    const ft_delta_map_node* previous;
    bool ascending;
} ft_delta_map_order_state;

static ft_status ft_delta_map_check_order(const ft_delta_map_node* node, void* context)
{
    ft_delta_map_order_state* state = (ft_delta_map_order_state*)context;
    if (state->previous != NULL) {
        int comparison = 0;
        const ft_status status = ft_delta_map_compare(
            state->policy, state->previous->key->bytes, node->key->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison >= 0) {
            state->ascending = false;
        }
    }
    state->previous = node;
    return FT_STATUS_OK;
}

static bool ft_delta_map_counts_consistent(const ft_delta_map_node* node)
{
    if (node == NULL) {
        return true;
    }
    if (node->count !=
        1u + ft_delta_map_node_size(node->left) + ft_delta_map_node_size(node->right)) {
        return false;
    }
    return ft_delta_map_counts_consistent(node->left) &&
        ft_delta_map_counts_consistent(node->right);
}

typedef struct ft_delta_map_validation {
    const ft_delta_map* map;
    ft_delta_map_statistics statistics;
    size_t expected_changes;
    bool valid;
} ft_delta_map_validation;

static ft_status ft_delta_map_endpoint_matches(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_box* endpoint,
    const ft_delta_map_node* stored,
    bool* matches)
{
    if (endpoint == NULL || stored == NULL) {
        *matches = endpoint == NULL && stored == NULL;
        return FT_STATUS_OK;
    }
    return ft_delta_map_values_equal(
        policy, endpoint->bytes, stored->value->bytes, matches);
}

static ft_status ft_delta_map_check_change(const ft_delta_map_node* node, void* context)
{
    ft_delta_map_validation* state = (ft_delta_map_validation*)context;
    const ft_delta_map_policy_rep* policy = state->map->policy;
    ft_delta_map_node* stored = NULL;
    bool matches = false;
    ft_status status = FT_STATUS_OK;
    if (node->before == NULL && node->after == NULL) {
        state->valid = false;
        return FT_STATUS_OK;
    }
    if (node->value != NULL) {
        state->valid = false;
    }
    if (node->before != NULL && node->after != NULL) {
        bool equal = false;
        status = ft_delta_map_values_equal(
            policy, node->before->bytes, node->after->bytes, &equal);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (equal) {
            state->valid = false;
        }
        ++state->statistics.updated_count;
    } else if (node->before != NULL) {
        ++state->statistics.removed_count;
    } else {
        ++state->statistics.added_count;
    }
    status = ft_delta_map_find(policy, state->map->checkpoint, node->key->bytes, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_endpoint_matches(policy, node->before, stored, &matches);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (!matches) {
        state->valid = false;
    }
    status = ft_delta_map_find(policy, state->map->current, node->key->bytes, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_endpoint_matches(policy, node->after, stored, &matches);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (!matches) {
        state->valid = false;
    }
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_check_checkpoint_entry(
    const ft_delta_map_node* node,
    void* context)
{
    ft_delta_map_validation* state = (ft_delta_map_validation*)context;
    const ft_delta_map_policy_rep* policy = state->map->policy;
    ft_delta_map_node* stored = NULL;
    bool unchanged = false;
    ft_status status =
        ft_delta_map_find(policy, state->map->current, node->key->bytes, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (stored != NULL) {
        status = ft_delta_map_values_equal(
            policy, node->value->bytes, stored->value->bytes, &unchanged);
        if (status != FT_STATUS_OK) {
            return status;
        }
    }
    if (!unchanged) {
        ++state->expected_changes;
        status = ft_delta_map_find(policy, state->map->changes, node->key->bytes, &stored);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (stored == NULL) {
            state->valid = false;
        }
    }
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_check_current_entry(
    const ft_delta_map_node* node,
    void* context)
{
    ft_delta_map_validation* state = (ft_delta_map_validation*)context;
    const ft_delta_map_policy_rep* policy = state->map->policy;
    ft_delta_map_node* stored = NULL;
    ft_status status =
        ft_delta_map_find(policy, state->map->checkpoint, node->key->bytes, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (stored != NULL) {
        return FT_STATUS_OK;
    }
    ++state->expected_changes;
    status = ft_delta_map_find(policy, state->map->changes, node->key->bytes, &stored);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (stored == NULL) {
        state->valid = false;
    }
    return FT_STATUS_OK;
}

static ft_status ft_delta_map_check_ascending(
    const ft_delta_map_policy_rep* policy,
    const ft_delta_map_node* root,
    bool* valid)
{
    ft_delta_map_order_state order;
    ft_status status = FT_STATUS_OK;
    order.policy = policy;
    order.previous = NULL;
    order.ascending = true;
    status = ft_delta_map_visit_nodes(root, ft_delta_map_check_order, &order);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (!order.ascending || !ft_delta_map_counts_consistent(root)) {
        *valid = false;
    }
    return FT_STATUS_OK;
}

ft_status ft_delta_map_validate(
    const ft_delta_map* map,
    bool* valid,
    ft_delta_map_statistics* statistics)
{
    ft_delta_map_validation state;
    ft_status status = FT_STATUS_OK;
    if (!ft_delta_map_valid(map) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    (void)memset(&state, 0, sizeof(state));
    state.map = map;
    state.valid = true;
    state.statistics.size = ft_delta_map_node_size(map->current);
    state.statistics.checkpoint_size = ft_delta_map_node_size(map->checkpoint);
    state.statistics.change_count = ft_delta_map_node_size(map->changes);
    state.statistics.is_clean = map->current == map->checkpoint;
    if (map->changes == NULL && !state.statistics.is_clean) {
        state.valid = false;
    }
    status = ft_delta_map_check_ascending(map->policy, map->current, &state.valid);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_check_ascending(map->policy, map->checkpoint, &state.valid);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_check_ascending(map->policy, map->changes, &state.valid);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_visit_nodes(map->changes, ft_delta_map_check_change, &state);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_visit_nodes(
        map->checkpoint, ft_delta_map_check_checkpoint_entry, &state);
    if (status != FT_STATUS_OK) {
        return status;
    }
    status = ft_delta_map_visit_nodes(
        map->current, ft_delta_map_check_current_entry, &state);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (state.expected_changes != state.statistics.change_count) {
        state.valid = false;
    }
    *statistics = state.statistics;
    *valid = state.valid;
    return FT_STATUS_OK;
}
