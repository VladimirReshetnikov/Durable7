#include <tools/data_structures/finger_tree/priority_search_queue.h>

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
typedef volatile LONG64 ft_psq_ref_count;

static void ft_psq_ref_init(ft_psq_ref_count* value)
{
    *value = 1;
}

static void ft_psq_ref_retain(ft_psq_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_psq_ref_release(ft_psq_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_psq_ref_count;

static void ft_psq_ref_init(ft_psq_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_psq_ref_retain(ft_psq_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_psq_ref_release(ft_psq_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

typedef enum ft_psq_object_kind {
    FT_PSQ_OBJECT_KEY,
    FT_PSQ_OBJECT_PRIORITY,
    FT_PSQ_OBJECT_VALUE
} ft_psq_object_kind;

typedef struct ft_psq_object {
    ft_psq_ref_count refs;
    ft_psq_object_kind kind;
    void* bytes;
} ft_psq_object;

struct ft_psq_entry_rep {
    ft_psq_ref_count refs;
    ft_psq_object* key;
    ft_psq_object* priority;
    ft_psq_object* value;
};

struct ft_psq_node {
    ft_psq_ref_count refs;
    struct ft_psq_node* release_next;
    ft_psq_entry_rep* entry;
    ft_psq_entry_rep* winner;
    struct ft_psq_node* left;
    struct ft_psq_node* right;
    size_t count;
    size_t height;
};

struct ft_psq_policy_rep {
    ft_psq_ref_count refs;
    ft_psq_policy_config config;
};

typedef struct ft_psq_path_frame {
    ft_psq_node* node;
    bool went_left;
} ft_psq_path_frame;

typedef struct ft_psq_validation_frame {
    ft_psq_node* node;
    const void* lower_key;
    const void* upper_key;
    bool has_lower;
    bool has_upper;
} ft_psq_validation_frame;

typedef struct ft_psq_query_frame {
    ft_psq_node* node;
    bool emit;
} ft_psq_query_frame;

static void* ft_psq_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_psq_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_psq_type_valid(
    const ft_psq_type_policy* type,
    bool require_equals)
{
    return type->size != 0 && type->type_identity != NULL &&
        (!require_equals || type->equals != NULL) &&
        (type->destroy == NULL || type->copy != NULL);
}

static bool ft_psq_config_valid(const ft_psq_policy_config* config)
{
    return config != NULL && ft_psq_type_valid(&config->key, false) &&
        ft_psq_type_valid(&config->priority, true) &&
        ft_psq_type_valid(&config->value, true) &&
        config->key_compare != NULL && config->priority_compare != NULL &&
        config->allocator.allocate != NULL && config->allocator.deallocate != NULL;
}

static const ft_psq_type_policy* ft_psq_type_for(
    const ft_psq_policy_rep* policy,
    ft_psq_object_kind kind)
{
    if (kind == FT_PSQ_OBJECT_KEY) {
        return &policy->config.key;
    }
    if (kind == FT_PSQ_OBJECT_PRIORITY) {
        return &policy->config.priority;
    }
    return &policy->config.value;
}

static void* ft_psq_allocate_config(
    const ft_psq_policy_config* config,
    size_t size)
{
    return config->allocator.allocate(size, config->allocator.context);
}

static void ft_psq_deallocate_config(
    const ft_psq_policy_config* config,
    void* allocation)
{
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void* ft_psq_allocate(const ft_psq_policy_rep* policy, size_t size)
{
    return ft_psq_allocate_config(&policy->config, size);
}

static void ft_psq_deallocate(
    const ft_psq_policy_rep* policy,
    void* allocation)
{
    ft_psq_deallocate_config(&policy->config, allocation);
}

static bool ft_psq_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool ft_psq_add_overflows(size_t left, size_t right, size_t* result)
{
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static void ft_psq_policy_retain(ft_psq_policy_rep* policy)
{
    if (policy != NULL) {
        ft_psq_ref_retain(&policy->refs);
    }
}

static void ft_psq_policy_release(ft_psq_policy_rep* policy)
{
    if (policy != NULL && ft_psq_ref_release(&policy->refs)) {
        ft_psq_deallocate_config(&policy->config, policy);
    }
}

static void ft_psq_object_retain(ft_psq_object* object)
{
    if (object != NULL) {
        ft_psq_ref_retain(&object->refs);
    }
}

static void ft_psq_object_release(
    const ft_psq_policy_rep* policy,
    ft_psq_object* object)
{
    const ft_psq_type_policy* type = NULL;
    if (object == NULL || !ft_psq_ref_release(&object->refs)) {
        return;
    }
    type = ft_psq_type_for(policy, object->kind);
    if (type->destroy != NULL) {
        type->destroy(object->bytes, type->context);
    }
    ft_psq_deallocate(policy, object->bytes);
    ft_psq_deallocate(policy, object);
}

static ft_status ft_psq_object_create(
    const ft_psq_policy_rep* policy,
    ft_psq_object_kind kind,
    const void* source,
    ft_psq_object** result)
{
    const ft_psq_type_policy* type = ft_psq_type_for(policy, kind);
    ft_psq_object* object = NULL;
    ft_status status = FT_STATUS_OK;
    if (source == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    object = (ft_psq_object*)ft_psq_allocate(policy, sizeof(*object));
    if (object == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    object->bytes = ft_psq_allocate(policy, type->size);
    if (object->bytes == NULL) {
        ft_psq_deallocate(policy, object);
        return FT_STATUS_NO_MEMORY;
    }
    if (type->copy == NULL) {
        (void)memcpy(object->bytes, source, type->size);
    } else {
        status = type->copy(object->bytes, source, type->context);
        if (status != FT_STATUS_OK) {
            ft_psq_deallocate(policy, object->bytes);
            ft_psq_deallocate(policy, object);
            return status;
        }
    }
    ft_psq_ref_init(&object->refs);
    object->kind = kind;
    *result = object;
    return FT_STATUS_OK;
}

static void ft_psq_entry_retain(ft_psq_entry_rep* entry)
{
    if (entry != NULL) {
        ft_psq_ref_retain(&entry->refs);
    }
}

static void ft_psq_entry_release(
    const ft_psq_policy_rep* policy,
    ft_psq_entry_rep* entry)
{
    if (entry == NULL || !ft_psq_ref_release(&entry->refs)) {
        return;
    }
    ft_psq_object_release(policy, entry->value);
    ft_psq_object_release(policy, entry->priority);
    ft_psq_object_release(policy, entry->key);
    ft_psq_deallocate(policy, entry);
}

static ft_status ft_psq_entry_from_objects(
    const ft_psq_policy_rep* policy,
    ft_psq_object* key,
    ft_psq_object* priority,
    ft_psq_object* value,
    ft_psq_entry_rep** result)
{
    ft_psq_entry_rep* entry = NULL;
    entry = (ft_psq_entry_rep*)ft_psq_allocate(policy, sizeof(*entry));
    if (entry == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_psq_ref_init(&entry->refs);
    entry->key = key;
    entry->priority = priority;
    entry->value = value;
    ft_psq_object_retain(key);
    ft_psq_object_retain(priority);
    ft_psq_object_retain(value);
    *result = entry;
    return FT_STATUS_OK;
}

static ft_status ft_psq_entry_create(
    const ft_psq_policy_rep* policy,
    const void* key,
    const void* priority,
    const void* value,
    ft_psq_entry_rep** result)
{
    ft_psq_object* stored_key = NULL;
    ft_psq_object* stored_priority = NULL;
    ft_psq_object* stored_value = NULL;
    ft_status status = ft_psq_object_create(
        policy, FT_PSQ_OBJECT_KEY, key, &stored_key);
    if (status == FT_STATUS_OK) {
        status = ft_psq_object_create(
            policy, FT_PSQ_OBJECT_PRIORITY, priority, &stored_priority);
    }
    if (status == FT_STATUS_OK) {
        status = ft_psq_object_create(
            policy, FT_PSQ_OBJECT_VALUE, value, &stored_value);
    }
    if (status == FT_STATUS_OK) {
        status = ft_psq_entry_from_objects(
            policy, stored_key, stored_priority, stored_value, result);
    }
    ft_psq_object_release(policy, stored_value);
    ft_psq_object_release(policy, stored_priority);
    ft_psq_object_release(policy, stored_key);
    return status;
}

static ft_status ft_psq_entry_replace(
    const ft_psq_policy_rep* policy,
    ft_psq_entry_rep* existing,
    const void* priority,
    const void* value,
    ft_psq_entry_rep** result)
{
    ft_psq_object* stored_priority = NULL;
    ft_psq_object* stored_value = NULL;
    ft_status status = ft_psq_object_create(
        policy, FT_PSQ_OBJECT_PRIORITY, priority, &stored_priority);
    if (status == FT_STATUS_OK) {
        status = ft_psq_object_create(
            policy, FT_PSQ_OBJECT_VALUE, value, &stored_value);
    }
    if (status == FT_STATUS_OK) {
        status = ft_psq_entry_from_objects(
            policy, existing->key, stored_priority, stored_value, result);
    }
    ft_psq_object_release(policy, stored_value);
    ft_psq_object_release(policy, stored_priority);
    return status;
}

static ft_priority_search_entry_ref ft_psq_entry_ref_of(
    const ft_psq_entry_rep* entry)
{
    ft_priority_search_entry_ref result;
    result.key = entry->key->bytes;
    result.priority = entry->priority->bytes;
    result.value = entry->value->bytes;
    return result;
}

static void ft_psq_node_retain(ft_psq_node* node)
{
    if (node != NULL) {
        ft_psq_ref_retain(&node->refs);
    }
}

static void ft_psq_node_release(
    const ft_psq_policy_rep* policy,
    ft_psq_node* node)
{
    ft_psq_node* work = NULL;
    if (node == NULL || !ft_psq_ref_release(&node->refs)) {
        return;
    }
    node->release_next = NULL;
    work = node;
    while (work != NULL) {
        ft_psq_node* current = work;
        ft_psq_node* left = current->left;
        ft_psq_node* right = current->right;
        work = current->release_next;
        ft_psq_entry_release(policy, current->entry);
        ft_psq_deallocate(policy, current);
        if (left != NULL && ft_psq_ref_release(&left->refs)) {
            left->release_next = work;
            work = left;
        }
        if (right != NULL && ft_psq_ref_release(&right->refs)) {
            right->release_next = work;
            work = right;
        }
    }
}

static size_t ft_psq_node_height(const ft_psq_node* node)
{
    return node == NULL ? 0 : node->height;
}

static size_t ft_psq_node_count(const ft_psq_node* node)
{
    return node == NULL ? 0 : node->count;
}

static ft_status ft_psq_key_compare(
    const ft_psq_policy_rep* policy,
    const void* left,
    const void* right,
    int* comparison)
{
    return policy->config.key_compare(
        left, right, comparison, policy->config.key.context);
}

static ft_status ft_psq_priority_compare(
    const ft_psq_policy_rep* policy,
    const void* left,
    const void* right,
    int* comparison)
{
    return policy->config.priority_compare(
        left, right, comparison, policy->config.priority.context);
}

static ft_status ft_psq_priority_equals(
    const ft_psq_policy_rep* policy,
    const void* left,
    const void* right,
    bool* equal)
{
    if (left == right) {
        *equal = true;
        return FT_STATUS_OK;
    }
    return policy->config.priority.equals(
        left, right, equal, policy->config.priority.context);
}

static ft_status ft_psq_value_equals(
    const ft_psq_policy_rep* policy,
    const void* left,
    const void* right,
    bool* equal)
{
    if (left == right) {
        *equal = true;
        return FT_STATUS_OK;
    }
    return policy->config.value.equals(
        left, right, equal, policy->config.value.context);
}

static ft_status ft_psq_entry_before(
    const ft_psq_policy_rep* policy,
    const ft_psq_entry_rep* left,
    const ft_psq_entry_rep* right,
    bool* before)
{
    int comparison = 0;
    ft_status status = ft_psq_priority_compare(
        policy,
        left->priority->bytes,
        right->priority->bytes,
        &comparison);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (comparison == 0) {
        status = ft_psq_key_compare(
            policy, left->key->bytes, right->key->bytes, &comparison);
    }
    if (status == FT_STATUS_OK) {
        *before = comparison < 0;
    }
    return status;
}

static ft_status ft_psq_select_winner(
    const ft_psq_policy_rep* policy,
    ft_psq_entry_rep* entry,
    ft_psq_node* left,
    ft_psq_node* right,
    ft_psq_entry_rep** winner)
{
    ft_psq_entry_rep* selected = entry;
    bool before = false;
    ft_status status = FT_STATUS_OK;
    if (left != NULL) {
        status = ft_psq_entry_before(policy, left->winner, selected, &before);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (before) {
            selected = left->winner;
        }
    }
    if (right != NULL) {
        status = ft_psq_entry_before(policy, right->winner, selected, &before);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (before) {
            selected = right->winner;
        }
    }
    *winner = selected;
    return FT_STATUS_OK;
}

static ft_status ft_psq_node_create(
    const ft_psq_policy_rep* policy,
    ft_psq_entry_rep* entry,
    ft_psq_node* left,
    ft_psq_node* right,
    ft_psq_node** result)
{
    ft_psq_entry_rep* winner = NULL;
    ft_psq_node* node = NULL;
    size_t children = 0;
    size_t count = 0;
    size_t maximum_height = ft_psq_node_height(left) > ft_psq_node_height(right)
        ? ft_psq_node_height(left)
        : ft_psq_node_height(right);
    ft_status status = FT_STATUS_OK;
    if (entry == NULL || result == NULL || maximum_height == SIZE_MAX ||
        ft_psq_add_overflows(ft_psq_node_count(left), ft_psq_node_count(right), &children) ||
        ft_psq_add_overflows(children, 1, &count)) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_psq_select_winner(policy, entry, left, right, &winner);
    if (status != FT_STATUS_OK) {
        return status;
    }
    node = (ft_psq_node*)ft_psq_allocate(policy, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_psq_ref_init(&node->refs);
    node->release_next = NULL;
    node->entry = entry;
    node->winner = winner;
    node->left = left;
    node->right = right;
    node->count = count;
    node->height = maximum_height + 1;
    ft_psq_entry_retain(entry);
    ft_psq_node_retain(left);
    ft_psq_node_retain(right);
    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_psq_balance_create(
    const ft_psq_policy_rep* policy,
    ft_psq_entry_rep* entry,
    ft_psq_node* left,
    ft_psq_node* right,
    ft_psq_node** result)
{
    const size_t left_height = ft_psq_node_height(left);
    const size_t right_height = ft_psq_node_height(right);
    ft_psq_node* first = NULL;
    ft_psq_node* second = NULL;
    ft_psq_node* produced = NULL;
    ft_status status = FT_STATUS_OK;
    if (left_height > right_height + 1) {
        ft_psq_node* pivot = left;
        if (ft_psq_node_height(pivot->left) >= ft_psq_node_height(pivot->right)) {
            status = ft_psq_node_create(
                policy, entry, pivot->right, right, &first);
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    policy, pivot->entry, pivot->left, first, &produced);
            }
        } else {
            ft_psq_node* middle = pivot->right;
            status = ft_psq_node_create(
                policy, pivot->entry, pivot->left, middle->left, &first);
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    policy, entry, middle->right, right, &second);
            }
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    policy, middle->entry, first, second, &produced);
            }
        }
    } else if (right_height > left_height + 1) {
        ft_psq_node* pivot = right;
        if (ft_psq_node_height(pivot->right) >= ft_psq_node_height(pivot->left)) {
            status = ft_psq_node_create(
                policy, entry, left, pivot->left, &first);
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    policy, pivot->entry, first, pivot->right, &produced);
            }
        } else {
            ft_psq_node* middle = pivot->left;
            status = ft_psq_node_create(
                policy, entry, left, middle->left, &first);
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    policy, pivot->entry, middle->right, pivot->right, &second);
            }
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    policy, middle->entry, first, second, &produced);
            }
        }
    } else {
        status = ft_psq_node_create(policy, entry, left, right, &produced);
    }
    ft_psq_node_release(policy, second);
    ft_psq_node_release(policy, first);
    if (status == FT_STATUS_OK) {
        *result = produced;
    } else {
        ft_psq_node_release(policy, produced);
    }
    return status;
}

static ft_status ft_psq_allocate_path(
    const ft_psq_policy_rep* policy,
    size_t capacity,
    ft_psq_path_frame** path)
{
    size_t bytes = 0;
    if (capacity == 0) {
        *path = NULL;
        return FT_STATUS_OK;
    }
    if (ft_psq_multiply_overflows(capacity, sizeof(**path), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *path = (ft_psq_path_frame*)ft_psq_allocate(policy, bytes);
    return *path == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

static ft_status ft_psq_unwind_path(
    const ft_psq_policy_rep* policy,
    ft_psq_path_frame* path,
    size_t path_count,
    ft_psq_node* initial,
    ft_psq_node** result)
{
    ft_psq_node* current = initial;
    ft_psq_node_retain(current);
    while (path_count != 0) {
        ft_psq_path_frame frame = path[--path_count];
        ft_psq_node* parent = NULL;
        ft_status status = frame.went_left
            ? ft_psq_balance_create(
                policy, frame.node->entry, current, frame.node->right, &parent)
            : ft_psq_balance_create(
                policy, frame.node->entry, frame.node->left, current, &parent);
        ft_psq_node_release(policy, current);
        if (status != FT_STATUS_OK) {
            return status;
        }
        current = parent;
    }
    *result = current;
    return FT_STATUS_OK;
}

static ft_status ft_psq_set_node(
    const ft_priority_search_queue* queue,
    const void* key,
    const void* priority,
    const void* value,
    bool overwrite,
    bool* added,
    bool* changed,
    ft_psq_node** result)
{
    ft_psq_path_frame* path = NULL;
    size_t path_count = 0;
    size_t path_capacity = ft_psq_node_height(queue->root);
    ft_psq_node* cursor = queue->root;
    ft_psq_node* current = NULL;
    ft_psq_entry_rep* replacement = NULL;
    bool local_added = false;
    bool local_changed = false;
    ft_status status = ft_psq_allocate_path(queue->policy, path_capacity, &path);
    if (status != FT_STATUS_OK) {
        return status;
    }
    while (cursor != NULL) {
        int comparison = 0;
        status = ft_psq_key_compare(
            queue->policy, key, cursor->entry->key->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (comparison == 0) {
            if (!overwrite) {
                ft_psq_node_retain(queue->root);
                current = queue->root;
                local_added = false;
                local_changed = false;
                goto success;
            }
            {
                int priority_comparison = 0;
                bool priority_equal = false;
                bool value_equal = false;
                status = ft_psq_priority_compare(
                    queue->policy,
                    priority,
                    cursor->entry->priority->bytes,
                    &priority_comparison);
                if (status == FT_STATUS_OK && priority_comparison == 0) {
                    status = ft_psq_priority_equals(
                        queue->policy,
                        priority,
                        cursor->entry->priority->bytes,
                        &priority_equal);
                }
                if (status == FT_STATUS_OK && priority_comparison == 0 && priority_equal) {
                    status = ft_psq_value_equals(
                        queue->policy,
                        value,
                        cursor->entry->value->bytes,
                        &value_equal);
                }
                if (status != FT_STATUS_OK) {
                    goto cleanup;
                }
                if (priority_comparison == 0 && priority_equal && value_equal) {
                    ft_psq_node_retain(queue->root);
                    current = queue->root;
                    local_added = false;
                    local_changed = false;
                    goto success;
                }
            }
            status = ft_psq_entry_replace(
                queue->policy, cursor->entry, priority, value, &replacement);
            if (status == FT_STATUS_OK) {
                status = ft_psq_node_create(
                    queue->policy,
                    replacement,
                    cursor->left,
                    cursor->right,
                    &current);
            }
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            local_added = false;
            local_changed = true;
            goto unwind;
        }
        if (path_count == path_capacity) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            goto cleanup;
        }
        path[path_count].node = cursor;
        path[path_count].went_left = comparison < 0;
        ++path_count;
        cursor = comparison < 0 ? cursor->left : cursor->right;
    }
    status = ft_psq_entry_create(
        queue->policy, key, priority, value, &replacement);
    if (status == FT_STATUS_OK) {
        status = ft_psq_node_create(
            queue->policy, replacement, NULL, NULL, &current);
    }
    if (status != FT_STATUS_OK) {
        goto cleanup;
    }
    local_added = true;
    local_changed = true;

unwind:
    {
        ft_psq_node* root = NULL;
        status = ft_psq_unwind_path(
            queue->policy, path, path_count, current, &root);
        ft_psq_node_release(queue->policy, current);
        current = status == FT_STATUS_OK ? root : NULL;
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }

success:
    *added = local_added;
    *changed = local_changed;
    *result = current;
    current = NULL;

cleanup:
    ft_psq_entry_release(queue->policy, replacement);
    ft_psq_node_release(queue->policy, current);
    ft_psq_deallocate(queue->policy, path);
    return status;
}

static ft_status ft_psq_remove_min_node(
    const ft_psq_policy_rep* policy,
    ft_psq_node* source,
    ft_psq_entry_rep** minimum,
    ft_psq_node** result)
{
    ft_psq_path_frame* path = NULL;
    size_t path_count = 0;
    ft_psq_node* cursor = source;
    ft_psq_node* current = NULL;
    ft_status status = ft_psq_allocate_path(policy, source->height, &path);
    if (status != FT_STATUS_OK) {
        return status;
    }
    while (cursor->left != NULL) {
        path[path_count++] = (ft_psq_path_frame){cursor, true};
        cursor = cursor->left;
    }
    current = cursor->right;
    status = ft_psq_unwind_path(policy, path, path_count, current, result);
    if (status == FT_STATUS_OK) {
        *minimum = cursor->entry;
    }
    ft_psq_deallocate(policy, path);
    return status;
}

static ft_status ft_psq_remove_node(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* removed,
    ft_psq_entry_rep** removed_entry,
    ft_psq_node** result)
{
    ft_psq_path_frame* path = NULL;
    size_t path_count = 0;
    const size_t path_capacity = ft_psq_node_height(queue->root);
    ft_psq_node* cursor = queue->root;
    ft_psq_node* current = NULL;
    ft_psq_entry_rep* local_removed = NULL;
    ft_status status = ft_psq_allocate_path(queue->policy, path_capacity, &path);
    if (status != FT_STATUS_OK) {
        return status;
    }
    while (cursor != NULL) {
        int comparison = 0;
        status = ft_psq_key_compare(
            queue->policy, key, cursor->entry->key->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (comparison == 0) {
            break;
        }
        if (path_count == path_capacity) {
            status = FT_STATUS_INCONSISTENT_POLICY;
            goto cleanup;
        }
        path[path_count++] = (ft_psq_path_frame){cursor, comparison < 0};
        cursor = comparison < 0 ? cursor->left : cursor->right;
    }
    if (cursor == NULL) {
        ft_psq_node_retain(queue->root);
        *removed = false;
        *removed_entry = NULL;
        *result = queue->root;
        goto cleanup;
    }
    local_removed = cursor->entry;
    if (cursor->left == NULL || cursor->right == NULL) {
        current = cursor->left == NULL ? cursor->right : cursor->left;
        ft_psq_node_retain(current);
    } else {
        ft_psq_node* new_right = NULL;
        ft_psq_entry_rep* successor = NULL;
        status = ft_psq_remove_min_node(
            queue->policy, cursor->right, &successor, &new_right);
        if (status == FT_STATUS_OK) {
            status = ft_psq_balance_create(
                queue->policy, successor, cursor->left, new_right, &current);
        }
        ft_psq_node_release(queue->policy, new_right);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    {
        ft_psq_node* root = NULL;
        status = ft_psq_unwind_path(
            queue->policy, path, path_count, current, &root);
        ft_psq_node_release(queue->policy, current);
        current = status == FT_STATUS_OK ? root : NULL;
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
    }
    *removed = true;
    *removed_entry = local_removed;
    *result = current;
    current = NULL;

cleanup:
    ft_psq_node_release(queue->policy, current);
    ft_psq_deallocate(queue->policy, path);
    return status;
}

static bool ft_psq_queue_valid(const ft_priority_search_queue* queue)
{
    return queue != NULL && queue->policy != NULL;
}

static ft_status ft_psq_queue_adopt(
    ft_psq_policy_rep* policy,
    ft_psq_node* root,
    ft_priority_search_queue* result)
{
    if (policy == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_psq_policy_retain(policy);
    result->policy = policy;
    result->root = root;
    return FT_STATUS_OK;
}

static void ft_psq_publish_queue(
    const ft_priority_search_queue* source,
    ft_priority_search_queue* result,
    ft_priority_search_queue produced)
{
    if (source == result) {
        ft_priority_search_queue old = *result;
        *result = produced;
        ft_priority_search_queue_dispose(&old);
    } else {
        *result = produced;
    }
}

static ft_status ft_psq_owned_entry_from_borrowed(
    ft_psq_policy_rep* policy,
    ft_psq_entry_rep* rep,
    ft_priority_search_entry* result)
{
    if (policy == NULL || rep == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_psq_policy_retain(policy);
    ft_psq_entry_retain(rep);
    result->policy = policy;
    result->rep = rep;
    return FT_STATUS_OK;
}

void ft_psq_policy_config_init(
    ft_psq_policy_config* config,
    size_t key_size,
    const void* key_type_identity,
    ft_psq_compare_fn key_compare,
    size_t priority_size,
    const void* priority_type_identity,
    ft_psq_compare_fn priority_compare,
    ft_psq_equals_fn priority_equals,
    size_t value_size,
    const void* value_type_identity,
    ft_psq_equals_fn value_equals)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->key.size = key_size;
    config->key.type_identity = key_type_identity;
    config->priority.size = priority_size;
    config->priority.type_identity = priority_type_identity;
    config->priority.equals = priority_equals;
    config->value.size = value_size;
    config->value.type_identity = value_type_identity;
    config->value.equals = value_equals;
    config->key_compare = key_compare;
    config->priority_compare = priority_compare;
    config->allocator.allocate = ft_psq_default_allocate;
    config->allocator.deallocate = ft_psq_default_deallocate;
}

ft_status ft_psq_policy_create(
    ft_psq_policy* policy,
    const ft_psq_policy_config* config)
{
    ft_psq_policy_rep* rep = NULL;
    if (policy == NULL || !ft_psq_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_psq_policy_rep*)ft_psq_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_psq_ref_init(&rep->refs);
    rep->config = *config;
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_psq_policy_copy(
    const ft_psq_policy* source,
    ft_psq_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_psq_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_psq_policy_move(ft_psq_policy* destination, ft_psq_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    destination->rep = source->rep;
    source->rep = NULL;
}

void ft_psq_policy_dispose(ft_psq_policy* policy)
{
    if (policy != NULL) {
        ft_psq_policy_release(policy->rep);
        policy->rep = NULL;
    }
}

bool ft_psq_policy_same(const ft_psq_policy* left, const ft_psq_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL &&
        left->rep == right->rep;
}

ft_status ft_priority_search_entry_copy(
    const ft_priority_search_entry* source,
    ft_priority_search_entry* destination)
{
    if (source == NULL || source->policy == NULL || source->rep == NULL ||
        destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_psq_policy_retain(source->policy);
    ft_psq_entry_retain(source->rep);
    destination->policy = source->policy;
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_priority_search_entry_move(
    ft_priority_search_entry* destination,
    ft_priority_search_entry* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_priority_search_entry_dispose(ft_priority_search_entry* entry)
{
    if (entry == NULL) {
        return;
    }
    if (entry->policy != NULL) {
        ft_psq_entry_release(entry->policy, entry->rep);
        ft_psq_policy_release(entry->policy);
    }
    (void)memset(entry, 0, sizeof(*entry));
}

ft_status ft_priority_search_entry_get_ref(
    const ft_priority_search_entry* entry,
    ft_priority_search_entry_ref* entry_ref)
{
    if (entry == NULL || entry_ref == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (entry->policy == NULL || entry->rep == NULL) {
        return FT_STATUS_NOT_FOUND;
    }
    *entry_ref = ft_psq_entry_ref_of(entry->rep);
    return FT_STATUS_OK;
}

ft_status ft_priority_search_queue_init(
    ft_priority_search_queue* queue,
    const ft_psq_policy* policy)
{
    if (queue == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_psq_queue_adopt(policy->rep, NULL, queue);
}

ft_status ft_priority_search_queue_copy(
    const ft_priority_search_queue* source,
    ft_priority_search_queue* destination)
{
    if (!ft_psq_queue_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_psq_policy_retain(source->policy);
    ft_psq_node_retain(source->root);
    destination->policy = source->policy;
    destination->root = source->root;
    return FT_STATUS_OK;
}

void ft_priority_search_queue_move(
    ft_priority_search_queue* destination,
    ft_priority_search_queue* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_priority_search_queue_dispose(ft_priority_search_queue* queue)
{
    if (queue == NULL) {
        return;
    }
    if (queue->policy != NULL) {
        ft_psq_node_release(queue->policy, queue->root);
        ft_psq_policy_release(queue->policy);
    }
    (void)memset(queue, 0, sizeof(*queue));
}

bool ft_priority_search_queue_empty(const ft_priority_search_queue* queue)
{
    return ft_psq_queue_valid(queue) && queue->root == NULL;
}

size_t ft_priority_search_queue_size(const ft_priority_search_queue* queue)
{
    return ft_psq_queue_valid(queue) ? ft_psq_node_count(queue->root) : 0;
}

size_t ft_priority_search_queue_height(const ft_priority_search_queue* queue)
{
    return ft_psq_queue_valid(queue) ? ft_psq_node_height(queue->root) : 0;
}

static ft_status ft_psq_find_node(
    const ft_priority_search_queue* queue,
    const void* key,
    ft_psq_node** result)
{
    ft_psq_node* cursor = queue->root;
    while (cursor != NULL) {
        int comparison = 0;
        ft_status status = ft_psq_key_compare(
            queue->policy, key, cursor->entry->key->bytes, &comparison);
        if (status != FT_STATUS_OK) {
            return status;
        }
        if (comparison == 0) {
            *result = cursor;
            return FT_STATUS_OK;
        }
        cursor = comparison < 0 ? cursor->left : cursor->right;
    }
    *result = NULL;
    return FT_STATUS_OK;
}

ft_status ft_priority_search_queue_contains_key(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* found)
{
    ft_psq_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || found == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_find_node(queue, key, &node);
    if (status == FT_STATUS_OK) {
        *found = node != NULL;
    }
    return status;
}

ft_status ft_priority_search_queue_try_get_entry_ref(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* found,
    ft_priority_search_entry_ref* entry)
{
    ft_psq_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || found == NULL || entry == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_find_node(queue, key, &node);
    if (status == FT_STATUS_OK) {
        *found = node != NULL;
        if (node != NULL) {
            *entry = ft_psq_entry_ref_of(node->entry);
        } else {
            (void)memset(entry, 0, sizeof(*entry));
        }
    }
    return status;
}

static ft_status ft_psq_publish_set_result(
    const ft_priority_search_queue* queue,
    ft_psq_node* root,
    ft_priority_search_queue* result)
{
    ft_priority_search_queue produced = {0};
    ft_status status = ft_psq_queue_adopt(queue->policy, root, &produced);
    if (status == FT_STATUS_OK) {
        ft_psq_publish_queue(queue, result, produced);
    }
    return status;
}

ft_status ft_priority_search_queue_set(
    const ft_priority_search_queue* queue,
    const void* key,
    const void* priority,
    const void* value,
    ft_priority_search_queue* result)
{
    ft_psq_node* root = NULL;
    bool added = false;
    bool changed = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || priority == NULL ||
        value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_set_node(
        queue, key, priority, value, true, &added, &changed, &root);
    (void)added;
    (void)changed;
    if (status == FT_STATUS_OK) {
        status = ft_psq_publish_set_result(queue, root, result);
        root = NULL;
    }
    if (status != FT_STATUS_OK) {
        ft_psq_node_release(queue->policy, root);
    }
    return status;
}

ft_status ft_priority_search_queue_try_add(
    const ft_priority_search_queue* queue,
    const void* key,
    const void* priority,
    const void* value,
    bool* added,
    ft_priority_search_queue* result)
{
    ft_psq_node* root = NULL;
    bool local_added = false;
    bool changed = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || priority == NULL ||
        value == NULL || added == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_set_node(
        queue, key, priority, value, false, &local_added, &changed, &root);
    (void)changed;
    if (status == FT_STATUS_OK) {
        status = ft_psq_publish_set_result(queue, root, result);
        root = NULL;
    }
    if (status == FT_STATUS_OK) {
        *added = local_added;
    } else {
        ft_psq_node_release(queue->policy, root);
    }
    return status;
}

static ft_status ft_psq_publish_remove(
    const ft_priority_search_queue* queue,
    ft_psq_node* root,
    ft_psq_entry_rep* removed_rep,
    bool removed,
    bool* removed_output,
    ft_priority_search_entry* entry_output,
    ft_priority_search_queue* result)
{
    ft_priority_search_queue produced = {0};
    ft_priority_search_entry entry = {0};
    ft_status status = ft_psq_queue_adopt(queue->policy, root, &produced);
    if (status == FT_STATUS_OK && removed) {
        status = ft_psq_owned_entry_from_borrowed(
            queue->policy, removed_rep, &entry);
    }
    if (status != FT_STATUS_OK) {
        ft_priority_search_entry_dispose(&entry);
        ft_priority_search_queue_dispose(&produced);
        return status;
    }
    ft_psq_publish_queue(queue, result, produced);
    *entry_output = entry;
    *removed_output = removed;
    return FT_STATUS_OK;
}

ft_status ft_priority_search_queue_remove(
    const ft_priority_search_queue* queue,
    const void* key,
    ft_priority_search_queue* result)
{
    ft_psq_node* root = NULL;
    ft_psq_entry_rep* removed_entry = NULL;
    bool removed = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_remove_node(queue, key, &removed, &removed_entry, &root);
    if (status == FT_STATUS_OK) {
        status = ft_psq_publish_set_result(queue, root, result);
        root = NULL;
    }
    if (status != FT_STATUS_OK) {
        ft_psq_node_release(queue->policy, root);
    }
    return status;
}

ft_status ft_priority_search_queue_try_remove(
    const ft_priority_search_queue* queue,
    const void* key,
    bool* removed,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result)
{
    ft_psq_node* root = NULL;
    ft_psq_entry_rep* removed_rep = NULL;
    bool local_removed = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || removed == NULL ||
        entry == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_remove_node(
        queue, key, &local_removed, &removed_rep, &root);
    if (status == FT_STATUS_OK) {
        status = ft_psq_publish_remove(
            queue,
            root,
            removed_rep,
            local_removed,
            removed,
            entry,
            result);
        root = NULL;
    }
    if (status != FT_STATUS_OK) {
        ft_psq_node_release(queue->policy, root);
    }
    return status;
}

ft_status ft_priority_search_queue_try_get_minimum(
    const ft_priority_search_queue* queue,
    bool* found,
    ft_priority_search_entry* entry)
{
    ft_priority_search_entry produced = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || found == NULL || entry == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->root != NULL) {
        status = ft_psq_owned_entry_from_borrowed(
            queue->policy, queue->root->winner, &produced);
    }
    if (status == FT_STATUS_OK) {
        *entry = produced;
        *found = queue->root != NULL;
    }
    return status;
}

ft_status ft_priority_search_queue_delete_minimum(
    const ft_priority_search_queue* queue,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result)
{
    bool removed = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || entry == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->root == NULL) {
        return FT_STATUS_EMPTY;
    }
    status = ft_priority_search_queue_try_remove(
        queue,
        queue->root->winner->key->bytes,
        &removed,
        entry,
        result);
    return status == FT_STATUS_OK && !removed
        ? FT_STATUS_INCONSISTENT_POLICY
        : status;
}

ft_status ft_priority_search_queue_try_delete_minimum(
    const ft_priority_search_queue* queue,
    bool* removed,
    ft_priority_search_entry* entry,
    ft_priority_search_queue* result)
{
    if (!ft_psq_queue_valid(queue) || removed == NULL || entry == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->root == NULL) {
        ft_priority_search_queue produced = {0};
        ft_status status = ft_priority_search_queue_copy(queue, &produced);
        if (status == FT_STATUS_OK) {
            ft_psq_publish_queue(queue, result, produced);
            (void)memset(entry, 0, sizeof(*entry));
            *removed = false;
        }
        return status;
    }
    return ft_priority_search_queue_try_remove(
        queue,
        queue->root->winner->key->bytes,
        removed,
        entry,
        result);
}

ft_status ft_priority_search_queue_from_array(
    ft_priority_search_queue* queue,
    const ft_psq_policy* policy,
    const ft_priority_search_input* entries,
    size_t count)
{
    ft_priority_search_queue working = {0};
    ft_status status = FT_STATUS_OK;
    if (queue == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && entries == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_priority_search_queue_init(&working, policy);
    for (size_t index = 0; status == FT_STATUS_OK && index != count; ++index) {
        if (entries[index].key == NULL || entries[index].priority == NULL ||
            entries[index].value == NULL) {
            status = FT_STATUS_INVALID_ARGUMENT;
            break;
        }
        status = ft_priority_search_queue_set(
            &working,
            entries[index].key,
            entries[index].priority,
            entries[index].value,
            &working);
    }
    if (status == FT_STATUS_OK) {
        *queue = working;
    } else {
        ft_priority_search_queue_dispose(&working);
    }
    return status;
}

static ft_status ft_psq_allocate_array(
    const ft_psq_policy_rep* policy,
    size_t count,
    size_t element_size,
    void** result)
{
    size_t bytes = 0;
    if (count == 0) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    if (ft_psq_multiply_overflows(count, element_size, &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    *result = ft_psq_allocate(policy, bytes);
    return *result == NULL ? FT_STATUS_NO_MEMORY : FT_STATUS_OK;
}

ft_status ft_priority_search_queue_visit(
    const ft_priority_search_queue* queue,
    ft_priority_search_visit_fn visitor,
    void* context)
{
    ft_psq_node** stack = NULL;
    ft_psq_node* cursor = NULL;
    size_t pending = 0;
    const size_t capacity = ft_psq_node_height(queue == NULL ? NULL : queue->root);
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_allocate_array(
        queue->policy, capacity, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    cursor = queue->root;
    while (cursor != NULL || pending != 0) {
        while (cursor != NULL) {
            if (pending == capacity) {
                status = FT_STATUS_INCONSISTENT_POLICY;
                goto cleanup;
            }
            stack[pending++] = cursor;
            cursor = cursor->left;
        }
        cursor = stack[--pending];
        status = visitor(ft_psq_entry_ref_of(cursor->entry), context);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        cursor = cursor->right;
    }

cleanup:
    ft_psq_deallocate(queue->policy, stack);
    return status;
}

ft_status ft_priority_search_queue_visit_at_most(
    const ft_priority_search_queue* queue,
    const void* minimum_key,
    const void* maximum_key,
    const void* maximum_priority,
    ft_priority_search_visit_fn visitor,
    void* context)
{
    ft_psq_query_frame* stack = NULL;
    size_t capacity = 0;
    size_t pending = 0;
    int range_comparison = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || minimum_key == NULL || maximum_key == NULL ||
        maximum_priority == NULL || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_key_compare(
        queue->policy, minimum_key, maximum_key, &range_comparison);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (range_comparison > 0) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->root == NULL) {
        return FT_STATUS_OK;
    }
    if (ft_psq_multiply_overflows(queue->root->height, 2, &capacity) ||
        ft_psq_add_overflows(capacity, 1, &capacity)) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_psq_allocate_array(
        queue->policy, capacity, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    stack[pending++] = (ft_psq_query_frame){queue->root, false};
    while (pending != 0) {
        ft_psq_query_frame frame = stack[--pending];
        ft_psq_node* node = frame.node;
        if (frame.emit) {
            status = visitor(ft_psq_entry_ref_of(node->entry), context);
            if (status != FT_STATUS_OK) {
                break;
            }
            continue;
        }
        {
            int winner_comparison = 0;
            status = ft_psq_priority_compare(
                queue->policy,
                node->winner->priority->bytes,
                maximum_priority,
                &winner_comparison);
            if (status != FT_STATUS_OK) {
                break;
            }
            if (winner_comparison > 0) {
                continue;
            }
        }
        {
            int lower = 0;
            int upper = 0;
            status = ft_psq_key_compare(
                queue->policy, node->entry->key->bytes, minimum_key, &lower);
            if (status == FT_STATUS_OK) {
                status = ft_psq_key_compare(
                    queue->policy, node->entry->key->bytes, maximum_key, &upper);
            }
            if (status != FT_STATUS_OK) {
                break;
            }
            if (upper < 0 && node->right != NULL) {
                if (pending == capacity) {
                    status = FT_STATUS_INCONSISTENT_POLICY;
                    break;
                }
                stack[pending++] = (ft_psq_query_frame){node->right, false};
            }
            if (lower >= 0 && upper <= 0) {
                int entry_comparison = 0;
                status = ft_psq_priority_compare(
                    queue->policy,
                    node->entry->priority->bytes,
                    maximum_priority,
                    &entry_comparison);
                if (status != FT_STATUS_OK) {
                    break;
                }
                if (entry_comparison <= 0) {
                    if (pending == capacity) {
                        status = FT_STATUS_INCONSISTENT_POLICY;
                        break;
                    }
                    stack[pending++] = (ft_psq_query_frame){node, true};
                }
            }
            if (lower > 0 && node->left != NULL) {
                if (pending == capacity) {
                    status = FT_STATUS_INCONSISTENT_POLICY;
                    break;
                }
                stack[pending++] = (ft_psq_query_frame){node->left, false};
            }
        }
    }
    ft_psq_deallocate(queue->policy, stack);
    return status;
}

const void* ft_priority_search_queue_root_identity(
    const ft_priority_search_queue* queue)
{
    return ft_psq_queue_valid(queue) ? queue->root : NULL;
}

ft_status ft_priority_search_queue_node_identity(
    const ft_priority_search_queue* queue,
    const void* key,
    const void** identity)
{
    ft_psq_node* node = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || key == NULL || identity == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_psq_find_node(queue, key, &node);
    if (status == FT_STATUS_OK) {
        *identity = node;
    }
    return status;
}

ft_status ft_priority_search_queue_shared_node_count(
    const ft_priority_search_queue* left,
    const ft_priority_search_queue* right,
    size_t* shared_count)
{
    const ft_priority_search_queue* source = NULL;
    const ft_priority_search_queue* target = NULL;
    ft_psq_node** stack = NULL;
    ft_psq_node* cursor = NULL;
    size_t pending = 0;
    size_t count = 0;
    size_t capacity = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(left) || !ft_psq_queue_valid(right) ||
        shared_count == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    source = ft_psq_node_count(left->root) <= ft_psq_node_count(right->root)
        ? left
        : right;
    target = source == left ? right : left;
    capacity = ft_psq_node_height(source->root);
    status = ft_psq_allocate_array(
        source->policy, capacity, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    cursor = source->root;
    while (cursor != NULL || pending != 0) {
        while (cursor != NULL) {
            if (pending == capacity) {
                status = FT_STATUS_INCONSISTENT_POLICY;
                goto cleanup;
            }
            stack[pending++] = cursor;
            cursor = cursor->left;
        }
        cursor = stack[--pending];
        {
            ft_psq_node* target_node = NULL;
            status = ft_psq_find_node(
                target, cursor->entry->key->bytes, &target_node);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (target_node == cursor) {
                ++count;
            }
        }
        cursor = cursor->right;
    }

cleanup:
    ft_psq_deallocate(source->policy, stack);
    if (status == FT_STATUS_OK) {
        *shared_count = count;
    }
    return status;
}

static unsigned ft_psq_absolute_balance(
    size_t left_height,
    size_t right_height)
{
    size_t difference = left_height > right_height
        ? left_height - right_height
        : right_height - left_height;
    return difference > UINT_MAX ? UINT_MAX : (unsigned)difference;
}

ft_status ft_priority_search_queue_validate(
    const ft_priority_search_queue* queue,
    bool* valid,
    ft_priority_search_queue_statistics* statistics)
{
    ft_psq_validation_frame* stack = NULL;
    ft_priority_search_queue_statistics local = {0, 0, 0};
    size_t capacity = 0;
    size_t pending = 0;
    bool local_valid = true;
    ft_status status = FT_STATUS_OK;
    if (!ft_psq_queue_valid(queue) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (queue->root == NULL) {
        *valid = true;
        *statistics = local;
        return FT_STATUS_OK;
    }
    if (ft_psq_add_overflows(queue->root->height, 1, &capacity)) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_psq_allocate_array(
        queue->policy, capacity, sizeof(*stack), (void**)&stack);
    if (status != FT_STATUS_OK) {
        return status;
    }
    stack[pending++] = (ft_psq_validation_frame){queue->root, NULL, NULL, false, false};
    while (pending != 0 && local_valid) {
        ft_psq_validation_frame frame = stack[--pending];
        ft_psq_node* node = frame.node;
        size_t children = 0;
        size_t expected_count = 0;
        size_t expected_height = 0;
        const size_t left_height = ft_psq_node_height(node->left);
        const size_t right_height = ft_psq_node_height(node->right);
        const unsigned balance = ft_psq_absolute_balance(left_height, right_height);
        ft_psq_entry_rep* expected_winner = NULL;
        int comparison = 0;
        if (local.count == queue->root->count) {
            local_valid = false;
            break;
        }
        if (frame.has_lower) {
            status = ft_psq_key_compare(
                queue->policy, node->entry->key->bytes, frame.lower_key, &comparison);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (comparison <= 0) {
                local_valid = false;
                break;
            }
        }
        if (frame.has_upper) {
            status = ft_psq_key_compare(
                queue->policy, node->entry->key->bytes, frame.upper_key, &comparison);
            if (status != FT_STATUS_OK) {
                goto cleanup;
            }
            if (comparison >= 0) {
                local_valid = false;
                break;
            }
        }
        if (ft_psq_add_overflows(
                ft_psq_node_count(node->left),
                ft_psq_node_count(node->right),
                &children) ||
            ft_psq_add_overflows(children, 1, &expected_count) ||
            (left_height > right_height ? left_height : right_height) == SIZE_MAX) {
            local_valid = false;
            break;
        }
        expected_height = 1 + (left_height > right_height ? left_height : right_height);
        if (node->count != expected_count || node->height != expected_height || balance > 1) {
            local_valid = false;
            break;
        }
        status = ft_psq_select_winner(
            queue->policy, node->entry, node->left, node->right, &expected_winner);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (node->winner != expected_winner) {
            local_valid = false;
            break;
        }
        ++local.count;
        if (balance > local.maximum_absolute_balance_factor) {
            local.maximum_absolute_balance_factor = balance;
        }
        if (node->right != NULL) {
            if (pending == capacity) {
                local_valid = false;
                break;
            }
            stack[pending++] = (ft_psq_validation_frame){
                node->right,
                node->entry->key->bytes,
                frame.upper_key,
                true,
                frame.has_upper};
        }
        if (node->left != NULL) {
            if (pending == capacity) {
                local_valid = false;
                break;
            }
            stack[pending++] = (ft_psq_validation_frame){
                node->left,
                frame.lower_key,
                node->entry->key->bytes,
                frame.has_lower,
                true};
        }
    }
    if (local_valid && local.count != queue->root->count) {
        local_valid = false;
    }
    local.height = queue->root->height;

cleanup:
    ft_psq_deallocate(queue->policy, stack);
    if (status == FT_STATUS_OK) {
        *valid = local_valid;
        *statistics = local;
    }
    return status;
}
