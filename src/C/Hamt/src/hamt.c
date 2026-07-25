#include <durable7/hamt/hamt.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef D7_HAMT_TESTING
static size_t d7_hamt_test_fail_after = SIZE_MAX;
static size_t d7_hamt_test_allocation_count = 0;

void d7_hamt_test_fail_allocations_after(size_t successful_allocations) {
    d7_hamt_test_fail_after = successful_allocations;
    d7_hamt_test_allocation_count = 0;
}

void d7_hamt_test_reset_allocator(void) {
    d7_hamt_test_fail_after = SIZE_MAX;
    d7_hamt_test_allocation_count = 0;
}
#endif

static void *d7_hamt_allocate(size_t size) {
#ifdef D7_HAMT_TESTING
    if (d7_hamt_test_allocation_count == d7_hamt_test_fail_after) {
        return NULL;
    }

    ++d7_hamt_test_allocation_count;
#endif
    return malloc(size);
}

enum {
    D7_HAMT_BITS_PER_LEVEL = 5,
    D7_HAMT_BRANCH_MASK = 31
};

typedef struct d7_hamt_node d7_hamt_node;

struct d7_hamt_map_transient_state {
    size_t ref_count;
    size_t version;
    bool active;
    d7_hamt_map map;
};

struct d7_hamt_node {
    d7_hamt_node_kind kind;
    size_t ref_count;
    size_t subtree_count;
};

typedef struct d7_hamt_leaf_node {
    d7_hamt_node base;
    uint32_t hash;
    void *key;
    void *value;
} d7_hamt_leaf_node;

typedef struct d7_hamt_collision_node {
    d7_hamt_node base;
    uint32_t hash;
    size_t count;
    d7_hamt_entry entries[];
} d7_hamt_collision_node;

typedef struct d7_hamt_inline_entry {
    uint32_t hash;
    d7_hamt_entry entry;
} d7_hamt_inline_entry;

typedef struct d7_hamt_bitmap_node {
    d7_hamt_node base;
    uint32_t data_map;
    uint32_t node_map;
    size_t data_count;
    size_t node_count;
    unsigned char storage[];
} d7_hamt_bitmap_node;

typedef struct d7_hamt_entry_run_view {
    uint32_t hash;
    size_t count;
    const d7_hamt_entry *entries;
    d7_hamt_entry single;
    bool is_single;
} d7_hamt_entry_run_view;

typedef struct d7_hamt_diff_operand {
    const d7_hamt_node *node;
    const d7_hamt_inline_entry *inline_entry;
} d7_hamt_diff_operand;

typedef enum d7_hamt_combine_operation {
    D7_HAMT_COMBINE_UNION,
    D7_HAMT_COMBINE_INTERSECT,
    D7_HAMT_COMBINE_EXCEPT,
    D7_HAMT_COMBINE_SYMMETRIC_EXCEPT
} d7_hamt_combine_operation;

typedef enum d7_hamt_factory_operation {
    D7_HAMT_FACTORY_GET_OR_ADD,
    D7_HAMT_FACTORY_ADD_OR_UPDATE
} d7_hamt_factory_operation;

typedef struct d7_hamt_factory_selection {
    d7_hamt_factory_operation operation;
    d7_hamt_map_add_factory_fn add_factory;
    void *add_context;
    d7_hamt_map_update_factory_fn update_factory;
    void *update_context;
} d7_hamt_factory_selection;

static d7_hamt_inline_entry *d7_hamt_bitmap_data(d7_hamt_bitmap_node *node);
static const d7_hamt_inline_entry *d7_hamt_bitmap_data_const(const d7_hamt_bitmap_node *node);
static d7_hamt_node **d7_hamt_bitmap_children(d7_hamt_bitmap_node *node);
static d7_hamt_node *const *d7_hamt_bitmap_children_const(const d7_hamt_bitmap_node *node);

static uint32_t d7_hamt_pointer_hash(const void *item, void *context);
static bool d7_hamt_pointer_equal(const void *left, const void *right, void *context);
static void *d7_hamt_identity_retain(const void *item, void *context);
static bool d7_hamt_unit_equal(const void *left, const void *right, void *context);

static d7_hamt_policy d7_hamt_normalize_policy(const d7_hamt_policy *policy);
static d7_hamt_set_policy d7_hamt_normalize_set_policy(const d7_hamt_set_policy *policy);
static d7_hamt_policy d7_hamt_map_policy_from_set_policy(const d7_hamt_set_policy *policy);

static d7_hamt_node *d7_hamt_node_retain(const d7_hamt_node *node);
static void d7_hamt_node_release(const d7_hamt_policy *policy, const d7_hamt_node *node);
static uint32_t d7_hamt_get_hash(const d7_hamt_map *map, const void *key);
static bool d7_hamt_keys_equal(const d7_hamt_policy *policy, const void *left, const void *right);
static bool d7_hamt_values_equal(const d7_hamt_policy *policy, const void *left, const void *right);
static void *d7_hamt_retain_key(const d7_hamt_policy *policy, const void *key);
static void *d7_hamt_retain_value(const d7_hamt_policy *policy, const void *value);
static void d7_hamt_release_key(const d7_hamt_policy *policy, void *key);
static void d7_hamt_release_value(const d7_hamt_policy *policy, void *value);

static int d7_hamt_index(uint32_t hash, int shift);
static uint32_t d7_hamt_bit(int index);
static size_t d7_hamt_slot(uint32_t bitmap, uint32_t bit);
static size_t d7_hamt_popcount(uint32_t value);

static d7_hamt_status d7_hamt_leaf_create(
    const d7_hamt_policy *policy,
    uint32_t hash,
    const void *key,
    const void *value,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_leaf_create_from_retained(
    uint32_t hash,
    void *key,
    void *value,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_collision_create(
    const d7_hamt_policy *policy,
    d7_hamt_node *left,
    d7_hamt_node *right,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_bitmap_create(
    uint32_t data_map,
    uint32_t node_map,
    const d7_hamt_inline_entry *data,
    size_t data_count,
    d7_hamt_node **children,
    size_t child_count,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_bitmap_copy_create(
    const d7_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const d7_hamt_inline_entry *data,
    size_t data_count,
    d7_hamt_node *const *children,
    size_t child_count,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_merge_hash_nodes(
    const d7_hamt_policy *policy,
    d7_hamt_node *left,
    d7_hamt_node *right,
    int shift,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_merge_hash_nodes_selected(
    const d7_hamt_policy *policy,
    d7_hamt_node *left,
    d7_hamt_node *right,
    int shift,
    d7_hamt_node **result,
    const void **selected_value);
static d7_hamt_status d7_hamt_node_set(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    const void *key,
    const void *value,
    uint32_t hash,
    int shift,
    bool overwrite,
    bool *added,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_node_factory_update(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    const void *key,
    uint32_t hash,
    int shift,
    const d7_hamt_factory_selection *selection,
    bool *added,
    d7_hamt_node **result,
    const void **selected_value);
static d7_hamt_status d7_hamt_node_remove(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    const void *key,
    uint32_t hash,
    int shift,
    bool *removed,
    const void **removed_value,
    d7_hamt_node **result);
static d7_hamt_status d7_hamt_bitmap_rebuild(
    const d7_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const d7_hamt_inline_entry *data,
    size_t data_count,
    d7_hamt_node *const *children,
    size_t child_count,
    d7_hamt_node **result);
static bool d7_hamt_policy_callbacks_compatible(
    const d7_hamt_policy *left,
    const d7_hamt_policy *right);
static d7_hamt_status d7_hamt_combine_nodes(
    const d7_hamt_policy *policy,
    const d7_hamt_node *left,
    const d7_hamt_node *right,
    int shift,
    d7_hamt_combine_operation operation,
    d7_hamt_node **result);

static bool d7_hamt_try_get_entry(
    const d7_hamt_map *map,
    const void *key,
    const void **actual_key,
    const void **value);

d7_hamt_policy d7_hamt_policy_default(void) {
    d7_hamt_policy policy;
    memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_pointer_hash;
    policy.key_equal = d7_hamt_pointer_equal;
    policy.value_equal = d7_hamt_pointer_equal;
    return policy;
}

d7_hamt_set_policy d7_hamt_set_policy_default(void) {
    d7_hamt_set_policy policy;
    memset(&policy, 0, sizeof(policy));
    policy.hash = d7_hamt_pointer_hash;
    policy.equal = d7_hamt_pointer_equal;
    return policy;
}

d7_hamt_map d7_hamt_map_create(const d7_hamt_policy *policy) {
    d7_hamt_map map;
    map.root = NULL;
    map.count = 0;
    map.policy = d7_hamt_normalize_policy(policy);
    return map;
}

d7_hamt_status d7_hamt_map_create_range(
    const d7_hamt_policy *policy,
    const d7_hamt_entry *entries,
    size_t entry_count,
    d7_hamt_map *result) {
    if (result == NULL || (entry_count != 0 && entries == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_map map = d7_hamt_map_create(policy);
    for (size_t i = 0; i < entry_count; ++i) {
        d7_hamt_map next;
        const d7_hamt_status status = d7_hamt_map_set(&map, entries[i].key, entries[i].value, &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_map_destroy(&map);
            return status;
        }

        d7_hamt_map_destroy(&map);
        map = next;
    }

    *result = map;
    return D7_HAMT_OK;
}

d7_hamt_map d7_hamt_map_clone(const d7_hamt_map *map) {
    if (map == NULL) {
        return d7_hamt_map_create(NULL);
    }

    d7_hamt_map clone = *map;
    clone.root = d7_hamt_node_retain(map->root);
    return clone;
}

void d7_hamt_map_destroy(d7_hamt_map *map) {
    if (map == NULL) {
        return;
    }

    d7_hamt_node_release(&map->policy, map->root);
    map->root = NULL;
    map->count = 0;
}

size_t d7_hamt_map_count(const d7_hamt_map *map) {
    return map == NULL ? 0 : map->count;
}

bool d7_hamt_map_is_empty(const d7_hamt_map *map) {
    return d7_hamt_map_count(map) == 0;
}

bool d7_hamt_map_contains_key(const d7_hamt_map *map, const void *key) {
    const void *value = NULL;
    return d7_hamt_map_try_get(map, key, &value);
}

bool d7_hamt_map_try_get(const d7_hamt_map *map, const void *key, const void **value) {
    const void *actual_key = NULL;
    const void *found_value = NULL;
    const bool found = d7_hamt_try_get_entry(map, key, &actual_key, &found_value);
    if (value != NULL) {
        *value = found ? found_value : NULL;
    }

    return found;
}

bool d7_hamt_map_try_get_key(const d7_hamt_map *map, const void *equal_key, const void **actual_key) {
    const void *found_key = NULL;
    const void *value = NULL;
    const bool found = d7_hamt_try_get_entry(map, equal_key, &found_key, &value);
    if (actual_key != NULL) {
        *actual_key = found ? found_key : equal_key;
    }

    return found;
}

d7_hamt_status d7_hamt_map_set(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result) {
    if (map == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const uint32_t hash = d7_hamt_get_hash(map, key);
    d7_hamt_node *new_root = NULL;
    bool added = false;
    d7_hamt_status status;

    if (map->root == NULL) {
        status = d7_hamt_leaf_create(&map->policy, hash, key, value, &new_root);
        added = true;
    } else {
        status = d7_hamt_node_set(
            &map->policy,
            map->root,
            key,
            value,
            hash,
            0,
            true,
            &added,
            &new_root);
    }

    if (status != D7_HAMT_OK) {
        return status;
    }

    if (result == map) {
        /* In-place update: drop the reference the source owned so the
         * overwritten root is not leaked. */
        d7_hamt_node_release(&map->policy, map->root);
    }
    result->root = new_root;
    result->count = map->count + (added ? 1u : 0u);
    result->policy = map->policy;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_set_many(
    const d7_hamt_map *map,
    const d7_hamt_entry *entries,
    size_t entry_count,
    d7_hamt_map *result) {
    if (map == NULL || result == NULL || (entry_count != 0 && entries == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_map current = d7_hamt_map_clone(map);
    for (size_t i = 0; i < entry_count; ++i) {
        d7_hamt_map next;
        const d7_hamt_status status = d7_hamt_map_set(&current, entries[i].key, entries[i].value, &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_map_destroy(&current);
            return status;
        }

        d7_hamt_map_destroy(&current);
        current = next;
    }

    if (result == map) {
        d7_hamt_node_release(&map->policy, map->root);
    }
    *result = current;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_add(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result) {
    bool added = false;
    const d7_hamt_status status = d7_hamt_map_try_add(map, key, value, result, &added);
    if (status != D7_HAMT_OK) {
        return status;
    }

    if (!added) {
        /* A rejected duplicate publishes the source root re-retained, so an
         * aliased result already holds the original version with balanced
         * reference counts; destroying it would free the caller's only
         * handle. Only a distinct result owns a reference to release. */
        if (result != map) {
            d7_hamt_map_destroy(result);
        }
        return D7_HAMT_DUPLICATE_KEY;
    }

    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_try_add(
    const d7_hamt_map *map,
    const void *key,
    const void *value,
    d7_hamt_map *result,
    bool *added) {
    if (map == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    if (added != NULL) {
        *added = false;
    }

    const uint32_t hash = d7_hamt_get_hash(map, key);
    d7_hamt_node *new_root = NULL;
    bool local_added = false;
    d7_hamt_status status;

    if (map->root == NULL) {
        status = d7_hamt_leaf_create(&map->policy, hash, key, value, &new_root);
        local_added = true;
    } else {
        status = d7_hamt_node_set(
            &map->policy,
            map->root,
            key,
            value,
            hash,
            0,
            false,
            &local_added,
            &new_root);
    }

    if (status != D7_HAMT_OK) {
        return status;
    }

    if (result == map) {
        d7_hamt_node_release(&map->policy, map->root);
    }
    result->root = new_root;
    result->count = map->count + (local_added ? 1u : 0u);
    result->policy = map->policy;
    if (added != NULL) {
        *added = local_added;
    }

    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_map_factory_update(
    const d7_hamt_map *map,
    const void *key,
    const d7_hamt_factory_selection *selection,
    d7_hamt_map *result,
    const void **selected_value) {
    if (map == NULL
        || result == NULL
        || selection == NULL
        || selection->add_factory == NULL
        || (selection->operation == D7_HAMT_FACTORY_ADD_OR_UPDATE
            && selection->update_factory == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const uint32_t hash = d7_hamt_get_hash(map, key);
    d7_hamt_node *new_root = NULL;
    const void *local_selected = NULL;
    bool added = false;
    d7_hamt_status status;

    if (map->root == NULL) {
        const void *candidate = NULL;
        status = selection->add_factory(key, selection->add_context, &candidate);
        if (status == D7_HAMT_OK) {
            status = d7_hamt_leaf_create(&map->policy, hash, key, candidate, &new_root);
        }
        if (status == D7_HAMT_OK) {
            local_selected = ((const d7_hamt_leaf_node *)new_root)->value;
            added = true;
        }
    } else {
        status = d7_hamt_node_factory_update(
            &map->policy,
            map->root,
            key,
            hash,
            0,
            selection,
            &added,
            &new_root,
            &local_selected);
    }

    if (status != D7_HAMT_OK) {
        return status;
    }

    if (result == map) {
        d7_hamt_node_release(&map->policy, map->root);
    }
    result->root = new_root;
    result->count = map->count + (added ? 1u : 0u);
    result->policy = map->policy;
    if (selected_value != NULL) {
        *selected_value = local_selected;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_get_or_add(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map_add_factory_fn add_factory,
    void *add_context,
    d7_hamt_map *result,
    const void **selected_value) {
    const d7_hamt_factory_selection selection = {
        D7_HAMT_FACTORY_GET_OR_ADD,
        add_factory,
        add_context,
        NULL,
        NULL
    };
    return d7_hamt_map_factory_update(map, key, &selection, result, selected_value);
}

d7_hamt_status d7_hamt_map_add_or_update(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map_add_factory_fn add_factory,
    void *add_context,
    d7_hamt_map_update_factory_fn update_factory,
    void *update_context,
    d7_hamt_map *result,
    const void **selected_value) {
    const d7_hamt_factory_selection selection = {
        D7_HAMT_FACTORY_ADD_OR_UPDATE,
        add_factory,
        add_context,
        update_factory,
        update_context
    };
    return d7_hamt_map_factory_update(map, key, &selection, result, selected_value);
}

d7_hamt_status d7_hamt_map_remove(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map *result) {
    bool removed = false;
    const void *removed_value = NULL;
    return d7_hamt_map_try_remove(map, key, result, &removed, &removed_value);
}

d7_hamt_status d7_hamt_map_try_remove(
    const d7_hamt_map *map,
    const void *key,
    d7_hamt_map *result,
    bool *removed,
    const void **removed_value) {
    if (map == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    if (removed != NULL) {
        *removed = false;
    }
    if (removed_value != NULL) {
        *removed_value = NULL;
    }

    if (map->root == NULL) {
        *result = d7_hamt_map_clone(map);
        if (removed != NULL) {
            *removed = false;
        }
        if (removed_value != NULL) {
            *removed_value = NULL;
        }
        return D7_HAMT_OK;
    }

    bool local_removed = false;
    const void *local_removed_value = NULL;
    d7_hamt_node *new_root = NULL;
    const d7_hamt_status status = d7_hamt_node_remove(
        &map->policy,
        map->root,
        key,
        d7_hamt_get_hash(map, key),
        0,
        &local_removed,
        &local_removed_value,
        &new_root);
    if (status != D7_HAMT_OK) {
        return status;
    }

    const bool aliased = result == map;
    if (aliased) {
        d7_hamt_node_release(&map->policy, map->root);
    }
    result->root = new_root;
    result->count = map->count - (local_removed ? 1u : 0u);
    result->policy = map->policy;
    if (removed != NULL) {
        *removed = local_removed;
    }
    if (removed_value != NULL) {
        /* The removed value pointer refers into the previous version's nodes.
         * Under aliasing that version's root has already been released above,
         * so an owning value policy may have freed the payload; report NULL
         * instead of a dangling pointer. Callers that need the removed value
         * must pass a distinct result. */
        *removed_value = (local_removed && !aliased) ? local_removed_value : NULL;
    }

    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_clear(const d7_hamt_map *map, d7_hamt_map *result) {
    if (map == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    if (map->count == 0) {
        *result = d7_hamt_map_clone(map);
    } else {
        if (result == map) {
            d7_hamt_node_release(&map->policy, map->root);
        }
        result->root = NULL;
        result->count = 0;
        result->policy = map->policy;
    }

    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_map_combine(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_combine_operation operation,
    d7_hamt_map *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_map normalized_right;
    const d7_hamt_map *compatible_right = right;
    bool owns_normalized_right = false;
    d7_hamt_status status = D7_HAMT_OK;
    if (!d7_hamt_policy_callbacks_compatible(&left->policy, &right->policy)) {
        normalized_right = d7_hamt_map_create(&left->policy);
        owns_normalized_right = true;
        d7_hamt_map_iterator iterator;
        d7_hamt_map_iterator_init(right, &iterator);
        const void *key = NULL;
        const void *value = NULL;
        while (status == D7_HAMT_OK &&
               d7_hamt_map_iterator_next(&iterator, &key, &value)) {
            status = d7_hamt_map_set(&normalized_right, key, value, &normalized_right);
        }
        compatible_right = &normalized_right;
    }

    d7_hamt_node *root = NULL;
    if (status == D7_HAMT_OK) {
        status = d7_hamt_combine_nodes(
            &left->policy,
            left->root,
            compatible_right->root,
            0,
            operation,
            &root);
    }
    if (owns_normalized_right) {
        d7_hamt_map_destroy(&normalized_right);
    }
    if (status != D7_HAMT_OK) {
        return status;
    }

    const size_t count = root == NULL ? 0 : root->subtree_count;
    if (result == left) {
        d7_hamt_node_release(&left->policy, left->root);
    }
    if (result == right && right != left) {
        d7_hamt_node_release(&right->policy, right->root);
    }
    result->root = root;
    result->count = count;
    result->policy = left->policy;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_union(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result) {
    return d7_hamt_map_combine(left, right, D7_HAMT_COMBINE_UNION, result);
}

d7_hamt_status d7_hamt_map_intersect(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result) {
    return d7_hamt_map_combine(left, right, D7_HAMT_COMBINE_INTERSECT, result);
}

d7_hamt_status d7_hamt_map_except(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result) {
    return d7_hamt_map_combine(left, right, D7_HAMT_COMBINE_EXCEPT, result);
}

d7_hamt_status d7_hamt_map_symmetric_except(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_map *result) {
    return d7_hamt_map_combine(
        left, right, D7_HAMT_COMBINE_SYMMETRIC_EXCEPT, result);
}

void d7_hamt_map_iterator_init(const d7_hamt_map *map, d7_hamt_map_iterator *iterator) {
    if (iterator == NULL) {
        return;
    }

    memset(iterator, 0, sizeof(*iterator));
    iterator->next = map == NULL ? NULL : map->root;
}

bool d7_hamt_map_iterator_next(
    d7_hamt_map_iterator *iterator,
    const void **key,
    const void **value) {
    if (iterator == NULL) {
        return false;
    }

    if (iterator->collision_entries != NULL) {
        if (iterator->collision_index < iterator->collision_count) {
            const d7_hamt_entry *entry = &iterator->collision_entries[iterator->collision_index++];
            if (key != NULL) {
                *key = entry->key;
            }
            if (value != NULL) {
                *value = entry->value;
            }
            return true;
        }

        iterator->collision_entries = NULL;
        iterator->collision_count = 0;
        iterator->collision_index = 0;
    }

    const d7_hamt_node *node = iterator->next;
    iterator->next = NULL;

    for (;;) {
        if (node == NULL) {
            if (iterator->depth == 0) {
                if (key != NULL) {
                    *key = NULL;
                }
                if (value != NULL) {
                    *value = NULL;
                }
                return false;
            }

            d7_hamt_map_iterator_frame *top = &iterator->frames[iterator->depth - 1];
            if (top->data_index < top->data_count) {
                const d7_hamt_inline_entry *data = (const d7_hamt_inline_entry *)top->data;
                const d7_hamt_inline_entry *entry = &data[top->data_index++];
                if (key != NULL) {
                    *key = entry->entry.key;
                }
                if (value != NULL) {
                    *value = entry->entry.value;
                }
                return true;
            }
            if (top->child_index == top->child_count) {
                memset(top, 0, sizeof(*top));
                --iterator->depth;
                continue;
            }

            node = top->children[top->child_index++];
        }

        if (node->kind == D7_HAMT_NODE_LEAF) {
            const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
            if (key != NULL) {
                *key = leaf->key;
            }
            if (value != NULL) {
                *value = leaf->value;
            }
            return true;
        }

        if (node->kind == D7_HAMT_NODE_COLLISION) {
            const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)node;
            iterator->collision_entries = collision->entries;
            iterator->collision_count = collision->count;
            iterator->collision_index = 0;
            return d7_hamt_map_iterator_next(iterator, key, value);
        }

        const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
        assert(iterator->depth < 7);
        iterator->frames[iterator->depth].data = d7_hamt_bitmap_data_const(branch);
        iterator->frames[iterator->depth].data_count = branch->data_count;
        iterator->frames[iterator->depth].data_index = 0;
        iterator->frames[iterator->depth].children =
            (const d7_hamt_node *const *)d7_hamt_bitmap_children_const(branch);
        iterator->frames[iterator->depth].child_count = branch->node_count;
        iterator->frames[iterator->depth].child_index = 0;
        ++iterator->depth;
        node = NULL;
    }
}

bool d7_hamt_map_shares_root(const d7_hamt_map *left, const d7_hamt_map *right) {
    return left != NULL && right != NULL && left->root == right->root;
}

static bool d7_hamt_policies_compatible(const d7_hamt_map *left, const d7_hamt_map *right) {
    return left->policy.hash == right->policy.hash
        && left->policy.key_equal == right->policy.key_equal
        && left->policy.value_equal == right->policy.value_equal
        && left->policy.context == right->policy.context;
}

static bool d7_hamt_nodes_equal(
    const d7_hamt_node *left,
    const d7_hamt_node *right,
    const d7_hamt_policy *policy) {
    if (left == right) {
        return true;
    }
    if (left == NULL || right == NULL || left->kind != right->kind) {
        return false;
    }
    if (left->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *l = (const d7_hamt_leaf_node *)left;
        const d7_hamt_leaf_node *r = (const d7_hamt_leaf_node *)right;
        return l->hash == r->hash
            && d7_hamt_keys_equal(policy, l->key, r->key)
            && d7_hamt_values_equal(policy, l->value, r->value);
    }
    if (left->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *l = (const d7_hamt_collision_node *)left;
        const d7_hamt_collision_node *r = (const d7_hamt_collision_node *)right;
        if (l->hash != r->hash || l->count != r->count) {
            return false;
        }
        for (size_t left_index = 0; left_index != l->count; ++left_index) {
            bool found = false;
            for (size_t right_index = 0; right_index != r->count; ++right_index) {
                if (d7_hamt_keys_equal(
                        policy,
                        l->entries[left_index].key,
                        r->entries[right_index].key)
                    && d7_hamt_values_equal(
                        policy,
                        l->entries[left_index].value,
                        r->entries[right_index].value)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    const d7_hamt_bitmap_node *l = (const d7_hamt_bitmap_node *)left;
    const d7_hamt_bitmap_node *r = (const d7_hamt_bitmap_node *)right;
    if (l->data_map != r->data_map || l->node_map != r->node_map
        || l->data_count != r->data_count || l->node_count != r->node_count) {
        return false;
    }
    const d7_hamt_inline_entry *left_data = d7_hamt_bitmap_data_const(l);
    const d7_hamt_inline_entry *right_data = d7_hamt_bitmap_data_const(r);
    for (size_t index = 0; index != l->data_count; ++index) {
        if (left_data[index].hash != right_data[index].hash
            || !d7_hamt_keys_equal(
                policy, left_data[index].entry.key, right_data[index].entry.key)
            || !d7_hamt_values_equal(
                policy, left_data[index].entry.value, right_data[index].entry.value)) {
            return false;
        }
    }
    d7_hamt_node *const *left_children = d7_hamt_bitmap_children_const(l);
    d7_hamt_node *const *right_children = d7_hamt_bitmap_children_const(r);
    for (size_t index = 0; index != l->node_count; ++index) {
        if (!d7_hamt_nodes_equal(left_children[index], right_children[index], policy)) {
            return false;
        }
    }
    return true;
}

static bool d7_hamt_diff_operand_is_empty(d7_hamt_diff_operand operand) {
    return operand.node == NULL && operand.inline_entry == NULL;
}

static bool d7_hamt_diff_operand_is_run(d7_hamt_diff_operand operand) {
    return operand.inline_entry != NULL
        || (operand.node != NULL && operand.node->kind != D7_HAMT_NODE_BITMAP_INDEXED);
}

static d7_hamt_entry_run_view d7_hamt_entry_run_from_operand(d7_hamt_diff_operand operand) {
    d7_hamt_entry_run_view result = { 0 };
    result.is_single = true;
    result.count = 1;
    if (operand.inline_entry != NULL) {
        result.hash = operand.inline_entry->hash;
        result.single = operand.inline_entry->entry;
        return result;
    }

    assert(operand.node != NULL);
    if (operand.node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)operand.node;
        result.hash = leaf->hash;
        result.single.key = leaf->key;
        result.single.value = leaf->value;
        return result;
    }

    assert(operand.node->kind == D7_HAMT_NODE_COLLISION);
    const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)operand.node;
    result.hash = collision->hash;
    result.count = collision->count;
    result.entries = collision->entries;
    result.is_single = false;
    return result;
}

static const d7_hamt_entry *d7_hamt_entry_run_at(
    const d7_hamt_entry_run_view *run,
    size_t index) {
    assert(index < run->count);
    return run->is_single ? &run->single : &run->entries[index];
}

static d7_hamt_diff_operand d7_hamt_diff_operand_logical_slot(
    d7_hamt_diff_operand operand,
    int index,
    int shift) {
    if (d7_hamt_diff_operand_is_empty(operand)) {
        return operand;
    }
    if (d7_hamt_diff_operand_is_run(operand)) {
        assert(shift < 32);
        const d7_hamt_entry_run_view run = d7_hamt_entry_run_from_operand(operand);
        return d7_hamt_index(run.hash, shift) == index
            ? operand
            : (d7_hamt_diff_operand){ NULL, NULL };
    }

    assert(operand.node->kind == D7_HAMT_NODE_BITMAP_INDEXED);
    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)operand.node;
    const uint32_t selected_bit = d7_hamt_bit(index);
    if ((branch->data_map & selected_bit) != 0) {
        const d7_hamt_inline_entry *data = d7_hamt_bitmap_data_const(branch);
        return (d7_hamt_diff_operand){
            NULL,
            &data[d7_hamt_slot(branch->data_map, selected_bit)] };
    }
    if ((branch->node_map & selected_bit) != 0) {
        d7_hamt_node *const *children = d7_hamt_bitmap_children_const(branch);
        return (d7_hamt_diff_operand){
            children[d7_hamt_slot(branch->node_map, selected_bit)],
            NULL };
    }
    return (d7_hamt_diff_operand){ NULL, NULL };
}

static void d7_hamt_emit_difference(
    d7_hamt_difference_kind kind,
    const void *key,
    const void *before,
    const void *after,
    d7_hamt_difference_visitor visitor,
    void *context) {
    const d7_hamt_difference difference = { kind, key, before, after };
    visitor(&difference, context);
}

static void d7_hamt_append_diff_operand(
    d7_hamt_diff_operand operand,
    bool added,
    d7_hamt_difference_visitor visitor,
    void *context) {
    if (d7_hamt_diff_operand_is_empty(operand)) {
        return;
    }
    if (d7_hamt_diff_operand_is_run(operand)) {
        const d7_hamt_entry_run_view run = d7_hamt_entry_run_from_operand(operand);
        for (size_t index = 0; index != run.count; ++index) {
            const d7_hamt_entry *entry = d7_hamt_entry_run_at(&run, index);
            d7_hamt_emit_difference(
                added ? D7_HAMT_DIFFERENCE_ADDED : D7_HAMT_DIFFERENCE_REMOVED,
                entry->key,
                added ? NULL : entry->value,
                added ? entry->value : NULL,
                visitor,
                context);
        }
        return;
    }

    for (int index = 0; index != D7_HAMT_BRANCH_MASK + 1; ++index) {
        d7_hamt_append_diff_operand(
            d7_hamt_diff_operand_logical_slot(operand, index, 0),
            added,
            visitor,
            context);
    }
}

static size_t d7_hamt_entry_run_find(
    const d7_hamt_entry_run_view *run,
    const void *key,
    const d7_hamt_policy *policy) {
    for (size_t index = 0; index != run->count; ++index) {
        if (d7_hamt_keys_equal(policy, d7_hamt_entry_run_at(run, index)->key, key)) {
            return index;
        }
    }
    return SIZE_MAX;
}

static void d7_hamt_diff_entry_runs(
    const d7_hamt_entry_run_view *left,
    const d7_hamt_entry_run_view *right,
    const d7_hamt_policy *policy,
    d7_hamt_difference_visitor visitor,
    void *context) {
    if (left->hash != right->hash) {
        for (size_t index = 0; index != left->count; ++index) {
            const d7_hamt_entry *entry = d7_hamt_entry_run_at(left, index);
            d7_hamt_emit_difference(
                D7_HAMT_DIFFERENCE_REMOVED,
                entry->key,
                entry->value,
                NULL,
                visitor,
                context);
        }
        for (size_t index = 0; index != right->count; ++index) {
            const d7_hamt_entry *entry = d7_hamt_entry_run_at(right, index);
            d7_hamt_emit_difference(
                D7_HAMT_DIFFERENCE_ADDED,
                entry->key,
                NULL,
                entry->value,
                visitor,
                context);
        }
        return;
    }

    for (size_t left_index = 0; left_index != left->count; ++left_index) {
        const d7_hamt_entry *before = d7_hamt_entry_run_at(left, left_index);
        const size_t right_index = d7_hamt_entry_run_find(right, before->key, policy);
        if (right_index == SIZE_MAX) {
            d7_hamt_emit_difference(
                D7_HAMT_DIFFERENCE_REMOVED,
                before->key,
                before->value,
                NULL,
                visitor,
                context);
            continue;
        }
        const d7_hamt_entry *after = d7_hamt_entry_run_at(right, right_index);
        if (!d7_hamt_values_equal(policy, before->value, after->value)) {
            d7_hamt_emit_difference(
                D7_HAMT_DIFFERENCE_CHANGED,
                before->key,
                before->value,
                after->value,
                visitor,
                context);
        }
    }
    for (size_t right_index = 0; right_index != right->count; ++right_index) {
        const d7_hamt_entry *after = d7_hamt_entry_run_at(right, right_index);
        if (d7_hamt_entry_run_find(left, after->key, policy) == SIZE_MAX) {
            d7_hamt_emit_difference(
                D7_HAMT_DIFFERENCE_ADDED,
                after->key,
                NULL,
                after->value,
                visitor,
                context);
        }
    }
}

static void d7_hamt_diff_operands(
    d7_hamt_diff_operand left,
    d7_hamt_diff_operand right,
    int shift,
    const d7_hamt_policy *policy,
    d7_hamt_difference_visitor visitor,
    void *context) {
    if (left.node != NULL && left.node == right.node
        && left.inline_entry == NULL && right.inline_entry == NULL) {
        return;
    }
    if (left.inline_entry != NULL && left.inline_entry == right.inline_entry
        && left.node == NULL && right.node == NULL) {
        return;
    }
    if (d7_hamt_diff_operand_is_empty(left)) {
        d7_hamt_append_diff_operand(right, true, visitor, context);
        return;
    }
    if (d7_hamt_diff_operand_is_empty(right)) {
        d7_hamt_append_diff_operand(left, false, visitor, context);
        return;
    }
    if (d7_hamt_diff_operand_is_run(left) && d7_hamt_diff_operand_is_run(right)) {
        const d7_hamt_entry_run_view left_run = d7_hamt_entry_run_from_operand(left);
        const d7_hamt_entry_run_view right_run = d7_hamt_entry_run_from_operand(right);
        d7_hamt_diff_entry_runs(&left_run, &right_run, policy, visitor, context);
        return;
    }

    assert(shift < 32);
    for (int index = 0; index != D7_HAMT_BRANCH_MASK + 1; ++index) {
        d7_hamt_diff_operands(
            d7_hamt_diff_operand_logical_slot(left, index, shift),
            d7_hamt_diff_operand_logical_slot(right, index, shift),
            shift + D7_HAMT_BITS_PER_LEVEL,
            policy,
            visitor,
            context);
    }
}

bool d7_hamt_map_equals(const d7_hamt_map *left, const d7_hamt_map *right) {
    if (left == NULL || right == NULL || !d7_hamt_policies_compatible(left, right)) {
        return false;
    }
    if (left->root == right->root) {
        return true;
    }
    if (left->count != right->count) {
        return false;
    }
    return d7_hamt_nodes_equal(left->root, right->root, &left->policy);
}

d7_hamt_status d7_hamt_map_diff(
    const d7_hamt_map *left,
    const d7_hamt_map *right,
    d7_hamt_difference_visitor visitor,
    void *context) {
    if (left == NULL || right == NULL || visitor == NULL
        || !d7_hamt_policies_compatible(left, right)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (left->root == right->root) {
        return D7_HAMT_OK;
    }
    d7_hamt_diff_operands(
        (d7_hamt_diff_operand){ left->root, NULL },
        (d7_hamt_diff_operand){ right->root, NULL },
        0,
        &left->policy,
        visitor,
        context);
    return D7_HAMT_OK;
}

const void *d7_hamt_map_debug_root_identity(const d7_hamt_map *map) {
    return map == NULL ? NULL : map->root;
}

d7_hamt_node_kind d7_hamt_map_debug_root_kind(const d7_hamt_map *map) {
    if (map == NULL || map->root == NULL) {
        return D7_HAMT_NODE_EMPTY;
    }

    return map->root->kind;
}

size_t d7_hamt_map_debug_root_child_identities(
    const d7_hamt_map *map,
    const void **children,
    size_t child_capacity) {
    if (map == NULL || map->root == NULL || map->root->kind != D7_HAMT_NODE_BITMAP_INDEXED) {
        return 0;
    }

    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)map->root;
    if (children == NULL) {
        return branch->node_count;
    }

    const size_t copy_count = branch->node_count < child_capacity ? branch->node_count : child_capacity;
    d7_hamt_node *const *branch_children = d7_hamt_bitmap_children_const(branch);
    for (size_t i = 0; i < copy_count; ++i) {
        children[i] = branch_children[i];
    }

    return branch->node_count;
}

static bool d7_hamt_debug_hash_has_prefix(uint32_t hash, uint32_t prefix, uint32_t mask) {
    return (hash & mask) == prefix;
}

static uint32_t d7_hamt_debug_next_prefix_mask(int shift) {
    return shift >= 27 ? UINT32_MAX : (d7_hamt_bit(shift + D7_HAMT_BITS_PER_LEVEL) - 1u);
}

static bool d7_hamt_debug_validate_node(
    const d7_hamt_node *node,
    int shift,
    uint32_t prefix,
    uint32_t prefix_mask,
    size_t *entry_count) {
    if (node == NULL) {
        *entry_count = 0;
        return true;
    }
    if (node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
        *entry_count = 1;
        return node->subtree_count == 1 &&
            d7_hamt_debug_hash_has_prefix(leaf->hash, prefix, prefix_mask);
    }
    if (node->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)node;
        *entry_count = collision->count;
        return collision->count >= 2 && node->subtree_count == collision->count &&
            d7_hamt_debug_hash_has_prefix(collision->hash, prefix, prefix_mask);
    }

    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
    if (shift > 30 ||
        (branch->data_map & branch->node_map) != 0 ||
        d7_hamt_popcount(branch->data_map) != branch->data_count ||
        d7_hamt_popcount(branch->node_map) != branch->node_count ||
        branch->data_count + branch->node_count == 0) {
        return false;
    }
    const uint32_t next_mask = d7_hamt_debug_next_prefix_mask(shift);
    const d7_hamt_inline_entry *data = d7_hamt_bitmap_data_const(branch);
    d7_hamt_node *const *children = d7_hamt_bitmap_children_const(branch);
    if (branch->data_count + branch->node_count < 2 &&
        !(branch->data_count == 0 && branch->node_count == 1 &&
          children[0]->kind == D7_HAMT_NODE_BITMAP_INDEXED)) {
        return false;
    }
    size_t total = branch->data_count;
    size_t data_index = 0;
    size_t child_index = 0;
    for (int slot = 0; slot != D7_HAMT_BRANCH_MASK + 1; ++slot) {
        const uint32_t slot_bit = d7_hamt_bit(slot);
        if (shift == 30 && slot > 3 && ((branch->data_map | branch->node_map) & slot_bit) != 0) {
            return false;
        }
        const uint32_t slot_prefix = prefix | ((uint32_t)slot << shift);
        if ((branch->data_map & slot_bit) != 0) {
            if (data_index >= branch->data_count ||
                !d7_hamt_debug_hash_has_prefix(data[data_index].hash, slot_prefix, next_mask)) {
                return false;
            }
            ++data_index;
        }
        if ((branch->node_map & slot_bit) != 0) {
            size_t child_count = 0;
            if (child_index >= branch->node_count ||
                children[child_index]->kind == D7_HAMT_NODE_LEAF ||
                !d7_hamt_debug_validate_node(
                    children[child_index], shift + D7_HAMT_BITS_PER_LEVEL,
                    slot_prefix, next_mask, &child_count) ||
                SIZE_MAX - total < child_count) {
                return false;
            }
            total += child_count;
            ++child_index;
        }
    }
    if (data_index != branch->data_count || child_index != branch->node_count) {
        return false;
    }
    *entry_count = total;
    return node->subtree_count == total;
}

bool d7_hamt_map_debug_validate_canonical(const d7_hamt_map *map) {
    size_t entries = 0;
    return map != NULL &&
        d7_hamt_debug_validate_node(map->root, 0, 0, 0, &entries) &&
        entries == map->count;
}

static bool d7_hamt_debug_nodes_topology_equal(
    const d7_hamt_node *left,
    const d7_hamt_node *right,
    const d7_hamt_policy *policy) {
    if (left == NULL || right == NULL) {
        return left == right;
    }
    if (left->kind != right->kind) {
        return false;
    }
    if (left->kind == D7_HAMT_NODE_LEAF) {
        return ((const d7_hamt_leaf_node *)left)->hash == ((const d7_hamt_leaf_node *)right)->hash;
    }
    if (left->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *l = (const d7_hamt_collision_node *)left;
        const d7_hamt_collision_node *r = (const d7_hamt_collision_node *)right;
        if (l->hash != r->hash || l->count != r->count) {
            return false;
        }
        for (size_t left_index = 0; left_index != l->count; ++left_index) {
            bool found = false;
            for (size_t right_index = 0; right_index != r->count; ++right_index) {
                if (d7_hamt_keys_equal(
                        policy,
                        l->entries[left_index].key,
                        r->entries[right_index].key)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }
    const d7_hamt_bitmap_node *l = (const d7_hamt_bitmap_node *)left;
    const d7_hamt_bitmap_node *r = (const d7_hamt_bitmap_node *)right;
    if (l->data_map != r->data_map || l->node_map != r->node_map ||
        l->data_count != r->data_count || l->node_count != r->node_count) {
        return false;
    }
    const d7_hamt_inline_entry *left_data = d7_hamt_bitmap_data_const(l);
    const d7_hamt_inline_entry *right_data = d7_hamt_bitmap_data_const(r);
    for (size_t index = 0; index != l->data_count; ++index) {
        if (left_data[index].hash != right_data[index].hash) {
            return false;
        }
    }
    d7_hamt_node *const *left_children = d7_hamt_bitmap_children_const(l);
    d7_hamt_node *const *right_children = d7_hamt_bitmap_children_const(r);
    for (size_t index = 0; index != l->node_count; ++index) {
        if (!d7_hamt_debug_nodes_topology_equal(
                left_children[index], right_children[index], policy)) {
            return false;
        }
    }
    return true;
}

bool d7_hamt_map_debug_topology_equal(const d7_hamt_map *left, const d7_hamt_map *right) {
    return left != NULL && right != NULL && d7_hamt_policies_compatible(left, right) &&
        d7_hamt_debug_nodes_topology_equal(left->root, right->root, &left->policy);
}

d7_hamt_set d7_hamt_set_create(const d7_hamt_set_policy *policy) {
    d7_hamt_set set;
    const d7_hamt_policy map_policy = d7_hamt_map_policy_from_set_policy(policy);
    set.map = d7_hamt_map_create(&map_policy);
    return set;
}

d7_hamt_status d7_hamt_set_create_range(
    const d7_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result) {
    if (result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set set = d7_hamt_set_create(policy);
    for (size_t i = 0; i < item_count; ++i) {
        d7_hamt_set next;
        const d7_hamt_status status = d7_hamt_set_add(&set, items[i], &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_set_destroy(&set);
            return status;
        }

        d7_hamt_set_destroy(&set);
        set = next;
    }

    *result = set;
    return D7_HAMT_OK;
}

d7_hamt_set d7_hamt_set_clone(const d7_hamt_set *set) {
    d7_hamt_set clone;
    clone.map = set == NULL ? d7_hamt_map_create(NULL) : d7_hamt_map_clone(&set->map);
    return clone;
}

void d7_hamt_set_destroy(d7_hamt_set *set) {
    if (set != NULL) {
        d7_hamt_map_destroy(&set->map);
    }
}

size_t d7_hamt_set_count(const d7_hamt_set *set) {
    return set == NULL ? 0 : set->map.count;
}

bool d7_hamt_set_is_empty(const d7_hamt_set *set) {
    return d7_hamt_set_count(set) == 0;
}

bool d7_hamt_set_contains(const d7_hamt_set *set, const void *item) {
    return set != NULL && d7_hamt_map_contains_key(&set->map, item);
}

bool d7_hamt_set_try_get_value(const d7_hamt_set *set, const void *equal_value, const void **actual_value) {
    return set != NULL && d7_hamt_map_try_get_key(&set->map, equal_value, actual_value);
}

d7_hamt_status d7_hamt_set_add(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result) {
    if (set == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_map map;
    const d7_hamt_status status = d7_hamt_map_set(&set->map, item, NULL, &map);
    if (status == D7_HAMT_OK) {
        if (result == set) {
            d7_hamt_node_release(&set->map.policy, set->map.root);
        }
        result->map = map;
    }

    return status;
}

d7_hamt_status d7_hamt_set_try_add(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result,
    bool *added) {
    if (set == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_map map;
    const d7_hamt_status status = d7_hamt_map_try_add(&set->map, item, NULL, &map, added);
    if (status == D7_HAMT_OK) {
        if (result == set) {
            d7_hamt_node_release(&set->map.policy, set->map.root);
        }
        result->map = map;
    }

    return status;
}

d7_hamt_status d7_hamt_set_remove(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result) {
    bool removed = false;
    return d7_hamt_set_try_remove(set, item, result, &removed);
}

d7_hamt_status d7_hamt_set_try_remove(
    const d7_hamt_set *set,
    const void *item,
    d7_hamt_set *result,
    bool *removed) {
    if (set == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    if (removed != NULL) {
        *removed = false;
    }

    d7_hamt_map map;
    bool local_removed = false;
    const void *removed_value = NULL;
    const d7_hamt_status status = d7_hamt_map_try_remove(
        &set->map,
        item,
        &map,
        &local_removed,
        &removed_value);
    if (status == D7_HAMT_OK) {
        if (result == set) {
            d7_hamt_node_release(&set->map.policy, set->map.root);
        }
        result->map = map;
        if (removed != NULL) {
            *removed = local_removed;
        }
    }

    return status;
}

d7_hamt_status d7_hamt_set_clear(const d7_hamt_set *set, d7_hamt_set *result) {
    if (set == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    return d7_hamt_map_clear(&set->map, &result->map);
}

d7_hamt_status d7_hamt_set_union(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_union(&left->map, &right->map, &result->map);
}

d7_hamt_status d7_hamt_set_intersect(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_intersect(&left->map, &right->map, &result->map);
}

d7_hamt_status d7_hamt_set_except(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_except(&left->map, &right->map, &result->map);
}

d7_hamt_status d7_hamt_set_symmetric_except(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    d7_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_symmetric_except(&left->map, &right->map, &result->map);
}

d7_hamt_status d7_hamt_set_union_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set current = d7_hamt_set_clone(set);
    for (size_t i = 0; i < item_count; ++i) {
        d7_hamt_set next;
        const d7_hamt_status status = d7_hamt_set_add(&current, items[i], &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_set_destroy(&current);
            return status;
        }

        d7_hamt_set_destroy(&current);
        current = next;
    }

    if (result == set) {
        d7_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = current;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_intersect_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set_policy policy = d7_hamt_normalize_set_policy(&(d7_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    d7_hamt_set probe;
    d7_hamt_status status = d7_hamt_set_create_range(&policy, items, item_count, &probe);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_set intersection = d7_hamt_set_create(&policy);
    d7_hamt_set_iterator iterator;
    d7_hamt_set_iterator_init(set, &iterator);
    const void *item = NULL;
    while (d7_hamt_set_iterator_next(&iterator, &item)) {
        if (!d7_hamt_set_contains(&probe, item)) {
            continue;
        }

        d7_hamt_set next;
        status = d7_hamt_set_add(&intersection, item, &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_set_destroy(&probe);
            d7_hamt_set_destroy(&intersection);
            return status;
        }

        d7_hamt_set_destroy(&intersection);
        intersection = next;
    }

    d7_hamt_set_destroy(&probe);
    if (result == set) {
        d7_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = intersection;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_except_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set current = d7_hamt_set_clone(set);
    for (size_t i = 0; i < item_count; ++i) {
        d7_hamt_set next;
        const d7_hamt_status status = d7_hamt_set_remove(&current, items[i], &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_set_destroy(&current);
            return status;
        }

        d7_hamt_set_destroy(&current);
        current = next;
    }

    if (result == set) {
        d7_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = current;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_symmetric_except_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set_policy policy = d7_hamt_normalize_set_policy(&(d7_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    d7_hamt_set toggles;
    d7_hamt_status status = d7_hamt_set_create_range(&policy, items, item_count, &toggles);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_set current = d7_hamt_set_clone(set);
    d7_hamt_set_iterator iterator;
    d7_hamt_set_iterator_init(&toggles, &iterator);
    const void *item = NULL;
    while (d7_hamt_set_iterator_next(&iterator, &item)) {
        d7_hamt_set next;
        status = d7_hamt_set_contains(&current, item)
            ? d7_hamt_set_remove(&current, item, &next)
            : d7_hamt_set_add(&current, item, &next);
        if (status != D7_HAMT_OK) {
            d7_hamt_set_destroy(&toggles);
            d7_hamt_set_destroy(&current);
            return status;
        }

        d7_hamt_set_destroy(&current);
        current = next;
    }

    d7_hamt_set_destroy(&toggles);
    if (result == set) {
        d7_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = current;
    return D7_HAMT_OK;
}

/* Builds the deduplicating probe set the relation predicates compare against. */
static d7_hamt_status d7_hamt_set_build_probe(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    d7_hamt_set *probe) {
    d7_hamt_set_policy policy = d7_hamt_normalize_set_policy(&(d7_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    return d7_hamt_set_create_range(&policy, items, item_count, probe);
}

static bool d7_hamt_set_contains_all_of(const d7_hamt_set *container, const d7_hamt_set *contained) {
    d7_hamt_set_iterator iterator;
    d7_hamt_set_iterator_init(contained, &iterator);
    const void *item = NULL;
    while (d7_hamt_set_iterator_next(&iterator, &item)) {
        if (!d7_hamt_set_contains(container, item)) {
            return false;
        }
    }

    return true;
}

d7_hamt_status d7_hamt_set_is_subset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    d7_hamt_set probe;
    const d7_hamt_status status = d7_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != D7_HAMT_OK) {
        return status;
    }

    *result = d7_hamt_set_contains_all_of(&probe, set);
    d7_hamt_set_destroy(&probe);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_is_proper_subset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    d7_hamt_set probe;
    const d7_hamt_status status = d7_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != D7_HAMT_OK) {
        return status;
    }

    *result = d7_hamt_set_count(set) < d7_hamt_set_count(&probe)
        && d7_hamt_set_contains_all_of(&probe, set);
    d7_hamt_set_destroy(&probe);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_is_superset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    *result = true;
    for (size_t i = 0; i < item_count; ++i) {
        if (!d7_hamt_set_contains(set, items[i])) {
            *result = false;
            break;
        }
    }

    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_is_proper_superset_of_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    d7_hamt_set probe;
    const d7_hamt_status status = d7_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != D7_HAMT_OK) {
        return status;
    }

    *result = d7_hamt_set_count(&probe) < d7_hamt_set_count(set)
        && d7_hamt_set_contains_all_of(set, &probe);
    d7_hamt_set_destroy(&probe);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_overlaps_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    for (size_t i = 0; i < item_count; ++i) {
        if (d7_hamt_set_contains(set, items[i])) {
            *result = true;
            break;
        }
    }

    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_equals_many(
    const d7_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    d7_hamt_set probe;
    const d7_hamt_status status = d7_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != D7_HAMT_OK) {
        return status;
    }

    *result = d7_hamt_set_count(set) == d7_hamt_set_count(&probe)
        && d7_hamt_set_contains_all_of(&probe, set);
    d7_hamt_set_destroy(&probe);
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_set_structural_relation_parts(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool need_union,
    d7_hamt_set *intersection,
    d7_hamt_set *united) {
    d7_hamt_status status = d7_hamt_set_intersect(left, right, intersection);
    if (status != D7_HAMT_OK || !need_union) {
        return status;
    }
    status = d7_hamt_set_union(left, right, united);
    if (status != D7_HAMT_OK) {
        d7_hamt_set_destroy(intersection);
    }
    return status;
}

d7_hamt_status d7_hamt_set_is_subset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    d7_hamt_set intersection;
    const d7_hamt_status status = d7_hamt_set_structural_relation_parts(
        left, right, false, &intersection, NULL);
    if (status == D7_HAMT_OK) {
        *result = intersection.map.count == left->map.count;
        d7_hamt_set_destroy(&intersection);
    }
    return status;
}

d7_hamt_status d7_hamt_set_is_proper_subset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    d7_hamt_set intersection;
    d7_hamt_set united;
    const d7_hamt_status status = d7_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == D7_HAMT_OK) {
        *result = intersection.map.count == left->map.count &&
            united.map.count > left->map.count;
        d7_hamt_set_destroy(&intersection);
        d7_hamt_set_destroy(&united);
    }
    return status;
}

d7_hamt_status d7_hamt_set_is_superset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    d7_hamt_set intersection;
    d7_hamt_set united;
    const d7_hamt_status status = d7_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == D7_HAMT_OK) {
        *result = united.map.count == left->map.count;
        d7_hamt_set_destroy(&intersection);
        d7_hamt_set_destroy(&united);
    }
    return status;
}

d7_hamt_status d7_hamt_set_is_proper_superset_of(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    d7_hamt_set intersection;
    d7_hamt_set united;
    const d7_hamt_status status = d7_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == D7_HAMT_OK) {
        *result = united.map.count == left->map.count &&
            intersection.map.count < left->map.count;
        d7_hamt_set_destroy(&intersection);
        d7_hamt_set_destroy(&united);
    }
    return status;
}

d7_hamt_status d7_hamt_set_overlaps(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    d7_hamt_set intersection;
    const d7_hamt_status status = d7_hamt_set_structural_relation_parts(
        left, right, false, &intersection, NULL);
    if (status == D7_HAMT_OK) {
        *result = intersection.map.count != 0;
        d7_hamt_set_destroy(&intersection);
    }
    return status;
}

d7_hamt_status d7_hamt_set_equals(
    const d7_hamt_set *left,
    const d7_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    if (left->map.root == right->map.root) {
        *result = true;
        return D7_HAMT_OK;
    }
    d7_hamt_set intersection;
    d7_hamt_set united;
    const d7_hamt_status status = d7_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == D7_HAMT_OK) {
        *result = intersection.map.count == left->map.count &&
            united.map.count == left->map.count;
        d7_hamt_set_destroy(&intersection);
        d7_hamt_set_destroy(&united);
    }
    return status;
}

void d7_hamt_set_iterator_init(const d7_hamt_set *set, d7_hamt_set_iterator *iterator) {
    if (iterator != NULL) {
        d7_hamt_map_iterator_init(set == NULL ? NULL : &set->map, &iterator->inner);
    }
}

bool d7_hamt_set_iterator_next(d7_hamt_set_iterator *iterator, const void **item) {
    const void *value = NULL;
    return iterator != NULL && d7_hamt_map_iterator_next(&iterator->inner, item, &value);
}

bool d7_hamt_set_shares_root(const d7_hamt_set *left, const d7_hamt_set *right) {
    return left != NULL && right != NULL && d7_hamt_map_shares_root(&left->map, &right->map);
}

const void *d7_hamt_set_debug_root_identity(const d7_hamt_set *set) {
    return set == NULL ? NULL : d7_hamt_map_debug_root_identity(&set->map);
}

d7_hamt_node_kind d7_hamt_set_debug_root_kind(const d7_hamt_set *set) {
    return set == NULL ? D7_HAMT_NODE_EMPTY : d7_hamt_map_debug_root_kind(&set->map);
}

static d7_hamt_status d7_hamt_map_transient_active_state(
    const d7_hamt_map_transient *transient,
    struct d7_hamt_map_transient_state **state) {
    if (transient == NULL || transient->state == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (!transient->state->active) {
        return D7_HAMT_TRANSIENT_CONSUMED;
    }

    if (state != NULL) {
        *state = transient->state;
    }
    return D7_HAMT_OK;
}

static void d7_hamt_map_transient_commit(
    struct d7_hamt_map_transient_state *state,
    d7_hamt_map *next) {
    const bool changed = state->map.root != next->root;
    d7_hamt_map_destroy(&state->map);
    state->map = *next;
    if (changed) {
        ++state->version;
    }
}

d7_hamt_status d7_hamt_map_transient_create(
    const d7_hamt_policy *policy,
    d7_hamt_map_transient *result) {
    if (result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const d7_hamt_map empty = d7_hamt_map_create(policy);
    return d7_hamt_map_to_transient(&empty, result);
}

d7_hamt_status d7_hamt_map_to_transient(
    const d7_hamt_map *map,
    d7_hamt_map_transient *result) {
    if (map == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    struct d7_hamt_map_transient_state *state =
        (struct d7_hamt_map_transient_state *)d7_hamt_allocate(sizeof(*state));
    if (state == NULL) {
        return D7_HAMT_OUT_OF_MEMORY;
    }

    state->ref_count = 1;
    state->version = 0;
    state->active = true;
    state->map = d7_hamt_map_clone(map);

    d7_hamt_map_transient transient;
    transient.state = state;
    *result = transient;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_clone(
    const d7_hamt_map_transient *transient,
    d7_hamt_map_transient *result) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || result == NULL || result == transient) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }
    if (state->ref_count == SIZE_MAX) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    ++state->ref_count;
    d7_hamt_map_transient clone;
    clone.state = state;
    *result = clone;
    return D7_HAMT_OK;
}

void d7_hamt_map_transient_destroy(d7_hamt_map_transient *transient) {
    if (transient == NULL || transient->state == NULL) {
        return;
    }

    struct d7_hamt_map_transient_state *state = transient->state;
    transient->state = NULL;
    assert(state->ref_count > 0);
    --state->ref_count;
    if (state->ref_count == 0) {
        d7_hamt_map_destroy(&state->map);
        free(state);
    }
}

bool d7_hamt_map_transient_is_active(const d7_hamt_map_transient *transient) {
    return transient != NULL && transient->state != NULL && transient->state->active;
}

d7_hamt_status d7_hamt_map_transient_get_policy(
    const d7_hamt_map_transient *transient,
    d7_hamt_policy *policy) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || policy == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    *policy = state->map.policy;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_count(
    const d7_hamt_map_transient *transient,
    size_t *count) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || count == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    *count = state->map.count;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_contains_key(
    const d7_hamt_map_transient *transient,
    const void *key,
    bool *contains) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || contains == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    *contains = d7_hamt_map_contains_key(&state->map, key);
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_try_get(
    const d7_hamt_map_transient *transient,
    const void *key,
    bool *found,
    const void **value) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || found == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    const void *local_value = NULL;
    const bool local_found = d7_hamt_map_try_get(&state->map, key, &local_value);
    *found = local_found;
    if (value != NULL) {
        *value = local_value;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_try_get_key(
    const d7_hamt_map_transient *transient,
    const void *equal_key,
    bool *found,
    const void **actual_key) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || found == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    const void *local_key = NULL;
    const bool local_found = d7_hamt_map_try_get_key(&state->map, equal_key, &local_key);
    *found = local_found;
    if (actual_key != NULL) {
        *actual_key = local_key;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_set(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value) {
    struct d7_hamt_map_transient_state *state = NULL;
    d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_map next;
    status = d7_hamt_map_set(&state->map, key, value, &next);
    if (status == D7_HAMT_OK) {
        d7_hamt_map_transient_commit(state, &next);
    }
    return status;
}

d7_hamt_status d7_hamt_map_transient_add(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value) {
    struct d7_hamt_map_transient_state *state = NULL;
    d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_map next;
    status = d7_hamt_map_add(&state->map, key, value, &next);
    if (status == D7_HAMT_OK) {
        d7_hamt_map_transient_commit(state, &next);
    }
    return status;
}

d7_hamt_status d7_hamt_map_transient_try_add(
    d7_hamt_map_transient *transient,
    const void *key,
    const void *value,
    bool *added) {
    struct d7_hamt_map_transient_state *state = NULL;
    d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK) {
        return status;
    }

    bool local_added = false;
    d7_hamt_map next;
    status = d7_hamt_map_try_add(&state->map, key, value, &next, &local_added);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_map_transient_commit(state, &next);
    if (added != NULL) {
        *added = local_added;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_remove(
    d7_hamt_map_transient *transient,
    const void *key) {
    struct d7_hamt_map_transient_state *state = NULL;
    d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_map next;
    status = d7_hamt_map_remove(&state->map, key, &next);
    if (status == D7_HAMT_OK) {
        d7_hamt_map_transient_commit(state, &next);
    }
    return status;
}

d7_hamt_status d7_hamt_map_transient_try_remove(
    d7_hamt_map_transient *transient,
    const void *key,
    bool *removed) {
    struct d7_hamt_map_transient_state *state = NULL;
    d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK) {
        return status;
    }

    bool local_removed = false;
    const void *removed_value = NULL;
    d7_hamt_map next;
    status = d7_hamt_map_try_remove(
        &state->map,
        key,
        &next,
        &local_removed,
        &removed_value);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_map_transient_commit(state, &next);
    if (removed != NULL) {
        *removed = local_removed;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_clear(d7_hamt_map_transient *transient) {
    struct d7_hamt_map_transient_state *state = NULL;
    d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_map next;
    status = d7_hamt_map_clear(&state->map, &next);
    if (status == D7_HAMT_OK) {
        d7_hamt_map_transient_commit(state, &next);
    }
    return status;
}

d7_hamt_status d7_hamt_map_transient_iterator_init(
    const d7_hamt_map_transient *transient,
    d7_hamt_map_transient_iterator *iterator) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || iterator == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    d7_hamt_map_transient_iterator local;
    local.state = state;
    local.version = state->version;
    d7_hamt_map_iterator_init(&state->map, &local.inner);
    *iterator = local;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_iterator_next(
    d7_hamt_map_transient_iterator *iterator,
    bool *has_value,
    const void **key,
    const void **value) {
    if (iterator == NULL || iterator->state == NULL || has_value == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }
    if (!iterator->state->active) {
        return D7_HAMT_TRANSIENT_CONSUMED;
    }
    if (iterator->version != iterator->state->version) {
        return D7_HAMT_TRANSIENT_MODIFIED;
    }

    const void *local_key = NULL;
    const void *local_value = NULL;
    const bool local_has_value =
        d7_hamt_map_iterator_next(&iterator->inner, &local_key, &local_value);
    *has_value = local_has_value;
    if (key != NULL) {
        *key = local_key;
    }
    if (value != NULL) {
        *value = local_value;
    }
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_map_transient_persist(
    d7_hamt_map_transient *transient,
    d7_hamt_map *result) {
    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status status = d7_hamt_map_transient_active_state(transient, &state);
    if (status != D7_HAMT_OK || result == NULL) {
        return status == D7_HAMT_OK ? D7_HAMT_INVALID_ARGUMENT : status;
    }

    const d7_hamt_map published = state->map;
    state->map.root = NULL;
    state->map.count = 0;
    state->active = false;
    ++state->version;
    *result = published;
    return D7_HAMT_OK;
}

const void *d7_hamt_map_transient_debug_root_identity(
    const d7_hamt_map_transient *transient) {
    return d7_hamt_map_transient_is_active(transient)
        ? transient->state->map.root
        : NULL;
}

d7_hamt_status d7_hamt_set_transient_create(
    const d7_hamt_set_policy *policy,
    d7_hamt_set_transient *result) {
    if (result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const d7_hamt_set empty = d7_hamt_set_create(policy);
    d7_hamt_set_transient transient;
    const d7_hamt_status status = d7_hamt_map_to_transient(&empty.map, &transient.inner);
    if (status == D7_HAMT_OK) {
        *result = transient;
    }
    return status;
}

d7_hamt_status d7_hamt_set_to_transient(
    const d7_hamt_set *set,
    d7_hamt_set_transient *result) {
    if (set == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set_transient transient;
    const d7_hamt_status status = d7_hamt_map_to_transient(&set->map, &transient.inner);
    if (status == D7_HAMT_OK) {
        *result = transient;
    }
    return status;
}

d7_hamt_status d7_hamt_set_transient_clone(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_transient *result) {
    if (transient == NULL || result == NULL || transient == result) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set_transient clone;
    const d7_hamt_status status =
        d7_hamt_map_transient_clone(&transient->inner, &clone.inner);
    if (status == D7_HAMT_OK) {
        *result = clone;
    }
    return status;
}

void d7_hamt_set_transient_destroy(d7_hamt_set_transient *transient) {
    if (transient != NULL) {
        d7_hamt_map_transient_destroy(&transient->inner);
    }
}

bool d7_hamt_set_transient_is_active(const d7_hamt_set_transient *transient) {
    return transient != NULL && d7_hamt_map_transient_is_active(&transient->inner);
}

d7_hamt_status d7_hamt_set_transient_get_policy(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_policy *policy) {
    if (transient == NULL || policy == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_policy map_policy;
    const d7_hamt_status status =
        d7_hamt_map_transient_get_policy(&transient->inner, &map_policy);
    if (status != D7_HAMT_OK) {
        return status;
    }

    d7_hamt_set_policy set_policy;
    set_policy.hash = map_policy.hash;
    set_policy.equal = map_policy.key_equal;
    set_policy.retain_item = map_policy.retain_key;
    set_policy.release_item = map_policy.release_key;
    set_policy.context = map_policy.context;
    *policy = set_policy;
    return D7_HAMT_OK;
}

d7_hamt_status d7_hamt_set_transient_count(
    const d7_hamt_set_transient *transient,
    size_t *count) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_count(&transient->inner, count);
}

d7_hamt_status d7_hamt_set_transient_contains(
    const d7_hamt_set_transient *transient,
    const void *item,
    bool *contains) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_contains_key(&transient->inner, item, contains);
}

d7_hamt_status d7_hamt_set_transient_try_get_value(
    const d7_hamt_set_transient *transient,
    const void *equal_value,
    bool *found,
    const void **actual_value) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_try_get_key(
            &transient->inner,
            equal_value,
            found,
            actual_value);
}

d7_hamt_status d7_hamt_set_transient_add(
    d7_hamt_set_transient *transient,
    const void *item) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_set(&transient->inner, item, NULL);
}

d7_hamt_status d7_hamt_set_transient_try_add(
    d7_hamt_set_transient *transient,
    const void *item,
    bool *added) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_try_add(&transient->inner, item, NULL, added);
}

d7_hamt_status d7_hamt_set_transient_remove(
    d7_hamt_set_transient *transient,
    const void *item) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_remove(&transient->inner, item);
}

d7_hamt_status d7_hamt_set_transient_try_remove(
    d7_hamt_set_transient *transient,
    const void *item,
    bool *removed) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_try_remove(&transient->inner, item, removed);
}

d7_hamt_status d7_hamt_set_transient_clear(d7_hamt_set_transient *transient) {
    return transient == NULL
        ? D7_HAMT_INVALID_ARGUMENT
        : d7_hamt_map_transient_clear(&transient->inner);
}

typedef enum d7_hamt_set_transient_relation {
    D7_HAMT_SET_TRANSIENT_SUBSET,
    D7_HAMT_SET_TRANSIENT_PROPER_SUBSET,
    D7_HAMT_SET_TRANSIENT_SUPERSET,
    D7_HAMT_SET_TRANSIENT_PROPER_SUPERSET,
    D7_HAMT_SET_TRANSIENT_OVERLAPS,
    D7_HAMT_SET_TRANSIENT_EQUALS
} d7_hamt_set_transient_relation;

static d7_hamt_status d7_hamt_set_transient_relation_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result,
    d7_hamt_set_transient_relation relation) {
    if (transient == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status active_status =
        d7_hamt_map_transient_active_state(&transient->inner, &state);
    if (active_status != D7_HAMT_OK) {
        return active_status;
    }
    if (result == NULL || (item_count != 0 && items == NULL)) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set borrowed;
    borrowed.map = state->map;
    bool local_result = false;
    d7_hamt_status status;
    switch (relation) {
    case D7_HAMT_SET_TRANSIENT_SUBSET:
        status = d7_hamt_set_is_subset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_PROPER_SUBSET:
        status = d7_hamt_set_is_proper_subset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_SUPERSET:
        status = d7_hamt_set_is_superset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_PROPER_SUPERSET:
        status = d7_hamt_set_is_proper_superset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_OVERLAPS:
        status = d7_hamt_set_overlaps_many(
            &borrowed, items, item_count, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_EQUALS:
        status = d7_hamt_set_equals_many(
            &borrowed, items, item_count, &local_result);
        break;
    default:
        return D7_HAMT_INVALID_ARGUMENT;
    }

    if (status == D7_HAMT_OK) {
        *result = local_result;
    }
    return status;
}

static d7_hamt_status d7_hamt_set_transient_relation_set(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result,
    d7_hamt_set_transient_relation relation) {
    if (transient == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    struct d7_hamt_map_transient_state *state = NULL;
    const d7_hamt_status active_status =
        d7_hamt_map_transient_active_state(&transient->inner, &state);
    if (active_status != D7_HAMT_OK) {
        return active_status;
    }
    if (other == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set borrowed;
    borrowed.map = state->map;
    bool local_result = false;
    d7_hamt_status status;
    switch (relation) {
    case D7_HAMT_SET_TRANSIENT_SUBSET:
        status = d7_hamt_set_is_subset_of(&borrowed, other, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_PROPER_SUBSET:
        status = d7_hamt_set_is_proper_subset_of(&borrowed, other, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_SUPERSET:
        status = d7_hamt_set_is_superset_of(&borrowed, other, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_PROPER_SUPERSET:
        status = d7_hamt_set_is_proper_superset_of(&borrowed, other, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_OVERLAPS:
        status = d7_hamt_set_overlaps(&borrowed, other, &local_result);
        break;
    case D7_HAMT_SET_TRANSIENT_EQUALS:
        status = d7_hamt_set_equals(&borrowed, other, &local_result);
        break;
    default:
        return D7_HAMT_INVALID_ARGUMENT;
    }

    if (status == D7_HAMT_OK) {
        *result = local_result;
    }
    return status;
}

d7_hamt_status d7_hamt_set_transient_is_subset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return d7_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        D7_HAMT_SET_TRANSIENT_SUBSET);
}

d7_hamt_status d7_hamt_set_transient_is_proper_subset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return d7_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        D7_HAMT_SET_TRANSIENT_PROPER_SUBSET);
}

d7_hamt_status d7_hamt_set_transient_is_superset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return d7_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        D7_HAMT_SET_TRANSIENT_SUPERSET);
}

d7_hamt_status d7_hamt_set_transient_is_proper_superset_of_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return d7_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        D7_HAMT_SET_TRANSIENT_PROPER_SUPERSET);
}

d7_hamt_status d7_hamt_set_transient_overlaps_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return d7_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        D7_HAMT_SET_TRANSIENT_OVERLAPS);
}

d7_hamt_status d7_hamt_set_transient_equals_many(
    const d7_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return d7_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        D7_HAMT_SET_TRANSIENT_EQUALS);
}

d7_hamt_status d7_hamt_set_transient_is_subset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result) {
    return d7_hamt_set_transient_relation_set(
        transient, other, result, D7_HAMT_SET_TRANSIENT_SUBSET);
}

d7_hamt_status d7_hamt_set_transient_is_proper_subset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result) {
    return d7_hamt_set_transient_relation_set(
        transient, other, result, D7_HAMT_SET_TRANSIENT_PROPER_SUBSET);
}

d7_hamt_status d7_hamt_set_transient_is_superset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result) {
    return d7_hamt_set_transient_relation_set(
        transient, other, result, D7_HAMT_SET_TRANSIENT_SUPERSET);
}

d7_hamt_status d7_hamt_set_transient_is_proper_superset_of(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result) {
    return d7_hamt_set_transient_relation_set(
        transient, other, result, D7_HAMT_SET_TRANSIENT_PROPER_SUPERSET);
}

d7_hamt_status d7_hamt_set_transient_overlaps(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result) {
    return d7_hamt_set_transient_relation_set(
        transient, other, result, D7_HAMT_SET_TRANSIENT_OVERLAPS);
}

d7_hamt_status d7_hamt_set_transient_equals(
    const d7_hamt_set_transient *transient,
    const d7_hamt_set *other,
    bool *result) {
    return d7_hamt_set_transient_relation_set(
        transient, other, result, D7_HAMT_SET_TRANSIENT_EQUALS);
}

d7_hamt_status d7_hamt_set_transient_iterator_init(
    const d7_hamt_set_transient *transient,
    d7_hamt_set_transient_iterator *iterator) {
    if (transient == NULL || iterator == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_set_transient_iterator local;
    const d7_hamt_status status =
        d7_hamt_map_transient_iterator_init(&transient->inner, &local.inner);
    if (status == D7_HAMT_OK) {
        *iterator = local;
    }
    return status;
}

d7_hamt_status d7_hamt_set_transient_iterator_next(
    d7_hamt_set_transient_iterator *iterator,
    bool *has_value,
    const void **item) {
    if (iterator == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const void *value = NULL;
    return d7_hamt_map_transient_iterator_next(
        &iterator->inner,
        has_value,
        item,
        &value);
}

d7_hamt_status d7_hamt_set_transient_persist(
    d7_hamt_set_transient *transient,
    d7_hamt_set *result) {
    if (transient == NULL || result == NULL) {
        return D7_HAMT_INVALID_ARGUMENT;
    }

    d7_hamt_map map;
    const d7_hamt_status status =
        d7_hamt_map_transient_persist(&transient->inner, &map);
    if (status == D7_HAMT_OK) {
        result->map = map;
    }
    return status;
}

const void *d7_hamt_set_transient_debug_root_identity(
    const d7_hamt_set_transient *transient) {
    return transient == NULL
        ? NULL
        : d7_hamt_map_transient_debug_root_identity(&transient->inner);
}

static uint32_t d7_hamt_pointer_hash(const void *item, void *context) {
    (void)context;
    /* Widen before mixing: on 32-bit targets a `uintptr_t >> 33` would be a
     * shift past the type width (undefined behavior) and the 64-bit Murmur3
     * finalizer constants would silently truncate. */
    uint64_t value = (uint64_t)(uintptr_t)item;
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33;
    return (uint32_t)value;
}

static bool d7_hamt_pointer_equal(const void *left, const void *right, void *context) {
    (void)context;
    return left == right;
}

static void *d7_hamt_identity_retain(const void *item, void *context) {
    (void)context;
    return (void *)item;
}

static bool d7_hamt_unit_equal(const void *left, const void *right, void *context) {
    (void)left;
    (void)right;
    (void)context;
    return true;
}

static d7_hamt_policy d7_hamt_normalize_policy(const d7_hamt_policy *policy) {
    d7_hamt_policy normalized = policy == NULL ? d7_hamt_policy_default() : *policy;
    if (normalized.hash == NULL) {
        normalized.hash = d7_hamt_pointer_hash;
    }
    if (normalized.key_equal == NULL) {
        normalized.key_equal = d7_hamt_pointer_equal;
    }
    if (normalized.value_equal == NULL) {
        normalized.value_equal = d7_hamt_pointer_equal;
    }
    if (normalized.retain_key == NULL) {
        normalized.retain_key = d7_hamt_identity_retain;
    }
    if (normalized.retain_value == NULL) {
        normalized.retain_value = d7_hamt_identity_retain;
    }

    return normalized;
}

static d7_hamt_set_policy d7_hamt_normalize_set_policy(const d7_hamt_set_policy *policy) {
    d7_hamt_set_policy normalized = policy == NULL ? d7_hamt_set_policy_default() : *policy;
    if (normalized.hash == NULL) {
        normalized.hash = d7_hamt_pointer_hash;
    }
    if (normalized.equal == NULL) {
        normalized.equal = d7_hamt_pointer_equal;
    }
    if (normalized.retain_item == NULL) {
        normalized.retain_item = d7_hamt_identity_retain;
    }

    return normalized;
}

static d7_hamt_policy d7_hamt_map_policy_from_set_policy(const d7_hamt_set_policy *policy) {
    const d7_hamt_set_policy set_policy = d7_hamt_normalize_set_policy(policy);
    d7_hamt_policy map_policy;
    memset(&map_policy, 0, sizeof(map_policy));
    map_policy.hash = set_policy.hash;
    map_policy.key_equal = set_policy.equal;
    map_policy.value_equal = d7_hamt_unit_equal;
    map_policy.retain_key = set_policy.retain_item;
    map_policy.retain_value = d7_hamt_identity_retain;
    map_policy.release_key = set_policy.release_item;
    map_policy.release_value = NULL;
    map_policy.context = set_policy.context;
    return map_policy;
}

static d7_hamt_node *d7_hamt_node_retain(const d7_hamt_node *node) {
    if (node == NULL) {
        return NULL;
    }

    d7_hamt_node *mutable_node = (d7_hamt_node *)node;
    ++mutable_node->ref_count;
    return mutable_node;
}

static void d7_hamt_node_release(const d7_hamt_policy *policy, const d7_hamt_node *node) {
    if (node == NULL) {
        return;
    }

    d7_hamt_node *mutable_node = (d7_hamt_node *)node;
    assert(mutable_node->ref_count > 0);
    --mutable_node->ref_count;
    if (mutable_node->ref_count != 0) {
        return;
    }

    switch (node->kind) {
    case D7_HAMT_NODE_LEAF: {
        d7_hamt_leaf_node *leaf = (d7_hamt_leaf_node *)mutable_node;
        d7_hamt_release_key(policy, leaf->key);
        d7_hamt_release_value(policy, leaf->value);
        free(leaf);
        break;
    }

    case D7_HAMT_NODE_COLLISION: {
        d7_hamt_collision_node *collision = (d7_hamt_collision_node *)mutable_node;
        for (size_t i = 0; i < collision->count; ++i) {
            d7_hamt_release_key(policy, (void *)collision->entries[i].key);
            d7_hamt_release_value(policy, (void *)collision->entries[i].value);
        }
        free(collision);
        break;
    }

    case D7_HAMT_NODE_BITMAP_INDEXED: {
        d7_hamt_bitmap_node *branch = (d7_hamt_bitmap_node *)mutable_node;
        d7_hamt_inline_entry *data = d7_hamt_bitmap_data(branch);
        for (size_t i = 0; i < branch->data_count; ++i) {
            d7_hamt_release_key(policy, (void *)data[i].entry.key);
            d7_hamt_release_value(policy, (void *)data[i].entry.value);
        }
        d7_hamt_node **children = d7_hamt_bitmap_children(branch);
        for (size_t i = 0; i < branch->node_count; ++i) {
            d7_hamt_node_release(policy, children[i]);
        }
        free(branch);
        break;
    }

    default:
        free(mutable_node);
        break;
    }
}

static uint32_t d7_hamt_get_hash(const d7_hamt_map *map, const void *key) {
    return map->policy.hash(key, map->policy.context);
}

static bool d7_hamt_keys_equal(const d7_hamt_policy *policy, const void *left, const void *right) {
    return policy->key_equal(left, right, policy->context);
}

static bool d7_hamt_values_equal(const d7_hamt_policy *policy, const void *left, const void *right) {
    return left == right || policy->value_equal(left, right, policy->context);
}

static void *d7_hamt_retain_key(const d7_hamt_policy *policy, const void *key) {
    return policy->retain_key(key, policy->context);
}

static void *d7_hamt_retain_value(const d7_hamt_policy *policy, const void *value) {
    return policy->retain_value(value, policy->context);
}

/* An allocating retain callback reports failure by returning NULL for a
 * non-NULL input; storing that NULL with D7_HAMT_OK would surface much later
 * as a NULL key/value reaching the user callbacks. */
static d7_hamt_status d7_hamt_checked_retain_key(
    const d7_hamt_policy *policy,
    const void *key,
    void **retained) {
    *retained = d7_hamt_retain_key(policy, key);
    return (*retained == NULL && key != NULL) ? D7_HAMT_OUT_OF_MEMORY : D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_checked_retain_value(
    const d7_hamt_policy *policy,
    const void *value,
    void **retained) {
    *retained = d7_hamt_retain_value(policy, value);
    return (*retained == NULL && value != NULL) ? D7_HAMT_OUT_OF_MEMORY : D7_HAMT_OK;
}

static void d7_hamt_release_key(const d7_hamt_policy *policy, void *key) {
    if (policy->release_key != NULL) {
        policy->release_key(key, policy->context);
    }
}

static void d7_hamt_release_value(const d7_hamt_policy *policy, void *value) {
    if (policy->release_value != NULL) {
        policy->release_value(value, policy->context);
    }
}

static d7_hamt_status d7_hamt_inline_copy(
    const d7_hamt_policy *policy,
    const d7_hamt_inline_entry *source,
    d7_hamt_inline_entry *target) {
    target->hash = source->hash;
    target->entry.key = NULL;
    target->entry.value = NULL;
    void *key = NULL;
    void *value = NULL;
    d7_hamt_status status = d7_hamt_checked_retain_key(policy, source->entry.key, &key);
    if (status == D7_HAMT_OK) {
        status = d7_hamt_checked_retain_value(policy, source->entry.value, &value);
    }
    if (status != D7_HAMT_OK) {
        d7_hamt_release_key(policy, key);
        d7_hamt_release_value(policy, value);
        return status;
    }
    target->entry.key = key;
    target->entry.value = value;
    return D7_HAMT_OK;
}

static void d7_hamt_inline_release(
    const d7_hamt_policy *policy,
    d7_hamt_inline_entry *data,
    size_t count) {
    for (size_t i = 0; i < count; ++i) {
        d7_hamt_release_key(policy, (void *)data[i].entry.key);
        d7_hamt_release_value(policy, (void *)data[i].entry.value);
    }
}

static int d7_hamt_index(uint32_t hash, int shift) {
    return (int)((hash >> shift) & D7_HAMT_BRANCH_MASK);
}

static uint32_t d7_hamt_bit(int index) {
    return 1u << index;
}

static size_t d7_hamt_slot(uint32_t bitmap, uint32_t bit) {
    return d7_hamt_popcount(bitmap & (bit - 1u));
}

static size_t d7_hamt_popcount(uint32_t value) {
    size_t count = 0;
    while (value != 0) {
        value &= value - 1u;
        ++count;
    }

    return count;
}

static d7_hamt_inline_entry *d7_hamt_bitmap_data(d7_hamt_bitmap_node *node) {
    return (d7_hamt_inline_entry *)node->storage;
}

static const d7_hamt_inline_entry *d7_hamt_bitmap_data_const(const d7_hamt_bitmap_node *node) {
    return (const d7_hamt_inline_entry *)node->storage;
}

static d7_hamt_node **d7_hamt_bitmap_children(d7_hamt_bitmap_node *node) {
    return (d7_hamt_node **)(node->storage + node->data_count * sizeof(d7_hamt_inline_entry));
}

static d7_hamt_node *const *d7_hamt_bitmap_children_const(const d7_hamt_bitmap_node *node) {
    return (d7_hamt_node *const *)(node->storage + node->data_count * sizeof(d7_hamt_inline_entry));
}

static d7_hamt_status d7_hamt_leaf_create(
    const d7_hamt_policy *policy,
    uint32_t hash,
    const void *key,
    const void *value,
    d7_hamt_node **result) {
    void *retained_key = NULL;
    void *retained_value = NULL;
    d7_hamt_status status = d7_hamt_checked_retain_key(policy, key, &retained_key);
    if (status == D7_HAMT_OK) {
        status = d7_hamt_checked_retain_value(policy, value, &retained_value);
    }
    if (status == D7_HAMT_OK) {
        status = d7_hamt_leaf_create_from_retained(hash, retained_key, retained_value, result);
    } else {
        *result = NULL;
    }
    if (status != D7_HAMT_OK) {
        d7_hamt_release_key(policy, retained_key);
        d7_hamt_release_value(policy, retained_value);
    }

    return status;
}

static d7_hamt_status d7_hamt_leaf_create_from_retained(
    uint32_t hash,
    void *key,
    void *value,
    d7_hamt_node **result) {
    d7_hamt_leaf_node *leaf = (d7_hamt_leaf_node *)d7_hamt_allocate(sizeof(*leaf));
    if (leaf == NULL) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }

    leaf->base.kind = D7_HAMT_NODE_LEAF;
    leaf->base.ref_count = 1;
    leaf->base.subtree_count = 1;
    leaf->hash = hash;
    leaf->key = key;
    leaf->value = value;
    *result = &leaf->base;
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_collision_write_entry(
    const d7_hamt_policy *policy,
    d7_hamt_collision_node *collision,
    size_t *written,
    const void *key,
    const void *value) {
    void *retained_key = NULL;
    void *retained_value = NULL;
    d7_hamt_status status = d7_hamt_checked_retain_key(policy, key, &retained_key);
    if (status == D7_HAMT_OK) {
        status = d7_hamt_checked_retain_value(policy, value, &retained_value);
        if (status != D7_HAMT_OK) {
            d7_hamt_release_key(policy, retained_key);
        }
    }

    if (status != D7_HAMT_OK) {
        return status;
    }

    collision->entries[*written].key = retained_key;
    collision->entries[*written].value = retained_value;
    ++(*written);
    return D7_HAMT_OK;
}

/* Precondition (mirrors the C# reference's Debug.Assert): equal-hash merges
 * only ever combine two leaves whose keys differ under the policy, because
 * equal-hash inserts into an existing collision node are handled inside the
 * collision branch of d7_hamt_node_set. The collision-left handling below is
 * defensively retained but is unreachable from the current call graph; note
 * that it appends `right` without a duplicate-key scan, which is only safe
 * under that precondition. */
static d7_hamt_status d7_hamt_collision_create(
    const d7_hamt_policy *policy,
    d7_hamt_node *left,
    d7_hamt_node *right,
    d7_hamt_node **result) {
    const uint32_t hash = left->kind == D7_HAMT_NODE_LEAF
        ? ((const d7_hamt_leaf_node *)left)->hash
        : ((const d7_hamt_collision_node *)left)->hash;
    const size_t left_count = left->kind == D7_HAMT_NODE_COLLISION
        ? ((const d7_hamt_collision_node *)left)->count
        : 1u;
    const size_t total_count = left_count + 1u;
    d7_hamt_collision_node *collision =
        (d7_hamt_collision_node *)d7_hamt_allocate(sizeof(*collision) + total_count * sizeof(d7_hamt_entry));
    if (collision == NULL) {
        d7_hamt_node_release(policy, left);
        d7_hamt_node_release(policy, right);
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }

    collision->base.kind = D7_HAMT_NODE_COLLISION;
    collision->base.ref_count = 1;
    collision->base.subtree_count = total_count;
    collision->hash = hash;
    collision->count = total_count;

    d7_hamt_status status = D7_HAMT_OK;
    size_t written = 0;
    if (left->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *source = (const d7_hamt_collision_node *)left;
        for (size_t i = 0; status == D7_HAMT_OK && i < source->count; ++i) {
            status = d7_hamt_collision_write_entry(
                policy, collision, &written, source->entries[i].key, source->entries[i].value);
        }
    } else {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)left;
        status = d7_hamt_collision_write_entry(policy, collision, &written, leaf->key, leaf->value);
    }

    if (status == D7_HAMT_OK) {
        const d7_hamt_leaf_node *right_leaf = (const d7_hamt_leaf_node *)right;
        status = d7_hamt_collision_write_entry(policy, collision, &written, right_leaf->key, right_leaf->value);
    }

    if (status != D7_HAMT_OK) {
        collision->count = written;
        d7_hamt_node_release(policy, &collision->base);
        d7_hamt_node_release(policy, left);
        d7_hamt_node_release(policy, right);
        *result = NULL;
        return status;
    }

    d7_hamt_node_release(policy, left);
    d7_hamt_node_release(policy, right);
    *result = &collision->base;
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_bitmap_create(
    uint32_t data_map,
    uint32_t node_map,
    const d7_hamt_inline_entry *data,
    size_t data_count,
    d7_hamt_node **children,
    size_t child_count,
    d7_hamt_node **result) {
    d7_hamt_bitmap_node *branch =
        (d7_hamt_bitmap_node *)d7_hamt_allocate(
            sizeof(*branch)
            + data_count * sizeof(d7_hamt_inline_entry)
            + child_count * sizeof(d7_hamt_node *));
    if (branch == NULL) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }

    branch->base.kind = D7_HAMT_NODE_BITMAP_INDEXED;
    branch->base.ref_count = 1;
    branch->data_map = data_map;
    branch->node_map = node_map;
    branch->data_count = data_count;
    branch->node_count = child_count;
    d7_hamt_inline_entry *target_data = d7_hamt_bitmap_data(branch);
    for (size_t i = 0; i < data_count; ++i) {
        target_data[i] = data[i];
    }
    d7_hamt_node **target_children = d7_hamt_bitmap_children(branch);
    for (size_t i = 0; i < child_count; ++i) {
        target_children[i] = children[i];
    }
    branch->base.subtree_count = data_count;
    for (size_t i = 0; i < child_count; ++i) {
        branch->base.subtree_count += children[i]->subtree_count;
    }

    *result = &branch->base;
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_bitmap_copy_create(
    const d7_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const d7_hamt_inline_entry *data,
    size_t data_count,
    d7_hamt_node *const *children,
    size_t child_count,
    d7_hamt_node **result) {
    d7_hamt_bitmap_node *branch =
        (d7_hamt_bitmap_node *)d7_hamt_allocate(
            sizeof(*branch)
            + data_count * sizeof(d7_hamt_inline_entry)
            + child_count * sizeof(d7_hamt_node *));
    if (branch == NULL) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }
    branch->base.kind = D7_HAMT_NODE_BITMAP_INDEXED;
    branch->base.ref_count = 1;
    branch->data_map = data_map;
    branch->node_map = node_map;
    branch->data_count = data_count;
    branch->node_count = 0;
    d7_hamt_inline_entry *target_data = d7_hamt_bitmap_data(branch);
    d7_hamt_status status = D7_HAMT_OK;
    size_t written_data = 0;
    while (status == D7_HAMT_OK && written_data < data_count) {
        status = d7_hamt_inline_copy(
            policy, &data[written_data], &target_data[written_data]);
        if (status == D7_HAMT_OK) {
            ++written_data;
        }
    }
    if (status != D7_HAMT_OK) {
        d7_hamt_inline_release(policy, target_data, written_data);
        free(branch);
        *result = NULL;
        return status;
    }
    d7_hamt_node **target_children =
        (d7_hamt_node **)(branch->storage + data_count * sizeof(d7_hamt_inline_entry));
    while (branch->node_count < child_count) {
        target_children[branch->node_count] = d7_hamt_node_retain(children[branch->node_count]);
        ++branch->node_count;
    }
    branch->base.subtree_count = data_count;
    for (size_t i = 0; i < child_count; ++i) {
        branch->base.subtree_count += children[i]->subtree_count;
    }
    *result = &branch->base;
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_merge_hash_nodes(
    const d7_hamt_policy *policy,
    d7_hamt_node *left,
    d7_hamt_node *right,
    int shift,
    d7_hamt_node **result) {
    const uint32_t left_hash = left->kind == D7_HAMT_NODE_LEAF
        ? ((const d7_hamt_leaf_node *)left)->hash
        : ((const d7_hamt_collision_node *)left)->hash;
    const uint32_t right_hash = ((const d7_hamt_leaf_node *)right)->hash;

    if (left_hash == right_hash) {
        return d7_hamt_collision_create(policy, left, right, result);
    }

    if (shift >= 32) {
        d7_hamt_node_release(policy, left);
        d7_hamt_node_release(policy, right);
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const int left_index = d7_hamt_index(left_hash, shift);
    const int right_index = d7_hamt_index(right_hash, shift);
    const uint32_t left_bit = d7_hamt_bit(left_index);
    const uint32_t right_bit = d7_hamt_bit(right_index);

    if (left_index == right_index) {
        d7_hamt_node *child = NULL;
        d7_hamt_status status = d7_hamt_merge_hash_nodes(
            policy,
            left,
            right,
            shift + D7_HAMT_BITS_PER_LEVEL,
            &child);
        if (status != D7_HAMT_OK) {
            return status;
        }

        d7_hamt_node *children[1] = { child };
        status = d7_hamt_bitmap_create(0, left_bit, NULL, 0, children, 1, result);
        if (status != D7_HAMT_OK) {
            d7_hamt_node_release(policy, child);
        }
        return status;
    }

    d7_hamt_status status;
    const d7_hamt_leaf_node *right_leaf = (const d7_hamt_leaf_node *)right;
    if (left->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *left_leaf = (const d7_hamt_leaf_node *)left;
        d7_hamt_inline_entry data[2];
        const size_t left_slot = left_index < right_index ? 0u : 1u;
        const size_t right_slot = 1u - left_slot;
        data[left_slot].hash = left_leaf->hash;
        data[left_slot].entry.key = left_leaf->key;
        data[left_slot].entry.value = left_leaf->value;
        data[right_slot].hash = right_leaf->hash;
        data[right_slot].entry.key = right_leaf->key;
        data[right_slot].entry.value = right_leaf->value;
        status = d7_hamt_bitmap_copy_create(
            policy, left_bit | right_bit, 0, data, 2, NULL, 0, result);
    } else {
        d7_hamt_inline_entry data;
        data.hash = right_leaf->hash;
        data.entry.key = right_leaf->key;
        data.entry.value = right_leaf->value;
        d7_hamt_node *children[1] = { left };
        status = d7_hamt_bitmap_copy_create(
            policy, right_bit, left_bit, &data, 1, children, 1, result);
    }
    d7_hamt_node_release(policy, left);
    d7_hamt_node_release(policy, right);

    return status;
}

/* Merges an insertion leaf while carrying the concrete retained value from
 * that leaf into the published topology. This avoids a post-construction
 * lookup (and therefore preserves the one-descent factory contract) even when
 * ownership callbacks replace borrowed candidates with distinct objects. */
static d7_hamt_status d7_hamt_merge_hash_nodes_selected(
    const d7_hamt_policy *policy,
    d7_hamt_node *left,
    d7_hamt_node *right,
    int shift,
    d7_hamt_node **result,
    const void **selected_value) {
    const uint32_t left_hash = left->kind == D7_HAMT_NODE_LEAF
        ? ((const d7_hamt_leaf_node *)left)->hash
        : ((const d7_hamt_collision_node *)left)->hash;
    const uint32_t right_hash = ((const d7_hamt_leaf_node *)right)->hash;

    if (left_hash == right_hash) {
        const d7_hamt_status status = d7_hamt_collision_create(policy, left, right, result);
        if (status == D7_HAMT_OK) {
            const d7_hamt_collision_node *collision =
                (const d7_hamt_collision_node *)*result;
            *selected_value = collision->entries[collision->count - 1u].value;
        }
        return status;
    }

    if (shift >= 32) {
        d7_hamt_node_release(policy, left);
        d7_hamt_node_release(policy, right);
        return D7_HAMT_INVALID_ARGUMENT;
    }

    const int left_index = d7_hamt_index(left_hash, shift);
    const int right_index = d7_hamt_index(right_hash, shift);
    const uint32_t left_bit = d7_hamt_bit(left_index);
    const uint32_t right_bit = d7_hamt_bit(right_index);

    if (left_index == right_index) {
        d7_hamt_node *child = NULL;
        const void *child_selected = NULL;
        d7_hamt_status status = d7_hamt_merge_hash_nodes_selected(
            policy,
            left,
            right,
            shift + D7_HAMT_BITS_PER_LEVEL,
            &child,
            &child_selected);
        if (status != D7_HAMT_OK) {
            return status;
        }

        d7_hamt_node *children[1] = { child };
        status = d7_hamt_bitmap_create(0, left_bit, NULL, 0, children, 1, result);
        if (status != D7_HAMT_OK) {
            d7_hamt_node_release(policy, child);
            return status;
        }
        *selected_value = child_selected;
        return D7_HAMT_OK;
    }

    d7_hamt_status status;
    size_t selected_slot = 0;
    const d7_hamt_leaf_node *right_leaf = (const d7_hamt_leaf_node *)right;
    if (left->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *left_leaf = (const d7_hamt_leaf_node *)left;
        d7_hamt_inline_entry data[2];
        const size_t left_slot = left_index < right_index ? 0u : 1u;
        selected_slot = 1u - left_slot;
        data[left_slot].hash = left_leaf->hash;
        data[left_slot].entry.key = left_leaf->key;
        data[left_slot].entry.value = left_leaf->value;
        data[selected_slot].hash = right_leaf->hash;
        data[selected_slot].entry.key = right_leaf->key;
        data[selected_slot].entry.value = right_leaf->value;
        status = d7_hamt_bitmap_copy_create(
            policy, left_bit | right_bit, 0, data, 2, NULL, 0, result);
    } else {
        d7_hamt_inline_entry data;
        data.hash = right_leaf->hash;
        data.entry.key = right_leaf->key;
        data.entry.value = right_leaf->value;
        d7_hamt_node *children[1] = { left };
        status = d7_hamt_bitmap_copy_create(
            policy, right_bit, left_bit, &data, 1, children, 1, result);
    }
    d7_hamt_node_release(policy, left);
    d7_hamt_node_release(policy, right);

    if (status == D7_HAMT_OK) {
        const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)*result;
        *selected_value = d7_hamt_bitmap_data_const(branch)[selected_slot].entry.value;
    }
    return status;
}

static d7_hamt_status d7_hamt_node_set(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    const void *key,
    const void *value,
    uint32_t hash,
    int shift,
    bool overwrite,
    bool *added,
    d7_hamt_node **result) {
    if (node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
        if (leaf->hash == hash && d7_hamt_keys_equal(policy, leaf->key, key)) {
            *added = false;
            if (!overwrite || d7_hamt_values_equal(policy, leaf->value, value)) {
                *result = d7_hamt_node_retain(node);
                return D7_HAMT_OK;
            }

            void *retained_key = NULL;
            void *retained_value = NULL;
            d7_hamt_status status = d7_hamt_checked_retain_key(policy, leaf->key, &retained_key);
            if (status == D7_HAMT_OK) {
                status = d7_hamt_checked_retain_value(policy, value, &retained_value);
            }
            if (status == D7_HAMT_OK) {
                status = d7_hamt_leaf_create_from_retained(leaf->hash, retained_key, retained_value, result);
            } else {
                *result = NULL;
            }
            if (status != D7_HAMT_OK) {
                d7_hamt_release_key(policy, retained_key);
                d7_hamt_release_value(policy, retained_value);
            }
            return status;
        }

        *added = true;
        d7_hamt_node *left = d7_hamt_node_retain(node);
        d7_hamt_node *right = NULL;
        d7_hamt_status status = d7_hamt_leaf_create(policy, hash, key, value, &right);
        if (status != D7_HAMT_OK) {
            d7_hamt_node_release(policy, left);
            return status;
        }
        return d7_hamt_merge_hash_nodes(policy, left, right, shift, result);
    }

    if (node->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)node;
        if (collision->hash != hash) {
            *added = true;
            d7_hamt_node *left = d7_hamt_node_retain(node);
            d7_hamt_node *right = NULL;
            d7_hamt_status status = d7_hamt_leaf_create(policy, hash, key, value, &right);
            if (status != D7_HAMT_OK) {
                d7_hamt_node_release(policy, left);
                return status;
            }
            return d7_hamt_merge_hash_nodes(policy, left, right, shift, result);
        }

        for (size_t i = 0; i < collision->count; ++i) {
            if (!d7_hamt_keys_equal(policy, collision->entries[i].key, key)) {
                continue;
            }

            *added = false;
            if (!overwrite || d7_hamt_values_equal(policy, collision->entries[i].value, value)) {
                *result = d7_hamt_node_retain(node);
                return D7_HAMT_OK;
            }

            d7_hamt_collision_node *replaced =
                (d7_hamt_collision_node *)d7_hamt_allocate(
                    sizeof(*replaced) + collision->count * sizeof(d7_hamt_entry));
            if (replaced == NULL) {
                *result = NULL;
                return D7_HAMT_OUT_OF_MEMORY;
            }

            replaced->base.kind = D7_HAMT_NODE_COLLISION;
            replaced->base.ref_count = 1;
            replaced->base.subtree_count = collision->count;
            replaced->hash = collision->hash;
            replaced->count = collision->count;
            d7_hamt_status status = D7_HAMT_OK;
            size_t written = 0;
            for (size_t j = 0; status == D7_HAMT_OK && j < collision->count; ++j) {
                status = d7_hamt_collision_write_entry(
                    policy,
                    replaced,
                    &written,
                    collision->entries[j].key,
                    j == i ? value : collision->entries[j].value);
            }
            if (status != D7_HAMT_OK) {
                replaced->count = written;
                d7_hamt_node_release(policy, &replaced->base);
                *result = NULL;
                return status;
            }
            *result = &replaced->base;
            return D7_HAMT_OK;
        }

        d7_hamt_collision_node *expanded =
            (d7_hamt_collision_node *)d7_hamt_allocate(
                sizeof(*expanded) + (collision->count + 1u) * sizeof(d7_hamt_entry));
        if (expanded == NULL) {
            *result = NULL;
            return D7_HAMT_OUT_OF_MEMORY;
        }

        expanded->base.kind = D7_HAMT_NODE_COLLISION;
        expanded->base.ref_count = 1;
        expanded->base.subtree_count = collision->count + 1;
        expanded->hash = collision->hash;
        expanded->count = collision->count + 1u;
        d7_hamt_status status = D7_HAMT_OK;
        size_t written = 0;
        for (size_t i = 0; status == D7_HAMT_OK && i < collision->count; ++i) {
            status = d7_hamt_collision_write_entry(
                policy, expanded, &written, collision->entries[i].key, collision->entries[i].value);
        }
        if (status == D7_HAMT_OK) {
            status = d7_hamt_collision_write_entry(policy, expanded, &written, key, value);
        }
        if (status != D7_HAMT_OK) {
            expanded->count = written;
            d7_hamt_node_release(policy, &expanded->base);
            *result = NULL;
            return status;
        }
        *added = true;
        *result = &expanded->base;
        return D7_HAMT_OK;
    }

    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
    const d7_hamt_inline_entry *source_data = d7_hamt_bitmap_data_const(branch);
    d7_hamt_node *const *source_children = d7_hamt_bitmap_children_const(branch);
    const uint32_t selected_bit = d7_hamt_bit(d7_hamt_index(hash, shift));
    d7_hamt_inline_entry data[32];
    d7_hamt_node *children[32];

    if ((branch->data_map & selected_bit) != 0) {
        const size_t data_slot = d7_hamt_slot(branch->data_map, selected_bit);
        const d7_hamt_inline_entry *existing = &source_data[data_slot];
        if (existing->hash == hash && d7_hamt_keys_equal(policy, existing->entry.key, key)) {
            *added = false;
            if (!overwrite || d7_hamt_values_equal(policy, existing->entry.value, value)) {
                *result = d7_hamt_node_retain(node);
                return D7_HAMT_OK;
            }
            memcpy(data, source_data, branch->data_count * sizeof(*data));
            data[data_slot].entry.value = value;
            return d7_hamt_bitmap_copy_create(
                policy, branch->data_map, branch->node_map,
                data, branch->data_count, source_children, branch->node_count, result);
        }

        d7_hamt_node *left = NULL;
        d7_hamt_node *right = NULL;
        d7_hamt_status status = d7_hamt_leaf_create(
            policy, existing->hash, existing->entry.key, existing->entry.value, &left);
        if (status == D7_HAMT_OK) {
            status = d7_hamt_leaf_create(policy, hash, key, value, &right);
        }
        if (status != D7_HAMT_OK) {
            d7_hamt_node_release(policy, left);
            return status;
        }
        d7_hamt_node *child = NULL;
        status = d7_hamt_merge_hash_nodes(
            policy, left, right, shift + D7_HAMT_BITS_PER_LEVEL, &child);
        if (status != D7_HAMT_OK) {
            return status;
        }
        for (size_t source = 0, target = 0; source < branch->data_count; ++source) {
            if (source != data_slot) {
                data[target++] = source_data[source];
            }
        }
        const size_t node_slot = d7_hamt_slot(branch->node_map, selected_bit);
        for (size_t source = 0, target = 0; target < branch->node_count + 1u; ++target) {
            children[target] = target == node_slot ? child : source_children[source++];
        }
        status = d7_hamt_bitmap_copy_create(
            policy,
            branch->data_map & ~selected_bit,
            branch->node_map | selected_bit,
            data,
            branch->data_count - 1u,
            children,
            branch->node_count + 1u,
            result);
        d7_hamt_node_release(policy, child);
        *added = true;
        return status;
    }

    if ((branch->node_map & selected_bit) == 0) {
        const size_t data_slot = d7_hamt_slot(branch->data_map, selected_bit);
        for (size_t source = 0, target = 0; target < branch->data_count + 1u; ++target) {
            if (target == data_slot) {
                data[target].hash = hash;
                data[target].entry.key = key;
                data[target].entry.value = value;
            } else {
                data[target] = source_data[source++];
            }
        }
        *added = true;
        return d7_hamt_bitmap_copy_create(
            policy,
            branch->data_map | selected_bit,
            branch->node_map,
            data,
            branch->data_count + 1u,
            source_children,
            branch->node_count,
            result);
    }

    const size_t selected_slot = d7_hamt_slot(branch->node_map, selected_bit);
    d7_hamt_node *new_child = NULL;
    d7_hamt_status status = d7_hamt_node_set(
        policy,
        source_children[selected_slot],
        key,
        value,
        hash,
        shift + D7_HAMT_BITS_PER_LEVEL,
        overwrite,
        added,
        &new_child);
    if (status != D7_HAMT_OK) {
        return status;
    }

    if (new_child == source_children[selected_slot]) {
        d7_hamt_node_release(policy, new_child);
        *result = d7_hamt_node_retain(node);
        return D7_HAMT_OK;
    }

    for (size_t i = 0; i < branch->node_count; ++i) {
        children[i] = i == selected_slot ? new_child : source_children[i];
    }
    status = d7_hamt_bitmap_copy_create(
        policy, branch->data_map, branch->node_map,
        source_data, branch->data_count, children, branch->node_count, result);
    d7_hamt_node_release(policy, new_child);
    return status;
}

static d7_hamt_status d7_hamt_select_added_value(
    const d7_hamt_factory_selection *selection,
    const void *key,
    const void **candidate) {
    *candidate = NULL;
    return selection->add_factory(key, selection->add_context, candidate);
}

static d7_hamt_status d7_hamt_select_present_value(
    const d7_hamt_factory_selection *selection,
    const void *key,
    const void *stored_value,
    const void **candidate) {
    if (selection->operation == D7_HAMT_FACTORY_GET_OR_ADD) {
        *candidate = stored_value;
        return D7_HAMT_OK;
    }

    *candidate = NULL;
    return selection->update_factory(
        key,
        stored_value,
        selection->update_context,
        candidate);
}

static d7_hamt_status d7_hamt_node_factory_update(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    const void *key,
    uint32_t hash,
    int shift,
    const d7_hamt_factory_selection *selection,
    bool *added,
    d7_hamt_node **result,
    const void **selected_value) {
    if (node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
        if (leaf->hash == hash && d7_hamt_keys_equal(policy, leaf->key, key)) {
            *added = false;
            const void *candidate = NULL;
            d7_hamt_status status =
                d7_hamt_select_present_value(selection, key, leaf->value, &candidate);
            if (status != D7_HAMT_OK) {
                return status;
            }
            if (candidate == leaf->value || d7_hamt_values_equal(policy, leaf->value, candidate)) {
                *result = d7_hamt_node_retain(node);
                *selected_value = leaf->value;
                return D7_HAMT_OK;
            }

            status = d7_hamt_leaf_create(policy, leaf->hash, leaf->key, candidate, result);
            if (status == D7_HAMT_OK) {
                *selected_value = ((const d7_hamt_leaf_node *)*result)->value;
            }
            return status;
        }

        const void *candidate = NULL;
        d7_hamt_status status = d7_hamt_select_added_value(selection, key, &candidate);
        if (status != D7_HAMT_OK) {
            return status;
        }
        d7_hamt_node *right = NULL;
        status = d7_hamt_leaf_create(policy, hash, key, candidate, &right);
        if (status != D7_HAMT_OK) {
            return status;
        }
        *added = true;
        return d7_hamt_merge_hash_nodes_selected(
            policy,
            d7_hamt_node_retain(node),
            right,
            shift,
            result,
            selected_value);
    }

    if (node->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)node;
        if (collision->hash != hash) {
            const void *candidate = NULL;
            d7_hamt_status status = d7_hamt_select_added_value(selection, key, &candidate);
            if (status != D7_HAMT_OK) {
                return status;
            }
            d7_hamt_node *right = NULL;
            status = d7_hamt_leaf_create(policy, hash, key, candidate, &right);
            if (status != D7_HAMT_OK) {
                return status;
            }
            *added = true;
            return d7_hamt_merge_hash_nodes_selected(
                policy,
                d7_hamt_node_retain(node),
                right,
                shift,
                result,
                selected_value);
        }

        for (size_t i = 0; i < collision->count; ++i) {
            if (!d7_hamt_keys_equal(policy, collision->entries[i].key, key)) {
                continue;
            }

            *added = false;
            const void *candidate = NULL;
            d7_hamt_status status = d7_hamt_select_present_value(
                selection,
                key,
                collision->entries[i].value,
                &candidate);
            if (status != D7_HAMT_OK) {
                return status;
            }
            if (candidate == collision->entries[i].value
                || d7_hamt_values_equal(policy, collision->entries[i].value, candidate)) {
                *result = d7_hamt_node_retain(node);
                *selected_value = collision->entries[i].value;
                return D7_HAMT_OK;
            }

            d7_hamt_collision_node *replaced =
                (d7_hamt_collision_node *)d7_hamt_allocate(
                    sizeof(*replaced) + collision->count * sizeof(d7_hamt_entry));
            if (replaced == NULL) {
                return D7_HAMT_OUT_OF_MEMORY;
            }
            replaced->base.kind = D7_HAMT_NODE_COLLISION;
            replaced->base.ref_count = 1;
            replaced->base.subtree_count = collision->count;
            replaced->hash = collision->hash;
            replaced->count = collision->count;

            size_t written = 0;
            const void *local_selected = NULL;
            for (size_t j = 0; status == D7_HAMT_OK && j < collision->count; ++j) {
                status = d7_hamt_collision_write_entry(
                    policy,
                    replaced,
                    &written,
                    collision->entries[j].key,
                    j == i ? candidate : collision->entries[j].value);
                if (status == D7_HAMT_OK && j == i) {
                    local_selected = replaced->entries[written - 1u].value;
                }
            }
            if (status != D7_HAMT_OK) {
                replaced->count = written;
                d7_hamt_node_release(policy, &replaced->base);
                return status;
            }
            *result = &replaced->base;
            *selected_value = local_selected;
            return D7_HAMT_OK;
        }

        const void *candidate = NULL;
        d7_hamt_status status = d7_hamt_select_added_value(selection, key, &candidate);
        if (status != D7_HAMT_OK) {
            return status;
        }
        d7_hamt_collision_node *expanded =
            (d7_hamt_collision_node *)d7_hamt_allocate(
                sizeof(*expanded) + (collision->count + 1u) * sizeof(d7_hamt_entry));
        if (expanded == NULL) {
            return D7_HAMT_OUT_OF_MEMORY;
        }
        expanded->base.kind = D7_HAMT_NODE_COLLISION;
        expanded->base.ref_count = 1;
        expanded->base.subtree_count = collision->count + 1u;
        expanded->hash = collision->hash;
        expanded->count = collision->count + 1u;
        size_t written = 0;
        for (size_t i = 0; status == D7_HAMT_OK && i < collision->count; ++i) {
            status = d7_hamt_collision_write_entry(
                policy,
                expanded,
                &written,
                collision->entries[i].key,
                collision->entries[i].value);
        }
        if (status == D7_HAMT_OK) {
            status = d7_hamt_collision_write_entry(
                policy,
                expanded,
                &written,
                key,
                candidate);
        }
        if (status != D7_HAMT_OK) {
            expanded->count = written;
            d7_hamt_node_release(policy, &expanded->base);
            return status;
        }
        *added = true;
        *result = &expanded->base;
        *selected_value = expanded->entries[expanded->count - 1u].value;
        return D7_HAMT_OK;
    }

    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
    const d7_hamt_inline_entry *source_data = d7_hamt_bitmap_data_const(branch);
    d7_hamt_node *const *source_children = d7_hamt_bitmap_children_const(branch);
    const uint32_t selected_bit = d7_hamt_bit(d7_hamt_index(hash, shift));
    d7_hamt_inline_entry data[32];
    d7_hamt_node *children[32];

    if ((branch->data_map & selected_bit) != 0) {
        const size_t data_slot = d7_hamt_slot(branch->data_map, selected_bit);
        const d7_hamt_inline_entry *existing = &source_data[data_slot];
        if (existing->hash == hash && d7_hamt_keys_equal(policy, existing->entry.key, key)) {
            *added = false;
            const void *candidate = NULL;
            d7_hamt_status status = d7_hamt_select_present_value(
                selection,
                key,
                existing->entry.value,
                &candidate);
            if (status != D7_HAMT_OK) {
                return status;
            }
            if (candidate == existing->entry.value
                || d7_hamt_values_equal(policy, existing->entry.value, candidate)) {
                *result = d7_hamt_node_retain(node);
                *selected_value = existing->entry.value;
                return D7_HAMT_OK;
            }
            memcpy(data, source_data, branch->data_count * sizeof(*data));
            data[data_slot].entry.value = candidate;
            status = d7_hamt_bitmap_copy_create(
                policy,
                branch->data_map,
                branch->node_map,
                data,
                branch->data_count,
                source_children,
                branch->node_count,
                result);
            if (status == D7_HAMT_OK) {
                *selected_value =
                    d7_hamt_bitmap_data_const((const d7_hamt_bitmap_node *)*result)[data_slot]
                        .entry.value;
            }
            return status;
        }

        const void *candidate = NULL;
        d7_hamt_status status = d7_hamt_select_added_value(selection, key, &candidate);
        if (status != D7_HAMT_OK) {
            return status;
        }
        d7_hamt_node *left = NULL;
        d7_hamt_node *right = NULL;
        status = d7_hamt_leaf_create(
            policy,
            existing->hash,
            existing->entry.key,
            existing->entry.value,
            &left);
        if (status == D7_HAMT_OK) {
            status = d7_hamt_leaf_create(policy, hash, key, candidate, &right);
        }
        if (status != D7_HAMT_OK) {
            d7_hamt_node_release(policy, left);
            return status;
        }
        d7_hamt_node *child = NULL;
        const void *child_selected = NULL;
        status = d7_hamt_merge_hash_nodes_selected(
            policy,
            left,
            right,
            shift + D7_HAMT_BITS_PER_LEVEL,
            &child,
            &child_selected);
        if (status != D7_HAMT_OK) {
            return status;
        }
        for (size_t source = 0, target = 0; source < branch->data_count; ++source) {
            if (source != data_slot) {
                data[target++] = source_data[source];
            }
        }
        const size_t node_slot = d7_hamt_slot(branch->node_map, selected_bit);
        for (size_t source = 0, target = 0; target < branch->node_count + 1u; ++target) {
            children[target] = target == node_slot ? child : source_children[source++];
        }
        status = d7_hamt_bitmap_copy_create(
            policy,
            branch->data_map & ~selected_bit,
            branch->node_map | selected_bit,
            data,
            branch->data_count - 1u,
            children,
            branch->node_count + 1u,
            result);
        d7_hamt_node_release(policy, child);
        if (status == D7_HAMT_OK) {
            *added = true;
            *selected_value = child_selected;
        }
        return status;
    }

    if ((branch->node_map & selected_bit) == 0) {
        const void *candidate = NULL;
        d7_hamt_status status = d7_hamt_select_added_value(selection, key, &candidate);
        if (status != D7_HAMT_OK) {
            return status;
        }
        const size_t data_slot = d7_hamt_slot(branch->data_map, selected_bit);
        for (size_t source = 0, target = 0; target < branch->data_count + 1u; ++target) {
            if (target == data_slot) {
                data[target].hash = hash;
                data[target].entry.key = key;
                data[target].entry.value = candidate;
            } else {
                data[target] = source_data[source++];
            }
        }
        status = d7_hamt_bitmap_copy_create(
            policy,
            branch->data_map | selected_bit,
            branch->node_map,
            data,
            branch->data_count + 1u,
            source_children,
            branch->node_count,
            result);
        if (status == D7_HAMT_OK) {
            *added = true;
            *selected_value =
                d7_hamt_bitmap_data_const((const d7_hamt_bitmap_node *)*result)[data_slot]
                    .entry.value;
        }
        return status;
    }

    const size_t selected_slot = d7_hamt_slot(branch->node_map, selected_bit);
    d7_hamt_node *new_child = NULL;
    const void *child_selected = NULL;
    d7_hamt_status status = d7_hamt_node_factory_update(
        policy,
        source_children[selected_slot],
        key,
        hash,
        shift + D7_HAMT_BITS_PER_LEVEL,
        selection,
        added,
        &new_child,
        &child_selected);
    if (status != D7_HAMT_OK) {
        return status;
    }

    if (new_child == source_children[selected_slot]) {
        d7_hamt_node_release(policy, new_child);
        *result = d7_hamt_node_retain(node);
        *selected_value = child_selected;
        return D7_HAMT_OK;
    }

    for (size_t i = 0; i < branch->node_count; ++i) {
        children[i] = i == selected_slot ? new_child : source_children[i];
    }
    status = d7_hamt_bitmap_copy_create(
        policy,
        branch->data_map,
        branch->node_map,
        source_data,
        branch->data_count,
        children,
        branch->node_count,
        result);
    d7_hamt_node_release(policy, new_child);
    if (status == D7_HAMT_OK) {
        *selected_value = child_selected;
    }
    return status;
}

static d7_hamt_status d7_hamt_node_remove(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    const void *key,
    uint32_t hash,
    int shift,
    bool *removed,
    const void **removed_value,
    d7_hamt_node **result) {
    if (node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
        if (leaf->hash == hash && d7_hamt_keys_equal(policy, leaf->key, key)) {
            *removed = true;
            *removed_value = leaf->value;
            *result = NULL;
            return D7_HAMT_OK;
        }

        *removed = false;
        *removed_value = NULL;
        *result = d7_hamt_node_retain(node);
        return D7_HAMT_OK;
    }

    if (node->kind == D7_HAMT_NODE_COLLISION) {
        const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)node;
        if (collision->hash != hash) {
            *removed = false;
            *removed_value = NULL;
            *result = d7_hamt_node_retain(node);
            return D7_HAMT_OK;
        }

        for (size_t i = 0; i < collision->count; ++i) {
            if (!d7_hamt_keys_equal(policy, collision->entries[i].key, key)) {
                continue;
            }

            *removed = true;
            *removed_value = collision->entries[i].value;
            if (collision->count == 2) {
                const size_t remaining = 1u - i;
                void *retained_key = NULL;
                void *retained_value = NULL;
                d7_hamt_status status =
                    d7_hamt_checked_retain_key(policy, collision->entries[remaining].key, &retained_key);
                if (status == D7_HAMT_OK) {
                    status = d7_hamt_checked_retain_value(
                        policy, collision->entries[remaining].value, &retained_value);
                }
                if (status == D7_HAMT_OK) {
                    status = d7_hamt_leaf_create_from_retained(
                        collision->hash, retained_key, retained_value, result);
                } else {
                    *result = NULL;
                }
                if (status != D7_HAMT_OK) {
                    d7_hamt_release_key(policy, retained_key);
                    d7_hamt_release_value(policy, retained_value);
                }
                return status;
            }

            d7_hamt_collision_node *shrunk =
                (d7_hamt_collision_node *)d7_hamt_allocate(
                    sizeof(*shrunk) + (collision->count - 1u) * sizeof(d7_hamt_entry));
            if (shrunk == NULL) {
                *result = NULL;
                return D7_HAMT_OUT_OF_MEMORY;
            }

            shrunk->base.kind = D7_HAMT_NODE_COLLISION;
            shrunk->base.ref_count = 1;
            shrunk->base.subtree_count = collision->count - 1;
            shrunk->hash = collision->hash;
            shrunk->count = collision->count - 1u;
            d7_hamt_status status = D7_HAMT_OK;
            size_t written = 0;
            for (size_t source = 0; status == D7_HAMT_OK && source < collision->count; ++source) {
                if (source == i) {
                    continue;
                }
                status = d7_hamt_collision_write_entry(
                    policy, shrunk, &written, collision->entries[source].key, collision->entries[source].value);
            }
            if (status != D7_HAMT_OK) {
                shrunk->count = written;
                d7_hamt_node_release(policy, &shrunk->base);
                *result = NULL;
                return status;
            }

            *result = &shrunk->base;
            return D7_HAMT_OK;
        }

        *removed = false;
        *removed_value = NULL;
        *result = d7_hamt_node_retain(node);
        return D7_HAMT_OK;
    }

    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
    const d7_hamt_inline_entry *source_data = d7_hamt_bitmap_data_const(branch);
    d7_hamt_node *const *source_children = d7_hamt_bitmap_children_const(branch);
    const uint32_t selected_bit = d7_hamt_bit(d7_hamt_index(hash, shift));
    d7_hamt_inline_entry data[32];
    d7_hamt_node *children[32];

    if ((branch->data_map & selected_bit) != 0) {
        const size_t selected_slot = d7_hamt_slot(branch->data_map, selected_bit);
        const d7_hamt_inline_entry *existing = &source_data[selected_slot];
        if (existing->hash != hash || !d7_hamt_keys_equal(policy, existing->entry.key, key)) {
            *removed = false;
            *removed_value = NULL;
            *result = d7_hamt_node_retain(node);
            return D7_HAMT_OK;
        }
        for (size_t source = 0, target = 0; source < branch->data_count; ++source) {
            if (source != selected_slot) {
                data[target++] = source_data[source];
            }
        }
        *removed = true;
        *removed_value = existing->entry.value;
        return d7_hamt_bitmap_rebuild(
            policy,
            branch->data_map & ~selected_bit,
            branch->node_map,
            data,
            branch->data_count - 1u,
            source_children,
            branch->node_count,
            result);
    }

    if ((branch->node_map & selected_bit) == 0) {
        *removed = false;
        *removed_value = NULL;
        *result = d7_hamt_node_retain(node);
        return D7_HAMT_OK;
    }

    const size_t selected_slot = d7_hamt_slot(branch->node_map, selected_bit);
    d7_hamt_node *new_child = NULL;
    d7_hamt_status status = d7_hamt_node_remove(
        policy,
        source_children[selected_slot],
        key,
        hash,
        shift + D7_HAMT_BITS_PER_LEVEL,
        removed,
        removed_value,
        &new_child);
    if (status != D7_HAMT_OK) {
        return status;
    }

    if (!*removed) {
        d7_hamt_node_release(policy, new_child);
        *result = d7_hamt_node_retain(node);
        return D7_HAMT_OK;
    }

    if (new_child == NULL) {
        for (size_t source = 0, target = 0; source < branch->node_count; ++source) {
            if (source == selected_slot) {
                continue;
            }
            children[target++] = source_children[source];
        }
        return d7_hamt_bitmap_rebuild(
            policy,
            branch->data_map,
            branch->node_map & ~selected_bit,
            source_data,
            branch->data_count,
            children,
            branch->node_count - 1u,
            result);
    }

    if (new_child->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)new_child;
        const size_t data_slot = d7_hamt_slot(branch->data_map, selected_bit);
        for (size_t source = 0, target = 0; target < branch->data_count + 1u; ++target) {
            if (target == data_slot) {
                data[target].hash = leaf->hash;
                data[target].entry.key = leaf->key;
                data[target].entry.value = leaf->value;
            } else {
                data[target] = source_data[source++];
            }
        }
        for (size_t source = 0, target = 0; source < branch->node_count; ++source) {
            if (source != selected_slot) {
                children[target++] = source_children[source];
            }
        }
        status = d7_hamt_bitmap_rebuild(
            policy,
            branch->data_map | selected_bit,
            branch->node_map & ~selected_bit,
            data,
            branch->data_count + 1u,
            children,
            branch->node_count - 1u,
            result);
        d7_hamt_node_release(policy, new_child);
        return status;
    }

    for (size_t i = 0; i < branch->node_count; ++i) {
        children[i] = i == selected_slot ? new_child : source_children[i];
    }
    status = d7_hamt_bitmap_rebuild(
        policy, branch->data_map, branch->node_map,
        source_data, branch->data_count,
        children, branch->node_count, result);
    d7_hamt_node_release(policy, new_child);
    return status;
}

static d7_hamt_status d7_hamt_bitmap_rebuild(
    const d7_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const d7_hamt_inline_entry *data,
    size_t data_count,
    d7_hamt_node *const *children,
    size_t child_count,
    d7_hamt_node **result) {
    if (data_count == 0 && child_count == 0) {
        *result = NULL;
        return D7_HAMT_OK;
    }
    if (data_count == 1u && child_count == 0) {
        return d7_hamt_leaf_create(
            policy, data[0].hash, data[0].entry.key, data[0].entry.value, result);
    }
    if (data_count == 0 && child_count == 1u
        && children[0]->kind != D7_HAMT_NODE_BITMAP_INDEXED) {
        *result = d7_hamt_node_retain(children[0]);
        return D7_HAMT_OK;
    }
    return d7_hamt_bitmap_copy_create(
        policy, data_map, node_map, data, data_count, children, child_count, result);
}

static bool d7_hamt_policy_callbacks_compatible(
    const d7_hamt_policy *left,
    const d7_hamt_policy *right) {
    return left->hash == right->hash &&
        left->key_equal == right->key_equal &&
        left->value_equal == right->value_equal &&
        left->retain_key == right->retain_key &&
        left->retain_value == right->retain_value &&
        left->release_key == right->release_key &&
        left->release_value == right->release_value &&
        left->context == right->context;
}

static uint32_t d7_hamt_hash_node_hash(const d7_hamt_node *node) {
    return node->kind == D7_HAMT_NODE_LEAF
        ? ((const d7_hamt_leaf_node *)node)->hash
        : ((const d7_hamt_collision_node *)node)->hash;
}

static size_t d7_hamt_hash_node_entry_count(const d7_hamt_node *node) {
    return node->kind == D7_HAMT_NODE_LEAF
        ? 1u
        : ((const d7_hamt_collision_node *)node)->count;
}

static d7_hamt_entry d7_hamt_hash_node_entry_at(
    const d7_hamt_node *node,
    size_t index) {
    if (node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
        return (d7_hamt_entry){ leaf->key, leaf->value };
    }
    return ((const d7_hamt_collision_node *)node)->entries[index];
}

static d7_hamt_status d7_hamt_hash_result_create(
    const d7_hamt_policy *policy,
    uint32_t hash,
    const d7_hamt_entry *entries,
    size_t count,
    d7_hamt_node **result) {
    if (count == 0) {
        *result = NULL;
        return D7_HAMT_OK;
    }
    if (count == 1) {
        return d7_hamt_leaf_create(
            policy, hash, entries[0].key, entries[0].value, result);
    }
    if (count > (SIZE_MAX - sizeof(d7_hamt_collision_node)) / sizeof(d7_hamt_entry)) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }
    d7_hamt_collision_node *collision =
        (d7_hamt_collision_node *)d7_hamt_allocate(
            sizeof(*collision) + count * sizeof(d7_hamt_entry));
    if (collision == NULL) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }
    collision->base.kind = D7_HAMT_NODE_COLLISION;
    collision->base.ref_count = 1;
    collision->base.subtree_count = count;
    collision->hash = hash;
    collision->count = 0;
    d7_hamt_status status = D7_HAMT_OK;
    while (status == D7_HAMT_OK && collision->count < count) {
        status = d7_hamt_collision_write_entry(
            policy,
            collision,
            &collision->count,
            entries[collision->count].key,
            entries[collision->count].value);
    }
    if (status != D7_HAMT_OK) {
        d7_hamt_node_release(policy, &collision->base);
        *result = NULL;
        return status;
    }
    *result = &collision->base;
    return D7_HAMT_OK;
}

static d7_hamt_status d7_hamt_logical_slot(
    const d7_hamt_policy *policy,
    const d7_hamt_node *node,
    int slot_index,
    int shift,
    d7_hamt_node **result) {
    if (node == NULL) {
        *result = NULL;
        return D7_HAMT_OK;
    }
    if (node->kind != D7_HAMT_NODE_BITMAP_INDEXED) {
        *result = d7_hamt_index(d7_hamt_hash_node_hash(node), shift) == slot_index
            ? d7_hamt_node_retain(node)
            : NULL;
        return D7_HAMT_OK;
    }
    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
    const uint32_t selected_bit = d7_hamt_bit(slot_index);
    if ((branch->data_map & selected_bit) != 0) {
        const d7_hamt_inline_entry *entry =
            &d7_hamt_bitmap_data_const(branch)[d7_hamt_slot(branch->data_map, selected_bit)];
        return d7_hamt_leaf_create(
            policy, entry->hash, entry->entry.key, entry->entry.value, result);
    }
    *result = (branch->node_map & selected_bit) != 0
        ? d7_hamt_node_retain(
            d7_hamt_bitmap_children_const(branch)[d7_hamt_slot(branch->node_map, selected_bit)])
        : NULL;
    return D7_HAMT_OK;
}

static void d7_hamt_release_slots(
    const d7_hamt_policy *policy,
    d7_hamt_node **slots) {
    for (size_t index = 0; index < 32; ++index) {
        d7_hamt_node_release(policy, slots[index]);
        slots[index] = NULL;
    }
}

static bool d7_hamt_slot_matches_inline(
    const d7_hamt_policy *policy,
    const d7_hamt_node *actual,
    const d7_hamt_inline_entry *expected) {
    if (actual == NULL || actual->kind != D7_HAMT_NODE_LEAF) {
        return false;
    }
    const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)actual;
    return leaf->hash == expected->hash &&
        d7_hamt_keys_equal(policy, leaf->key, expected->entry.key) &&
        d7_hamt_values_equal(policy, leaf->value, expected->entry.value);
}

static bool d7_hamt_logical_slots_match(
    const d7_hamt_policy *policy,
    d7_hamt_node *const *slots,
    const d7_hamt_node *original,
    int shift) {
    if (original->kind != D7_HAMT_NODE_BITMAP_INDEXED) {
        const int occupied = d7_hamt_index(d7_hamt_hash_node_hash(original), shift);
        for (int index = 0; index < 32; ++index) {
            if ((index == occupied && slots[index] != original) ||
                (index != occupied && slots[index] != NULL)) {
                return false;
            }
        }
        return true;
    }
    const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)original;
    const d7_hamt_inline_entry *data = d7_hamt_bitmap_data_const(branch);
    d7_hamt_node *const *children = d7_hamt_bitmap_children_const(branch);
    for (int index = 0; index < 32; ++index) {
        const uint32_t selected_bit = d7_hamt_bit(index);
        if ((branch->data_map & selected_bit) != 0) {
            if (!d7_hamt_slot_matches_inline(
                    policy,
                    slots[index],
                    &data[d7_hamt_slot(branch->data_map, selected_bit)])) {
                return false;
            }
        } else if ((branch->node_map & selected_bit) != 0) {
            if (slots[index] != children[d7_hamt_slot(branch->node_map, selected_bit)]) {
                return false;
            }
        } else if (slots[index] != NULL) {
            return false;
        }
    }
    return true;
}

static d7_hamt_status d7_hamt_build_logical_node(
    const d7_hamt_policy *policy,
    d7_hamt_node **slots,
    const d7_hamt_node *original_left,
    int shift,
    d7_hamt_node **result) {
    if (d7_hamt_logical_slots_match(policy, slots, original_left, shift)) {
        *result = d7_hamt_node_retain(original_left);
        d7_hamt_release_slots(policy, slots);
        return D7_HAMT_OK;
    }
    uint32_t data_map = 0;
    uint32_t node_map = 0;
    d7_hamt_inline_entry data[32];
    d7_hamt_node *children[32];
    size_t data_count = 0;
    size_t child_count = 0;
    for (int index = 0; index < 32; ++index) {
        const d7_hamt_node *node = slots[index];
        if (node == NULL) {
            continue;
        }
        if (node->kind == D7_HAMT_NODE_LEAF) {
            const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
            data_map |= d7_hamt_bit(index);
            data[data_count++] = (d7_hamt_inline_entry){
                leaf->hash, { leaf->key, leaf->value }
            };
        } else {
            node_map |= d7_hamt_bit(index);
            children[child_count++] = slots[index];
        }
    }
    const d7_hamt_status status = d7_hamt_bitmap_rebuild(
        policy,
        data_map,
        node_map,
        data,
        data_count,
        children,
        child_count,
        result);
    d7_hamt_release_slots(policy, slots);
    return status;
}

static d7_hamt_status d7_hamt_combine_hash_nodes(
    const d7_hamt_policy *policy,
    const d7_hamt_node *left,
    const d7_hamt_node *right,
    int shift,
    d7_hamt_combine_operation operation,
    d7_hamt_node **result) {
    const uint32_t left_hash = d7_hamt_hash_node_hash(left);
    const uint32_t right_hash = d7_hamt_hash_node_hash(right);
    if (left_hash != right_hash) {
        if (operation == D7_HAMT_COMBINE_INTERSECT) {
            *result = NULL;
            return D7_HAMT_OK;
        }
        if (operation == D7_HAMT_COMBINE_EXCEPT) {
            *result = d7_hamt_node_retain(left);
            return D7_HAMT_OK;
        }
        if (shift >= 32) {
            *result = NULL;
            return D7_HAMT_INVALID_ARGUMENT;
        }
        d7_hamt_node *slots[32] = { NULL };
        const int left_index = d7_hamt_index(left_hash, shift);
        const int right_index = d7_hamt_index(right_hash, shift);
        d7_hamt_status status = D7_HAMT_OK;
        if (left_index != right_index) {
            slots[left_index] = d7_hamt_node_retain(left);
            slots[right_index] = d7_hamt_node_retain(right);
        } else {
            status = d7_hamt_combine_hash_nodes(
                policy,
                left,
                right,
                shift + D7_HAMT_BITS_PER_LEVEL,
                operation,
                &slots[left_index]);
        }
        if (status != D7_HAMT_OK) {
            d7_hamt_release_slots(policy, slots);
            return status;
        }
        return d7_hamt_build_logical_node(policy, slots, left, shift, result);
    }

    const size_t left_count = d7_hamt_hash_node_entry_count(left);
    const size_t right_count = d7_hamt_hash_node_entry_count(right);
    if (left_count > SIZE_MAX - right_count ||
        left_count + right_count > SIZE_MAX / sizeof(d7_hamt_entry)) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }
    d7_hamt_entry *entries = (d7_hamt_entry *)d7_hamt_allocate(
        (left_count + right_count) * sizeof(*entries));
    if (entries == NULL) {
        *result = NULL;
        return D7_HAMT_OUT_OF_MEMORY;
    }
    size_t written = 0;
    for (size_t i = 0; i < left_count; ++i) {
        const d7_hamt_entry left_entry = d7_hamt_hash_node_entry_at(left, i);
        bool found = false;
        d7_hamt_entry matching_right = { NULL, NULL };
        for (size_t j = 0; j < right_count; ++j) {
            const d7_hamt_entry candidate = d7_hamt_hash_node_entry_at(right, j);
            if (d7_hamt_keys_equal(policy, left_entry.key, candidate.key)) {
                found = true;
                matching_right = candidate;
                break;
            }
        }
        if (operation == D7_HAMT_COMBINE_UNION) {
            entries[written++] = (d7_hamt_entry){
                left_entry.key,
                found && !d7_hamt_values_equal(
                    policy, left_entry.value, matching_right.value)
                    ? matching_right.value
                    : left_entry.value
            };
        } else if (operation == D7_HAMT_COMBINE_INTERSECT && found) {
            entries[written++] = left_entry;
        } else if ((operation == D7_HAMT_COMBINE_EXCEPT ||
                    operation == D7_HAMT_COMBINE_SYMMETRIC_EXCEPT) && !found) {
            entries[written++] = left_entry;
        }
    }
    if (operation == D7_HAMT_COMBINE_UNION ||
        operation == D7_HAMT_COMBINE_SYMMETRIC_EXCEPT) {
        for (size_t j = 0; j < right_count; ++j) {
            const d7_hamt_entry right_entry = d7_hamt_hash_node_entry_at(right, j);
            bool found = false;
            for (size_t i = 0; i < left_count; ++i) {
                const d7_hamt_entry left_entry = d7_hamt_hash_node_entry_at(left, i);
                if (d7_hamt_keys_equal(policy, left_entry.key, right_entry.key)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                entries[written++] = right_entry;
            }
        }
    }
    bool matches_left = written == left_count;
    for (size_t i = 0; matches_left && i < written; ++i) {
        const d7_hamt_entry original = d7_hamt_hash_node_entry_at(left, i);
        matches_left = d7_hamt_keys_equal(policy, original.key, entries[i].key) &&
            d7_hamt_values_equal(policy, original.value, entries[i].value);
    }
    d7_hamt_status status;
    if (matches_left) {
        *result = d7_hamt_node_retain(left);
        status = D7_HAMT_OK;
    } else {
        status = d7_hamt_hash_result_create(
            policy, left_hash, entries, written, result);
    }
    free(entries);
    return status;
}

static d7_hamt_status d7_hamt_combine_nodes(
    const d7_hamt_policy *policy,
    const d7_hamt_node *left,
    const d7_hamt_node *right,
    int shift,
    d7_hamt_combine_operation operation,
    d7_hamt_node **result) {
    if (left == right) {
        *result = operation == D7_HAMT_COMBINE_UNION ||
                operation == D7_HAMT_COMBINE_INTERSECT
            ? d7_hamt_node_retain(left)
            : NULL;
        return D7_HAMT_OK;
    }
    if (left == NULL) {
        *result = operation == D7_HAMT_COMBINE_UNION ||
                operation == D7_HAMT_COMBINE_SYMMETRIC_EXCEPT
            ? d7_hamt_node_retain(right)
            : NULL;
        return D7_HAMT_OK;
    }
    if (right == NULL) {
        *result = operation == D7_HAMT_COMBINE_INTERSECT
            ? NULL
            : d7_hamt_node_retain(left);
        return D7_HAMT_OK;
    }
    if (left->kind != D7_HAMT_NODE_BITMAP_INDEXED &&
        right->kind != D7_HAMT_NODE_BITMAP_INDEXED) {
        return d7_hamt_combine_hash_nodes(
            policy, left, right, shift, operation, result);
    }

    d7_hamt_node *slots[32] = { NULL };
    d7_hamt_status status = D7_HAMT_OK;
    for (int index = 0; status == D7_HAMT_OK && index < 32; ++index) {
        d7_hamt_node *left_slot = NULL;
        d7_hamt_node *right_slot = NULL;
        status = d7_hamt_logical_slot(policy, left, index, shift, &left_slot);
        if (status == D7_HAMT_OK) {
            status = d7_hamt_logical_slot(policy, right, index, shift, &right_slot);
        }
        if (status == D7_HAMT_OK) {
            status = d7_hamt_combine_nodes(
                policy,
                left_slot,
                right_slot,
                shift + D7_HAMT_BITS_PER_LEVEL,
                operation,
                &slots[index]);
        }
        d7_hamt_node_release(policy, left_slot);
        d7_hamt_node_release(policy, right_slot);
    }
    if (status != D7_HAMT_OK) {
        d7_hamt_release_slots(policy, slots);
        *result = NULL;
        return status;
    }
    return d7_hamt_build_logical_node(policy, slots, left, shift, result);
}

static bool d7_hamt_try_get_entry(
    const d7_hamt_map *map,
    const void *key,
    const void **actual_key,
    const void **value) {
    if (map == NULL || map->root == NULL) {
        return false;
    }

    const uint32_t hash = d7_hamt_get_hash(map, key);
    int shift = 0;
    const d7_hamt_node *node = map->root;

    while (node->kind == D7_HAMT_NODE_BITMAP_INDEXED) {
        const d7_hamt_bitmap_node *branch = (const d7_hamt_bitmap_node *)node;
        const uint32_t selected_bit = d7_hamt_bit(d7_hamt_index(hash, shift));
        if ((branch->data_map & selected_bit) != 0) {
            const d7_hamt_inline_entry *entry =
                &d7_hamt_bitmap_data_const(branch)[d7_hamt_slot(branch->data_map, selected_bit)];
            if (entry->hash == hash && d7_hamt_keys_equal(&map->policy, entry->entry.key, key)) {
                if (actual_key != NULL) {
                    *actual_key = entry->entry.key;
                }
                if (value != NULL) {
                    *value = entry->entry.value;
                }
                return true;
            }
            return false;
        }
        if ((branch->node_map & selected_bit) == 0) {
            return false;
        }

        node = d7_hamt_bitmap_children_const(branch)[d7_hamt_slot(branch->node_map, selected_bit)];
        shift += D7_HAMT_BITS_PER_LEVEL;
    }

    if (node->kind == D7_HAMT_NODE_LEAF) {
        const d7_hamt_leaf_node *leaf = (const d7_hamt_leaf_node *)node;
        if (leaf->hash == hash && d7_hamt_keys_equal(&map->policy, leaf->key, key)) {
            if (actual_key != NULL) {
                *actual_key = leaf->key;
            }
            if (value != NULL) {
                *value = leaf->value;
            }
            return true;
        }

        return false;
    }

    const d7_hamt_collision_node *collision = (const d7_hamt_collision_node *)node;
    if (collision->hash != hash) {
        return false;
    }

    for (size_t i = 0; i < collision->count; ++i) {
        if (d7_hamt_keys_equal(&map->policy, collision->entries[i].key, key)) {
            if (actual_key != NULL) {
                *actual_key = collision->entries[i].key;
            }
            if (value != NULL) {
                *value = collision->entries[i].value;
            }
            return true;
        }
    }

    return false;
}
