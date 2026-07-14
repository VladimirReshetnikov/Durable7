#include <Tools/DataStructures/Hamt/hamt.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef TDS_HAMT_TESTING
static size_t tds_hamt_test_fail_after = SIZE_MAX;
static size_t tds_hamt_test_allocation_count = 0;

void tds_hamt_test_fail_allocations_after(size_t successful_allocations) {
    tds_hamt_test_fail_after = successful_allocations;
    tds_hamt_test_allocation_count = 0;
}

void tds_hamt_test_reset_allocator(void) {
    tds_hamt_test_fail_after = SIZE_MAX;
    tds_hamt_test_allocation_count = 0;
}
#endif

static void *tds_hamt_allocate(size_t size) {
#ifdef TDS_HAMT_TESTING
    if (tds_hamt_test_allocation_count == tds_hamt_test_fail_after) {
        return NULL;
    }

    ++tds_hamt_test_allocation_count;
#endif
    return malloc(size);
}

enum {
    TDS_HAMT_BITS_PER_LEVEL = 5,
    TDS_HAMT_BRANCH_MASK = 31
};

typedef struct tds_hamt_node tds_hamt_node;

struct tds_hamt_map_transient_state {
    size_t ref_count;
    size_t version;
    bool active;
    tds_hamt_map map;
};

struct tds_hamt_node {
    tds_hamt_node_kind kind;
    size_t ref_count;
    size_t subtree_count;
};

typedef struct tds_hamt_leaf_node {
    tds_hamt_node base;
    uint32_t hash;
    void *key;
    void *value;
} tds_hamt_leaf_node;

typedef struct tds_hamt_collision_node {
    tds_hamt_node base;
    uint32_t hash;
    size_t count;
    tds_hamt_entry entries[];
} tds_hamt_collision_node;

typedef struct tds_hamt_inline_entry {
    uint32_t hash;
    tds_hamt_entry entry;
} tds_hamt_inline_entry;

typedef struct tds_hamt_bitmap_node {
    tds_hamt_node base;
    uint32_t data_map;
    uint32_t node_map;
    size_t data_count;
    size_t node_count;
    unsigned char storage[];
} tds_hamt_bitmap_node;

typedef struct tds_hamt_entry_run_view {
    uint32_t hash;
    size_t count;
    const tds_hamt_entry *entries;
    tds_hamt_entry single;
    bool is_single;
} tds_hamt_entry_run_view;

typedef struct tds_hamt_diff_operand {
    const tds_hamt_node *node;
    const tds_hamt_inline_entry *inline_entry;
} tds_hamt_diff_operand;

typedef enum tds_hamt_combine_operation {
    TDS_HAMT_COMBINE_UNION,
    TDS_HAMT_COMBINE_INTERSECT,
    TDS_HAMT_COMBINE_EXCEPT,
    TDS_HAMT_COMBINE_SYMMETRIC_EXCEPT
} tds_hamt_combine_operation;

static tds_hamt_inline_entry *tds_hamt_bitmap_data(tds_hamt_bitmap_node *node);
static const tds_hamt_inline_entry *tds_hamt_bitmap_data_const(const tds_hamt_bitmap_node *node);
static tds_hamt_node **tds_hamt_bitmap_children(tds_hamt_bitmap_node *node);
static tds_hamt_node *const *tds_hamt_bitmap_children_const(const tds_hamt_bitmap_node *node);

static uint32_t tds_hamt_pointer_hash(const void *item, void *context);
static bool tds_hamt_pointer_equal(const void *left, const void *right, void *context);
static void *tds_hamt_identity_retain(const void *item, void *context);
static bool tds_hamt_unit_equal(const void *left, const void *right, void *context);

static tds_hamt_policy tds_hamt_normalize_policy(const tds_hamt_policy *policy);
static tds_hamt_set_policy tds_hamt_normalize_set_policy(const tds_hamt_set_policy *policy);
static tds_hamt_policy tds_hamt_map_policy_from_set_policy(const tds_hamt_set_policy *policy);

static tds_hamt_node *tds_hamt_node_retain(const tds_hamt_node *node);
static void tds_hamt_node_release(const tds_hamt_policy *policy, const tds_hamt_node *node);
static uint32_t tds_hamt_get_hash(const tds_hamt_map *map, const void *key);
static bool tds_hamt_keys_equal(const tds_hamt_policy *policy, const void *left, const void *right);
static bool tds_hamt_values_equal(const tds_hamt_policy *policy, const void *left, const void *right);
static void *tds_hamt_retain_key(const tds_hamt_policy *policy, const void *key);
static void *tds_hamt_retain_value(const tds_hamt_policy *policy, const void *value);
static void tds_hamt_release_key(const tds_hamt_policy *policy, void *key);
static void tds_hamt_release_value(const tds_hamt_policy *policy, void *value);

static int tds_hamt_index(uint32_t hash, int shift);
static uint32_t tds_hamt_bit(int index);
static size_t tds_hamt_slot(uint32_t bitmap, uint32_t bit);
static size_t tds_hamt_popcount(uint32_t value);

static tds_hamt_status tds_hamt_leaf_create(
    const tds_hamt_policy *policy,
    uint32_t hash,
    const void *key,
    const void *value,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_leaf_create_from_retained(
    uint32_t hash,
    void *key,
    void *value,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_collision_create(
    const tds_hamt_policy *policy,
    tds_hamt_node *left,
    tds_hamt_node *right,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_bitmap_create(
    uint32_t data_map,
    uint32_t node_map,
    const tds_hamt_inline_entry *data,
    size_t data_count,
    tds_hamt_node **children,
    size_t child_count,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_bitmap_copy_create(
    const tds_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const tds_hamt_inline_entry *data,
    size_t data_count,
    tds_hamt_node *const *children,
    size_t child_count,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_merge_hash_nodes(
    const tds_hamt_policy *policy,
    tds_hamt_node *left,
    tds_hamt_node *right,
    int shift,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_node_set(
    const tds_hamt_policy *policy,
    const tds_hamt_node *node,
    const void *key,
    const void *value,
    uint32_t hash,
    int shift,
    bool overwrite,
    bool *added,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_node_remove(
    const tds_hamt_policy *policy,
    const tds_hamt_node *node,
    const void *key,
    uint32_t hash,
    int shift,
    bool *removed,
    const void **removed_value,
    tds_hamt_node **result);
static tds_hamt_status tds_hamt_bitmap_rebuild(
    const tds_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const tds_hamt_inline_entry *data,
    size_t data_count,
    tds_hamt_node *const *children,
    size_t child_count,
    tds_hamt_node **result);
static bool tds_hamt_policy_callbacks_compatible(
    const tds_hamt_policy *left,
    const tds_hamt_policy *right);
static tds_hamt_status tds_hamt_combine_nodes(
    const tds_hamt_policy *policy,
    const tds_hamt_node *left,
    const tds_hamt_node *right,
    int shift,
    tds_hamt_combine_operation operation,
    tds_hamt_node **result);

static bool tds_hamt_try_get_entry(
    const tds_hamt_map *map,
    const void *key,
    const void **actual_key,
    const void **value);

tds_hamt_policy tds_hamt_policy_default(void) {
    tds_hamt_policy policy;
    memset(&policy, 0, sizeof(policy));
    policy.hash = tds_hamt_pointer_hash;
    policy.key_equal = tds_hamt_pointer_equal;
    policy.value_equal = tds_hamt_pointer_equal;
    return policy;
}

tds_hamt_set_policy tds_hamt_set_policy_default(void) {
    tds_hamt_set_policy policy;
    memset(&policy, 0, sizeof(policy));
    policy.hash = tds_hamt_pointer_hash;
    policy.equal = tds_hamt_pointer_equal;
    return policy;
}

tds_hamt_map tds_hamt_map_create(const tds_hamt_policy *policy) {
    tds_hamt_map map;
    map.root = NULL;
    map.count = 0;
    map.policy = tds_hamt_normalize_policy(policy);
    return map;
}

tds_hamt_status tds_hamt_map_create_range(
    const tds_hamt_policy *policy,
    const tds_hamt_entry *entries,
    size_t entry_count,
    tds_hamt_map *result) {
    if (result == NULL || (entry_count != 0 && entries == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map map = tds_hamt_map_create(policy);
    for (size_t i = 0; i < entry_count; ++i) {
        tds_hamt_map next;
        const tds_hamt_status status = tds_hamt_map_set(&map, entries[i].key, entries[i].value, &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_map_destroy(&map);
            return status;
        }

        tds_hamt_map_destroy(&map);
        map = next;
    }

    *result = map;
    return TDS_HAMT_OK;
}

tds_hamt_map tds_hamt_map_clone(const tds_hamt_map *map) {
    if (map == NULL) {
        return tds_hamt_map_create(NULL);
    }

    tds_hamt_map clone = *map;
    clone.root = tds_hamt_node_retain(map->root);
    return clone;
}

void tds_hamt_map_destroy(tds_hamt_map *map) {
    if (map == NULL) {
        return;
    }

    tds_hamt_node_release(&map->policy, map->root);
    map->root = NULL;
    map->count = 0;
}

size_t tds_hamt_map_count(const tds_hamt_map *map) {
    return map == NULL ? 0 : map->count;
}

bool tds_hamt_map_is_empty(const tds_hamt_map *map) {
    return tds_hamt_map_count(map) == 0;
}

bool tds_hamt_map_contains_key(const tds_hamt_map *map, const void *key) {
    const void *value = NULL;
    return tds_hamt_map_try_get(map, key, &value);
}

bool tds_hamt_map_try_get(const tds_hamt_map *map, const void *key, const void **value) {
    const void *actual_key = NULL;
    const void *found_value = NULL;
    const bool found = tds_hamt_try_get_entry(map, key, &actual_key, &found_value);
    if (value != NULL) {
        *value = found ? found_value : NULL;
    }

    return found;
}

bool tds_hamt_map_try_get_key(const tds_hamt_map *map, const void *equal_key, const void **actual_key) {
    const void *found_key = NULL;
    const void *value = NULL;
    const bool found = tds_hamt_try_get_entry(map, equal_key, &found_key, &value);
    if (actual_key != NULL) {
        *actual_key = found ? found_key : equal_key;
    }

    return found;
}

tds_hamt_status tds_hamt_map_set(
    const tds_hamt_map *map,
    const void *key,
    const void *value,
    tds_hamt_map *result) {
    if (map == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const uint32_t hash = tds_hamt_get_hash(map, key);
    tds_hamt_node *new_root = NULL;
    bool added = false;
    tds_hamt_status status;

    if (map->root == NULL) {
        status = tds_hamt_leaf_create(&map->policy, hash, key, value, &new_root);
        added = true;
    } else {
        status = tds_hamt_node_set(
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

    if (status != TDS_HAMT_OK) {
        return status;
    }

    if (result == map) {
        /* In-place update: drop the reference the source owned so the
         * overwritten root is not leaked. */
        tds_hamt_node_release(&map->policy, map->root);
    }
    result->root = new_root;
    result->count = map->count + (added ? 1u : 0u);
    result->policy = map->policy;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_set_many(
    const tds_hamt_map *map,
    const tds_hamt_entry *entries,
    size_t entry_count,
    tds_hamt_map *result) {
    if (map == NULL || result == NULL || (entry_count != 0 && entries == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map current = tds_hamt_map_clone(map);
    for (size_t i = 0; i < entry_count; ++i) {
        tds_hamt_map next;
        const tds_hamt_status status = tds_hamt_map_set(&current, entries[i].key, entries[i].value, &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_map_destroy(&current);
            return status;
        }

        tds_hamt_map_destroy(&current);
        current = next;
    }

    if (result == map) {
        tds_hamt_node_release(&map->policy, map->root);
    }
    *result = current;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_add(
    const tds_hamt_map *map,
    const void *key,
    const void *value,
    tds_hamt_map *result) {
    bool added = false;
    const tds_hamt_status status = tds_hamt_map_try_add(map, key, value, result, &added);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    if (!added) {
        /* A rejected duplicate publishes the source root re-retained, so an
         * aliased result already holds the original version with balanced
         * reference counts; destroying it would free the caller's only
         * handle. Only a distinct result owns a reference to release. */
        if (result != map) {
            tds_hamt_map_destroy(result);
        }
        return TDS_HAMT_DUPLICATE_KEY;
    }

    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_try_add(
    const tds_hamt_map *map,
    const void *key,
    const void *value,
    tds_hamt_map *result,
    bool *added) {
    if (map == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    if (added != NULL) {
        *added = false;
    }

    const uint32_t hash = tds_hamt_get_hash(map, key);
    tds_hamt_node *new_root = NULL;
    bool local_added = false;
    tds_hamt_status status;

    if (map->root == NULL) {
        status = tds_hamt_leaf_create(&map->policy, hash, key, value, &new_root);
        local_added = true;
    } else {
        status = tds_hamt_node_set(
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

    if (status != TDS_HAMT_OK) {
        return status;
    }

    if (result == map) {
        tds_hamt_node_release(&map->policy, map->root);
    }
    result->root = new_root;
    result->count = map->count + (local_added ? 1u : 0u);
    result->policy = map->policy;
    if (added != NULL) {
        *added = local_added;
    }

    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_remove(
    const tds_hamt_map *map,
    const void *key,
    tds_hamt_map *result) {
    bool removed = false;
    const void *removed_value = NULL;
    return tds_hamt_map_try_remove(map, key, result, &removed, &removed_value);
}

tds_hamt_status tds_hamt_map_try_remove(
    const tds_hamt_map *map,
    const void *key,
    tds_hamt_map *result,
    bool *removed,
    const void **removed_value) {
    if (map == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    if (removed != NULL) {
        *removed = false;
    }
    if (removed_value != NULL) {
        *removed_value = NULL;
    }

    if (map->root == NULL) {
        *result = tds_hamt_map_clone(map);
        if (removed != NULL) {
            *removed = false;
        }
        if (removed_value != NULL) {
            *removed_value = NULL;
        }
        return TDS_HAMT_OK;
    }

    bool local_removed = false;
    const void *local_removed_value = NULL;
    tds_hamt_node *new_root = NULL;
    const tds_hamt_status status = tds_hamt_node_remove(
        &map->policy,
        map->root,
        key,
        tds_hamt_get_hash(map, key),
        0,
        &local_removed,
        &local_removed_value,
        &new_root);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    const bool aliased = result == map;
    if (aliased) {
        tds_hamt_node_release(&map->policy, map->root);
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

    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_clear(const tds_hamt_map *map, tds_hamt_map *result) {
    if (map == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    if (map->count == 0) {
        *result = tds_hamt_map_clone(map);
    } else {
        if (result == map) {
            tds_hamt_node_release(&map->policy, map->root);
        }
        result->root = NULL;
        result->count = 0;
        result->policy = map->policy;
    }

    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_map_combine(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_combine_operation operation,
    tds_hamt_map *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map normalized_right;
    const tds_hamt_map *compatible_right = right;
    bool owns_normalized_right = false;
    tds_hamt_status status = TDS_HAMT_OK;
    if (!tds_hamt_policy_callbacks_compatible(&left->policy, &right->policy)) {
        normalized_right = tds_hamt_map_create(&left->policy);
        owns_normalized_right = true;
        tds_hamt_map_iterator iterator;
        tds_hamt_map_iterator_init(right, &iterator);
        const void *key = NULL;
        const void *value = NULL;
        while (status == TDS_HAMT_OK &&
               tds_hamt_map_iterator_next(&iterator, &key, &value)) {
            status = tds_hamt_map_set(&normalized_right, key, value, &normalized_right);
        }
        compatible_right = &normalized_right;
    }

    tds_hamt_node *root = NULL;
    if (status == TDS_HAMT_OK) {
        status = tds_hamt_combine_nodes(
            &left->policy,
            left->root,
            compatible_right->root,
            0,
            operation,
            &root);
    }
    if (owns_normalized_right) {
        tds_hamt_map_destroy(&normalized_right);
    }
    if (status != TDS_HAMT_OK) {
        return status;
    }

    const size_t count = root == NULL ? 0 : root->subtree_count;
    if (result == left) {
        tds_hamt_node_release(&left->policy, left->root);
    }
    if (result == right && right != left) {
        tds_hamt_node_release(&right->policy, right->root);
    }
    result->root = root;
    result->count = count;
    result->policy = left->policy;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_union(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_map *result) {
    return tds_hamt_map_combine(left, right, TDS_HAMT_COMBINE_UNION, result);
}

tds_hamt_status tds_hamt_map_intersect(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_map *result) {
    return tds_hamt_map_combine(left, right, TDS_HAMT_COMBINE_INTERSECT, result);
}

tds_hamt_status tds_hamt_map_except(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_map *result) {
    return tds_hamt_map_combine(left, right, TDS_HAMT_COMBINE_EXCEPT, result);
}

tds_hamt_status tds_hamt_map_symmetric_except(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_map *result) {
    return tds_hamt_map_combine(
        left, right, TDS_HAMT_COMBINE_SYMMETRIC_EXCEPT, result);
}

void tds_hamt_map_iterator_init(const tds_hamt_map *map, tds_hamt_map_iterator *iterator) {
    if (iterator == NULL) {
        return;
    }

    memset(iterator, 0, sizeof(*iterator));
    iterator->next = map == NULL ? NULL : map->root;
}

bool tds_hamt_map_iterator_next(
    tds_hamt_map_iterator *iterator,
    const void **key,
    const void **value) {
    if (iterator == NULL) {
        return false;
    }

    if (iterator->collision_entries != NULL) {
        if (iterator->collision_index < iterator->collision_count) {
            const tds_hamt_entry *entry = &iterator->collision_entries[iterator->collision_index++];
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

    const tds_hamt_node *node = iterator->next;
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

            tds_hamt_map_iterator_frame *top = &iterator->frames[iterator->depth - 1];
            if (top->data_index < top->data_count) {
                const tds_hamt_inline_entry *data = (const tds_hamt_inline_entry *)top->data;
                const tds_hamt_inline_entry *entry = &data[top->data_index++];
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

        if (node->kind == TDS_HAMT_NODE_LEAF) {
            const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
            if (key != NULL) {
                *key = leaf->key;
            }
            if (value != NULL) {
                *value = leaf->value;
            }
            return true;
        }

        if (node->kind == TDS_HAMT_NODE_COLLISION) {
            const tds_hamt_collision_node *collision = (const tds_hamt_collision_node *)node;
            iterator->collision_entries = collision->entries;
            iterator->collision_count = collision->count;
            iterator->collision_index = 0;
            return tds_hamt_map_iterator_next(iterator, key, value);
        }

        const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
        assert(iterator->depth < 7);
        iterator->frames[iterator->depth].data = tds_hamt_bitmap_data_const(branch);
        iterator->frames[iterator->depth].data_count = branch->data_count;
        iterator->frames[iterator->depth].data_index = 0;
        iterator->frames[iterator->depth].children =
            (const tds_hamt_node *const *)tds_hamt_bitmap_children_const(branch);
        iterator->frames[iterator->depth].child_count = branch->node_count;
        iterator->frames[iterator->depth].child_index = 0;
        ++iterator->depth;
        node = NULL;
    }
}

bool tds_hamt_map_shares_root(const tds_hamt_map *left, const tds_hamt_map *right) {
    return left != NULL && right != NULL && left->root == right->root;
}

static bool tds_hamt_policies_compatible(const tds_hamt_map *left, const tds_hamt_map *right) {
    return left->policy.hash == right->policy.hash
        && left->policy.key_equal == right->policy.key_equal
        && left->policy.value_equal == right->policy.value_equal
        && left->policy.context == right->policy.context;
}

static bool tds_hamt_nodes_equal(
    const tds_hamt_node *left,
    const tds_hamt_node *right,
    const tds_hamt_policy *policy) {
    if (left == right) {
        return true;
    }
    if (left == NULL || right == NULL || left->kind != right->kind) {
        return false;
    }
    if (left->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *l = (const tds_hamt_leaf_node *)left;
        const tds_hamt_leaf_node *r = (const tds_hamt_leaf_node *)right;
        return l->hash == r->hash
            && tds_hamt_keys_equal(policy, l->key, r->key)
            && tds_hamt_values_equal(policy, l->value, r->value);
    }
    if (left->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *l = (const tds_hamt_collision_node *)left;
        const tds_hamt_collision_node *r = (const tds_hamt_collision_node *)right;
        if (l->hash != r->hash || l->count != r->count) {
            return false;
        }
        for (size_t left_index = 0; left_index != l->count; ++left_index) {
            bool found = false;
            for (size_t right_index = 0; right_index != r->count; ++right_index) {
                if (tds_hamt_keys_equal(
                        policy,
                        l->entries[left_index].key,
                        r->entries[right_index].key)
                    && tds_hamt_values_equal(
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

    const tds_hamt_bitmap_node *l = (const tds_hamt_bitmap_node *)left;
    const tds_hamt_bitmap_node *r = (const tds_hamt_bitmap_node *)right;
    if (l->data_map != r->data_map || l->node_map != r->node_map
        || l->data_count != r->data_count || l->node_count != r->node_count) {
        return false;
    }
    const tds_hamt_inline_entry *left_data = tds_hamt_bitmap_data_const(l);
    const tds_hamt_inline_entry *right_data = tds_hamt_bitmap_data_const(r);
    for (size_t index = 0; index != l->data_count; ++index) {
        if (left_data[index].hash != right_data[index].hash
            || !tds_hamt_keys_equal(
                policy, left_data[index].entry.key, right_data[index].entry.key)
            || !tds_hamt_values_equal(
                policy, left_data[index].entry.value, right_data[index].entry.value)) {
            return false;
        }
    }
    tds_hamt_node *const *left_children = tds_hamt_bitmap_children_const(l);
    tds_hamt_node *const *right_children = tds_hamt_bitmap_children_const(r);
    for (size_t index = 0; index != l->node_count; ++index) {
        if (!tds_hamt_nodes_equal(left_children[index], right_children[index], policy)) {
            return false;
        }
    }
    return true;
}

static bool tds_hamt_diff_operand_is_empty(tds_hamt_diff_operand operand) {
    return operand.node == NULL && operand.inline_entry == NULL;
}

static bool tds_hamt_diff_operand_is_run(tds_hamt_diff_operand operand) {
    return operand.inline_entry != NULL
        || (operand.node != NULL && operand.node->kind != TDS_HAMT_NODE_BITMAP_INDEXED);
}

static tds_hamt_entry_run_view tds_hamt_entry_run_from_operand(tds_hamt_diff_operand operand) {
    tds_hamt_entry_run_view result = { 0 };
    result.is_single = true;
    result.count = 1;
    if (operand.inline_entry != NULL) {
        result.hash = operand.inline_entry->hash;
        result.single = operand.inline_entry->entry;
        return result;
    }

    assert(operand.node != NULL);
    if (operand.node->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)operand.node;
        result.hash = leaf->hash;
        result.single.key = leaf->key;
        result.single.value = leaf->value;
        return result;
    }

    assert(operand.node->kind == TDS_HAMT_NODE_COLLISION);
    const tds_hamt_collision_node *collision = (const tds_hamt_collision_node *)operand.node;
    result.hash = collision->hash;
    result.count = collision->count;
    result.entries = collision->entries;
    result.is_single = false;
    return result;
}

static const tds_hamt_entry *tds_hamt_entry_run_at(
    const tds_hamt_entry_run_view *run,
    size_t index) {
    assert(index < run->count);
    return run->is_single ? &run->single : &run->entries[index];
}

static tds_hamt_diff_operand tds_hamt_diff_operand_logical_slot(
    tds_hamt_diff_operand operand,
    int index,
    int shift) {
    if (tds_hamt_diff_operand_is_empty(operand)) {
        return operand;
    }
    if (tds_hamt_diff_operand_is_run(operand)) {
        assert(shift < 32);
        const tds_hamt_entry_run_view run = tds_hamt_entry_run_from_operand(operand);
        return tds_hamt_index(run.hash, shift) == index
            ? operand
            : (tds_hamt_diff_operand){ NULL, NULL };
    }

    assert(operand.node->kind == TDS_HAMT_NODE_BITMAP_INDEXED);
    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)operand.node;
    const uint32_t selected_bit = tds_hamt_bit(index);
    if ((branch->data_map & selected_bit) != 0) {
        const tds_hamt_inline_entry *data = tds_hamt_bitmap_data_const(branch);
        return (tds_hamt_diff_operand){
            NULL,
            &data[tds_hamt_slot(branch->data_map, selected_bit)] };
    }
    if ((branch->node_map & selected_bit) != 0) {
        tds_hamt_node *const *children = tds_hamt_bitmap_children_const(branch);
        return (tds_hamt_diff_operand){
            children[tds_hamt_slot(branch->node_map, selected_bit)],
            NULL };
    }
    return (tds_hamt_diff_operand){ NULL, NULL };
}

static void tds_hamt_emit_difference(
    tds_hamt_difference_kind kind,
    const void *key,
    const void *before,
    const void *after,
    tds_hamt_difference_visitor visitor,
    void *context) {
    const tds_hamt_difference difference = { kind, key, before, after };
    visitor(&difference, context);
}

static void tds_hamt_append_diff_operand(
    tds_hamt_diff_operand operand,
    bool added,
    tds_hamt_difference_visitor visitor,
    void *context) {
    if (tds_hamt_diff_operand_is_empty(operand)) {
        return;
    }
    if (tds_hamt_diff_operand_is_run(operand)) {
        const tds_hamt_entry_run_view run = tds_hamt_entry_run_from_operand(operand);
        for (size_t index = 0; index != run.count; ++index) {
            const tds_hamt_entry *entry = tds_hamt_entry_run_at(&run, index);
            tds_hamt_emit_difference(
                added ? TDS_HAMT_DIFFERENCE_ADDED : TDS_HAMT_DIFFERENCE_REMOVED,
                entry->key,
                added ? NULL : entry->value,
                added ? entry->value : NULL,
                visitor,
                context);
        }
        return;
    }

    for (int index = 0; index != TDS_HAMT_BRANCH_MASK + 1; ++index) {
        tds_hamt_append_diff_operand(
            tds_hamt_diff_operand_logical_slot(operand, index, 0),
            added,
            visitor,
            context);
    }
}

static size_t tds_hamt_entry_run_find(
    const tds_hamt_entry_run_view *run,
    const void *key,
    const tds_hamt_policy *policy) {
    for (size_t index = 0; index != run->count; ++index) {
        if (tds_hamt_keys_equal(policy, tds_hamt_entry_run_at(run, index)->key, key)) {
            return index;
        }
    }
    return SIZE_MAX;
}

static void tds_hamt_diff_entry_runs(
    const tds_hamt_entry_run_view *left,
    const tds_hamt_entry_run_view *right,
    const tds_hamt_policy *policy,
    tds_hamt_difference_visitor visitor,
    void *context) {
    if (left->hash != right->hash) {
        for (size_t index = 0; index != left->count; ++index) {
            const tds_hamt_entry *entry = tds_hamt_entry_run_at(left, index);
            tds_hamt_emit_difference(
                TDS_HAMT_DIFFERENCE_REMOVED,
                entry->key,
                entry->value,
                NULL,
                visitor,
                context);
        }
        for (size_t index = 0; index != right->count; ++index) {
            const tds_hamt_entry *entry = tds_hamt_entry_run_at(right, index);
            tds_hamt_emit_difference(
                TDS_HAMT_DIFFERENCE_ADDED,
                entry->key,
                NULL,
                entry->value,
                visitor,
                context);
        }
        return;
    }

    for (size_t left_index = 0; left_index != left->count; ++left_index) {
        const tds_hamt_entry *before = tds_hamt_entry_run_at(left, left_index);
        const size_t right_index = tds_hamt_entry_run_find(right, before->key, policy);
        if (right_index == SIZE_MAX) {
            tds_hamt_emit_difference(
                TDS_HAMT_DIFFERENCE_REMOVED,
                before->key,
                before->value,
                NULL,
                visitor,
                context);
            continue;
        }
        const tds_hamt_entry *after = tds_hamt_entry_run_at(right, right_index);
        if (!tds_hamt_values_equal(policy, before->value, after->value)) {
            tds_hamt_emit_difference(
                TDS_HAMT_DIFFERENCE_CHANGED,
                before->key,
                before->value,
                after->value,
                visitor,
                context);
        }
    }
    for (size_t right_index = 0; right_index != right->count; ++right_index) {
        const tds_hamt_entry *after = tds_hamt_entry_run_at(right, right_index);
        if (tds_hamt_entry_run_find(left, after->key, policy) == SIZE_MAX) {
            tds_hamt_emit_difference(
                TDS_HAMT_DIFFERENCE_ADDED,
                after->key,
                NULL,
                after->value,
                visitor,
                context);
        }
    }
}

static void tds_hamt_diff_operands(
    tds_hamt_diff_operand left,
    tds_hamt_diff_operand right,
    int shift,
    const tds_hamt_policy *policy,
    tds_hamt_difference_visitor visitor,
    void *context) {
    if (left.node != NULL && left.node == right.node
        && left.inline_entry == NULL && right.inline_entry == NULL) {
        return;
    }
    if (left.inline_entry != NULL && left.inline_entry == right.inline_entry
        && left.node == NULL && right.node == NULL) {
        return;
    }
    if (tds_hamt_diff_operand_is_empty(left)) {
        tds_hamt_append_diff_operand(right, true, visitor, context);
        return;
    }
    if (tds_hamt_diff_operand_is_empty(right)) {
        tds_hamt_append_diff_operand(left, false, visitor, context);
        return;
    }
    if (tds_hamt_diff_operand_is_run(left) && tds_hamt_diff_operand_is_run(right)) {
        const tds_hamt_entry_run_view left_run = tds_hamt_entry_run_from_operand(left);
        const tds_hamt_entry_run_view right_run = tds_hamt_entry_run_from_operand(right);
        tds_hamt_diff_entry_runs(&left_run, &right_run, policy, visitor, context);
        return;
    }

    assert(shift < 32);
    for (int index = 0; index != TDS_HAMT_BRANCH_MASK + 1; ++index) {
        tds_hamt_diff_operands(
            tds_hamt_diff_operand_logical_slot(left, index, shift),
            tds_hamt_diff_operand_logical_slot(right, index, shift),
            shift + TDS_HAMT_BITS_PER_LEVEL,
            policy,
            visitor,
            context);
    }
}

bool tds_hamt_map_equals(const tds_hamt_map *left, const tds_hamt_map *right) {
    if (left == NULL || right == NULL || !tds_hamt_policies_compatible(left, right)) {
        return false;
    }
    if (left->root == right->root) {
        return true;
    }
    if (left->count != right->count) {
        return false;
    }
    return tds_hamt_nodes_equal(left->root, right->root, &left->policy);
}

tds_hamt_status tds_hamt_map_diff(
    const tds_hamt_map *left,
    const tds_hamt_map *right,
    tds_hamt_difference_visitor visitor,
    void *context) {
    if (left == NULL || right == NULL || visitor == NULL
        || !tds_hamt_policies_compatible(left, right)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (left->root == right->root) {
        return TDS_HAMT_OK;
    }
    tds_hamt_diff_operands(
        (tds_hamt_diff_operand){ left->root, NULL },
        (tds_hamt_diff_operand){ right->root, NULL },
        0,
        &left->policy,
        visitor,
        context);
    return TDS_HAMT_OK;
}

const void *tds_hamt_map_debug_root_identity(const tds_hamt_map *map) {
    return map == NULL ? NULL : map->root;
}

tds_hamt_node_kind tds_hamt_map_debug_root_kind(const tds_hamt_map *map) {
    if (map == NULL || map->root == NULL) {
        return TDS_HAMT_NODE_EMPTY;
    }

    return map->root->kind;
}

size_t tds_hamt_map_debug_root_child_identities(
    const tds_hamt_map *map,
    const void **children,
    size_t child_capacity) {
    if (map == NULL || map->root == NULL || map->root->kind != TDS_HAMT_NODE_BITMAP_INDEXED) {
        return 0;
    }

    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)map->root;
    if (children == NULL) {
        return branch->node_count;
    }

    const size_t copy_count = branch->node_count < child_capacity ? branch->node_count : child_capacity;
    tds_hamt_node *const *branch_children = tds_hamt_bitmap_children_const(branch);
    for (size_t i = 0; i < copy_count; ++i) {
        children[i] = branch_children[i];
    }

    return branch->node_count;
}

static bool tds_hamt_debug_hash_has_prefix(uint32_t hash, uint32_t prefix, uint32_t mask) {
    return (hash & mask) == prefix;
}

static uint32_t tds_hamt_debug_next_prefix_mask(int shift) {
    return shift >= 27 ? UINT32_MAX : (tds_hamt_bit(shift + TDS_HAMT_BITS_PER_LEVEL) - 1u);
}

static bool tds_hamt_debug_validate_node(
    const tds_hamt_node *node,
    int shift,
    uint32_t prefix,
    uint32_t prefix_mask,
    size_t *entry_count) {
    if (node == NULL) {
        *entry_count = 0;
        return true;
    }
    if (node->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
        *entry_count = 1;
        return node->subtree_count == 1 &&
            tds_hamt_debug_hash_has_prefix(leaf->hash, prefix, prefix_mask);
    }
    if (node->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *collision = (const tds_hamt_collision_node *)node;
        *entry_count = collision->count;
        return collision->count >= 2 && node->subtree_count == collision->count &&
            tds_hamt_debug_hash_has_prefix(collision->hash, prefix, prefix_mask);
    }

    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
    if (shift > 30 ||
        (branch->data_map & branch->node_map) != 0 ||
        tds_hamt_popcount(branch->data_map) != branch->data_count ||
        tds_hamt_popcount(branch->node_map) != branch->node_count ||
        branch->data_count + branch->node_count == 0) {
        return false;
    }
    const uint32_t next_mask = tds_hamt_debug_next_prefix_mask(shift);
    const tds_hamt_inline_entry *data = tds_hamt_bitmap_data_const(branch);
    tds_hamt_node *const *children = tds_hamt_bitmap_children_const(branch);
    if (branch->data_count + branch->node_count < 2 &&
        !(branch->data_count == 0 && branch->node_count == 1 &&
          children[0]->kind == TDS_HAMT_NODE_BITMAP_INDEXED)) {
        return false;
    }
    size_t total = branch->data_count;
    size_t data_index = 0;
    size_t child_index = 0;
    for (int slot = 0; slot != TDS_HAMT_BRANCH_MASK + 1; ++slot) {
        const uint32_t slot_bit = tds_hamt_bit(slot);
        if (shift == 30 && slot > 3 && ((branch->data_map | branch->node_map) & slot_bit) != 0) {
            return false;
        }
        const uint32_t slot_prefix = prefix | ((uint32_t)slot << shift);
        if ((branch->data_map & slot_bit) != 0) {
            if (data_index >= branch->data_count ||
                !tds_hamt_debug_hash_has_prefix(data[data_index].hash, slot_prefix, next_mask)) {
                return false;
            }
            ++data_index;
        }
        if ((branch->node_map & slot_bit) != 0) {
            size_t child_count = 0;
            if (child_index >= branch->node_count ||
                children[child_index]->kind == TDS_HAMT_NODE_LEAF ||
                !tds_hamt_debug_validate_node(
                    children[child_index], shift + TDS_HAMT_BITS_PER_LEVEL,
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

bool tds_hamt_map_debug_validate_canonical(const tds_hamt_map *map) {
    size_t entries = 0;
    return map != NULL &&
        tds_hamt_debug_validate_node(map->root, 0, 0, 0, &entries) &&
        entries == map->count;
}

static bool tds_hamt_debug_nodes_topology_equal(
    const tds_hamt_node *left,
    const tds_hamt_node *right,
    const tds_hamt_policy *policy) {
    if (left == NULL || right == NULL) {
        return left == right;
    }
    if (left->kind != right->kind) {
        return false;
    }
    if (left->kind == TDS_HAMT_NODE_LEAF) {
        return ((const tds_hamt_leaf_node *)left)->hash == ((const tds_hamt_leaf_node *)right)->hash;
    }
    if (left->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *l = (const tds_hamt_collision_node *)left;
        const tds_hamt_collision_node *r = (const tds_hamt_collision_node *)right;
        if (l->hash != r->hash || l->count != r->count) {
            return false;
        }
        for (size_t left_index = 0; left_index != l->count; ++left_index) {
            bool found = false;
            for (size_t right_index = 0; right_index != r->count; ++right_index) {
                if (tds_hamt_keys_equal(
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
    const tds_hamt_bitmap_node *l = (const tds_hamt_bitmap_node *)left;
    const tds_hamt_bitmap_node *r = (const tds_hamt_bitmap_node *)right;
    if (l->data_map != r->data_map || l->node_map != r->node_map ||
        l->data_count != r->data_count || l->node_count != r->node_count) {
        return false;
    }
    const tds_hamt_inline_entry *left_data = tds_hamt_bitmap_data_const(l);
    const tds_hamt_inline_entry *right_data = tds_hamt_bitmap_data_const(r);
    for (size_t index = 0; index != l->data_count; ++index) {
        if (left_data[index].hash != right_data[index].hash) {
            return false;
        }
    }
    tds_hamt_node *const *left_children = tds_hamt_bitmap_children_const(l);
    tds_hamt_node *const *right_children = tds_hamt_bitmap_children_const(r);
    for (size_t index = 0; index != l->node_count; ++index) {
        if (!tds_hamt_debug_nodes_topology_equal(
                left_children[index], right_children[index], policy)) {
            return false;
        }
    }
    return true;
}

bool tds_hamt_map_debug_topology_equal(const tds_hamt_map *left, const tds_hamt_map *right) {
    return left != NULL && right != NULL && tds_hamt_policies_compatible(left, right) &&
        tds_hamt_debug_nodes_topology_equal(left->root, right->root, &left->policy);
}

tds_hamt_set tds_hamt_set_create(const tds_hamt_set_policy *policy) {
    tds_hamt_set set;
    const tds_hamt_policy map_policy = tds_hamt_map_policy_from_set_policy(policy);
    set.map = tds_hamt_map_create(&map_policy);
    return set;
}

tds_hamt_status tds_hamt_set_create_range(
    const tds_hamt_set_policy *policy,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result) {
    if (result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set set = tds_hamt_set_create(policy);
    for (size_t i = 0; i < item_count; ++i) {
        tds_hamt_set next;
        const tds_hamt_status status = tds_hamt_set_add(&set, items[i], &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_set_destroy(&set);
            return status;
        }

        tds_hamt_set_destroy(&set);
        set = next;
    }

    *result = set;
    return TDS_HAMT_OK;
}

tds_hamt_set tds_hamt_set_clone(const tds_hamt_set *set) {
    tds_hamt_set clone;
    clone.map = set == NULL ? tds_hamt_map_create(NULL) : tds_hamt_map_clone(&set->map);
    return clone;
}

void tds_hamt_set_destroy(tds_hamt_set *set) {
    if (set != NULL) {
        tds_hamt_map_destroy(&set->map);
    }
}

size_t tds_hamt_set_count(const tds_hamt_set *set) {
    return set == NULL ? 0 : set->map.count;
}

bool tds_hamt_set_is_empty(const tds_hamt_set *set) {
    return tds_hamt_set_count(set) == 0;
}

bool tds_hamt_set_contains(const tds_hamt_set *set, const void *item) {
    return set != NULL && tds_hamt_map_contains_key(&set->map, item);
}

bool tds_hamt_set_try_get_value(const tds_hamt_set *set, const void *equal_value, const void **actual_value) {
    return set != NULL && tds_hamt_map_try_get_key(&set->map, equal_value, actual_value);
}

tds_hamt_status tds_hamt_set_add(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result) {
    if (set == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map map;
    const tds_hamt_status status = tds_hamt_map_set(&set->map, item, NULL, &map);
    if (status == TDS_HAMT_OK) {
        if (result == set) {
            tds_hamt_node_release(&set->map.policy, set->map.root);
        }
        result->map = map;
    }

    return status;
}

tds_hamt_status tds_hamt_set_try_add(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result,
    bool *added) {
    if (set == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map map;
    const tds_hamt_status status = tds_hamt_map_try_add(&set->map, item, NULL, &map, added);
    if (status == TDS_HAMT_OK) {
        if (result == set) {
            tds_hamt_node_release(&set->map.policy, set->map.root);
        }
        result->map = map;
    }

    return status;
}

tds_hamt_status tds_hamt_set_remove(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result) {
    bool removed = false;
    return tds_hamt_set_try_remove(set, item, result, &removed);
}

tds_hamt_status tds_hamt_set_try_remove(
    const tds_hamt_set *set,
    const void *item,
    tds_hamt_set *result,
    bool *removed) {
    if (set == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    if (removed != NULL) {
        *removed = false;
    }

    tds_hamt_map map;
    bool local_removed = false;
    const void *removed_value = NULL;
    const tds_hamt_status status = tds_hamt_map_try_remove(
        &set->map,
        item,
        &map,
        &local_removed,
        &removed_value);
    if (status == TDS_HAMT_OK) {
        if (result == set) {
            tds_hamt_node_release(&set->map.policy, set->map.root);
        }
        result->map = map;
        if (removed != NULL) {
            *removed = local_removed;
        }
    }

    return status;
}

tds_hamt_status tds_hamt_set_clear(const tds_hamt_set *set, tds_hamt_set *result) {
    if (set == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    return tds_hamt_map_clear(&set->map, &result->map);
}

tds_hamt_status tds_hamt_set_union(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    tds_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_union(&left->map, &right->map, &result->map);
}

tds_hamt_status tds_hamt_set_intersect(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    tds_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_intersect(&left->map, &right->map, &result->map);
}

tds_hamt_status tds_hamt_set_except(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    tds_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_except(&left->map, &right->map, &result->map);
}

tds_hamt_status tds_hamt_set_symmetric_except(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    tds_hamt_set *result) {
    return left == NULL || right == NULL || result == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_symmetric_except(&left->map, &right->map, &result->map);
}

tds_hamt_status tds_hamt_set_union_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set current = tds_hamt_set_clone(set);
    for (size_t i = 0; i < item_count; ++i) {
        tds_hamt_set next;
        const tds_hamt_status status = tds_hamt_set_add(&current, items[i], &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_set_destroy(&current);
            return status;
        }

        tds_hamt_set_destroy(&current);
        current = next;
    }

    if (result == set) {
        tds_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = current;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_intersect_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    tds_hamt_set probe;
    tds_hamt_status status = tds_hamt_set_create_range(&policy, items, item_count, &probe);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_set intersection = tds_hamt_set_create(&policy);
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(set, &iterator);
    const void *item = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &item)) {
        if (!tds_hamt_set_contains(&probe, item)) {
            continue;
        }

        tds_hamt_set next;
        status = tds_hamt_set_add(&intersection, item, &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_set_destroy(&probe);
            tds_hamt_set_destroy(&intersection);
            return status;
        }

        tds_hamt_set_destroy(&intersection);
        intersection = next;
    }

    tds_hamt_set_destroy(&probe);
    if (result == set) {
        tds_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = intersection;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_except_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set current = tds_hamt_set_clone(set);
    for (size_t i = 0; i < item_count; ++i) {
        tds_hamt_set next;
        const tds_hamt_status status = tds_hamt_set_remove(&current, items[i], &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_set_destroy(&current);
            return status;
        }

        tds_hamt_set_destroy(&current);
        current = next;
    }

    if (result == set) {
        tds_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = current;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_symmetric_except_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    tds_hamt_set toggles;
    tds_hamt_status status = tds_hamt_set_create_range(&policy, items, item_count, &toggles);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_set current = tds_hamt_set_clone(set);
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(&toggles, &iterator);
    const void *item = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &item)) {
        tds_hamt_set next;
        status = tds_hamt_set_contains(&current, item)
            ? tds_hamt_set_remove(&current, item, &next)
            : tds_hamt_set_add(&current, item, &next);
        if (status != TDS_HAMT_OK) {
            tds_hamt_set_destroy(&toggles);
            tds_hamt_set_destroy(&current);
            return status;
        }

        tds_hamt_set_destroy(&current);
        current = next;
    }

    tds_hamt_set_destroy(&toggles);
    if (result == set) {
        tds_hamt_node_release(&set->map.policy, set->map.root);
    }
    *result = current;
    return TDS_HAMT_OK;
}

/* Builds the deduplicating probe set the relation predicates compare against. */
static tds_hamt_status tds_hamt_set_build_probe(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    tds_hamt_set *probe) {
    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    return tds_hamt_set_create_range(&policy, items, item_count, probe);
}

static bool tds_hamt_set_contains_all_of(const tds_hamt_set *container, const tds_hamt_set *contained) {
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(contained, &iterator);
    const void *item = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &item)) {
        if (!tds_hamt_set_contains(container, item)) {
            return false;
        }
    }

    return true;
}

tds_hamt_status tds_hamt_set_is_subset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    tds_hamt_set probe;
    const tds_hamt_status status = tds_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    *result = tds_hamt_set_contains_all_of(&probe, set);
    tds_hamt_set_destroy(&probe);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_is_proper_subset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    tds_hamt_set probe;
    const tds_hamt_status status = tds_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    *result = tds_hamt_set_count(set) < tds_hamt_set_count(&probe)
        && tds_hamt_set_contains_all_of(&probe, set);
    tds_hamt_set_destroy(&probe);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_is_superset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    *result = true;
    for (size_t i = 0; i < item_count; ++i) {
        if (!tds_hamt_set_contains(set, items[i])) {
            *result = false;
            break;
        }
    }

    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_is_proper_superset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    tds_hamt_set probe;
    const tds_hamt_status status = tds_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    *result = tds_hamt_set_count(&probe) < tds_hamt_set_count(set)
        && tds_hamt_set_contains_all_of(set, &probe);
    tds_hamt_set_destroy(&probe);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_overlaps_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    for (size_t i = 0; i < item_count; ++i) {
        if (tds_hamt_set_contains(set, items[i])) {
            *result = true;
            break;
        }
    }

    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_equals_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count,
    bool *result) {
    if (set == NULL || result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    *result = false;
    tds_hamt_set probe;
    const tds_hamt_status status = tds_hamt_set_build_probe(set, items, item_count, &probe);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    *result = tds_hamt_set_count(set) == tds_hamt_set_count(&probe)
        && tds_hamt_set_contains_all_of(&probe, set);
    tds_hamt_set_destroy(&probe);
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_set_structural_relation_parts(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool need_union,
    tds_hamt_set *intersection,
    tds_hamt_set *united) {
    tds_hamt_status status = tds_hamt_set_intersect(left, right, intersection);
    if (status != TDS_HAMT_OK || !need_union) {
        return status;
    }
    status = tds_hamt_set_union(left, right, united);
    if (status != TDS_HAMT_OK) {
        tds_hamt_set_destroy(intersection);
    }
    return status;
}

tds_hamt_status tds_hamt_set_is_subset_of(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    tds_hamt_set intersection;
    const tds_hamt_status status = tds_hamt_set_structural_relation_parts(
        left, right, false, &intersection, NULL);
    if (status == TDS_HAMT_OK) {
        *result = intersection.map.count == left->map.count;
        tds_hamt_set_destroy(&intersection);
    }
    return status;
}

tds_hamt_status tds_hamt_set_is_proper_subset_of(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    tds_hamt_set intersection;
    tds_hamt_set united;
    const tds_hamt_status status = tds_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == TDS_HAMT_OK) {
        *result = intersection.map.count == left->map.count &&
            united.map.count > left->map.count;
        tds_hamt_set_destroy(&intersection);
        tds_hamt_set_destroy(&united);
    }
    return status;
}

tds_hamt_status tds_hamt_set_is_superset_of(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    tds_hamt_set intersection;
    tds_hamt_set united;
    const tds_hamt_status status = tds_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == TDS_HAMT_OK) {
        *result = united.map.count == left->map.count;
        tds_hamt_set_destroy(&intersection);
        tds_hamt_set_destroy(&united);
    }
    return status;
}

tds_hamt_status tds_hamt_set_is_proper_superset_of(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    tds_hamt_set intersection;
    tds_hamt_set united;
    const tds_hamt_status status = tds_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == TDS_HAMT_OK) {
        *result = united.map.count == left->map.count &&
            intersection.map.count < left->map.count;
        tds_hamt_set_destroy(&intersection);
        tds_hamt_set_destroy(&united);
    }
    return status;
}

tds_hamt_status tds_hamt_set_overlaps(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    tds_hamt_set intersection;
    const tds_hamt_status status = tds_hamt_set_structural_relation_parts(
        left, right, false, &intersection, NULL);
    if (status == TDS_HAMT_OK) {
        *result = intersection.map.count != 0;
        tds_hamt_set_destroy(&intersection);
    }
    return status;
}

tds_hamt_status tds_hamt_set_equals(
    const tds_hamt_set *left,
    const tds_hamt_set *right,
    bool *result) {
    if (left == NULL || right == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    *result = false;
    if (left->map.root == right->map.root) {
        *result = true;
        return TDS_HAMT_OK;
    }
    tds_hamt_set intersection;
    tds_hamt_set united;
    const tds_hamt_status status = tds_hamt_set_structural_relation_parts(
        left, right, true, &intersection, &united);
    if (status == TDS_HAMT_OK) {
        *result = intersection.map.count == left->map.count &&
            united.map.count == left->map.count;
        tds_hamt_set_destroy(&intersection);
        tds_hamt_set_destroy(&united);
    }
    return status;
}

void tds_hamt_set_iterator_init(const tds_hamt_set *set, tds_hamt_set_iterator *iterator) {
    if (iterator != NULL) {
        tds_hamt_map_iterator_init(set == NULL ? NULL : &set->map, &iterator->inner);
    }
}

bool tds_hamt_set_iterator_next(tds_hamt_set_iterator *iterator, const void **item) {
    const void *value = NULL;
    return iterator != NULL && tds_hamt_map_iterator_next(&iterator->inner, item, &value);
}

bool tds_hamt_set_shares_root(const tds_hamt_set *left, const tds_hamt_set *right) {
    return left != NULL && right != NULL && tds_hamt_map_shares_root(&left->map, &right->map);
}

const void *tds_hamt_set_debug_root_identity(const tds_hamt_set *set) {
    return set == NULL ? NULL : tds_hamt_map_debug_root_identity(&set->map);
}

tds_hamt_node_kind tds_hamt_set_debug_root_kind(const tds_hamt_set *set) {
    return set == NULL ? TDS_HAMT_NODE_EMPTY : tds_hamt_map_debug_root_kind(&set->map);
}

static tds_hamt_status tds_hamt_map_transient_active_state(
    const tds_hamt_map_transient *transient,
    struct tds_hamt_map_transient_state **state) {
    if (transient == NULL || transient->state == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (!transient->state->active) {
        return TDS_HAMT_TRANSIENT_CONSUMED;
    }

    if (state != NULL) {
        *state = transient->state;
    }
    return TDS_HAMT_OK;
}

static void tds_hamt_map_transient_commit(
    struct tds_hamt_map_transient_state *state,
    tds_hamt_map *next) {
    const bool changed = state->map.root != next->root;
    tds_hamt_map_destroy(&state->map);
    state->map = *next;
    if (changed) {
        ++state->version;
    }
}

tds_hamt_status tds_hamt_map_transient_create(
    const tds_hamt_policy *policy,
    tds_hamt_map_transient *result) {
    if (result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const tds_hamt_map empty = tds_hamt_map_create(policy);
    return tds_hamt_map_to_transient(&empty, result);
}

tds_hamt_status tds_hamt_map_to_transient(
    const tds_hamt_map *map,
    tds_hamt_map_transient *result) {
    if (map == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    struct tds_hamt_map_transient_state *state =
        (struct tds_hamt_map_transient_state *)tds_hamt_allocate(sizeof(*state));
    if (state == NULL) {
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    state->ref_count = 1;
    state->version = 0;
    state->active = true;
    state->map = tds_hamt_map_clone(map);

    tds_hamt_map_transient transient;
    transient.state = state;
    *result = transient;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_clone(
    const tds_hamt_map_transient *transient,
    tds_hamt_map_transient *result) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || result == NULL || result == transient) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }
    if (state->ref_count == SIZE_MAX) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    ++state->ref_count;
    tds_hamt_map_transient clone;
    clone.state = state;
    *result = clone;
    return TDS_HAMT_OK;
}

void tds_hamt_map_transient_destroy(tds_hamt_map_transient *transient) {
    if (transient == NULL || transient->state == NULL) {
        return;
    }

    struct tds_hamt_map_transient_state *state = transient->state;
    transient->state = NULL;
    assert(state->ref_count > 0);
    --state->ref_count;
    if (state->ref_count == 0) {
        tds_hamt_map_destroy(&state->map);
        free(state);
    }
}

bool tds_hamt_map_transient_is_active(const tds_hamt_map_transient *transient) {
    return transient != NULL && transient->state != NULL && transient->state->active;
}

tds_hamt_status tds_hamt_map_transient_get_policy(
    const tds_hamt_map_transient *transient,
    tds_hamt_policy *policy) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || policy == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    *policy = state->map.policy;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_count(
    const tds_hamt_map_transient *transient,
    size_t *count) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || count == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    *count = state->map.count;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_contains_key(
    const tds_hamt_map_transient *transient,
    const void *key,
    bool *contains) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || contains == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    *contains = tds_hamt_map_contains_key(&state->map, key);
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_try_get(
    const tds_hamt_map_transient *transient,
    const void *key,
    bool *found,
    const void **value) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || found == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    const void *local_value = NULL;
    const bool local_found = tds_hamt_map_try_get(&state->map, key, &local_value);
    *found = local_found;
    if (value != NULL) {
        *value = local_value;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_try_get_key(
    const tds_hamt_map_transient *transient,
    const void *equal_key,
    bool *found,
    const void **actual_key) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || found == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    const void *local_key = NULL;
    const bool local_found = tds_hamt_map_try_get_key(&state->map, equal_key, &local_key);
    *found = local_found;
    if (actual_key != NULL) {
        *actual_key = local_key;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_set(
    tds_hamt_map_transient *transient,
    const void *key,
    const void *value) {
    struct tds_hamt_map_transient_state *state = NULL;
    tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_map next;
    status = tds_hamt_map_set(&state->map, key, value, &next);
    if (status == TDS_HAMT_OK) {
        tds_hamt_map_transient_commit(state, &next);
    }
    return status;
}

tds_hamt_status tds_hamt_map_transient_add(
    tds_hamt_map_transient *transient,
    const void *key,
    const void *value) {
    struct tds_hamt_map_transient_state *state = NULL;
    tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_map next;
    status = tds_hamt_map_add(&state->map, key, value, &next);
    if (status == TDS_HAMT_OK) {
        tds_hamt_map_transient_commit(state, &next);
    }
    return status;
}

tds_hamt_status tds_hamt_map_transient_try_add(
    tds_hamt_map_transient *transient,
    const void *key,
    const void *value,
    bool *added) {
    struct tds_hamt_map_transient_state *state = NULL;
    tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    bool local_added = false;
    tds_hamt_map next;
    status = tds_hamt_map_try_add(&state->map, key, value, &next, &local_added);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_map_transient_commit(state, &next);
    if (added != NULL) {
        *added = local_added;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_remove(
    tds_hamt_map_transient *transient,
    const void *key) {
    struct tds_hamt_map_transient_state *state = NULL;
    tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_map next;
    status = tds_hamt_map_remove(&state->map, key, &next);
    if (status == TDS_HAMT_OK) {
        tds_hamt_map_transient_commit(state, &next);
    }
    return status;
}

tds_hamt_status tds_hamt_map_transient_try_remove(
    tds_hamt_map_transient *transient,
    const void *key,
    bool *removed) {
    struct tds_hamt_map_transient_state *state = NULL;
    tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    bool local_removed = false;
    const void *removed_value = NULL;
    tds_hamt_map next;
    status = tds_hamt_map_try_remove(
        &state->map,
        key,
        &next,
        &local_removed,
        &removed_value);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_map_transient_commit(state, &next);
    if (removed != NULL) {
        *removed = local_removed;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_clear(tds_hamt_map_transient *transient) {
    struct tds_hamt_map_transient_state *state = NULL;
    tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_map next;
    status = tds_hamt_map_clear(&state->map, &next);
    if (status == TDS_HAMT_OK) {
        tds_hamt_map_transient_commit(state, &next);
    }
    return status;
}

tds_hamt_status tds_hamt_map_transient_iterator_init(
    const tds_hamt_map_transient *transient,
    tds_hamt_map_transient_iterator *iterator) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || iterator == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    tds_hamt_map_transient_iterator local;
    local.state = state;
    local.version = state->version;
    tds_hamt_map_iterator_init(&state->map, &local.inner);
    *iterator = local;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_iterator_next(
    tds_hamt_map_transient_iterator *iterator,
    bool *has_value,
    const void **key,
    const void **value) {
    if (iterator == NULL || iterator->state == NULL || has_value == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }
    if (!iterator->state->active) {
        return TDS_HAMT_TRANSIENT_CONSUMED;
    }
    if (iterator->version != iterator->state->version) {
        return TDS_HAMT_TRANSIENT_MODIFIED;
    }

    const void *local_key = NULL;
    const void *local_value = NULL;
    const bool local_has_value =
        tds_hamt_map_iterator_next(&iterator->inner, &local_key, &local_value);
    *has_value = local_has_value;
    if (key != NULL) {
        *key = local_key;
    }
    if (value != NULL) {
        *value = local_value;
    }
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_map_transient_persist(
    tds_hamt_map_transient *transient,
    tds_hamt_map *result) {
    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status status = tds_hamt_map_transient_active_state(transient, &state);
    if (status != TDS_HAMT_OK || result == NULL) {
        return status == TDS_HAMT_OK ? TDS_HAMT_INVALID_ARGUMENT : status;
    }

    const tds_hamt_map published = state->map;
    state->map.root = NULL;
    state->map.count = 0;
    state->active = false;
    ++state->version;
    *result = published;
    return TDS_HAMT_OK;
}

const void *tds_hamt_map_transient_debug_root_identity(
    const tds_hamt_map_transient *transient) {
    return tds_hamt_map_transient_is_active(transient)
        ? transient->state->map.root
        : NULL;
}

tds_hamt_status tds_hamt_set_transient_create(
    const tds_hamt_set_policy *policy,
    tds_hamt_set_transient *result) {
    if (result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const tds_hamt_set empty = tds_hamt_set_create(policy);
    tds_hamt_set_transient transient;
    const tds_hamt_status status = tds_hamt_map_to_transient(&empty.map, &transient.inner);
    if (status == TDS_HAMT_OK) {
        *result = transient;
    }
    return status;
}

tds_hamt_status tds_hamt_set_to_transient(
    const tds_hamt_set *set,
    tds_hamt_set_transient *result) {
    if (set == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set_transient transient;
    const tds_hamt_status status = tds_hamt_map_to_transient(&set->map, &transient.inner);
    if (status == TDS_HAMT_OK) {
        *result = transient;
    }
    return status;
}

tds_hamt_status tds_hamt_set_transient_clone(
    const tds_hamt_set_transient *transient,
    tds_hamt_set_transient *result) {
    if (transient == NULL || result == NULL || transient == result) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set_transient clone;
    const tds_hamt_status status =
        tds_hamt_map_transient_clone(&transient->inner, &clone.inner);
    if (status == TDS_HAMT_OK) {
        *result = clone;
    }
    return status;
}

void tds_hamt_set_transient_destroy(tds_hamt_set_transient *transient) {
    if (transient != NULL) {
        tds_hamt_map_transient_destroy(&transient->inner);
    }
}

bool tds_hamt_set_transient_is_active(const tds_hamt_set_transient *transient) {
    return transient != NULL && tds_hamt_map_transient_is_active(&transient->inner);
}

tds_hamt_status tds_hamt_set_transient_get_policy(
    const tds_hamt_set_transient *transient,
    tds_hamt_set_policy *policy) {
    if (transient == NULL || policy == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_policy map_policy;
    const tds_hamt_status status =
        tds_hamt_map_transient_get_policy(&transient->inner, &map_policy);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    tds_hamt_set_policy set_policy;
    set_policy.hash = map_policy.hash;
    set_policy.equal = map_policy.key_equal;
    set_policy.retain_item = map_policy.retain_key;
    set_policy.release_item = map_policy.release_key;
    set_policy.context = map_policy.context;
    *policy = set_policy;
    return TDS_HAMT_OK;
}

tds_hamt_status tds_hamt_set_transient_count(
    const tds_hamt_set_transient *transient,
    size_t *count) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_count(&transient->inner, count);
}

tds_hamt_status tds_hamt_set_transient_contains(
    const tds_hamt_set_transient *transient,
    const void *item,
    bool *contains) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_contains_key(&transient->inner, item, contains);
}

tds_hamt_status tds_hamt_set_transient_try_get_value(
    const tds_hamt_set_transient *transient,
    const void *equal_value,
    bool *found,
    const void **actual_value) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_try_get_key(
            &transient->inner,
            equal_value,
            found,
            actual_value);
}

tds_hamt_status tds_hamt_set_transient_add(
    tds_hamt_set_transient *transient,
    const void *item) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_set(&transient->inner, item, NULL);
}

tds_hamt_status tds_hamt_set_transient_try_add(
    tds_hamt_set_transient *transient,
    const void *item,
    bool *added) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_try_add(&transient->inner, item, NULL, added);
}

tds_hamt_status tds_hamt_set_transient_remove(
    tds_hamt_set_transient *transient,
    const void *item) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_remove(&transient->inner, item);
}

tds_hamt_status tds_hamt_set_transient_try_remove(
    tds_hamt_set_transient *transient,
    const void *item,
    bool *removed) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_try_remove(&transient->inner, item, removed);
}

tds_hamt_status tds_hamt_set_transient_clear(tds_hamt_set_transient *transient) {
    return transient == NULL
        ? TDS_HAMT_INVALID_ARGUMENT
        : tds_hamt_map_transient_clear(&transient->inner);
}

typedef enum tds_hamt_set_transient_relation {
    TDS_HAMT_SET_TRANSIENT_SUBSET,
    TDS_HAMT_SET_TRANSIENT_PROPER_SUBSET,
    TDS_HAMT_SET_TRANSIENT_SUPERSET,
    TDS_HAMT_SET_TRANSIENT_PROPER_SUPERSET,
    TDS_HAMT_SET_TRANSIENT_OVERLAPS,
    TDS_HAMT_SET_TRANSIENT_EQUALS
} tds_hamt_set_transient_relation;

static tds_hamt_status tds_hamt_set_transient_relation_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result,
    tds_hamt_set_transient_relation relation) {
    if (transient == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status active_status =
        tds_hamt_map_transient_active_state(&transient->inner, &state);
    if (active_status != TDS_HAMT_OK) {
        return active_status;
    }
    if (result == NULL || (item_count != 0 && items == NULL)) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set borrowed;
    borrowed.map = state->map;
    bool local_result = false;
    tds_hamt_status status;
    switch (relation) {
    case TDS_HAMT_SET_TRANSIENT_SUBSET:
        status = tds_hamt_set_is_subset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_PROPER_SUBSET:
        status = tds_hamt_set_is_proper_subset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_SUPERSET:
        status = tds_hamt_set_is_superset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_PROPER_SUPERSET:
        status = tds_hamt_set_is_proper_superset_of_many(
            &borrowed, items, item_count, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_OVERLAPS:
        status = tds_hamt_set_overlaps_many(
            &borrowed, items, item_count, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_EQUALS:
        status = tds_hamt_set_equals_many(
            &borrowed, items, item_count, &local_result);
        break;
    default:
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    if (status == TDS_HAMT_OK) {
        *result = local_result;
    }
    return status;
}

static tds_hamt_status tds_hamt_set_transient_relation_set(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result,
    tds_hamt_set_transient_relation relation) {
    if (transient == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    struct tds_hamt_map_transient_state *state = NULL;
    const tds_hamt_status active_status =
        tds_hamt_map_transient_active_state(&transient->inner, &state);
    if (active_status != TDS_HAMT_OK) {
        return active_status;
    }
    if (other == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set borrowed;
    borrowed.map = state->map;
    bool local_result = false;
    tds_hamt_status status;
    switch (relation) {
    case TDS_HAMT_SET_TRANSIENT_SUBSET:
        status = tds_hamt_set_is_subset_of(&borrowed, other, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_PROPER_SUBSET:
        status = tds_hamt_set_is_proper_subset_of(&borrowed, other, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_SUPERSET:
        status = tds_hamt_set_is_superset_of(&borrowed, other, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_PROPER_SUPERSET:
        status = tds_hamt_set_is_proper_superset_of(&borrowed, other, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_OVERLAPS:
        status = tds_hamt_set_overlaps(&borrowed, other, &local_result);
        break;
    case TDS_HAMT_SET_TRANSIENT_EQUALS:
        status = tds_hamt_set_equals(&borrowed, other, &local_result);
        break;
    default:
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    if (status == TDS_HAMT_OK) {
        *result = local_result;
    }
    return status;
}

tds_hamt_status tds_hamt_set_transient_is_subset_of_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return tds_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        TDS_HAMT_SET_TRANSIENT_SUBSET);
}

tds_hamt_status tds_hamt_set_transient_is_proper_subset_of_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return tds_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        TDS_HAMT_SET_TRANSIENT_PROPER_SUBSET);
}

tds_hamt_status tds_hamt_set_transient_is_superset_of_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return tds_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        TDS_HAMT_SET_TRANSIENT_SUPERSET);
}

tds_hamt_status tds_hamt_set_transient_is_proper_superset_of_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return tds_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        TDS_HAMT_SET_TRANSIENT_PROPER_SUPERSET);
}

tds_hamt_status tds_hamt_set_transient_overlaps_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return tds_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        TDS_HAMT_SET_TRANSIENT_OVERLAPS);
}

tds_hamt_status tds_hamt_set_transient_equals_many(
    const tds_hamt_set_transient *transient,
    const void *const *items,
    size_t item_count,
    bool *result) {
    return tds_hamt_set_transient_relation_many(
        transient,
        items,
        item_count,
        result,
        TDS_HAMT_SET_TRANSIENT_EQUALS);
}

tds_hamt_status tds_hamt_set_transient_is_subset_of(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result) {
    return tds_hamt_set_transient_relation_set(
        transient, other, result, TDS_HAMT_SET_TRANSIENT_SUBSET);
}

tds_hamt_status tds_hamt_set_transient_is_proper_subset_of(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result) {
    return tds_hamt_set_transient_relation_set(
        transient, other, result, TDS_HAMT_SET_TRANSIENT_PROPER_SUBSET);
}

tds_hamt_status tds_hamt_set_transient_is_superset_of(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result) {
    return tds_hamt_set_transient_relation_set(
        transient, other, result, TDS_HAMT_SET_TRANSIENT_SUPERSET);
}

tds_hamt_status tds_hamt_set_transient_is_proper_superset_of(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result) {
    return tds_hamt_set_transient_relation_set(
        transient, other, result, TDS_HAMT_SET_TRANSIENT_PROPER_SUPERSET);
}

tds_hamt_status tds_hamt_set_transient_overlaps(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result) {
    return tds_hamt_set_transient_relation_set(
        transient, other, result, TDS_HAMT_SET_TRANSIENT_OVERLAPS);
}

tds_hamt_status tds_hamt_set_transient_equals(
    const tds_hamt_set_transient *transient,
    const tds_hamt_set *other,
    bool *result) {
    return tds_hamt_set_transient_relation_set(
        transient, other, result, TDS_HAMT_SET_TRANSIENT_EQUALS);
}

tds_hamt_status tds_hamt_set_transient_iterator_init(
    const tds_hamt_set_transient *transient,
    tds_hamt_set_transient_iterator *iterator) {
    if (transient == NULL || iterator == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_set_transient_iterator local;
    const tds_hamt_status status =
        tds_hamt_map_transient_iterator_init(&transient->inner, &local.inner);
    if (status == TDS_HAMT_OK) {
        *iterator = local;
    }
    return status;
}

tds_hamt_status tds_hamt_set_transient_iterator_next(
    tds_hamt_set_transient_iterator *iterator,
    bool *has_value,
    const void **item) {
    if (iterator == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const void *value = NULL;
    return tds_hamt_map_transient_iterator_next(
        &iterator->inner,
        has_value,
        item,
        &value);
}

tds_hamt_status tds_hamt_set_transient_persist(
    tds_hamt_set_transient *transient,
    tds_hamt_set *result) {
    if (transient == NULL || result == NULL) {
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    tds_hamt_map map;
    const tds_hamt_status status =
        tds_hamt_map_transient_persist(&transient->inner, &map);
    if (status == TDS_HAMT_OK) {
        result->map = map;
    }
    return status;
}

const void *tds_hamt_set_transient_debug_root_identity(
    const tds_hamt_set_transient *transient) {
    return transient == NULL
        ? NULL
        : tds_hamt_map_transient_debug_root_identity(&transient->inner);
}

static uint32_t tds_hamt_pointer_hash(const void *item, void *context) {
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

static bool tds_hamt_pointer_equal(const void *left, const void *right, void *context) {
    (void)context;
    return left == right;
}

static void *tds_hamt_identity_retain(const void *item, void *context) {
    (void)context;
    return (void *)item;
}

static bool tds_hamt_unit_equal(const void *left, const void *right, void *context) {
    (void)left;
    (void)right;
    (void)context;
    return true;
}

static tds_hamt_policy tds_hamt_normalize_policy(const tds_hamt_policy *policy) {
    tds_hamt_policy normalized = policy == NULL ? tds_hamt_policy_default() : *policy;
    if (normalized.hash == NULL) {
        normalized.hash = tds_hamt_pointer_hash;
    }
    if (normalized.key_equal == NULL) {
        normalized.key_equal = tds_hamt_pointer_equal;
    }
    if (normalized.value_equal == NULL) {
        normalized.value_equal = tds_hamt_pointer_equal;
    }
    if (normalized.retain_key == NULL) {
        normalized.retain_key = tds_hamt_identity_retain;
    }
    if (normalized.retain_value == NULL) {
        normalized.retain_value = tds_hamt_identity_retain;
    }

    return normalized;
}

static tds_hamt_set_policy tds_hamt_normalize_set_policy(const tds_hamt_set_policy *policy) {
    tds_hamt_set_policy normalized = policy == NULL ? tds_hamt_set_policy_default() : *policy;
    if (normalized.hash == NULL) {
        normalized.hash = tds_hamt_pointer_hash;
    }
    if (normalized.equal == NULL) {
        normalized.equal = tds_hamt_pointer_equal;
    }
    if (normalized.retain_item == NULL) {
        normalized.retain_item = tds_hamt_identity_retain;
    }

    return normalized;
}

static tds_hamt_policy tds_hamt_map_policy_from_set_policy(const tds_hamt_set_policy *policy) {
    const tds_hamt_set_policy set_policy = tds_hamt_normalize_set_policy(policy);
    tds_hamt_policy map_policy;
    memset(&map_policy, 0, sizeof(map_policy));
    map_policy.hash = set_policy.hash;
    map_policy.key_equal = set_policy.equal;
    map_policy.value_equal = tds_hamt_unit_equal;
    map_policy.retain_key = set_policy.retain_item;
    map_policy.retain_value = tds_hamt_identity_retain;
    map_policy.release_key = set_policy.release_item;
    map_policy.release_value = NULL;
    map_policy.context = set_policy.context;
    return map_policy;
}

static tds_hamt_node *tds_hamt_node_retain(const tds_hamt_node *node) {
    if (node == NULL) {
        return NULL;
    }

    tds_hamt_node *mutable_node = (tds_hamt_node *)node;
    ++mutable_node->ref_count;
    return mutable_node;
}

static void tds_hamt_node_release(const tds_hamt_policy *policy, const tds_hamt_node *node) {
    if (node == NULL) {
        return;
    }

    tds_hamt_node *mutable_node = (tds_hamt_node *)node;
    assert(mutable_node->ref_count > 0);
    --mutable_node->ref_count;
    if (mutable_node->ref_count != 0) {
        return;
    }

    switch (node->kind) {
    case TDS_HAMT_NODE_LEAF: {
        tds_hamt_leaf_node *leaf = (tds_hamt_leaf_node *)mutable_node;
        tds_hamt_release_key(policy, leaf->key);
        tds_hamt_release_value(policy, leaf->value);
        free(leaf);
        break;
    }

    case TDS_HAMT_NODE_COLLISION: {
        tds_hamt_collision_node *collision = (tds_hamt_collision_node *)mutable_node;
        for (size_t i = 0; i < collision->count; ++i) {
            tds_hamt_release_key(policy, (void *)collision->entries[i].key);
            tds_hamt_release_value(policy, (void *)collision->entries[i].value);
        }
        free(collision);
        break;
    }

    case TDS_HAMT_NODE_BITMAP_INDEXED: {
        tds_hamt_bitmap_node *branch = (tds_hamt_bitmap_node *)mutable_node;
        tds_hamt_inline_entry *data = tds_hamt_bitmap_data(branch);
        for (size_t i = 0; i < branch->data_count; ++i) {
            tds_hamt_release_key(policy, (void *)data[i].entry.key);
            tds_hamt_release_value(policy, (void *)data[i].entry.value);
        }
        tds_hamt_node **children = tds_hamt_bitmap_children(branch);
        for (size_t i = 0; i < branch->node_count; ++i) {
            tds_hamt_node_release(policy, children[i]);
        }
        free(branch);
        break;
    }

    default:
        free(mutable_node);
        break;
    }
}

static uint32_t tds_hamt_get_hash(const tds_hamt_map *map, const void *key) {
    return map->policy.hash(key, map->policy.context);
}

static bool tds_hamt_keys_equal(const tds_hamt_policy *policy, const void *left, const void *right) {
    return policy->key_equal(left, right, policy->context);
}

static bool tds_hamt_values_equal(const tds_hamt_policy *policy, const void *left, const void *right) {
    return left == right || policy->value_equal(left, right, policy->context);
}

static void *tds_hamt_retain_key(const tds_hamt_policy *policy, const void *key) {
    return policy->retain_key(key, policy->context);
}

static void *tds_hamt_retain_value(const tds_hamt_policy *policy, const void *value) {
    return policy->retain_value(value, policy->context);
}

/* An allocating retain callback reports failure by returning NULL for a
 * non-NULL input; storing that NULL with TDS_HAMT_OK would surface much later
 * as a NULL key/value reaching the user callbacks. */
static tds_hamt_status tds_hamt_checked_retain_key(
    const tds_hamt_policy *policy,
    const void *key,
    void **retained) {
    *retained = tds_hamt_retain_key(policy, key);
    return (*retained == NULL && key != NULL) ? TDS_HAMT_OUT_OF_MEMORY : TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_checked_retain_value(
    const tds_hamt_policy *policy,
    const void *value,
    void **retained) {
    *retained = tds_hamt_retain_value(policy, value);
    return (*retained == NULL && value != NULL) ? TDS_HAMT_OUT_OF_MEMORY : TDS_HAMT_OK;
}

static void tds_hamt_release_key(const tds_hamt_policy *policy, void *key) {
    if (policy->release_key != NULL) {
        policy->release_key(key, policy->context);
    }
}

static void tds_hamt_release_value(const tds_hamt_policy *policy, void *value) {
    if (policy->release_value != NULL) {
        policy->release_value(value, policy->context);
    }
}

static tds_hamt_status tds_hamt_inline_copy(
    const tds_hamt_policy *policy,
    const tds_hamt_inline_entry *source,
    tds_hamt_inline_entry *target) {
    target->hash = source->hash;
    target->entry.key = NULL;
    target->entry.value = NULL;
    void *key = NULL;
    void *value = NULL;
    tds_hamt_status status = tds_hamt_checked_retain_key(policy, source->entry.key, &key);
    if (status == TDS_HAMT_OK) {
        status = tds_hamt_checked_retain_value(policy, source->entry.value, &value);
    }
    if (status != TDS_HAMT_OK) {
        tds_hamt_release_key(policy, key);
        tds_hamt_release_value(policy, value);
        return status;
    }
    target->entry.key = key;
    target->entry.value = value;
    return TDS_HAMT_OK;
}

static void tds_hamt_inline_release(
    const tds_hamt_policy *policy,
    tds_hamt_inline_entry *data,
    size_t count) {
    for (size_t i = 0; i < count; ++i) {
        tds_hamt_release_key(policy, (void *)data[i].entry.key);
        tds_hamt_release_value(policy, (void *)data[i].entry.value);
    }
}

static int tds_hamt_index(uint32_t hash, int shift) {
    return (int)((hash >> shift) & TDS_HAMT_BRANCH_MASK);
}

static uint32_t tds_hamt_bit(int index) {
    return 1u << index;
}

static size_t tds_hamt_slot(uint32_t bitmap, uint32_t bit) {
    return tds_hamt_popcount(bitmap & (bit - 1u));
}

static size_t tds_hamt_popcount(uint32_t value) {
    size_t count = 0;
    while (value != 0) {
        value &= value - 1u;
        ++count;
    }

    return count;
}

static tds_hamt_inline_entry *tds_hamt_bitmap_data(tds_hamt_bitmap_node *node) {
    return (tds_hamt_inline_entry *)node->storage;
}

static const tds_hamt_inline_entry *tds_hamt_bitmap_data_const(const tds_hamt_bitmap_node *node) {
    return (const tds_hamt_inline_entry *)node->storage;
}

static tds_hamt_node **tds_hamt_bitmap_children(tds_hamt_bitmap_node *node) {
    return (tds_hamt_node **)(node->storage + node->data_count * sizeof(tds_hamt_inline_entry));
}

static tds_hamt_node *const *tds_hamt_bitmap_children_const(const tds_hamt_bitmap_node *node) {
    return (tds_hamt_node *const *)(node->storage + node->data_count * sizeof(tds_hamt_inline_entry));
}

static tds_hamt_status tds_hamt_leaf_create(
    const tds_hamt_policy *policy,
    uint32_t hash,
    const void *key,
    const void *value,
    tds_hamt_node **result) {
    void *retained_key = NULL;
    void *retained_value = NULL;
    tds_hamt_status status = tds_hamt_checked_retain_key(policy, key, &retained_key);
    if (status == TDS_HAMT_OK) {
        status = tds_hamt_checked_retain_value(policy, value, &retained_value);
    }
    if (status == TDS_HAMT_OK) {
        status = tds_hamt_leaf_create_from_retained(hash, retained_key, retained_value, result);
    } else {
        *result = NULL;
    }
    if (status != TDS_HAMT_OK) {
        tds_hamt_release_key(policy, retained_key);
        tds_hamt_release_value(policy, retained_value);
    }

    return status;
}

static tds_hamt_status tds_hamt_leaf_create_from_retained(
    uint32_t hash,
    void *key,
    void *value,
    tds_hamt_node **result) {
    tds_hamt_leaf_node *leaf = (tds_hamt_leaf_node *)tds_hamt_allocate(sizeof(*leaf));
    if (leaf == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    leaf->base.kind = TDS_HAMT_NODE_LEAF;
    leaf->base.ref_count = 1;
    leaf->base.subtree_count = 1;
    leaf->hash = hash;
    leaf->key = key;
    leaf->value = value;
    *result = &leaf->base;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_collision_write_entry(
    const tds_hamt_policy *policy,
    tds_hamt_collision_node *collision,
    size_t *written,
    const void *key,
    const void *value) {
    void *retained_key = NULL;
    void *retained_value = NULL;
    tds_hamt_status status = tds_hamt_checked_retain_key(policy, key, &retained_key);
    if (status == TDS_HAMT_OK) {
        status = tds_hamt_checked_retain_value(policy, value, &retained_value);
        if (status != TDS_HAMT_OK) {
            tds_hamt_release_key(policy, retained_key);
        }
    }

    if (status != TDS_HAMT_OK) {
        return status;
    }

    collision->entries[*written].key = retained_key;
    collision->entries[*written].value = retained_value;
    ++(*written);
    return TDS_HAMT_OK;
}

/* Precondition (mirrors the C# reference's Debug.Assert): equal-hash merges
 * only ever combine two leaves whose keys differ under the policy, because
 * equal-hash inserts into an existing collision node are handled inside the
 * collision branch of tds_hamt_node_set. The collision-left handling below is
 * defensively retained but is unreachable from the current call graph; note
 * that it appends `right` without a duplicate-key scan, which is only safe
 * under that precondition. */
static tds_hamt_status tds_hamt_collision_create(
    const tds_hamt_policy *policy,
    tds_hamt_node *left,
    tds_hamt_node *right,
    tds_hamt_node **result) {
    const uint32_t hash = left->kind == TDS_HAMT_NODE_LEAF
        ? ((const tds_hamt_leaf_node *)left)->hash
        : ((const tds_hamt_collision_node *)left)->hash;
    const size_t left_count = left->kind == TDS_HAMT_NODE_COLLISION
        ? ((const tds_hamt_collision_node *)left)->count
        : 1u;
    const size_t total_count = left_count + 1u;
    tds_hamt_collision_node *collision =
        (tds_hamt_collision_node *)tds_hamt_allocate(sizeof(*collision) + total_count * sizeof(tds_hamt_entry));
    if (collision == NULL) {
        tds_hamt_node_release(policy, left);
        tds_hamt_node_release(policy, right);
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    collision->base.kind = TDS_HAMT_NODE_COLLISION;
    collision->base.ref_count = 1;
    collision->base.subtree_count = total_count;
    collision->hash = hash;
    collision->count = total_count;

    tds_hamt_status status = TDS_HAMT_OK;
    size_t written = 0;
    if (left->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *source = (const tds_hamt_collision_node *)left;
        for (size_t i = 0; status == TDS_HAMT_OK && i < source->count; ++i) {
            status = tds_hamt_collision_write_entry(
                policy, collision, &written, source->entries[i].key, source->entries[i].value);
        }
    } else {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)left;
        status = tds_hamt_collision_write_entry(policy, collision, &written, leaf->key, leaf->value);
    }

    if (status == TDS_HAMT_OK) {
        const tds_hamt_leaf_node *right_leaf = (const tds_hamt_leaf_node *)right;
        status = tds_hamt_collision_write_entry(policy, collision, &written, right_leaf->key, right_leaf->value);
    }

    if (status != TDS_HAMT_OK) {
        collision->count = written;
        tds_hamt_node_release(policy, &collision->base);
        tds_hamt_node_release(policy, left);
        tds_hamt_node_release(policy, right);
        *result = NULL;
        return status;
    }

    tds_hamt_node_release(policy, left);
    tds_hamt_node_release(policy, right);
    *result = &collision->base;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bitmap_create(
    uint32_t data_map,
    uint32_t node_map,
    const tds_hamt_inline_entry *data,
    size_t data_count,
    tds_hamt_node **children,
    size_t child_count,
    tds_hamt_node **result) {
    tds_hamt_bitmap_node *branch =
        (tds_hamt_bitmap_node *)tds_hamt_allocate(
            sizeof(*branch)
            + data_count * sizeof(tds_hamt_inline_entry)
            + child_count * sizeof(tds_hamt_node *));
    if (branch == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    branch->base.kind = TDS_HAMT_NODE_BITMAP_INDEXED;
    branch->base.ref_count = 1;
    branch->data_map = data_map;
    branch->node_map = node_map;
    branch->data_count = data_count;
    branch->node_count = child_count;
    tds_hamt_inline_entry *target_data = tds_hamt_bitmap_data(branch);
    for (size_t i = 0; i < data_count; ++i) {
        target_data[i] = data[i];
    }
    tds_hamt_node **target_children = tds_hamt_bitmap_children(branch);
    for (size_t i = 0; i < child_count; ++i) {
        target_children[i] = children[i];
    }
    branch->base.subtree_count = data_count;
    for (size_t i = 0; i < child_count; ++i) {
        branch->base.subtree_count += children[i]->subtree_count;
    }

    *result = &branch->base;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bitmap_copy_create(
    const tds_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const tds_hamt_inline_entry *data,
    size_t data_count,
    tds_hamt_node *const *children,
    size_t child_count,
    tds_hamt_node **result) {
    tds_hamt_bitmap_node *branch =
        (tds_hamt_bitmap_node *)tds_hamt_allocate(
            sizeof(*branch)
            + data_count * sizeof(tds_hamt_inline_entry)
            + child_count * sizeof(tds_hamt_node *));
    if (branch == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    branch->base.kind = TDS_HAMT_NODE_BITMAP_INDEXED;
    branch->base.ref_count = 1;
    branch->data_map = data_map;
    branch->node_map = node_map;
    branch->data_count = data_count;
    branch->node_count = 0;
    tds_hamt_inline_entry *target_data = tds_hamt_bitmap_data(branch);
    tds_hamt_status status = TDS_HAMT_OK;
    size_t written_data = 0;
    while (status == TDS_HAMT_OK && written_data < data_count) {
        status = tds_hamt_inline_copy(
            policy, &data[written_data], &target_data[written_data]);
        if (status == TDS_HAMT_OK) {
            ++written_data;
        }
    }
    if (status != TDS_HAMT_OK) {
        tds_hamt_inline_release(policy, target_data, written_data);
        free(branch);
        *result = NULL;
        return status;
    }
    tds_hamt_node **target_children =
        (tds_hamt_node **)(branch->storage + data_count * sizeof(tds_hamt_inline_entry));
    while (branch->node_count < child_count) {
        target_children[branch->node_count] = tds_hamt_node_retain(children[branch->node_count]);
        ++branch->node_count;
    }
    branch->base.subtree_count = data_count;
    for (size_t i = 0; i < child_count; ++i) {
        branch->base.subtree_count += children[i]->subtree_count;
    }
    *result = &branch->base;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_merge_hash_nodes(
    const tds_hamt_policy *policy,
    tds_hamt_node *left,
    tds_hamt_node *right,
    int shift,
    tds_hamt_node **result) {
    const uint32_t left_hash = left->kind == TDS_HAMT_NODE_LEAF
        ? ((const tds_hamt_leaf_node *)left)->hash
        : ((const tds_hamt_collision_node *)left)->hash;
    const uint32_t right_hash = ((const tds_hamt_leaf_node *)right)->hash;

    if (left_hash == right_hash) {
        return tds_hamt_collision_create(policy, left, right, result);
    }

    if (shift >= 32) {
        tds_hamt_node_release(policy, left);
        tds_hamt_node_release(policy, right);
        return TDS_HAMT_INVALID_ARGUMENT;
    }

    const int left_index = tds_hamt_index(left_hash, shift);
    const int right_index = tds_hamt_index(right_hash, shift);
    const uint32_t left_bit = tds_hamt_bit(left_index);
    const uint32_t right_bit = tds_hamt_bit(right_index);

    if (left_index == right_index) {
        tds_hamt_node *child = NULL;
        tds_hamt_status status = tds_hamt_merge_hash_nodes(
            policy,
            left,
            right,
            shift + TDS_HAMT_BITS_PER_LEVEL,
            &child);
        if (status != TDS_HAMT_OK) {
            return status;
        }

        tds_hamt_node *children[1] = { child };
        status = tds_hamt_bitmap_create(0, left_bit, NULL, 0, children, 1, result);
        if (status != TDS_HAMT_OK) {
            tds_hamt_node_release(policy, child);
        }
        return status;
    }

    tds_hamt_status status;
    const tds_hamt_leaf_node *right_leaf = (const tds_hamt_leaf_node *)right;
    if (left->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *left_leaf = (const tds_hamt_leaf_node *)left;
        tds_hamt_inline_entry data[2];
        const size_t left_slot = left_index < right_index ? 0u : 1u;
        const size_t right_slot = 1u - left_slot;
        data[left_slot].hash = left_leaf->hash;
        data[left_slot].entry.key = left_leaf->key;
        data[left_slot].entry.value = left_leaf->value;
        data[right_slot].hash = right_leaf->hash;
        data[right_slot].entry.key = right_leaf->key;
        data[right_slot].entry.value = right_leaf->value;
        status = tds_hamt_bitmap_copy_create(
            policy, left_bit | right_bit, 0, data, 2, NULL, 0, result);
    } else {
        tds_hamt_inline_entry data;
        data.hash = right_leaf->hash;
        data.entry.key = right_leaf->key;
        data.entry.value = right_leaf->value;
        tds_hamt_node *children[1] = { left };
        status = tds_hamt_bitmap_copy_create(
            policy, right_bit, left_bit, &data, 1, children, 1, result);
    }
    tds_hamt_node_release(policy, left);
    tds_hamt_node_release(policy, right);

    return status;
}

static tds_hamt_status tds_hamt_node_set(
    const tds_hamt_policy *policy,
    const tds_hamt_node *node,
    const void *key,
    const void *value,
    uint32_t hash,
    int shift,
    bool overwrite,
    bool *added,
    tds_hamt_node **result) {
    if (node->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
        if (leaf->hash == hash && tds_hamt_keys_equal(policy, leaf->key, key)) {
            *added = false;
            if (!overwrite || tds_hamt_values_equal(policy, leaf->value, value)) {
                *result = tds_hamt_node_retain(node);
                return TDS_HAMT_OK;
            }

            void *retained_key = NULL;
            void *retained_value = NULL;
            tds_hamt_status status = tds_hamt_checked_retain_key(policy, leaf->key, &retained_key);
            if (status == TDS_HAMT_OK) {
                status = tds_hamt_checked_retain_value(policy, value, &retained_value);
            }
            if (status == TDS_HAMT_OK) {
                status = tds_hamt_leaf_create_from_retained(leaf->hash, retained_key, retained_value, result);
            } else {
                *result = NULL;
            }
            if (status != TDS_HAMT_OK) {
                tds_hamt_release_key(policy, retained_key);
                tds_hamt_release_value(policy, retained_value);
            }
            return status;
        }

        *added = true;
        tds_hamt_node *left = tds_hamt_node_retain(node);
        tds_hamt_node *right = NULL;
        tds_hamt_status status = tds_hamt_leaf_create(policy, hash, key, value, &right);
        if (status != TDS_HAMT_OK) {
            tds_hamt_node_release(policy, left);
            return status;
        }
        return tds_hamt_merge_hash_nodes(policy, left, right, shift, result);
    }

    if (node->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *collision = (const tds_hamt_collision_node *)node;
        if (collision->hash != hash) {
            *added = true;
            tds_hamt_node *left = tds_hamt_node_retain(node);
            tds_hamt_node *right = NULL;
            tds_hamt_status status = tds_hamt_leaf_create(policy, hash, key, value, &right);
            if (status != TDS_HAMT_OK) {
                tds_hamt_node_release(policy, left);
                return status;
            }
            return tds_hamt_merge_hash_nodes(policy, left, right, shift, result);
        }

        for (size_t i = 0; i < collision->count; ++i) {
            if (!tds_hamt_keys_equal(policy, collision->entries[i].key, key)) {
                continue;
            }

            *added = false;
            if (!overwrite || tds_hamt_values_equal(policy, collision->entries[i].value, value)) {
                *result = tds_hamt_node_retain(node);
                return TDS_HAMT_OK;
            }

            tds_hamt_collision_node *replaced =
                (tds_hamt_collision_node *)tds_hamt_allocate(
                    sizeof(*replaced) + collision->count * sizeof(tds_hamt_entry));
            if (replaced == NULL) {
                *result = NULL;
                return TDS_HAMT_OUT_OF_MEMORY;
            }

            replaced->base.kind = TDS_HAMT_NODE_COLLISION;
            replaced->base.ref_count = 1;
            replaced->base.subtree_count = collision->count;
            replaced->hash = collision->hash;
            replaced->count = collision->count;
            tds_hamt_status status = TDS_HAMT_OK;
            size_t written = 0;
            for (size_t j = 0; status == TDS_HAMT_OK && j < collision->count; ++j) {
                status = tds_hamt_collision_write_entry(
                    policy,
                    replaced,
                    &written,
                    collision->entries[j].key,
                    j == i ? value : collision->entries[j].value);
            }
            if (status != TDS_HAMT_OK) {
                replaced->count = written;
                tds_hamt_node_release(policy, &replaced->base);
                *result = NULL;
                return status;
            }
            *result = &replaced->base;
            return TDS_HAMT_OK;
        }

        tds_hamt_collision_node *expanded =
            (tds_hamt_collision_node *)tds_hamt_allocate(
                sizeof(*expanded) + (collision->count + 1u) * sizeof(tds_hamt_entry));
        if (expanded == NULL) {
            *result = NULL;
            return TDS_HAMT_OUT_OF_MEMORY;
        }

        expanded->base.kind = TDS_HAMT_NODE_COLLISION;
        expanded->base.ref_count = 1;
        expanded->base.subtree_count = collision->count + 1;
        expanded->hash = collision->hash;
        expanded->count = collision->count + 1u;
        tds_hamt_status status = TDS_HAMT_OK;
        size_t written = 0;
        for (size_t i = 0; status == TDS_HAMT_OK && i < collision->count; ++i) {
            status = tds_hamt_collision_write_entry(
                policy, expanded, &written, collision->entries[i].key, collision->entries[i].value);
        }
        if (status == TDS_HAMT_OK) {
            status = tds_hamt_collision_write_entry(policy, expanded, &written, key, value);
        }
        if (status != TDS_HAMT_OK) {
            expanded->count = written;
            tds_hamt_node_release(policy, &expanded->base);
            *result = NULL;
            return status;
        }
        *added = true;
        *result = &expanded->base;
        return TDS_HAMT_OK;
    }

    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
    const tds_hamt_inline_entry *source_data = tds_hamt_bitmap_data_const(branch);
    tds_hamt_node *const *source_children = tds_hamt_bitmap_children_const(branch);
    const uint32_t selected_bit = tds_hamt_bit(tds_hamt_index(hash, shift));
    tds_hamt_inline_entry data[32];
    tds_hamt_node *children[32];

    if ((branch->data_map & selected_bit) != 0) {
        const size_t data_slot = tds_hamt_slot(branch->data_map, selected_bit);
        const tds_hamt_inline_entry *existing = &source_data[data_slot];
        if (existing->hash == hash && tds_hamt_keys_equal(policy, existing->entry.key, key)) {
            *added = false;
            if (!overwrite || tds_hamt_values_equal(policy, existing->entry.value, value)) {
                *result = tds_hamt_node_retain(node);
                return TDS_HAMT_OK;
            }
            memcpy(data, source_data, branch->data_count * sizeof(*data));
            data[data_slot].entry.value = value;
            return tds_hamt_bitmap_copy_create(
                policy, branch->data_map, branch->node_map,
                data, branch->data_count, source_children, branch->node_count, result);
        }

        tds_hamt_node *left = NULL;
        tds_hamt_node *right = NULL;
        tds_hamt_status status = tds_hamt_leaf_create(
            policy, existing->hash, existing->entry.key, existing->entry.value, &left);
        if (status == TDS_HAMT_OK) {
            status = tds_hamt_leaf_create(policy, hash, key, value, &right);
        }
        if (status != TDS_HAMT_OK) {
            tds_hamt_node_release(policy, left);
            return status;
        }
        tds_hamt_node *child = NULL;
        status = tds_hamt_merge_hash_nodes(
            policy, left, right, shift + TDS_HAMT_BITS_PER_LEVEL, &child);
        if (status != TDS_HAMT_OK) {
            return status;
        }
        for (size_t source = 0, target = 0; source < branch->data_count; ++source) {
            if (source != data_slot) {
                data[target++] = source_data[source];
            }
        }
        const size_t node_slot = tds_hamt_slot(branch->node_map, selected_bit);
        for (size_t source = 0, target = 0; target < branch->node_count + 1u; ++target) {
            children[target] = target == node_slot ? child : source_children[source++];
        }
        status = tds_hamt_bitmap_copy_create(
            policy,
            branch->data_map & ~selected_bit,
            branch->node_map | selected_bit,
            data,
            branch->data_count - 1u,
            children,
            branch->node_count + 1u,
            result);
        tds_hamt_node_release(policy, child);
        *added = true;
        return status;
    }

    if ((branch->node_map & selected_bit) == 0) {
        const size_t data_slot = tds_hamt_slot(branch->data_map, selected_bit);
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
        return tds_hamt_bitmap_copy_create(
            policy,
            branch->data_map | selected_bit,
            branch->node_map,
            data,
            branch->data_count + 1u,
            source_children,
            branch->node_count,
            result);
    }

    const size_t selected_slot = tds_hamt_slot(branch->node_map, selected_bit);
    tds_hamt_node *new_child = NULL;
    tds_hamt_status status = tds_hamt_node_set(
        policy,
        source_children[selected_slot],
        key,
        value,
        hash,
        shift + TDS_HAMT_BITS_PER_LEVEL,
        overwrite,
        added,
        &new_child);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    if (new_child == source_children[selected_slot]) {
        tds_hamt_node_release(policy, new_child);
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    for (size_t i = 0; i < branch->node_count; ++i) {
        children[i] = i == selected_slot ? new_child : source_children[i];
    }
    status = tds_hamt_bitmap_copy_create(
        policy, branch->data_map, branch->node_map,
        source_data, branch->data_count, children, branch->node_count, result);
    tds_hamt_node_release(policy, new_child);
    return status;
}

static tds_hamt_status tds_hamt_node_remove(
    const tds_hamt_policy *policy,
    const tds_hamt_node *node,
    const void *key,
    uint32_t hash,
    int shift,
    bool *removed,
    const void **removed_value,
    tds_hamt_node **result) {
    if (node->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
        if (leaf->hash == hash && tds_hamt_keys_equal(policy, leaf->key, key)) {
            *removed = true;
            *removed_value = leaf->value;
            *result = NULL;
            return TDS_HAMT_OK;
        }

        *removed = false;
        *removed_value = NULL;
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    if (node->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *collision = (const tds_hamt_collision_node *)node;
        if (collision->hash != hash) {
            *removed = false;
            *removed_value = NULL;
            *result = tds_hamt_node_retain(node);
            return TDS_HAMT_OK;
        }

        for (size_t i = 0; i < collision->count; ++i) {
            if (!tds_hamt_keys_equal(policy, collision->entries[i].key, key)) {
                continue;
            }

            *removed = true;
            *removed_value = collision->entries[i].value;
            if (collision->count == 2) {
                const size_t remaining = 1u - i;
                void *retained_key = NULL;
                void *retained_value = NULL;
                tds_hamt_status status =
                    tds_hamt_checked_retain_key(policy, collision->entries[remaining].key, &retained_key);
                if (status == TDS_HAMT_OK) {
                    status = tds_hamt_checked_retain_value(
                        policy, collision->entries[remaining].value, &retained_value);
                }
                if (status == TDS_HAMT_OK) {
                    status = tds_hamt_leaf_create_from_retained(
                        collision->hash, retained_key, retained_value, result);
                } else {
                    *result = NULL;
                }
                if (status != TDS_HAMT_OK) {
                    tds_hamt_release_key(policy, retained_key);
                    tds_hamt_release_value(policy, retained_value);
                }
                return status;
            }

            tds_hamt_collision_node *shrunk =
                (tds_hamt_collision_node *)tds_hamt_allocate(
                    sizeof(*shrunk) + (collision->count - 1u) * sizeof(tds_hamt_entry));
            if (shrunk == NULL) {
                *result = NULL;
                return TDS_HAMT_OUT_OF_MEMORY;
            }

            shrunk->base.kind = TDS_HAMT_NODE_COLLISION;
            shrunk->base.ref_count = 1;
            shrunk->base.subtree_count = collision->count - 1;
            shrunk->hash = collision->hash;
            shrunk->count = collision->count - 1u;
            tds_hamt_status status = TDS_HAMT_OK;
            size_t written = 0;
            for (size_t source = 0; status == TDS_HAMT_OK && source < collision->count; ++source) {
                if (source == i) {
                    continue;
                }
                status = tds_hamt_collision_write_entry(
                    policy, shrunk, &written, collision->entries[source].key, collision->entries[source].value);
            }
            if (status != TDS_HAMT_OK) {
                shrunk->count = written;
                tds_hamt_node_release(policy, &shrunk->base);
                *result = NULL;
                return status;
            }

            *result = &shrunk->base;
            return TDS_HAMT_OK;
        }

        *removed = false;
        *removed_value = NULL;
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
    const tds_hamt_inline_entry *source_data = tds_hamt_bitmap_data_const(branch);
    tds_hamt_node *const *source_children = tds_hamt_bitmap_children_const(branch);
    const uint32_t selected_bit = tds_hamt_bit(tds_hamt_index(hash, shift));
    tds_hamt_inline_entry data[32];
    tds_hamt_node *children[32];

    if ((branch->data_map & selected_bit) != 0) {
        const size_t selected_slot = tds_hamt_slot(branch->data_map, selected_bit);
        const tds_hamt_inline_entry *existing = &source_data[selected_slot];
        if (existing->hash != hash || !tds_hamt_keys_equal(policy, existing->entry.key, key)) {
            *removed = false;
            *removed_value = NULL;
            *result = tds_hamt_node_retain(node);
            return TDS_HAMT_OK;
        }
        for (size_t source = 0, target = 0; source < branch->data_count; ++source) {
            if (source != selected_slot) {
                data[target++] = source_data[source];
            }
        }
        *removed = true;
        *removed_value = existing->entry.value;
        return tds_hamt_bitmap_rebuild(
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
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    const size_t selected_slot = tds_hamt_slot(branch->node_map, selected_bit);
    tds_hamt_node *new_child = NULL;
    tds_hamt_status status = tds_hamt_node_remove(
        policy,
        source_children[selected_slot],
        key,
        hash,
        shift + TDS_HAMT_BITS_PER_LEVEL,
        removed,
        removed_value,
        &new_child);
    if (status != TDS_HAMT_OK) {
        return status;
    }

    if (!*removed) {
        tds_hamt_node_release(policy, new_child);
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    if (new_child == NULL) {
        for (size_t source = 0, target = 0; source < branch->node_count; ++source) {
            if (source == selected_slot) {
                continue;
            }
            children[target++] = source_children[source];
        }
        return tds_hamt_bitmap_rebuild(
            policy,
            branch->data_map,
            branch->node_map & ~selected_bit,
            source_data,
            branch->data_count,
            children,
            branch->node_count - 1u,
            result);
    }

    if (new_child->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)new_child;
        const size_t data_slot = tds_hamt_slot(branch->data_map, selected_bit);
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
        status = tds_hamt_bitmap_rebuild(
            policy,
            branch->data_map | selected_bit,
            branch->node_map & ~selected_bit,
            data,
            branch->data_count + 1u,
            children,
            branch->node_count - 1u,
            result);
        tds_hamt_node_release(policy, new_child);
        return status;
    }

    for (size_t i = 0; i < branch->node_count; ++i) {
        children[i] = i == selected_slot ? new_child : source_children[i];
    }
    status = tds_hamt_bitmap_rebuild(
        policy, branch->data_map, branch->node_map,
        source_data, branch->data_count,
        children, branch->node_count, result);
    tds_hamt_node_release(policy, new_child);
    return status;
}

static tds_hamt_status tds_hamt_bitmap_rebuild(
    const tds_hamt_policy *policy,
    uint32_t data_map,
    uint32_t node_map,
    const tds_hamt_inline_entry *data,
    size_t data_count,
    tds_hamt_node *const *children,
    size_t child_count,
    tds_hamt_node **result) {
    if (data_count == 0 && child_count == 0) {
        *result = NULL;
        return TDS_HAMT_OK;
    }
    if (data_count == 1u && child_count == 0) {
        return tds_hamt_leaf_create(
            policy, data[0].hash, data[0].entry.key, data[0].entry.value, result);
    }
    if (data_count == 0 && child_count == 1u
        && children[0]->kind != TDS_HAMT_NODE_BITMAP_INDEXED) {
        *result = tds_hamt_node_retain(children[0]);
        return TDS_HAMT_OK;
    }
    return tds_hamt_bitmap_copy_create(
        policy, data_map, node_map, data, data_count, children, child_count, result);
}

static bool tds_hamt_policy_callbacks_compatible(
    const tds_hamt_policy *left,
    const tds_hamt_policy *right) {
    return left->hash == right->hash &&
        left->key_equal == right->key_equal &&
        left->value_equal == right->value_equal &&
        left->retain_key == right->retain_key &&
        left->retain_value == right->retain_value &&
        left->release_key == right->release_key &&
        left->release_value == right->release_value &&
        left->context == right->context;
}

static uint32_t tds_hamt_hash_node_hash(const tds_hamt_node *node) {
    return node->kind == TDS_HAMT_NODE_LEAF
        ? ((const tds_hamt_leaf_node *)node)->hash
        : ((const tds_hamt_collision_node *)node)->hash;
}

static size_t tds_hamt_hash_node_entry_count(const tds_hamt_node *node) {
    return node->kind == TDS_HAMT_NODE_LEAF
        ? 1u
        : ((const tds_hamt_collision_node *)node)->count;
}

static tds_hamt_entry tds_hamt_hash_node_entry_at(
    const tds_hamt_node *node,
    size_t index) {
    if (node->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
        return (tds_hamt_entry){ leaf->key, leaf->value };
    }
    return ((const tds_hamt_collision_node *)node)->entries[index];
}

static tds_hamt_status tds_hamt_hash_result_create(
    const tds_hamt_policy *policy,
    uint32_t hash,
    const tds_hamt_entry *entries,
    size_t count,
    tds_hamt_node **result) {
    if (count == 0) {
        *result = NULL;
        return TDS_HAMT_OK;
    }
    if (count == 1) {
        return tds_hamt_leaf_create(
            policy, hash, entries[0].key, entries[0].value, result);
    }
    if (count > (SIZE_MAX - sizeof(tds_hamt_collision_node)) / sizeof(tds_hamt_entry)) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    tds_hamt_collision_node *collision =
        (tds_hamt_collision_node *)tds_hamt_allocate(
            sizeof(*collision) + count * sizeof(tds_hamt_entry));
    if (collision == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    collision->base.kind = TDS_HAMT_NODE_COLLISION;
    collision->base.ref_count = 1;
    collision->base.subtree_count = count;
    collision->hash = hash;
    collision->count = 0;
    tds_hamt_status status = TDS_HAMT_OK;
    while (status == TDS_HAMT_OK && collision->count < count) {
        status = tds_hamt_collision_write_entry(
            policy,
            collision,
            &collision->count,
            entries[collision->count].key,
            entries[collision->count].value);
    }
    if (status != TDS_HAMT_OK) {
        tds_hamt_node_release(policy, &collision->base);
        *result = NULL;
        return status;
    }
    *result = &collision->base;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_logical_slot(
    const tds_hamt_policy *policy,
    const tds_hamt_node *node,
    int slot_index,
    int shift,
    tds_hamt_node **result) {
    if (node == NULL) {
        *result = NULL;
        return TDS_HAMT_OK;
    }
    if (node->kind != TDS_HAMT_NODE_BITMAP_INDEXED) {
        *result = tds_hamt_index(tds_hamt_hash_node_hash(node), shift) == slot_index
            ? tds_hamt_node_retain(node)
            : NULL;
        return TDS_HAMT_OK;
    }
    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
    const uint32_t selected_bit = tds_hamt_bit(slot_index);
    if ((branch->data_map & selected_bit) != 0) {
        const tds_hamt_inline_entry *entry =
            &tds_hamt_bitmap_data_const(branch)[tds_hamt_slot(branch->data_map, selected_bit)];
        return tds_hamt_leaf_create(
            policy, entry->hash, entry->entry.key, entry->entry.value, result);
    }
    *result = (branch->node_map & selected_bit) != 0
        ? tds_hamt_node_retain(
            tds_hamt_bitmap_children_const(branch)[tds_hamt_slot(branch->node_map, selected_bit)])
        : NULL;
    return TDS_HAMT_OK;
}

static void tds_hamt_release_slots(
    const tds_hamt_policy *policy,
    tds_hamt_node **slots) {
    for (size_t index = 0; index < 32; ++index) {
        tds_hamt_node_release(policy, slots[index]);
        slots[index] = NULL;
    }
}

static bool tds_hamt_slot_matches_inline(
    const tds_hamt_policy *policy,
    const tds_hamt_node *actual,
    const tds_hamt_inline_entry *expected) {
    if (actual == NULL || actual->kind != TDS_HAMT_NODE_LEAF) {
        return false;
    }
    const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)actual;
    return leaf->hash == expected->hash &&
        tds_hamt_keys_equal(policy, leaf->key, expected->entry.key) &&
        tds_hamt_values_equal(policy, leaf->value, expected->entry.value);
}

static bool tds_hamt_logical_slots_match(
    const tds_hamt_policy *policy,
    tds_hamt_node *const *slots,
    const tds_hamt_node *original,
    int shift) {
    if (original->kind != TDS_HAMT_NODE_BITMAP_INDEXED) {
        const int occupied = tds_hamt_index(tds_hamt_hash_node_hash(original), shift);
        for (int index = 0; index < 32; ++index) {
            if ((index == occupied && slots[index] != original) ||
                (index != occupied && slots[index] != NULL)) {
                return false;
            }
        }
        return true;
    }
    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)original;
    const tds_hamt_inline_entry *data = tds_hamt_bitmap_data_const(branch);
    tds_hamt_node *const *children = tds_hamt_bitmap_children_const(branch);
    for (int index = 0; index < 32; ++index) {
        const uint32_t selected_bit = tds_hamt_bit(index);
        if ((branch->data_map & selected_bit) != 0) {
            if (!tds_hamt_slot_matches_inline(
                    policy,
                    slots[index],
                    &data[tds_hamt_slot(branch->data_map, selected_bit)])) {
                return false;
            }
        } else if ((branch->node_map & selected_bit) != 0) {
            if (slots[index] != children[tds_hamt_slot(branch->node_map, selected_bit)]) {
                return false;
            }
        } else if (slots[index] != NULL) {
            return false;
        }
    }
    return true;
}

static tds_hamt_status tds_hamt_build_logical_node(
    const tds_hamt_policy *policy,
    tds_hamt_node **slots,
    const tds_hamt_node *original_left,
    int shift,
    tds_hamt_node **result) {
    if (tds_hamt_logical_slots_match(policy, slots, original_left, shift)) {
        *result = tds_hamt_node_retain(original_left);
        tds_hamt_release_slots(policy, slots);
        return TDS_HAMT_OK;
    }
    uint32_t data_map = 0;
    uint32_t node_map = 0;
    tds_hamt_inline_entry data[32];
    tds_hamt_node *children[32];
    size_t data_count = 0;
    size_t child_count = 0;
    for (int index = 0; index < 32; ++index) {
        const tds_hamt_node *node = slots[index];
        if (node == NULL) {
            continue;
        }
        if (node->kind == TDS_HAMT_NODE_LEAF) {
            const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
            data_map |= tds_hamt_bit(index);
            data[data_count++] = (tds_hamt_inline_entry){
                leaf->hash, { leaf->key, leaf->value }
            };
        } else {
            node_map |= tds_hamt_bit(index);
            children[child_count++] = slots[index];
        }
    }
    const tds_hamt_status status = tds_hamt_bitmap_rebuild(
        policy,
        data_map,
        node_map,
        data,
        data_count,
        children,
        child_count,
        result);
    tds_hamt_release_slots(policy, slots);
    return status;
}

static tds_hamt_status tds_hamt_combine_hash_nodes(
    const tds_hamt_policy *policy,
    const tds_hamt_node *left,
    const tds_hamt_node *right,
    int shift,
    tds_hamt_combine_operation operation,
    tds_hamt_node **result) {
    const uint32_t left_hash = tds_hamt_hash_node_hash(left);
    const uint32_t right_hash = tds_hamt_hash_node_hash(right);
    if (left_hash != right_hash) {
        if (operation == TDS_HAMT_COMBINE_INTERSECT) {
            *result = NULL;
            return TDS_HAMT_OK;
        }
        if (operation == TDS_HAMT_COMBINE_EXCEPT) {
            *result = tds_hamt_node_retain(left);
            return TDS_HAMT_OK;
        }
        if (shift >= 32) {
            *result = NULL;
            return TDS_HAMT_INVALID_ARGUMENT;
        }
        tds_hamt_node *slots[32] = { NULL };
        const int left_index = tds_hamt_index(left_hash, shift);
        const int right_index = tds_hamt_index(right_hash, shift);
        tds_hamt_status status = TDS_HAMT_OK;
        if (left_index != right_index) {
            slots[left_index] = tds_hamt_node_retain(left);
            slots[right_index] = tds_hamt_node_retain(right);
        } else {
            status = tds_hamt_combine_hash_nodes(
                policy,
                left,
                right,
                shift + TDS_HAMT_BITS_PER_LEVEL,
                operation,
                &slots[left_index]);
        }
        if (status != TDS_HAMT_OK) {
            tds_hamt_release_slots(policy, slots);
            return status;
        }
        return tds_hamt_build_logical_node(policy, slots, left, shift, result);
    }

    const size_t left_count = tds_hamt_hash_node_entry_count(left);
    const size_t right_count = tds_hamt_hash_node_entry_count(right);
    if (left_count > SIZE_MAX - right_count ||
        left_count + right_count > SIZE_MAX / sizeof(tds_hamt_entry)) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    tds_hamt_entry *entries = (tds_hamt_entry *)tds_hamt_allocate(
        (left_count + right_count) * sizeof(*entries));
    if (entries == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }
    size_t written = 0;
    for (size_t i = 0; i < left_count; ++i) {
        const tds_hamt_entry left_entry = tds_hamt_hash_node_entry_at(left, i);
        bool found = false;
        tds_hamt_entry matching_right = { NULL, NULL };
        for (size_t j = 0; j < right_count; ++j) {
            const tds_hamt_entry candidate = tds_hamt_hash_node_entry_at(right, j);
            if (tds_hamt_keys_equal(policy, left_entry.key, candidate.key)) {
                found = true;
                matching_right = candidate;
                break;
            }
        }
        if (operation == TDS_HAMT_COMBINE_UNION) {
            entries[written++] = (tds_hamt_entry){
                left_entry.key,
                found && !tds_hamt_values_equal(
                    policy, left_entry.value, matching_right.value)
                    ? matching_right.value
                    : left_entry.value
            };
        } else if (operation == TDS_HAMT_COMBINE_INTERSECT && found) {
            entries[written++] = left_entry;
        } else if ((operation == TDS_HAMT_COMBINE_EXCEPT ||
                    operation == TDS_HAMT_COMBINE_SYMMETRIC_EXCEPT) && !found) {
            entries[written++] = left_entry;
        }
    }
    if (operation == TDS_HAMT_COMBINE_UNION ||
        operation == TDS_HAMT_COMBINE_SYMMETRIC_EXCEPT) {
        for (size_t j = 0; j < right_count; ++j) {
            const tds_hamt_entry right_entry = tds_hamt_hash_node_entry_at(right, j);
            bool found = false;
            for (size_t i = 0; i < left_count; ++i) {
                const tds_hamt_entry left_entry = tds_hamt_hash_node_entry_at(left, i);
                if (tds_hamt_keys_equal(policy, left_entry.key, right_entry.key)) {
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
        const tds_hamt_entry original = tds_hamt_hash_node_entry_at(left, i);
        matches_left = tds_hamt_keys_equal(policy, original.key, entries[i].key) &&
            tds_hamt_values_equal(policy, original.value, entries[i].value);
    }
    tds_hamt_status status;
    if (matches_left) {
        *result = tds_hamt_node_retain(left);
        status = TDS_HAMT_OK;
    } else {
        status = tds_hamt_hash_result_create(
            policy, left_hash, entries, written, result);
    }
    free(entries);
    return status;
}

static tds_hamt_status tds_hamt_combine_nodes(
    const tds_hamt_policy *policy,
    const tds_hamt_node *left,
    const tds_hamt_node *right,
    int shift,
    tds_hamt_combine_operation operation,
    tds_hamt_node **result) {
    if (left == right) {
        *result = operation == TDS_HAMT_COMBINE_UNION ||
                operation == TDS_HAMT_COMBINE_INTERSECT
            ? tds_hamt_node_retain(left)
            : NULL;
        return TDS_HAMT_OK;
    }
    if (left == NULL) {
        *result = operation == TDS_HAMT_COMBINE_UNION ||
                operation == TDS_HAMT_COMBINE_SYMMETRIC_EXCEPT
            ? tds_hamt_node_retain(right)
            : NULL;
        return TDS_HAMT_OK;
    }
    if (right == NULL) {
        *result = operation == TDS_HAMT_COMBINE_INTERSECT
            ? NULL
            : tds_hamt_node_retain(left);
        return TDS_HAMT_OK;
    }
    if (left->kind != TDS_HAMT_NODE_BITMAP_INDEXED &&
        right->kind != TDS_HAMT_NODE_BITMAP_INDEXED) {
        return tds_hamt_combine_hash_nodes(
            policy, left, right, shift, operation, result);
    }

    tds_hamt_node *slots[32] = { NULL };
    tds_hamt_status status = TDS_HAMT_OK;
    for (int index = 0; status == TDS_HAMT_OK && index < 32; ++index) {
        tds_hamt_node *left_slot = NULL;
        tds_hamt_node *right_slot = NULL;
        status = tds_hamt_logical_slot(policy, left, index, shift, &left_slot);
        if (status == TDS_HAMT_OK) {
            status = tds_hamt_logical_slot(policy, right, index, shift, &right_slot);
        }
        if (status == TDS_HAMT_OK) {
            status = tds_hamt_combine_nodes(
                policy,
                left_slot,
                right_slot,
                shift + TDS_HAMT_BITS_PER_LEVEL,
                operation,
                &slots[index]);
        }
        tds_hamt_node_release(policy, left_slot);
        tds_hamt_node_release(policy, right_slot);
    }
    if (status != TDS_HAMT_OK) {
        tds_hamt_release_slots(policy, slots);
        *result = NULL;
        return status;
    }
    return tds_hamt_build_logical_node(policy, slots, left, shift, result);
}

static bool tds_hamt_try_get_entry(
    const tds_hamt_map *map,
    const void *key,
    const void **actual_key,
    const void **value) {
    if (map == NULL || map->root == NULL) {
        return false;
    }

    const uint32_t hash = tds_hamt_get_hash(map, key);
    int shift = 0;
    const tds_hamt_node *node = map->root;

    while (node->kind == TDS_HAMT_NODE_BITMAP_INDEXED) {
        const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
        const uint32_t selected_bit = tds_hamt_bit(tds_hamt_index(hash, shift));
        if ((branch->data_map & selected_bit) != 0) {
            const tds_hamt_inline_entry *entry =
                &tds_hamt_bitmap_data_const(branch)[tds_hamt_slot(branch->data_map, selected_bit)];
            if (entry->hash == hash && tds_hamt_keys_equal(&map->policy, entry->entry.key, key)) {
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

        node = tds_hamt_bitmap_children_const(branch)[tds_hamt_slot(branch->node_map, selected_bit)];
        shift += TDS_HAMT_BITS_PER_LEVEL;
    }

    if (node->kind == TDS_HAMT_NODE_LEAF) {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)node;
        if (leaf->hash == hash && tds_hamt_keys_equal(&map->policy, leaf->key, key)) {
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

    const tds_hamt_collision_node *collision = (const tds_hamt_collision_node *)node;
    if (collision->hash != hash) {
        return false;
    }

    for (size_t i = 0; i < collision->count; ++i) {
        if (tds_hamt_keys_equal(&map->policy, collision->entries[i].key, key)) {
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
