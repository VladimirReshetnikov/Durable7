#include <Tools/DataStructures/Hamt/hamt.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum {
    TDS_HAMT_BITS_PER_LEVEL = 5,
    TDS_HAMT_BRANCH_MASK = 31
};

typedef struct tds_hamt_node tds_hamt_node;

struct tds_hamt_node {
    tds_hamt_node_kind kind;
    size_t ref_count;
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

typedef struct tds_hamt_bitmap_node {
    tds_hamt_node base;
    uint32_t bitmap;
    size_t count;
    tds_hamt_node *children[];
} tds_hamt_bitmap_node;

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
    uint32_t bitmap,
    tds_hamt_node **children,
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
    uint32_t bitmap,
    tds_hamt_node **children,
    size_t child_count,
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
        tds_hamt_map_destroy(result);
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

    result->root = new_root;
    result->count = map->count - (local_removed ? 1u : 0u);
    result->policy = map->policy;
    if (removed != NULL) {
        *removed = local_removed;
    }
    if (removed_value != NULL) {
        *removed_value = local_removed ? local_removed_value : NULL;
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
        result->root = NULL;
        result->count = 0;
        result->policy = map->policy;
    }

    return TDS_HAMT_OK;
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
            if (top->index == top->child_count) {
                memset(top, 0, sizeof(*top));
                --iterator->depth;
                continue;
            }

            node = top->children[top->index++];
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
        iterator->frames[iterator->depth].children = (const tds_hamt_node *const *)branch->children;
        iterator->frames[iterator->depth].child_count = branch->count;
        iterator->frames[iterator->depth].index = 0;
        ++iterator->depth;
        node = NULL;
    }
}

bool tds_hamt_map_shares_root(const tds_hamt_map *left, const tds_hamt_map *right) {
    return left != NULL && right != NULL && left->root == right->root;
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
        return branch->count;
    }

    const size_t copy_count = branch->count < child_capacity ? branch->count : child_capacity;
    for (size_t i = 0; i < copy_count; ++i) {
        children[i] = branch->children[i];
    }

    return branch->count;
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
    *result = current;
    return TDS_HAMT_OK;
}

bool tds_hamt_set_is_subset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count) {
    if (set == NULL || (item_count != 0 && items == NULL)) {
        return false;
    }

    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    tds_hamt_set probe;
    if (tds_hamt_set_create_range(&policy, items, item_count, &probe) != TDS_HAMT_OK) {
        return false;
    }

    bool is_subset = true;
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(set, &iterator);
    const void *item = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &item)) {
        if (!tds_hamt_set_contains(&probe, item)) {
            is_subset = false;
            break;
        }
    }

    tds_hamt_set_destroy(&probe);
    return is_subset;
}

bool tds_hamt_set_is_proper_subset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count) {
    if (set == NULL || (item_count != 0 && items == NULL)) {
        return false;
    }

    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    tds_hamt_set probe;
    if (tds_hamt_set_create_range(&policy, items, item_count, &probe) != TDS_HAMT_OK) {
        return false;
    }

    const bool proper_by_count = tds_hamt_set_count(set) < tds_hamt_set_count(&probe);
    tds_hamt_set_destroy(&probe);
    return proper_by_count && tds_hamt_set_is_subset_of_many(set, items, item_count);
}

bool tds_hamt_set_is_superset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count) {
    if (set == NULL || (item_count != 0 && items == NULL)) {
        return false;
    }

    for (size_t i = 0; i < item_count; ++i) {
        if (!tds_hamt_set_contains(set, items[i])) {
            return false;
        }
    }

    return true;
}

bool tds_hamt_set_is_proper_superset_of_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count) {
    if (set == NULL || (item_count != 0 && items == NULL)) {
        return false;
    }

    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    tds_hamt_set probe;
    if (tds_hamt_set_create_range(&policy, items, item_count, &probe) != TDS_HAMT_OK) {
        return false;
    }

    const bool smaller = tds_hamt_set_count(&probe) < tds_hamt_set_count(set);
    bool contains_all = true;
    tds_hamt_set_iterator iterator;
    tds_hamt_set_iterator_init(&probe, &iterator);
    const void *item = NULL;
    while (tds_hamt_set_iterator_next(&iterator, &item)) {
        if (!tds_hamt_set_contains(set, item)) {
            contains_all = false;
            break;
        }
    }

    tds_hamt_set_destroy(&probe);
    return smaller && contains_all;
}

bool tds_hamt_set_overlaps_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count) {
    if (set == NULL || (item_count != 0 && items == NULL)) {
        return false;
    }

    for (size_t i = 0; i < item_count; ++i) {
        if (tds_hamt_set_contains(set, items[i])) {
            return true;
        }
    }

    return false;
}

bool tds_hamt_set_equals_many(
    const tds_hamt_set *set,
    const void *const *items,
    size_t item_count) {
    if (set == NULL || (item_count != 0 && items == NULL)) {
        return false;
    }

    tds_hamt_set_policy policy = tds_hamt_normalize_set_policy(&(tds_hamt_set_policy){
        set->map.policy.hash,
        set->map.policy.key_equal,
        set->map.policy.retain_key,
        set->map.policy.release_key,
        set->map.policy.context
    });
    tds_hamt_set probe;
    if (tds_hamt_set_create_range(&policy, items, item_count, &probe) != TDS_HAMT_OK) {
        return false;
    }

    const bool same_count = tds_hamt_set_count(set) == tds_hamt_set_count(&probe);
    tds_hamt_set_destroy(&probe);
    return same_count && tds_hamt_set_is_subset_of_many(set, items, item_count);
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

static uint32_t tds_hamt_pointer_hash(const void *item, void *context) {
    (void)context;
    uintptr_t value = (uintptr_t)item;
    value ^= value >> 33;
    value *= (uintptr_t)0xff51afd7ed558ccdull;
    value ^= value >> 33;
    value *= (uintptr_t)0xc4ceb9fe1a85ec53ull;
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
        for (size_t i = 0; i < branch->count; ++i) {
            tds_hamt_node_release(policy, branch->children[i]);
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
    return policy->value_equal(left, right, policy->context);
}

static void *tds_hamt_retain_key(const tds_hamt_policy *policy, const void *key) {
    return policy->retain_key(key, policy->context);
}

static void *tds_hamt_retain_value(const tds_hamt_policy *policy, const void *value) {
    return policy->retain_value(value, policy->context);
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

static tds_hamt_status tds_hamt_leaf_create(
    const tds_hamt_policy *policy,
    uint32_t hash,
    const void *key,
    const void *value,
    tds_hamt_node **result) {
    void *retained_key = tds_hamt_retain_key(policy, key);
    void *retained_value = tds_hamt_retain_value(policy, value);
    const tds_hamt_status status = tds_hamt_leaf_create_from_retained(hash, retained_key, retained_value, result);
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
    tds_hamt_leaf_node *leaf = (tds_hamt_leaf_node *)malloc(sizeof(*leaf));
    if (leaf == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    leaf->base.kind = TDS_HAMT_NODE_LEAF;
    leaf->base.ref_count = 1;
    leaf->hash = hash;
    leaf->key = key;
    leaf->value = value;
    *result = &leaf->base;
    return TDS_HAMT_OK;
}

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
        (tds_hamt_collision_node *)malloc(sizeof(*collision) + total_count * sizeof(tds_hamt_entry));
    if (collision == NULL) {
        tds_hamt_node_release(policy, left);
        tds_hamt_node_release(policy, right);
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    collision->base.kind = TDS_HAMT_NODE_COLLISION;
    collision->base.ref_count = 1;
    collision->hash = hash;
    collision->count = total_count;

    size_t written = 0;
    if (left->kind == TDS_HAMT_NODE_COLLISION) {
        const tds_hamt_collision_node *source = (const tds_hamt_collision_node *)left;
        for (size_t i = 0; i < source->count; ++i) {
            collision->entries[written].key = tds_hamt_retain_key(policy, source->entries[i].key);
            collision->entries[written].value = tds_hamt_retain_value(policy, source->entries[i].value);
            ++written;
        }
    } else {
        const tds_hamt_leaf_node *leaf = (const tds_hamt_leaf_node *)left;
        collision->entries[written].key = tds_hamt_retain_key(policy, leaf->key);
        collision->entries[written].value = tds_hamt_retain_value(policy, leaf->value);
        ++written;
    }

    const tds_hamt_leaf_node *right_leaf = (const tds_hamt_leaf_node *)right;
    collision->entries[written].key = tds_hamt_retain_key(policy, right_leaf->key);
    collision->entries[written].value = tds_hamt_retain_value(policy, right_leaf->value);

    tds_hamt_node_release(policy, left);
    tds_hamt_node_release(policy, right);
    *result = &collision->base;
    return TDS_HAMT_OK;
}

static tds_hamt_status tds_hamt_bitmap_create(
    uint32_t bitmap,
    tds_hamt_node **children,
    size_t child_count,
    tds_hamt_node **result) {
    tds_hamt_bitmap_node *branch =
        (tds_hamt_bitmap_node *)malloc(sizeof(*branch) + child_count * sizeof(tds_hamt_node *));
    if (branch == NULL) {
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    branch->base.kind = TDS_HAMT_NODE_BITMAP_INDEXED;
    branch->base.ref_count = 1;
    branch->bitmap = bitmap;
    branch->count = child_count;
    for (size_t i = 0; i < child_count; ++i) {
        branch->children[i] = children[i];
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
        status = tds_hamt_bitmap_create(left_bit, children, 1, result);
        if (status != TDS_HAMT_OK) {
            tds_hamt_node_release(policy, child);
        }
        return status;
    }

    tds_hamt_node *children[2];
    if (left_index < right_index) {
        children[0] = left;
        children[1] = right;
    } else {
        children[0] = right;
        children[1] = left;
    }

    const tds_hamt_status status = tds_hamt_bitmap_create(left_bit | right_bit, children, 2, result);
    if (status != TDS_HAMT_OK) {
        tds_hamt_node_release(policy, left);
        tds_hamt_node_release(policy, right);
    }

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

            void *retained_key = tds_hamt_retain_key(policy, leaf->key);
            void *retained_value = tds_hamt_retain_value(policy, value);
            const tds_hamt_status status =
                tds_hamt_leaf_create_from_retained(leaf->hash, retained_key, retained_value, result);
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
                (tds_hamt_collision_node *)malloc(sizeof(*replaced) + collision->count * sizeof(tds_hamt_entry));
            if (replaced == NULL) {
                *result = NULL;
                return TDS_HAMT_OUT_OF_MEMORY;
            }

            replaced->base.kind = TDS_HAMT_NODE_COLLISION;
            replaced->base.ref_count = 1;
            replaced->hash = collision->hash;
            replaced->count = collision->count;
            for (size_t j = 0; j < collision->count; ++j) {
                replaced->entries[j].key = tds_hamt_retain_key(policy, collision->entries[j].key);
                replaced->entries[j].value = tds_hamt_retain_value(
                    policy,
                    j == i ? value : collision->entries[j].value);
            }
            *result = &replaced->base;
            return TDS_HAMT_OK;
        }

        tds_hamt_collision_node *expanded =
            (tds_hamt_collision_node *)malloc(sizeof(*expanded) + (collision->count + 1u) * sizeof(tds_hamt_entry));
        if (expanded == NULL) {
            *result = NULL;
            return TDS_HAMT_OUT_OF_MEMORY;
        }

        expanded->base.kind = TDS_HAMT_NODE_COLLISION;
        expanded->base.ref_count = 1;
        expanded->hash = collision->hash;
        expanded->count = collision->count + 1u;
        for (size_t i = 0; i < collision->count; ++i) {
            expanded->entries[i].key = tds_hamt_retain_key(policy, collision->entries[i].key);
            expanded->entries[i].value = tds_hamt_retain_value(policy, collision->entries[i].value);
        }
        expanded->entries[collision->count].key = tds_hamt_retain_key(policy, key);
        expanded->entries[collision->count].value = tds_hamt_retain_value(policy, value);
        *added = true;
        *result = &expanded->base;
        return TDS_HAMT_OK;
    }

    const tds_hamt_bitmap_node *branch = (const tds_hamt_bitmap_node *)node;
    const uint32_t selected_bit = tds_hamt_bit(tds_hamt_index(hash, shift));
    const size_t selected_slot = tds_hamt_slot(branch->bitmap, selected_bit);

    if ((branch->bitmap & selected_bit) == 0) {
        tds_hamt_node **children = (tds_hamt_node **)malloc((branch->count + 1u) * sizeof(tds_hamt_node *));
        if (children == NULL) {
            *result = NULL;
            return TDS_HAMT_OUT_OF_MEMORY;
        }

        for (size_t i = 0; i < selected_slot; ++i) {
            children[i] = tds_hamt_node_retain(branch->children[i]);
        }

        tds_hamt_status status = tds_hamt_leaf_create(policy, hash, key, value, &children[selected_slot]);
        if (status != TDS_HAMT_OK) {
            for (size_t i = 0; i < selected_slot; ++i) {
                tds_hamt_node_release(policy, children[i]);
            }
            free(children);
            return status;
        }

        for (size_t i = selected_slot; i < branch->count; ++i) {
            children[i + 1u] = tds_hamt_node_retain(branch->children[i]);
        }

        status = tds_hamt_bitmap_create(branch->bitmap | selected_bit, children, branch->count + 1u, result);
        if (status != TDS_HAMT_OK) {
            for (size_t i = 0; i < branch->count + 1u; ++i) {
                tds_hamt_node_release(policy, children[i]);
            }
        }
        free(children);
        *added = true;
        return status;
    }

    tds_hamt_node *new_child = NULL;
    tds_hamt_status status = tds_hamt_node_set(
        policy,
        branch->children[selected_slot],
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

    if (new_child == branch->children[selected_slot]) {
        tds_hamt_node_release(policy, new_child);
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    tds_hamt_node **children = (tds_hamt_node **)malloc(branch->count * sizeof(tds_hamt_node *));
    if (children == NULL) {
        tds_hamt_node_release(policy, new_child);
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < branch->count; ++i) {
        children[i] = i == selected_slot ? new_child : tds_hamt_node_retain(branch->children[i]);
    }

    status = tds_hamt_bitmap_create(branch->bitmap, children, branch->count, result);
    if (status != TDS_HAMT_OK) {
        for (size_t i = 0; i < branch->count; ++i) {
            tds_hamt_node_release(policy, children[i]);
        }
    }
    free(children);
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
                void *retained_key = tds_hamt_retain_key(policy, collision->entries[remaining].key);
                void *retained_value = tds_hamt_retain_value(policy, collision->entries[remaining].value);
                const tds_hamt_status status =
                    tds_hamt_leaf_create_from_retained(collision->hash, retained_key, retained_value, result);
                if (status != TDS_HAMT_OK) {
                    tds_hamt_release_key(policy, retained_key);
                    tds_hamt_release_value(policy, retained_value);
                }
                return status;
            }

            tds_hamt_collision_node *shrunk =
                (tds_hamt_collision_node *)malloc(sizeof(*shrunk) + (collision->count - 1u) * sizeof(tds_hamt_entry));
            if (shrunk == NULL) {
                *result = NULL;
                return TDS_HAMT_OUT_OF_MEMORY;
            }

            shrunk->base.kind = TDS_HAMT_NODE_COLLISION;
            shrunk->base.ref_count = 1;
            shrunk->hash = collision->hash;
            shrunk->count = collision->count - 1u;
            for (size_t source = 0, target = 0; source < collision->count; ++source) {
                if (source == i) {
                    continue;
                }
                shrunk->entries[target].key = tds_hamt_retain_key(policy, collision->entries[source].key);
                shrunk->entries[target].value = tds_hamt_retain_value(policy, collision->entries[source].value);
                ++target;
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
    const uint32_t selected_bit = tds_hamt_bit(tds_hamt_index(hash, shift));
    if ((branch->bitmap & selected_bit) == 0) {
        *removed = false;
        *removed_value = NULL;
        *result = tds_hamt_node_retain(node);
        return TDS_HAMT_OK;
    }

    const size_t selected_slot = tds_hamt_slot(branch->bitmap, selected_bit);
    tds_hamt_node *new_child = NULL;
    tds_hamt_status status = tds_hamt_node_remove(
        policy,
        branch->children[selected_slot],
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
        if (branch->count == 1) {
            *result = NULL;
            return TDS_HAMT_OK;
        }

        tds_hamt_node **children = (tds_hamt_node **)malloc((branch->count - 1u) * sizeof(tds_hamt_node *));
        if (children == NULL) {
            *result = NULL;
            return TDS_HAMT_OUT_OF_MEMORY;
        }

        for (size_t source = 0, target = 0; source < branch->count; ++source) {
            if (source == selected_slot) {
                continue;
            }
            children[target++] = tds_hamt_node_retain(branch->children[source]);
        }

        status = tds_hamt_bitmap_rebuild(branch->bitmap ^ selected_bit, children, branch->count - 1u, result);
        if (status != TDS_HAMT_OK) {
            for (size_t i = 0; i < branch->count - 1u; ++i) {
                tds_hamt_node_release(policy, children[i]);
            }
        }
        free(children);
        return status;
    }

    tds_hamt_node **children = (tds_hamt_node **)malloc(branch->count * sizeof(tds_hamt_node *));
    if (children == NULL) {
        tds_hamt_node_release(policy, new_child);
        *result = NULL;
        return TDS_HAMT_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < branch->count; ++i) {
        children[i] = i == selected_slot ? new_child : tds_hamt_node_retain(branch->children[i]);
    }

    status = tds_hamt_bitmap_rebuild(branch->bitmap, children, branch->count, result);
    if (status != TDS_HAMT_OK) {
        for (size_t i = 0; i < branch->count; ++i) {
            tds_hamt_node_release(policy, children[i]);
        }
    }
    free(children);
    return status;
}

static tds_hamt_status tds_hamt_bitmap_rebuild(
    uint32_t bitmap,
    tds_hamt_node **children,
    size_t child_count,
    tds_hamt_node **result) {
    if (child_count == 1u && children[0]->kind != TDS_HAMT_NODE_BITMAP_INDEXED) {
        *result = children[0];
        return TDS_HAMT_OK;
    }

    return tds_hamt_bitmap_create(bitmap, children, child_count, result);
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
        if ((branch->bitmap & selected_bit) == 0) {
            return false;
        }

        node = branch->children[tds_hamt_slot(branch->bitmap, selected_bit)];
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
