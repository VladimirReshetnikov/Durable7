#include <Tools/DataStructures/Hamt/patricia.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef enum patricia_kind { PATRICIA_LEAF, PATRICIA_BRANCH } patricia_kind;
typedef struct tds_patricia_node {
    patricia_kind kind;
    size_t refs;
    size_t count;
    uint64_t path_or_prefix;
    uint64_t mask;
    int64_t key;
    void *value;
    struct tds_patricia_node *left;
    struct tds_patricia_node *right;
} patricia_node;

typedef tds_patricia_map patricia_map;
typedef const void *(*patricia_combine_fn)(
    int64_t key, const void *left, const void *right, void *context);

static bool identity_equal(const void *left, const void *right, void *context) { (void)context; return left == right; }
static void *identity_retain(const void *value, void *context) { (void)context; return (void *)value; }

tds_patricia_value_policy tds_patricia_value_policy_default(void) {
    tds_patricia_value_policy policy = { identity_equal, identity_retain, NULL, NULL };
    return policy;
}

static tds_patricia_value_policy normalize_policy(const tds_patricia_value_policy *policy) {
    tds_patricia_value_policy result = policy == NULL ? tds_patricia_value_policy_default() : *policy;
    if (result.equal == NULL) result.equal = identity_equal;
    if (result.retain == NULL) result.retain = identity_retain;
    return result;
}

static patricia_node *retain_node(const patricia_node *node) {
    if (node != NULL) ++((patricia_node *)node)->refs;
    return (patricia_node *)node;
}

static void release_node(const tds_patricia_value_policy *policy, const patricia_node *node) {
    if (node == NULL) return;
    patricia_node *owned = (patricia_node *)node;
    assert(owned->refs != 0);
    if (--owned->refs != 0) return;
    if (owned->kind == PATRICIA_LEAF) {
        if (policy->release != NULL) policy->release(owned->value, policy->context);
    } else {
        release_node(policy, owned->left);
        release_node(policy, owned->right);
    }
    free(owned);
}

static tds_hamt_status leaf_create(
    const tds_patricia_value_policy *policy, uint64_t path, int64_t key, const void *value, patricia_node **result) {
    void *retained = policy->retain(value, policy->context);
    if (retained == NULL && value != NULL) { *result = NULL; return TDS_HAMT_OUT_OF_MEMORY; }
    patricia_node *node = (patricia_node *)malloc(sizeof(*node));
    if (node == NULL) {
        if (policy->release != NULL) policy->release(retained, policy->context);
        *result = NULL; return TDS_HAMT_OUT_OF_MEMORY;
    }
    *node = (patricia_node){ PATRICIA_LEAF, 1, 1, path, 0, key, retained, NULL, NULL };
    *result = node; return TDS_HAMT_OK;
}

static tds_hamt_status branch_create(
    uint64_t prefix, uint64_t mask, patricia_node *left, patricia_node *right, patricia_node **result) {
    patricia_node *node = (patricia_node *)malloc(sizeof(*node));
    if (node == NULL) { *result = NULL; return TDS_HAMT_OUT_OF_MEMORY; }
    *node = (patricia_node){ PATRICIA_BRANCH, 1, left->count + right->count, prefix, mask, 0, NULL, left, right };
    *result = node; return TDS_HAMT_OK;
}

static tds_hamt_status branch_from_owned(
    const tds_patricia_value_policy *policy, const patricia_node *original,
    uint64_t prefix, uint64_t mask,
    patricia_node *left, patricia_node *right, patricia_node **result) {
    if (original != NULL && original->kind == PATRICIA_BRANCH
        && original->left == left && original->right == right) {
        release_node(policy, left);
        release_node(policy, right);
        *result = retain_node(original);
        return TDS_HAMT_OK;
    }
    tds_hamt_status status = branch_create(prefix, mask, left, right, result);
    if (status != TDS_HAMT_OK) {
        release_node(policy, left);
        release_node(policy, right);
    }
    return status;
}

static uint64_t prefix_of(uint64_t path, uint64_t mask) { return path & ~((mask << 1) - 1u); }
static uint64_t highest_bit(uint64_t value) {
    uint64_t bit = 1;
    while ((value >>= 1) != 0) bit <<= 1;
    return bit;
}

static tds_hamt_status join_owned(
    const tds_patricia_value_policy *policy,
    uint64_t left_path, patricia_node *left, uint64_t right_path, patricia_node *right, patricia_node **result) {
    const uint64_t mask = highest_bit(left_path ^ right_path);
    const uint64_t prefix = prefix_of(left_path, mask);
    tds_hamt_status status = (left_path & mask) == 0
        ? branch_create(prefix, mask, left, right, result)
        : branch_create(prefix, mask, right, left, result);
    if (status != TDS_HAMT_OK) { release_node(policy, left); release_node(policy, right); }
    return status;
}

static const patricia_node *find_node(const patricia_node *node, uint64_t path) {
    while (node != NULL) {
        if (node->kind == PATRICIA_LEAF) {
            return node->path_or_prefix == path ? node : NULL;
        }
        if (prefix_of(path, node->mask) != node->path_or_prefix) return NULL;
        node = (path & node->mask) == 0 ? node->left : node->right;
    }
    return NULL;
}

static bool try_get_node(const patricia_node *node, uint64_t path, const void **value) {
    const patricia_node *found = find_node(node, path);
    if (found == NULL) return false;
    if (value != NULL) *value = found->value;
    return true;
}

static tds_hamt_status set_node(
    const tds_patricia_value_policy *policy, const patricia_node *node,
    uint64_t path, int64_t key, const void *value, bool prefer_existing,
    patricia_combine_fn combine, void *combine_context, bool incoming_is_left,
    bool *added, bool *changed, patricia_node **result) {
    if (node == NULL) { *added = *changed = true; return leaf_create(policy, path, key, value, result); }
    if (node->kind == PATRICIA_LEAF) {
        if (node->path_or_prefix == path) {
            *added = false;
            const void *resolved = value;
            if (combine != NULL) {
                resolved = incoming_is_left
                    ? combine(node->key, value, node->value, combine_context)
                    : combine(node->key, node->value, value, combine_context);
            } else if (prefer_existing) {
                *changed = false; *result = retain_node(node); return TDS_HAMT_OK;
            }
            if (policy->equal(node->value, resolved, policy->context)) {
                *changed = false; *result = retain_node(node); return TDS_HAMT_OK;
            }
            *changed = true; return leaf_create(policy, path, node->key, resolved, result);
        }
        patricia_node *right = NULL;
        tds_hamt_status status = leaf_create(policy, path, key, value, &right);
        if (status != TDS_HAMT_OK) return status;
        *added = *changed = true;
        return join_owned(policy, node->path_or_prefix, retain_node(node), path, right, result);
    }
    if (prefix_of(path, node->mask) != node->path_or_prefix) {
        patricia_node *right = NULL;
        tds_hamt_status status = leaf_create(policy, path, key, value, &right);
        if (status != TDS_HAMT_OK) return status;
        *added = *changed = true;
        return join_owned(policy, node->path_or_prefix, retain_node(node), path, right, result);
    }
    const bool go_left = (path & node->mask) == 0;
    patricia_node *child = NULL;
    tds_hamt_status status = set_node(
        policy, go_left ? node->left : node->right, path, key, value,
        prefer_existing, combine, combine_context, incoming_is_left, added, changed, &child);
    if (status != TDS_HAMT_OK) return status;
    if (!*changed) { release_node(policy, child); *result = retain_node(node); return TDS_HAMT_OK; }
    patricia_node *other = retain_node(go_left ? node->right : node->left);
    status = go_left
        ? branch_create(node->path_or_prefix, node->mask, child, other, result)
        : branch_create(node->path_or_prefix, node->mask, other, child, result);
    if (status != TDS_HAMT_OK) { release_node(policy, child); release_node(policy, other); }
    return status;
}

static tds_hamt_status remove_node(
    const tds_patricia_value_policy *policy, const patricia_node *node,
    uint64_t path, bool *changed, patricia_node **result) {
    if (node == NULL) { *changed = false; *result = NULL; return TDS_HAMT_OK; }
    if (node->kind == PATRICIA_LEAF) {
        *changed = node->path_or_prefix == path;
        *result = *changed ? NULL : retain_node(node);
        return TDS_HAMT_OK;
    }
    if (prefix_of(path, node->mask) != node->path_or_prefix) {
        *changed = false; *result = retain_node(node); return TDS_HAMT_OK;
    }
    const bool go_left = (path & node->mask) == 0;
    patricia_node *child = NULL;
    tds_hamt_status status = remove_node(policy, go_left ? node->left : node->right, path, changed, &child);
    if (status != TDS_HAMT_OK) return status;
    if (!*changed) { release_node(policy, child); *result = retain_node(node); return TDS_HAMT_OK; }
    const patricia_node *other_source = go_left ? node->right : node->left;
    if (child == NULL) { *result = retain_node(other_source); return TDS_HAMT_OK; }
    patricia_node *other = retain_node(other_source);
    status = go_left
        ? branch_create(node->path_or_prefix, node->mask, child, other, result)
        : branch_create(node->path_or_prefix, node->mask, other, child, result);
    if (status != TDS_HAMT_OK) { release_node(policy, child); release_node(policy, other); }
    return status;
}

static tds_hamt_status union_nodes(
    const tds_patricia_value_policy *, const patricia_node *, const patricia_node *,
    patricia_combine_fn, void *, patricia_node **);
static tds_hamt_status intersect_nodes(
    const tds_patricia_value_policy *, const patricia_node *, const patricia_node *,
    patricia_combine_fn, void *, patricia_node **);
static tds_hamt_status except_nodes(const tds_patricia_value_policy *, const patricia_node *, const patricia_node *, patricia_node **);

static tds_hamt_status union_nodes(
    const tds_patricia_value_policy *policy, const patricia_node *left, const patricia_node *right,
    patricia_combine_fn combine, void *combine_context, patricia_node **result) {
    if (left == NULL) { *result = retain_node(right); return TDS_HAMT_OK; }
    if (right == NULL || (left == right && combine == NULL)) {
        *result = retain_node(left); return TDS_HAMT_OK;
    }
    if (left->kind == PATRICIA_LEAF && right->kind == PATRICIA_LEAF) {
        if (left->path_or_prefix != right->path_or_prefix) {
            return join_owned(policy, left->path_or_prefix, retain_node(left),
                right->path_or_prefix, retain_node(right), result);
        }
        if (combine == NULL) {
            *result = retain_node(policy->equal(
                left->value, right->value, policy->context) ? left : right);
            return TDS_HAMT_OK;
        }
        const void *resolved = combine(left->key, left->value, right->value, combine_context);
        if (policy->equal(left->value, resolved, policy->context)) {
            *result = retain_node(left); return TDS_HAMT_OK;
        }
        if (policy->equal(right->value, resolved, policy->context)) {
            *result = retain_node(right); return TDS_HAMT_OK;
        }
        return leaf_create(policy, left->path_or_prefix, left->key, resolved, result);
    }
    if (left->kind == PATRICIA_LEAF) {
        bool added, changed;
        return set_node(policy, right, left->path_or_prefix, left->key, left->value, true,
            combine, combine_context, true, &added, &changed, result);
    }
    if (right->kind == PATRICIA_LEAF) {
        bool added, changed;
        return set_node(policy, left, right->path_or_prefix, right->key, right->value, false,
            combine, combine_context, false, &added, &changed, result);
    }
    if (left->mask == right->mask && left->path_or_prefix == right->path_or_prefix) {
        patricia_node *l = NULL, *r = NULL;
        tds_hamt_status status = union_nodes(policy, left->left, right->left, combine, combine_context, &l);
        if (status == TDS_HAMT_OK) status = union_nodes(policy, left->right, right->right, combine, combine_context, &r);
        if (status == TDS_HAMT_OK) return branch_from_owned(
            policy, left, left->path_or_prefix, left->mask, l, r, result);
        release_node(policy, l); release_node(policy, r); return status;
    }
    if (left->mask > right->mask && prefix_of(right->path_or_prefix, left->mask) == left->path_or_prefix) {
        patricia_node *merged = NULL;
        const bool side = (right->path_or_prefix & left->mask) != 0;
        tds_hamt_status status = union_nodes(
            policy, side ? left->right : left->left, right, combine, combine_context, &merged);
        patricia_node *other = status == TDS_HAMT_OK ? retain_node(side ? left->left : left->right) : NULL;
        if (status == TDS_HAMT_OK) return side
            ? branch_from_owned(policy, left, left->path_or_prefix, left->mask, other, merged, result)
            : branch_from_owned(policy, left, left->path_or_prefix, left->mask, merged, other, result);
        release_node(policy, merged); release_node(policy, other); return status;
    }
    if (right->mask > left->mask && prefix_of(left->path_or_prefix, right->mask) == right->path_or_prefix) {
        patricia_node *merged = NULL;
        const bool side = (left->path_or_prefix & right->mask) != 0;
        tds_hamt_status status = union_nodes(
            policy, left, side ? right->right : right->left, combine, combine_context, &merged);
        patricia_node *other = status == TDS_HAMT_OK ? retain_node(side ? right->left : right->right) : NULL;
        if (status == TDS_HAMT_OK) return side
            ? branch_from_owned(policy, right, right->path_or_prefix, right->mask, other, merged, result)
            : branch_from_owned(policy, right, right->path_or_prefix, right->mask, merged, other, result);
        release_node(policy, merged); release_node(policy, other); return status;
    }
    return join_owned(policy, left->path_or_prefix, retain_node(left), right->path_or_prefix, retain_node(right), result);
}

static tds_hamt_status collapse_owned(
    const tds_patricia_value_policy *policy, const patricia_node *original,
    uint64_t prefix, uint64_t mask,
    patricia_node *left, patricia_node *right, patricia_node **result) {
    if (left == NULL) { *result = right; return TDS_HAMT_OK; }
    if (right == NULL) { *result = left; return TDS_HAMT_OK; }
    return branch_from_owned(policy, original, prefix, mask, left, right, result);
}

static tds_hamt_status intersect_nodes(
    const tds_patricia_value_policy *policy, const patricia_node *left, const patricia_node *right,
    patricia_combine_fn combine, void *combine_context, patricia_node **result) {
    if (left == NULL || right == NULL) { *result = NULL; return TDS_HAMT_OK; }
    if (left == right && combine == NULL) { *result = retain_node(left); return TDS_HAMT_OK; }
    if (left->kind == PATRICIA_LEAF || right->kind == PATRICIA_LEAF) {
        const patricia_node *left_leaf = left->kind == PATRICIA_LEAF
            ? left : find_node(left, right->path_or_prefix);
        const patricia_node *right_leaf = right->kind == PATRICIA_LEAF
            ? right : find_node(right, left->path_or_prefix);
        if (left_leaf == NULL || right_leaf == NULL
            || left_leaf->path_or_prefix != right_leaf->path_or_prefix) {
            *result = NULL; return TDS_HAMT_OK;
        }
        if (combine == NULL) { *result = retain_node(left_leaf); return TDS_HAMT_OK; }
        const void *resolved = combine(
            left_leaf->key, left_leaf->value, right_leaf->value, combine_context);
        if (policy->equal(left_leaf->value, resolved, policy->context)) {
            *result = retain_node(left_leaf); return TDS_HAMT_OK;
        }
        if (policy->equal(right_leaf->value, resolved, policy->context)) {
            *result = retain_node(right_leaf); return TDS_HAMT_OK;
        }
        return leaf_create(
            policy, left_leaf->path_or_prefix, left_leaf->key, resolved, result);
    }
    if (left->mask == right->mask && left->path_or_prefix == right->path_or_prefix) {
        patricia_node *l = NULL, *r = NULL;
        tds_hamt_status status = intersect_nodes(
            policy, left->left, right->left, combine, combine_context, &l);
        if (status == TDS_HAMT_OK) status = intersect_nodes(
            policy, left->right, right->right, combine, combine_context, &r);
        if (status == TDS_HAMT_OK) return collapse_owned(
            policy, left, left->path_or_prefix, left->mask, l, r, result);
        release_node(policy, l); release_node(policy, r); return status;
    }
    if (left->mask > right->mask && prefix_of(right->path_or_prefix, left->mask) == left->path_or_prefix)
        return intersect_nodes(policy,
            (right->path_or_prefix & left->mask) == 0 ? left->left : left->right,
            right, combine, combine_context, result);
    if (right->mask > left->mask && prefix_of(left->path_or_prefix, right->mask) == right->path_or_prefix)
        return intersect_nodes(policy, left,
            (left->path_or_prefix & right->mask) == 0 ? right->left : right->right,
            combine, combine_context, result);
    *result = NULL; return TDS_HAMT_OK;
}

static tds_hamt_status except_nodes(
    const tds_patricia_value_policy *policy, const patricia_node *left, const patricia_node *right, patricia_node **result) {
    if (left == NULL || right == NULL) { *result = retain_node(left); return TDS_HAMT_OK; }
    if (left == right) { *result = NULL; return TDS_HAMT_OK; }
    if (left->kind == PATRICIA_LEAF) { *result = try_get_node(right, left->path_or_prefix, NULL) ? NULL : retain_node(left); return TDS_HAMT_OK; }
    if (right->kind == PATRICIA_LEAF) { bool changed; return remove_node(policy, left, right->path_or_prefix, &changed, result); }
    if (left->mask == right->mask && left->path_or_prefix == right->path_or_prefix) {
        patricia_node *l = NULL, *r = NULL;
        tds_hamt_status status = except_nodes(policy, left->left, right->left, &l);
        if (status == TDS_HAMT_OK) status = except_nodes(policy, left->right, right->right, &r);
        if (status == TDS_HAMT_OK) return collapse_owned(
            policy, left, left->path_or_prefix, left->mask, l, r, result);
        release_node(policy, l); release_node(policy, r); return status;
    }
    if (left->mask > right->mask && prefix_of(right->path_or_prefix, left->mask) == left->path_or_prefix) {
        const bool side = (right->path_or_prefix & left->mask) != 0;
        patricia_node *changed = NULL;
        tds_hamt_status status = except_nodes(policy, side ? left->right : left->left, right, &changed);
        patricia_node *other = status == TDS_HAMT_OK ? retain_node(side ? left->left : left->right) : NULL;
        if (status == TDS_HAMT_OK) return side
            ? collapse_owned(policy, left, left->path_or_prefix, left->mask, other, changed, result)
            : collapse_owned(policy, left, left->path_or_prefix, left->mask, changed, other, result);
        release_node(policy, changed); release_node(policy, other); return status;
    }
    if (right->mask > left->mask && prefix_of(left->path_or_prefix, right->mask) == right->path_or_prefix)
        return except_nodes(policy, left, (left->path_or_prefix & right->mask) == 0 ? right->left : right->right, result);
    *result = retain_node(left); return TDS_HAMT_OK;
}

static size_t node_count(const patricia_node *node) {
    return node == NULL ? 0 : node->count;
}
static bool policies_match(const patricia_map *left, const patricia_map *right) {
    return left->policy.equal == right->policy.equal && left->policy.retain == right->policy.retain
        && left->policy.release == right->policy.release && left->policy.context == right->policy.context;
}
static uint64_t encode32(int64_t key) { return (uint32_t)((int32_t)key) ^ UINT32_C(0x80000000); }
static uint64_t encode64(int64_t key) { return (uint64_t)key ^ UINT64_C(0x8000000000000000); }

static patricia_map map_create(const tds_patricia_value_policy *policy) {
    patricia_map map = { NULL, 0, normalize_policy(policy) }; return map;
}
static patricia_map map_clone(const patricia_map *map) {
    patricia_map result = *map; result.root = retain_node(map->root); return result;
}
static void map_destroy(patricia_map *map) { if (map == NULL) return; release_node(&map->policy, map->root); memset(map, 0, sizeof(*map)); }
static tds_hamt_status map_set(const patricia_map *map, int64_t key, uint64_t path, const void *value, patricia_map *result) {
    if (map == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    bool added, changed; patricia_node *root = NULL;
    tds_hamt_status status = set_node(
        &map->policy, map->root, path, key, value, false,
        NULL, NULL, false, &added, &changed, &root);
    if (status != TDS_HAMT_OK) return status;
    if (result == map) release_node(&map->policy, map->root);
    result->root = root; result->count = map->count + (added ? 1u : 0u); result->policy = map->policy; return TDS_HAMT_OK;
}
static tds_hamt_status map_remove(const patricia_map *map, uint64_t path, patricia_map *result) {
    if (map == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT;
    bool changed; patricia_node *root = NULL;
    tds_hamt_status status = remove_node(&map->policy, map->root, path, &changed, &root);
    if (status != TDS_HAMT_OK) return status;
    if (result == map) release_node(&map->policy, map->root);
    result->root = root; result->count = map->count - (changed ? 1u : 0u); result->policy = map->policy; return TDS_HAMT_OK;
}
typedef tds_hamt_status (*algebra_fn)(
    const tds_patricia_value_policy *, const patricia_node *, const patricia_node *,
    patricia_combine_fn, void *, patricia_node **);
static tds_hamt_status except_nodes_operation(
    const tds_patricia_value_policy *policy, const patricia_node *left, const patricia_node *right,
    patricia_combine_fn combine, void *combine_context, patricia_node **result) {
    (void)combine;
    (void)combine_context;
    return except_nodes(policy, left, right, result);
}
static tds_hamt_status map_algebra(
    const patricia_map *left, const patricia_map *right, patricia_map *result,
    algebra_fn operation, patricia_combine_fn combine, void *combine_context) {
    if (left == NULL || right == NULL || result == NULL || !policies_match(left, right)) return TDS_HAMT_INVALID_ARGUMENT;
    patricia_node *root = NULL;
    tds_hamt_status status = operation(
        &left->policy, left->root, right->root, combine, combine_context, &root);
    if (status != TDS_HAMT_OK) return status;
    if (result == left) release_node(&left->policy, left->root);
    else if (result == right) release_node(&right->policy, right->root);
    result->root = root; result->count = node_count(root); result->policy = left->policy; return TDS_HAMT_OK;
}

typedef struct int_map_combine_context {
    tds_int_map_combine_fn combine;
    void *context;
} int_map_combine_context;
typedef struct long_map_combine_context {
    tds_long_map_combine_fn combine;
    void *context;
} long_map_combine_context;
static const void *int_map_combine_adapter(
    int64_t key, const void *left, const void *right, void *context) {
    int_map_combine_context *state = (int_map_combine_context *)context;
    return state->combine((int32_t)key, left, right, state->context);
}
static const void *long_map_combine_adapter(
    int64_t key, const void *left, const void *right, void *context) {
    long_map_combine_context *state = (long_map_combine_context *)context;
    return state->combine(key, left, right, state->context);
}

#define DEFINE_MAP(prefix, public_type, key_type, encode_fn, visitor_type, combine_type, combine_adapter, combine_context_type) \
public_type prefix##_create(const tds_patricia_value_policy *policy) { public_type out; *(patricia_map *)&out = map_create(policy); return out; } \
public_type prefix##_clone(const public_type *map) { public_type out; *(patricia_map *)&out = map_clone((const patricia_map *)map); return out; } \
void prefix##_destroy(public_type *map) { map_destroy((patricia_map *)map); } \
size_t prefix##_count(const public_type *map) { return map == NULL ? 0 : map->count; } \
bool prefix##_try_get(const public_type *map, key_type key, const void **value) { return map != NULL && try_get_node((const patricia_node *)map->root, encode_fn(key), value); } \
tds_hamt_status prefix##_set(const public_type *map, key_type key, const void *value, public_type *result) { return map_set((const patricia_map *)map, key, encode_fn(key), value, (patricia_map *)result); } \
tds_hamt_status prefix##_remove(const public_type *map, key_type key, public_type *result) { return map_remove((const patricia_map *)map, encode_fn(key), (patricia_map *)result); } \
tds_hamt_status prefix##_union(const public_type *left, const public_type *right, public_type *result) { return map_algebra((const patricia_map *)left, (const patricia_map *)right, (patricia_map *)result, union_nodes, NULL, NULL); } \
tds_hamt_status prefix##_union_with(const public_type *left, const public_type *right, combine_type combine, void *context, public_type *result) { if (combine == NULL) return TDS_HAMT_INVALID_ARGUMENT; combine_context_type state = { combine, context }; return map_algebra((const patricia_map *)left, (const patricia_map *)right, (patricia_map *)result, union_nodes, combine_adapter, &state); } \
tds_hamt_status prefix##_intersect(const public_type *left, const public_type *right, public_type *result) { return map_algebra((const patricia_map *)left, (const patricia_map *)right, (patricia_map *)result, intersect_nodes, NULL, NULL); } \
tds_hamt_status prefix##_intersect_with(const public_type *left, const public_type *right, combine_type combine, void *context, public_type *result) { if (combine == NULL) return TDS_HAMT_INVALID_ARGUMENT; combine_context_type state = { combine, context }; return map_algebra((const patricia_map *)left, (const patricia_map *)right, (patricia_map *)result, intersect_nodes, combine_adapter, &state); } \
tds_hamt_status prefix##_except(const public_type *left, const public_type *right, public_type *result) { return map_algebra((const patricia_map *)left, (const patricia_map *)right, (patricia_map *)result, except_nodes_operation, NULL, NULL); } \
bool prefix##_shares_root(const public_type *left, const public_type *right) { return left != NULL && right != NULL && left->root == right->root; } \
static void prefix##_visit_node(const patricia_node *node, visitor_type visitor, void *context) { if (node == NULL) return; if (node->kind == PATRICIA_LEAF) { visitor((key_type)node->key, node->value, context); } else { prefix##_visit_node(node->left, visitor, context); prefix##_visit_node(node->right, visitor, context); } } \
void prefix##_visit(const public_type *map, visitor_type visitor, void *context) { if (map != NULL && visitor != NULL) prefix##_visit_node((const patricia_node *)map->root, visitor, context); }

DEFINE_MAP(tds_int_map, tds_int_map, int32_t, encode32, tds_int_map_visitor,
    tds_int_map_combine_fn, int_map_combine_adapter, int_map_combine_context)
DEFINE_MAP(tds_long_map, tds_long_map, int64_t, encode64, tds_long_map_visitor,
    tds_long_map_combine_fn, long_map_combine_adapter, long_map_combine_context)

typedef struct int_set_visit_context { tds_int_set_visitor visitor; void *context; } int_set_visit_context;
typedef struct long_set_visit_context { tds_long_set_visitor visitor; void *context; } long_set_visit_context;
static void int_set_adapter(int32_t key, const void *value, void *context) { (void)value; int_set_visit_context *state = (int_set_visit_context *)context; state->visitor(key, state->context); }
static void long_set_adapter(int64_t key, const void *value, void *context) { (void)value; long_set_visit_context *state = (long_set_visit_context *)context; state->visitor(key, state->context); }

#define DEFINE_SET(prefix, set_type, map_prefix, map_type, key_type, visitor_type, adapter, visit_context_type) \
set_type prefix##_create(void) { set_type set; set.map = map_prefix##_create(NULL); return set; } \
set_type prefix##_clone(const set_type *set) { set_type out; out.map = map_prefix##_clone(&set->map); return out; } \
void prefix##_destroy(set_type *set) { if (set != NULL) map_prefix##_destroy(&set->map); } \
size_t prefix##_count(const set_type *set) { return set == NULL ? 0 : map_prefix##_count(&set->map); } \
bool prefix##_contains(const set_type *set, key_type value) { return set != NULL && map_prefix##_try_get(&set->map, value, NULL); } \
tds_hamt_status prefix##_add(const set_type *set, key_type value, set_type *result) { if (set == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT; return map_prefix##_set(&set->map, value, NULL, &result->map); } \
tds_hamt_status prefix##_remove(const set_type *set, key_type value, set_type *result) { if (set == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT; return map_prefix##_remove(&set->map, value, &result->map); } \
tds_hamt_status prefix##_union(const set_type *left, const set_type *right, set_type *result) { if (left == NULL || right == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT; return map_prefix##_union(&left->map, &right->map, &result->map); } \
tds_hamt_status prefix##_intersect(const set_type *left, const set_type *right, set_type *result) { if (left == NULL || right == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT; return map_prefix##_intersect(&left->map, &right->map, &result->map); } \
tds_hamt_status prefix##_except(const set_type *left, const set_type *right, set_type *result) { if (left == NULL || right == NULL || result == NULL) return TDS_HAMT_INVALID_ARGUMENT; return map_prefix##_except(&left->map, &right->map, &result->map); } \
void prefix##_visit(const set_type *set, visitor_type visitor, void *context) { visit_context_type state = { visitor, context }; if (set != NULL && visitor != NULL) map_prefix##_visit(&set->map, adapter, &state); }

DEFINE_SET(tds_int_set, tds_int_set, tds_int_map, tds_int_map, int32_t, tds_int_set_visitor, int_set_adapter, int_set_visit_context)
DEFINE_SET(tds_long_set, tds_long_set, tds_long_map, tds_long_map, int64_t, tds_long_set_visitor, long_set_adapter, long_set_visit_context)
