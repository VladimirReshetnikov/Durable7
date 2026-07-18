#include <tools/data_structures/finger_tree/range_update_sequence.h>

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
typedef volatile LONG64 ft_ru_ref_count;

static void ft_ru_ref_init(ft_ru_ref_count* value)
{
    *value = 1;
}

static void ft_ru_ref_retain(ft_ru_ref_count* value)
{
    (void)InterlockedIncrement64(value);
}

static bool ft_ru_ref_release(ft_ru_ref_count* value)
{
    return InterlockedDecrement64(value) == 0;
}
#else
typedef atomic_size_t ft_ru_ref_count;

static void ft_ru_ref_init(ft_ru_ref_count* value)
{
    atomic_init(value, 1);
}

static void ft_ru_ref_retain(ft_ru_ref_count* value)
{
    (void)atomic_fetch_add_explicit(value, 1, memory_order_relaxed);
}

static bool ft_ru_ref_release(ft_ru_ref_count* value)
{
    return atomic_fetch_sub_explicit(value, 1, memory_order_acq_rel) == 1;
}
#endif

typedef enum ft_ru_object_kind {
    FT_RU_OBJECT_ELEMENT,
    FT_RU_OBJECT_MEASURE,
    FT_RU_OBJECT_TAG
} ft_ru_object_kind;

typedef struct ft_ru_object {
    ft_ru_ref_count refs;
    ft_ru_object_kind kind;
    void* bytes;
} ft_ru_object;

struct ft_range_update_node {
    ft_ru_ref_count refs;
    ft_ru_object* element;
    struct ft_range_update_node* left;
    struct ft_range_update_node* right;
    size_t height;
    size_t count;
    ft_ru_object* measure;
    ft_ru_object* pending;
};

struct ft_range_update_policy_rep {
    ft_ru_ref_count refs;
    ft_range_update_policy_config config;
    ft_ru_object* empty_measure;
    ft_ru_object* identity_tag;
};

typedef struct ft_ru_removed_minimum {
    ft_ru_object* minimum;
    ft_range_update_node* remainder;
} ft_ru_removed_minimum;

typedef struct ft_ru_node_pair {
    ft_range_update_node* left;
    ft_range_update_node* right;
} ft_ru_node_pair;

typedef struct ft_ru_validation_entry {
    const ft_range_update_node* node;
    ft_ru_object* measure;
    ft_range_update_sequence_statistics statistics;
} ft_ru_validation_entry;

typedef struct ft_ru_validation_cache {
    ft_ru_validation_entry* entries;
    size_t count;
    size_t capacity;
} ft_ru_validation_cache;

typedef struct ft_ru_pointer_set {
    const ft_range_update_node** values;
    size_t count;
    size_t capacity;
} ft_ru_pointer_set;

static void* ft_ru_default_allocate(size_t size, void* context)
{
    (void)context;
    return malloc(size);
}

static void ft_ru_default_deallocate(void* allocation, void* context)
{
    (void)context;
    free(allocation);
}

static bool ft_ru_add_overflows(size_t left, size_t right, size_t* result)
{
    if (right > SIZE_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool ft_ru_multiply_overflows(size_t left, size_t right, size_t* result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool ft_ru_type_valid(const ft_range_update_type_policy* type)
{
    return type->size != 0 && type->type_identity != NULL &&
        (type->destroy == NULL || type->copy != NULL);
}

static bool ft_ru_config_valid(const ft_range_update_policy_config* config)
{
    return config != NULL &&
        ft_ru_type_valid(&config->element) &&
        ft_ru_type_valid(&config->measure) &&
        ft_ru_type_valid(&config->tag) &&
        config->empty_measure != NULL && config->identity_tag != NULL &&
        config->measure_element != NULL && config->combine != NULL &&
        config->measure_equals != NULL && config->is_identity != NULL &&
        config->compose != NULL && config->apply_element != NULL &&
        config->apply_measure != NULL &&
        config->allocator.allocate != NULL &&
        config->allocator.deallocate != NULL;
}

static const ft_range_update_type_policy* ft_ru_type_for(
    const ft_range_update_policy_rep* policy,
    ft_ru_object_kind kind)
{
    if (kind == FT_RU_OBJECT_ELEMENT) {
        return &policy->config.element;
    }
    if (kind == FT_RU_OBJECT_MEASURE) {
        return &policy->config.measure;
    }
    return &policy->config.tag;
}

static void* ft_ru_allocate_config(
    const ft_range_update_policy_config* config,
    size_t size)
{
    return config->allocator.allocate(size, config->allocator.context);
}

static void ft_ru_deallocate_config(
    const ft_range_update_policy_config* config,
    void* allocation)
{
    if (allocation != NULL) {
        config->allocator.deallocate(allocation, config->allocator.context);
    }
}

static void* ft_ru_allocate(const ft_range_update_policy_rep* policy, size_t size)
{
    return ft_ru_allocate_config(&policy->config, size);
}

static void ft_ru_deallocate(
    const ft_range_update_policy_rep* policy,
    void* allocation)
{
    ft_ru_deallocate_config(&policy->config, allocation);
}

static void ft_ru_policy_retain(ft_range_update_policy_rep* policy)
{
    if (policy != NULL) {
        ft_ru_ref_retain(&policy->refs);
    }
}

static void ft_ru_object_retain(ft_ru_object* object)
{
    if (object != NULL) {
        ft_ru_ref_retain(&object->refs);
    }
}

static void ft_ru_object_release(
    const ft_range_update_policy_rep* policy,
    ft_ru_object* object)
{
    const ft_range_update_type_policy* type = NULL;
    if (object == NULL || !ft_ru_ref_release(&object->refs)) {
        return;
    }
    type = ft_ru_type_for(policy, object->kind);
    if (type->destroy != NULL) {
        type->destroy(object->bytes, type->context);
    }
    ft_ru_deallocate(policy, object->bytes);
    ft_ru_deallocate(policy, object);
}

static ft_status ft_ru_object_allocate(
    const ft_range_update_policy_rep* policy,
    ft_ru_object_kind kind,
    ft_ru_object** result)
{
    const ft_range_update_type_policy* type = ft_ru_type_for(policy, kind);
    ft_ru_object* object = (ft_ru_object*)ft_ru_allocate(policy, sizeof(*object));
    if (object == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    object->bytes = ft_ru_allocate(policy, type->size);
    if (object->bytes == NULL) {
        ft_ru_deallocate(policy, object);
        return FT_STATUS_NO_MEMORY;
    }
    object->kind = kind;
    *result = object;
    return FT_STATUS_OK;
}

static void ft_ru_object_publish(ft_ru_object* object)
{
    ft_ru_ref_init(&object->refs);
}

static void ft_ru_object_abandon(
    const ft_range_update_policy_rep* policy,
    ft_ru_object* object)
{
    if (object != NULL) {
        ft_ru_deallocate(policy, object->bytes);
        ft_ru_deallocate(policy, object);
    }
}

static ft_status ft_ru_object_copy_from_bytes(
    const ft_range_update_policy_rep* policy,
    ft_ru_object_kind kind,
    const void* source,
    ft_ru_object** result)
{
    const ft_range_update_type_policy* type = NULL;
    ft_ru_object* object = NULL;
    ft_status status = FT_STATUS_OK;
    if (source == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ru_object_allocate(policy, kind, &object);
    if (status != FT_STATUS_OK) {
        return status;
    }
    type = ft_ru_type_for(policy, kind);
    if (type->copy == NULL) {
        (void)memcpy(object->bytes, source, type->size);
    } else {
        status = type->copy(object->bytes, source, type->context);
    }
    if (status != FT_STATUS_OK) {
        ft_ru_object_abandon(policy, object);
        return status;
    }
    ft_ru_object_publish(object);
    *result = object;
    return FT_STATUS_OK;
}

static ft_status ft_ru_object_measure_element(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* element,
    ft_ru_object** result)
{
    ft_ru_object* object = NULL;
    ft_status status = ft_ru_object_allocate(policy, FT_RU_OBJECT_MEASURE, &object);
    if (status == FT_STATUS_OK) {
        status = policy->config.measure_element(
            object->bytes, element->bytes, policy->config.algebra_context);
    }
    if (status != FT_STATUS_OK) {
        ft_ru_object_abandon(policy, object);
        return status;
    }
    ft_ru_object_publish(object);
    *result = object;
    return FT_STATUS_OK;
}

static ft_status ft_ru_object_combine(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* left,
    const ft_ru_object* right,
    ft_ru_object** result)
{
    ft_ru_object* object = NULL;
    ft_status status = ft_ru_object_allocate(policy, FT_RU_OBJECT_MEASURE, &object);
    if (status == FT_STATUS_OK) {
        status = policy->config.combine(
            object->bytes,
            left->bytes,
            right->bytes,
            policy->config.algebra_context);
    }
    if (status != FT_STATUS_OK) {
        ft_ru_object_abandon(policy, object);
        return status;
    }
    ft_ru_object_publish(object);
    *result = object;
    return FT_STATUS_OK;
}

static ft_status ft_ru_object_compose(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* newer,
    const ft_ru_object* older,
    ft_ru_object** result)
{
    ft_ru_object* object = NULL;
    ft_status status = ft_ru_object_allocate(policy, FT_RU_OBJECT_TAG, &object);
    if (status == FT_STATUS_OK) {
        status = policy->config.compose(
            object->bytes,
            newer->bytes,
            older->bytes,
            policy->config.algebra_context);
    }
    if (status != FT_STATUS_OK) {
        ft_ru_object_abandon(policy, object);
        return status;
    }
    ft_ru_object_publish(object);
    *result = object;
    return FT_STATUS_OK;
}

static ft_status ft_ru_object_apply_element(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* tag,
    const ft_ru_object* element,
    ft_ru_object** result)
{
    ft_ru_object* object = NULL;
    ft_status status = ft_ru_object_allocate(policy, FT_RU_OBJECT_ELEMENT, &object);
    if (status == FT_STATUS_OK) {
        status = policy->config.apply_element(
            object->bytes,
            tag->bytes,
            element->bytes,
            policy->config.algebra_context);
    }
    if (status != FT_STATUS_OK) {
        ft_ru_object_abandon(policy, object);
        return status;
    }
    ft_ru_object_publish(object);
    *result = object;
    return FT_STATUS_OK;
}

static ft_status ft_ru_object_apply_measure(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* tag,
    const ft_ru_object* measure,
    size_t count,
    ft_ru_object** result)
{
    ft_ru_object* object = NULL;
    ft_status status = ft_ru_object_allocate(policy, FT_RU_OBJECT_MEASURE, &object);
    if (status == FT_STATUS_OK) {
        status = policy->config.apply_measure(
            object->bytes,
            tag->bytes,
            measure->bytes,
            count,
            policy->config.algebra_context);
    }
    if (status != FT_STATUS_OK) {
        ft_ru_object_abandon(policy, object);
        return status;
    }
    ft_ru_object_publish(object);
    *result = object;
    return FT_STATUS_OK;
}

static ft_status ft_ru_is_identity(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* tag,
    bool* result)
{
    return policy->config.is_identity(
        tag->bytes, result, policy->config.algebra_context);
}

static void ft_ru_policy_release(ft_range_update_policy_rep* policy)
{
    if (policy == NULL || !ft_ru_ref_release(&policy->refs)) {
        return;
    }
    ft_ru_object_release(policy, policy->identity_tag);
    ft_ru_object_release(policy, policy->empty_measure);
    ft_ru_deallocate_config(&policy->config, policy);
}

static void ft_ru_node_retain(ft_range_update_node* node)
{
    if (node != NULL) {
        ft_ru_ref_retain(&node->refs);
    }
}

static void ft_ru_node_release(
    const ft_range_update_policy_rep* policy,
    ft_range_update_node* node)
{
    if (node == NULL || !ft_ru_ref_release(&node->refs)) {
        return;
    }
    ft_ru_node_release(policy, node->right);
    ft_ru_node_release(policy, node->left);
    ft_ru_object_release(policy, node->pending);
    ft_ru_object_release(policy, node->measure);
    ft_ru_object_release(policy, node->element);
    ft_ru_deallocate(policy, node);
}

static size_t ft_ru_height(const ft_range_update_node* node)
{
    return node == NULL ? 0 : node->height;
}

static size_t ft_ru_count(const ft_range_update_node* node)
{
    return node == NULL ? 0 : node->count;
}

static ft_status ft_ru_raw_node_create(
    const ft_range_update_policy_rep* policy,
    ft_ru_object* element,
    ft_range_update_node* left,
    ft_range_update_node* right,
    size_t height,
    size_t count,
    ft_ru_object* measure,
    ft_ru_object* pending,
    ft_range_update_node** result)
{
    ft_range_update_node* node = NULL;
    if (element == NULL || measure == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    node = (ft_range_update_node*)ft_ru_allocate(policy, sizeof(*node));
    if (node == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    ft_ru_ref_init(&node->refs);
    node->element = element;
    node->left = left;
    node->right = right;
    node->height = height;
    node->count = count;
    node->measure = measure;
    node->pending = pending;
    ft_ru_object_retain(element);
    ft_ru_node_retain(left);
    ft_ru_node_retain(right);
    ft_ru_object_retain(measure);
    ft_ru_object_retain(pending);
    *result = node;
    return FT_STATUS_OK;
}

static ft_status ft_ru_node_create(
    const ft_range_update_policy_rep* policy,
    ft_ru_object* element,
    ft_range_update_node* left,
    ft_range_update_node* right,
    ft_range_update_node** result)
{
    size_t partial_count = 0;
    size_t count = 0;
    size_t height = 0;
    ft_ru_object* aggregate = NULL;
    ft_ru_object* combined = NULL;
    ft_status status = FT_STATUS_OK;
    if (ft_ru_add_overflows(ft_ru_count(left), 1, &partial_count) ||
        ft_ru_add_overflows(partial_count, ft_ru_count(right), &count) ||
        ft_ru_add_overflows(
            ft_ru_height(left) > ft_ru_height(right)
                ? ft_ru_height(left)
                : ft_ru_height(right),
            1,
            &height)) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_ru_object_measure_element(policy, element, &aggregate);
    if (status == FT_STATUS_OK && left != NULL) {
        status = ft_ru_object_combine(policy, left->measure, aggregate, &combined);
        if (status == FT_STATUS_OK) {
            ft_ru_object_release(policy, aggregate);
            aggregate = combined;
            combined = NULL;
        }
    }
    if (status == FT_STATUS_OK && right != NULL) {
        status = ft_ru_object_combine(policy, aggregate, right->measure, &combined);
        if (status == FT_STATUS_OK) {
            ft_ru_object_release(policy, aggregate);
            aggregate = combined;
            combined = NULL;
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_raw_node_create(
            policy, element, left, right, height, count, aggregate, NULL, result);
    }
    ft_ru_object_release(policy, combined);
    ft_ru_object_release(policy, aggregate);
    return status;
}

static ft_status ft_ru_apply_subtree(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    ft_ru_object* newer,
    ft_range_update_node** result)
{
    ft_ru_object* element = NULL;
    ft_ru_object* measure = NULL;
    ft_ru_object* pending = NULL;
    bool identity = false;
    ft_status status = ft_ru_object_apply_element(
        policy, newer, current->element, &element);
    if (status == FT_STATUS_OK) {
        status = ft_ru_object_apply_measure(
            policy, newer, current->measure, current->count, &measure);
    }
    if (status == FT_STATUS_OK && current->pending == NULL) {
        pending = newer;
        ft_ru_object_retain(pending);
    } else if (status == FT_STATUS_OK) {
        status = ft_ru_object_compose(policy, newer, current->pending, &pending);
        if (status == FT_STATUS_OK) {
            status = ft_ru_is_identity(policy, pending, &identity);
        }
        if (status == FT_STATUS_OK && identity) {
            ft_ru_object_release(policy, pending);
            pending = NULL;
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_raw_node_create(
            policy,
            element,
            current->left,
            current->right,
            current->height,
            current->count,
            measure,
            pending,
            result);
    }
    ft_ru_object_release(policy, pending);
    ft_ru_object_release(policy, measure);
    ft_ru_object_release(policy, element);
    return status;
}

static ft_status ft_ru_push(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    ft_range_update_node** result)
{
    ft_range_update_node* left = NULL;
    ft_range_update_node* right = NULL;
    ft_status status = FT_STATUS_OK;
    if (current->pending == NULL) {
        ft_ru_node_retain((ft_range_update_node*)current);
        *result = (ft_range_update_node*)current;
        return FT_STATUS_OK;
    }
    if (current->left != NULL) {
        status = ft_ru_apply_subtree(policy, current->left, current->pending, &left);
    }
    if (status == FT_STATUS_OK && current->right != NULL) {
        status = ft_ru_apply_subtree(policy, current->right, current->pending, &right);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_raw_node_create(
            policy,
            current->element,
            left,
            right,
            current->height,
            current->count,
            current->measure,
            NULL,
            result);
    }
    ft_ru_node_release(policy, right);
    ft_ru_node_release(policy, left);
    return status;
}

static ft_status ft_ru_rotate_left(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    ft_range_update_node** result)
{
    ft_range_update_node* current = NULL;
    ft_range_update_node* pivot = NULL;
    ft_range_update_node* lower = NULL;
    ft_status status = ft_ru_push(policy, original, &current);
    if (status == FT_STATUS_OK && current->right == NULL) {
        status = FT_STATUS_INCONSISTENT_POLICY;
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_push(policy, current->right, &pivot);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_node_create(
            policy, current->element, current->left, pivot->left, &lower);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_node_create(
            policy, pivot->element, lower, pivot->right, result);
    }
    ft_ru_node_release(policy, lower);
    ft_ru_node_release(policy, pivot);
    ft_ru_node_release(policy, current);
    return status;
}

static ft_status ft_ru_rotate_right(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    ft_range_update_node** result)
{
    ft_range_update_node* current = NULL;
    ft_range_update_node* pivot = NULL;
    ft_range_update_node* lower = NULL;
    ft_status status = ft_ru_push(policy, original, &current);
    if (status == FT_STATUS_OK && current->left == NULL) {
        status = FT_STATUS_INCONSISTENT_POLICY;
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_push(policy, current->left, &pivot);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_node_create(
            policy, current->element, pivot->right, current->right, &lower);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_node_create(
            policy, pivot->element, pivot->left, lower, result);
    }
    ft_ru_node_release(policy, lower);
    ft_ru_node_release(policy, pivot);
    ft_ru_node_release(policy, current);
    return status;
}

static ft_status ft_ru_balance(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    ft_range_update_node** result)
{
    size_t left_height = ft_ru_height(original->left);
    size_t right_height = ft_ru_height(original->right);
    if (left_height > right_height && left_height - right_height > 1) {
        const ft_range_update_node* left = original->left;
        if (ft_ru_height(left->left) < ft_ru_height(left->right)) {
            ft_range_update_node* current = NULL;
            ft_range_update_node* rotated = NULL;
            ft_range_update_node* rebuilt = NULL;
            ft_status status = ft_ru_push(policy, original, &current);
            if (status == FT_STATUS_OK) {
                status = ft_ru_rotate_left(policy, current->left, &rotated);
            }
            if (status == FT_STATUS_OK) {
                status = ft_ru_node_create(
                    policy, current->element, rotated, current->right, &rebuilt);
            }
            if (status == FT_STATUS_OK) {
                status = ft_ru_rotate_right(policy, rebuilt, result);
            }
            ft_ru_node_release(policy, rebuilt);
            ft_ru_node_release(policy, rotated);
            ft_ru_node_release(policy, current);
            return status;
        }
        return ft_ru_rotate_right(policy, original, result);
    }
    if (right_height > left_height && right_height - left_height > 1) {
        const ft_range_update_node* right = original->right;
        if (ft_ru_height(right->right) < ft_ru_height(right->left)) {
            ft_range_update_node* current = NULL;
            ft_range_update_node* rotated = NULL;
            ft_range_update_node* rebuilt = NULL;
            ft_status status = ft_ru_push(policy, original, &current);
            if (status == FT_STATUS_OK) {
                status = ft_ru_rotate_right(policy, current->right, &rotated);
            }
            if (status == FT_STATUS_OK) {
                status = ft_ru_node_create(
                    policy, current->element, current->left, rotated, &rebuilt);
            }
            if (status == FT_STATUS_OK) {
                status = ft_ru_rotate_left(policy, rebuilt, result);
            }
            ft_ru_node_release(policy, rebuilt);
            ft_ru_node_release(policy, rotated);
            ft_ru_node_release(policy, current);
            return status;
        }
        return ft_ru_rotate_left(policy, original, result);
    }
    ft_ru_node_retain((ft_range_update_node*)original);
    *result = (ft_range_update_node*)original;
    return FT_STATUS_OK;
}

static ft_status ft_ru_join(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* left,
    ft_ru_object* pivot,
    const ft_range_update_node* right,
    ft_range_update_node** result)
{
    size_t left_height = ft_ru_height(left);
    size_t right_height = ft_ru_height(right);
    size_t difference = left_height > right_height
        ? left_height - right_height
        : right_height - left_height;
    if (difference <= 1) {
        return ft_ru_node_create(
            policy,
            pivot,
            (ft_range_update_node*)left,
            (ft_range_update_node*)right,
            result);
    }
    if (left_height > right_height) {
        ft_range_update_node* current = NULL;
        ft_range_update_node* boundary = NULL;
        ft_range_update_node* rebuilt = NULL;
        ft_status status = ft_ru_push(policy, left, &current);
        if (status == FT_STATUS_OK) {
            status = ft_ru_join(
                policy, current->right, pivot, right, &boundary);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, current->left, boundary, &rebuilt);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_balance(policy, rebuilt, result);
        }
        ft_ru_node_release(policy, rebuilt);
        ft_ru_node_release(policy, boundary);
        ft_ru_node_release(policy, current);
        return status;
    }
    {
        ft_range_update_node* current = NULL;
        ft_range_update_node* leading = NULL;
        ft_range_update_node* rebuilt = NULL;
        ft_status status = ft_ru_push(policy, right, &current);
        if (status == FT_STATUS_OK) {
            status = ft_ru_join(
                policy, left, pivot, current->left, &leading);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, leading, current->right, &rebuilt);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_balance(policy, rebuilt, result);
        }
        ft_ru_node_release(policy, rebuilt);
        ft_ru_node_release(policy, leading);
        ft_ru_node_release(policy, current);
        return status;
    }
}

static ft_status ft_ru_extract_minimum(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    ft_ru_removed_minimum* result)
{
    ft_range_update_node* current = NULL;
    ft_status status = ft_ru_push(policy, original, &current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (current->left == NULL) {
        ft_ru_object_retain(current->element);
        ft_ru_node_retain(current->right);
        result->minimum = current->element;
        result->remainder = current->right;
        ft_ru_node_release(policy, current);
        return FT_STATUS_OK;
    }
    {
        ft_ru_removed_minimum removed = {0};
        ft_range_update_node* rebuilt = NULL;
        ft_range_update_node* balanced = NULL;
        status = ft_ru_extract_minimum(policy, current->left, &removed);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy,
                current->element,
                removed.remainder,
                current->right,
                &rebuilt);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_balance(policy, rebuilt, &balanced);
        }
        if (status == FT_STATUS_OK) {
            result->minimum = removed.minimum;
            result->remainder = balanced;
            removed.minimum = NULL;
            balanced = NULL;
        }
        ft_ru_node_release(policy, balanced);
        ft_ru_node_release(policy, rebuilt);
        ft_ru_node_release(policy, removed.remainder);
        ft_ru_object_release(policy, removed.minimum);
        ft_ru_node_release(policy, current);
        return status;
    }
}

static ft_status ft_ru_concat_nodes(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* left,
    const ft_range_update_node* right,
    ft_range_update_node** result)
{
    ft_ru_removed_minimum removed = {0};
    ft_status status = FT_STATUS_OK;
    if (left == NULL) {
        ft_ru_node_retain((ft_range_update_node*)right);
        *result = (ft_range_update_node*)right;
        return FT_STATUS_OK;
    }
    if (right == NULL) {
        ft_ru_node_retain((ft_range_update_node*)left);
        *result = (ft_range_update_node*)left;
        return FT_STATUS_OK;
    }
    status = ft_ru_extract_minimum(policy, right, &removed);
    if (status == FT_STATUS_OK) {
        status = ft_ru_join(
            policy, left, removed.minimum, removed.remainder, result);
    }
    ft_ru_node_release(policy, removed.remainder);
    ft_ru_object_release(policy, removed.minimum);
    return status;
}

static ft_status ft_ru_split_nodes(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* root,
    size_t index,
    ft_ru_node_pair* result)
{
    ft_range_update_node* current = NULL;
    ft_status status = FT_STATUS_OK;
    if (root == NULL) {
        result->left = NULL;
        result->right = NULL;
        return FT_STATUS_OK;
    }
    if (index == 0) {
        ft_ru_node_retain((ft_range_update_node*)root);
        result->left = NULL;
        result->right = (ft_range_update_node*)root;
        return FT_STATUS_OK;
    }
    if (index == root->count) {
        ft_ru_node_retain((ft_range_update_node*)root);
        result->left = (ft_range_update_node*)root;
        result->right = NULL;
        return FT_STATUS_OK;
    }
    status = ft_ru_push(policy, root, &current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (index <= ft_ru_count(current->left)) {
        ft_ru_node_pair split = {0};
        ft_range_update_node* right = NULL;
        status = ft_ru_split_nodes(policy, current->left, index, &split);
        if (status == FT_STATUS_OK) {
            status = ft_ru_join(
                policy, split.right, current->element, current->right, &right);
        }
        if (status == FT_STATUS_OK) {
            result->left = split.left;
            result->right = right;
            split.left = NULL;
            right = NULL;
        }
        ft_ru_node_release(policy, right);
        ft_ru_node_release(policy, split.right);
        ft_ru_node_release(policy, split.left);
    } else {
        ft_ru_node_pair split = {0};
        ft_range_update_node* left_result = NULL;
        status = ft_ru_split_nodes(
            policy,
            current->right,
            index - ft_ru_count(current->left) - 1,
            &split);
        if (status == FT_STATUS_OK) {
            status = ft_ru_join(
                policy, current->left, current->element, split.left, &left_result);
        }
        if (status == FT_STATUS_OK) {
            result->left = left_result;
            result->right = split.right;
            left_result = NULL;
            split.right = NULL;
        }
        ft_ru_node_release(policy, left_result);
        ft_ru_node_release(policy, split.right);
        ft_ru_node_release(policy, split.left);
    }
    ft_ru_node_release(policy, current);
    return status;
}

static ft_status ft_ru_build_balanced(
    const ft_range_update_policy_rep* policy,
    const void* const* elements,
    size_t start,
    size_t count,
    ft_range_update_node** result)
{
    size_t left_count = 0;
    size_t middle = 0;
    ft_range_update_node* left = NULL;
    ft_range_update_node* right = NULL;
    ft_ru_object* element = NULL;
    ft_status status = FT_STATUS_OK;
    if (count == 0) {
        *result = NULL;
        return FT_STATUS_OK;
    }
    left_count = count / 2;
    middle = start + left_count;
    status = ft_ru_build_balanced(policy, elements, start, left_count, &left);
    if (status == FT_STATUS_OK) {
        status = ft_ru_build_balanced(
            policy,
            elements,
            middle + 1,
            count - left_count - 1,
            &right);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_object_copy_from_bytes(
            policy, FT_RU_OBJECT_ELEMENT, elements[middle], &element);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_node_create(policy, element, left, right, result);
    }
    ft_ru_object_release(policy, element);
    ft_ru_node_release(policy, right);
    ft_ru_node_release(policy, left);
    return status;
}

static ft_status ft_ru_insert_node(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    size_t index,
    ft_ru_object* element,
    ft_range_update_node** result)
{
    ft_range_update_node* current = NULL;
    ft_range_update_node* child = NULL;
    ft_range_update_node* rebuilt = NULL;
    ft_status status = FT_STATUS_OK;
    if (original == NULL) {
        return ft_ru_node_create(policy, element, NULL, NULL, result);
    }
    status = ft_ru_push(policy, original, &current);
    if (status == FT_STATUS_OK && index <= ft_ru_count(current->left)) {
        status = ft_ru_insert_node(
            policy, current->left, index, element, &child);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, child, current->right, &rebuilt);
        }
    } else if (status == FT_STATUS_OK) {
        status = ft_ru_insert_node(
            policy,
            current->right,
            index - ft_ru_count(current->left) - 1,
            element,
            &child);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, current->left, child, &rebuilt);
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_balance(policy, rebuilt, result);
    }
    ft_ru_node_release(policy, rebuilt);
    ft_ru_node_release(policy, child);
    ft_ru_node_release(policy, current);
    return status;
}

static ft_status ft_ru_set_node(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    size_t index,
    ft_ru_object* element,
    ft_range_update_node** result)
{
    ft_range_update_node* current = NULL;
    ft_range_update_node* child = NULL;
    ft_status status = ft_ru_push(policy, original, &current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (index < ft_ru_count(current->left)) {
        status = ft_ru_set_node(policy, current->left, index, element, &child);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, child, current->right, result);
        }
    } else if (index > ft_ru_count(current->left)) {
        status = ft_ru_set_node(
            policy,
            current->right,
            index - ft_ru_count(current->left) - 1,
            element,
            &child);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, current->left, child, result);
        }
    } else {
        status = ft_ru_node_create(
            policy, element, current->left, current->right, result);
    }
    ft_ru_node_release(policy, child);
    ft_ru_node_release(policy, current);
    return status;
}

static ft_status ft_ru_remove_node(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* original,
    size_t index,
    ft_range_update_node** result)
{
    ft_range_update_node* current = NULL;
    ft_range_update_node* child = NULL;
    ft_range_update_node* rebuilt = NULL;
    ft_status status = ft_ru_push(policy, original, &current);
    if (status != FT_STATUS_OK) {
        return status;
    }
    if (index < ft_ru_count(current->left)) {
        status = ft_ru_remove_node(policy, current->left, index, &child);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, child, current->right, &rebuilt);
        }
    } else if (index > ft_ru_count(current->left)) {
        status = ft_ru_remove_node(
            policy,
            current->right,
            index - ft_ru_count(current->left) - 1,
            &child);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy, current->element, current->left, child, &rebuilt);
        }
    } else if (current->left == NULL) {
        ft_ru_node_retain(current->right);
        *result = current->right;
        ft_ru_node_release(policy, current);
        return FT_STATUS_OK;
    } else if (current->right == NULL) {
        ft_ru_node_retain(current->left);
        *result = current->left;
        ft_ru_node_release(policy, current);
        return FT_STATUS_OK;
    } else {
        ft_ru_removed_minimum removed = {0};
        status = ft_ru_extract_minimum(policy, current->right, &removed);
        if (status == FT_STATUS_OK) {
            status = ft_ru_node_create(
                policy,
                removed.minimum,
                current->left,
                removed.remainder,
                &rebuilt);
        }
        ft_ru_node_release(policy, removed.remainder);
        ft_ru_object_release(policy, removed.minimum);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_balance(policy, rebuilt, result);
    }
    ft_ru_node_release(policy, rebuilt);
    ft_ru_node_release(policy, child);
    ft_ru_node_release(policy, current);
    return status;
}

static ft_status ft_ru_child_inheritance(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    ft_ru_object* inherited,
    ft_ru_object** result)
{
    ft_ru_object* composed = NULL;
    bool identity = false;
    ft_status status = FT_STATUS_OK;
    if (current->pending == NULL) {
        ft_ru_object_retain(inherited);
        *result = inherited;
        return FT_STATUS_OK;
    }
    if (inherited == NULL) {
        ft_ru_object_retain(current->pending);
        *result = current->pending;
        return FT_STATUS_OK;
    }
    status = ft_ru_object_compose(
        policy, inherited, current->pending, &composed);
    if (status == FT_STATUS_OK) {
        status = ft_ru_is_identity(policy, composed, &identity);
    }
    if (status == FT_STATUS_OK && identity) {
        ft_ru_object_release(policy, composed);
        composed = NULL;
    }
    if (status == FT_STATUS_OK) {
        *result = composed;
        composed = NULL;
    }
    ft_ru_object_release(policy, composed);
    return status;
}

static ft_status ft_ru_get_element(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* root,
    size_t index,
    ft_ru_object** result)
{
    const ft_range_update_node* current = root;
    ft_ru_object* inherited = NULL;
    ft_status status = FT_STATUS_OK;
    while (current != NULL) {
        size_t left_count = ft_ru_count(current->left);
        if (index == left_count) {
            if (inherited == NULL) {
                ft_ru_object_retain(current->element);
                *result = current->element;
            } else {
                status = ft_ru_object_apply_element(
                    policy, inherited, current->element, result);
            }
            ft_ru_object_release(policy, inherited);
            return status;
        }
        {
            ft_ru_object* next = NULL;
            status = ft_ru_child_inheritance(policy, current, inherited, &next);
            if (status != FT_STATUS_OK) {
                ft_ru_object_release(policy, inherited);
                return status;
            }
            ft_ru_object_release(policy, inherited);
            inherited = next;
        }
        if (index < left_count) {
            current = current->left;
        } else {
            index -= left_count + 1;
            current = current->right;
        }
    }
    ft_ru_object_release(policy, inherited);
    return FT_STATUS_OUT_OF_RANGE;
}

static ft_status ft_ru_add_measure(
    const ft_range_update_policy_rep* policy,
    ft_ru_object** aggregate,
    ft_ru_object* part)
{
    ft_ru_object* combined = NULL;
    ft_status status = FT_STATUS_OK;
    if (*aggregate == NULL) {
        ft_ru_object_retain(part);
        *aggregate = part;
        return FT_STATUS_OK;
    }
    status = ft_ru_object_combine(policy, *aggregate, part, &combined);
    if (status == FT_STATUS_OK) {
        ft_ru_object_release(policy, *aggregate);
        *aggregate = combined;
    }
    return status;
}

static ft_status ft_ru_measure_range_node(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    size_t start,
    size_t count,
    ft_ru_object* inherited,
    ft_ru_object** result)
{
    size_t end = start + count;
    size_t left_count = ft_ru_count(current->left);
    size_t right_start = left_count + 1;
    ft_ru_object* child_tag = NULL;
    ft_ru_object* aggregate = NULL;
    ft_ru_object* part = NULL;
    ft_ru_object* singleton = NULL;
    bool child_computed = false;
    ft_status status = FT_STATUS_OK;
    if (start == 0 && count == current->count) {
        if (inherited == NULL) {
            ft_ru_object_retain(current->measure);
            *result = current->measure;
            return FT_STATUS_OK;
        }
        return ft_ru_object_apply_measure(
            policy, inherited, current->measure, current->count, result);
    }

#define FT_RU_ENSURE_CHILD_TAG() \
    do { \
        if (!child_computed) { \
            status = ft_ru_child_inheritance( \
                policy, current, inherited, &child_tag); \
            child_computed = true; \
        } \
    } while (0)

    if (start < left_count) {
        size_t child_count = count < left_count - start
            ? count
            : left_count - start;
        FT_RU_ENSURE_CHILD_TAG();
        if (status == FT_STATUS_OK) {
            status = ft_ru_measure_range_node(
                policy, current->left, start, child_count, child_tag, &part);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_add_measure(policy, &aggregate, part);
        }
        ft_ru_object_release(policy, part);
        part = NULL;
    }
    if (status == FT_STATUS_OK && start <= left_count && end > left_count) {
        status = ft_ru_object_measure_element(policy, current->element, &singleton);
        if (status == FT_STATUS_OK && inherited != NULL) {
            status = ft_ru_object_apply_measure(
                policy, inherited, singleton, 1, &part);
            if (status == FT_STATUS_OK) {
                ft_ru_object_release(policy, singleton);
                singleton = part;
                part = NULL;
            }
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_add_measure(policy, &aggregate, singleton);
        }
        ft_ru_object_release(policy, singleton);
        singleton = NULL;
    }
    if (status == FT_STATUS_OK && end > right_start) {
        size_t local_start = start > right_start ? start - right_start : 0;
        size_t local_end = end - right_start;
        FT_RU_ENSURE_CHILD_TAG();
        if (status == FT_STATUS_OK) {
            status = ft_ru_measure_range_node(
                policy,
                current->right,
                local_start,
                local_end - local_start,
                child_tag,
                &part);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_add_measure(policy, &aggregate, part);
        }
        ft_ru_object_release(policy, part);
        part = NULL;
    }
#undef FT_RU_ENSURE_CHILD_TAG

    if (status == FT_STATUS_OK) {
        *result = aggregate;
        aggregate = NULL;
    }
    ft_ru_object_release(policy, singleton);
    ft_ru_object_release(policy, part);
    ft_ru_object_release(policy, aggregate);
    ft_ru_object_release(policy, child_tag);
    return status;
}

static ft_status ft_ru_visit_node(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    ft_ru_object* inherited,
    ft_range_update_visit_fn visitor,
    void* context)
{
    ft_ru_object* child_tag = NULL;
    ft_ru_object* logical = NULL;
    ft_status status = FT_STATUS_OK;
    if (current == NULL) {
        return FT_STATUS_OK;
    }
    if (current->left != NULL || current->right != NULL) {
        status = ft_ru_child_inheritance(policy, current, inherited, &child_tag);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_visit_node(
            policy, current->left, child_tag, visitor, context);
    }
    if (status == FT_STATUS_OK) {
        if (inherited == NULL) {
            logical = current->element;
            ft_ru_object_retain(logical);
        } else {
            status = ft_ru_object_apply_element(
                policy, inherited, current->element, &logical);
        }
    }
    if (status == FT_STATUS_OK) {
        status = visitor(logical->bytes, context);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_visit_node(
            policy, current->right, child_tag, visitor, context);
    }
    ft_ru_object_release(policy, logical);
    ft_ru_object_release(policy, child_tag);
    return status;
}

static ft_status ft_ru_pointer_set_reserve(
    const ft_range_update_policy_rep* policy,
    ft_ru_pointer_set* set,
    size_t required)
{
    size_t capacity = set->capacity == 0 ? 16 : set->capacity;
    size_t bytes = 0;
    const ft_range_update_node** values = NULL;
    if (required <= set->capacity) {
        return FT_STATUS_OK;
    }
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    if (ft_ru_multiply_overflows(capacity, sizeof(*values), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    values = (const ft_range_update_node**)ft_ru_allocate(policy, bytes);
    if (values == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    if (set->count != 0) {
        (void)memcpy(values, set->values, set->count * sizeof(*values));
    }
    ft_ru_deallocate(policy, set->values);
    set->values = values;
    set->capacity = capacity;
    return FT_STATUS_OK;
}

static bool ft_ru_pointer_set_contains(
    const ft_ru_pointer_set* set,
    const ft_range_update_node* value)
{
    size_t index = 0;
    for (index = 0; index < set->count; ++index) {
        if (set->values[index] == value) {
            return true;
        }
    }
    return false;
}

static ft_status ft_ru_pointer_set_add(
    const ft_range_update_policy_rep* policy,
    ft_ru_pointer_set* set,
    const ft_range_update_node* value,
    bool* added)
{
    ft_status status = FT_STATUS_OK;
    if (ft_ru_pointer_set_contains(set, value)) {
        *added = false;
        return FT_STATUS_OK;
    }
    if (set->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_ru_pointer_set_reserve(policy, set, set->count + 1);
    if (status == FT_STATUS_OK) {
        set->values[set->count++] = value;
        *added = true;
    }
    return status;
}

static ft_status ft_ru_collect_nodes(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    ft_ru_pointer_set* set)
{
    bool added = false;
    ft_status status = FT_STATUS_OK;
    if (current == NULL) {
        return FT_STATUS_OK;
    }
    status = ft_ru_pointer_set_add(policy, set, current, &added);
    if (status != FT_STATUS_OK || !added) {
        return status;
    }
    status = ft_ru_collect_nodes(policy, current->left, set);
    if (status == FT_STATUS_OK) {
        status = ft_ru_collect_nodes(policy, current->right, set);
    }
    return status;
}

static ft_status ft_ru_count_shared_nodes(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    const ft_ru_pointer_set* candidates,
    ft_ru_pointer_set* visited,
    size_t* count)
{
    bool added = false;
    ft_status status = FT_STATUS_OK;
    if (current == NULL) {
        return FT_STATUS_OK;
    }
    status = ft_ru_pointer_set_add(policy, visited, current, &added);
    if (status != FT_STATUS_OK || !added) {
        return status;
    }
    if (ft_ru_pointer_set_contains(candidates, current)) {
        ++*count;
    }
    status = ft_ru_count_shared_nodes(
        policy, current->left, candidates, visited, count);
    if (status == FT_STATUS_OK) {
        status = ft_ru_count_shared_nodes(
            policy, current->right, candidates, visited, count);
    }
    return status;
}

static ft_ru_validation_entry* ft_ru_validation_find(
    ft_ru_validation_cache* cache,
    const ft_range_update_node* node)
{
    size_t index = 0;
    for (index = 0; index < cache->count; ++index) {
        if (cache->entries[index].node == node) {
            return &cache->entries[index];
        }
    }
    return NULL;
}

static ft_status ft_ru_validation_reserve(
    const ft_range_update_policy_rep* policy,
    ft_ru_validation_cache* cache,
    size_t required)
{
    size_t capacity = cache->capacity == 0 ? 16 : cache->capacity;
    size_t bytes = 0;
    ft_ru_validation_entry* entries = NULL;
    if (required <= cache->capacity) {
        return FT_STATUS_OK;
    }
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    if (ft_ru_multiply_overflows(capacity, sizeof(*entries), &bytes)) {
        return FT_STATUS_OVERFLOW;
    }
    entries = (ft_ru_validation_entry*)ft_ru_allocate(policy, bytes);
    if (entries == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    if (cache->count != 0) {
        (void)memcpy(entries, cache->entries, cache->count * sizeof(*entries));
    }
    ft_ru_deallocate(policy, cache->entries);
    cache->entries = entries;
    cache->capacity = capacity;
    return FT_STATUS_OK;
}

static ft_status ft_ru_validation_store(
    const ft_range_update_policy_rep* policy,
    ft_ru_validation_cache* cache,
    const ft_ru_validation_entry* entry)
{
    ft_status status = FT_STATUS_OK;
    if (cache->count == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_ru_validation_reserve(policy, cache, cache->count + 1);
    if (status == FT_STATUS_OK) {
        cache->entries[cache->count] = *entry;
        ft_ru_object_retain(entry->measure);
        ++cache->count;
    }
    return status;
}

static void ft_ru_validation_dispose(
    const ft_range_update_policy_rep* policy,
    ft_ru_validation_cache* cache)
{
    size_t index = 0;
    for (index = 0; index < cache->count; ++index) {
        ft_ru_object_release(policy, cache->entries[index].measure);
    }
    ft_ru_deallocate(policy, cache->entries);
    (void)memset(cache, 0, sizeof(*cache));
}

static ft_status ft_ru_validate_node(
    const ft_range_update_policy_rep* policy,
    const ft_range_update_node* current,
    ft_ru_validation_cache* cache,
    bool* valid,
    ft_ru_validation_entry* result)
{
    ft_ru_validation_entry* found = ft_ru_validation_find(cache, current);
    ft_ru_validation_entry left = {0};
    ft_ru_validation_entry right = {0};
    ft_ru_object* left_measure = NULL;
    ft_ru_object* right_measure = NULL;
    ft_ru_object* expected = NULL;
    ft_ru_object* combined = NULL;
    size_t partial_count = 0;
    size_t expected_count = 0;
    size_t expected_height = 0;
    size_t balance = 0;
    size_t partial_pending = 0;
    bool identity = false;
    bool equal = false;
    ft_status status = FT_STATUS_OK;
    if (found != NULL) {
        *result = *found;
        ft_ru_object_retain(result->measure);
        return FT_STATUS_OK;
    }
    left.measure = policy->empty_measure;
    ft_ru_object_retain(left.measure);
    right.measure = policy->empty_measure;
    ft_ru_object_retain(right.measure);
    if (current->left != NULL) {
        ft_ru_object_release(policy, left.measure);
        left.measure = NULL;
        status = ft_ru_validate_node(policy, current->left, cache, valid, &left);
    }
    if (status == FT_STATUS_OK && *valid && current->right != NULL) {
        ft_ru_object_release(policy, right.measure);
        right.measure = NULL;
        status = ft_ru_validate_node(policy, current->right, cache, valid, &right);
    }
    if (status != FT_STATUS_OK || !*valid) {
        goto cleanup;
    }
    if (ft_ru_add_overflows(left.statistics.count, 1, &partial_count) ||
        ft_ru_add_overflows(partial_count, right.statistics.count, &expected_count) ||
        ft_ru_add_overflows(
            left.statistics.height > right.statistics.height
                ? left.statistics.height
                : right.statistics.height,
            1,
            &expected_height)) {
        *valid = false;
        goto cleanup;
    }
    balance = left.statistics.height > right.statistics.height
        ? left.statistics.height - right.statistics.height
        : right.statistics.height - left.statistics.height;
    if (current->count != expected_count || current->height != expected_height ||
        balance > 1) {
        *valid = false;
        goto cleanup;
    }
    if (current->pending != NULL) {
        status = ft_ru_is_identity(policy, current->pending, &identity);
        if (status != FT_STATUS_OK) {
            goto cleanup;
        }
        if (identity) {
            *valid = false;
            goto cleanup;
        }
    }
    left_measure = left.measure;
    ft_ru_object_retain(left_measure);
    if (current->pending != NULL && left.statistics.count != 0) {
        ft_ru_object_release(policy, left_measure);
        left_measure = NULL;
        status = ft_ru_object_apply_measure(
            policy,
            current->pending,
            left.measure,
            left.statistics.count,
            &left_measure);
    }
    right_measure = right.measure;
    ft_ru_object_retain(right_measure);
    if (status == FT_STATUS_OK && current->pending != NULL &&
        right.statistics.count != 0) {
        ft_ru_object_release(policy, right_measure);
        right_measure = NULL;
        status = ft_ru_object_apply_measure(
            policy,
            current->pending,
            right.measure,
            right.statistics.count,
            &right_measure);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_object_measure_element(policy, current->element, &expected);
    }
    if (status == FT_STATUS_OK && left.statistics.count != 0) {
        status = ft_ru_object_combine(policy, left_measure, expected, &combined);
        if (status == FT_STATUS_OK) {
            ft_ru_object_release(policy, expected);
            expected = combined;
            combined = NULL;
        }
    }
    if (status == FT_STATUS_OK && right.statistics.count != 0) {
        status = ft_ru_object_combine(policy, expected, right_measure, &combined);
        if (status == FT_STATUS_OK) {
            ft_ru_object_release(policy, expected);
            expected = combined;
            combined = NULL;
        }
    }
    if (status == FT_STATUS_OK) {
        status = policy->config.measure_equals(
            expected->bytes,
            current->measure->bytes,
            &equal,
            policy->config.algebra_context);
    }
    if (status != FT_STATUS_OK || !equal) {
        if (status == FT_STATUS_OK) {
            *valid = false;
        }
        goto cleanup;
    }
    if (ft_ru_add_overflows(
            left.statistics.pending_tag_count,
            current->pending != NULL ? 1 : 0,
            &partial_pending) ||
        ft_ru_add_overflows(
            partial_pending,
            right.statistics.pending_tag_count,
            &partial_pending)) {
        *valid = false;
        goto cleanup;
    }
    (void)memset(&result->statistics, 0, sizeof(result->statistics));
    result->node = current;
    result->measure = current->measure;
    ft_ru_object_retain(result->measure);
    result->statistics.count = expected_count;
    result->statistics.height = expected_height;
    result->statistics.logical_node_count = expected_count;
    result->statistics.pending_tag_count = partial_pending;
    result->statistics.maximum_absolute_balance_factor = balance;
    if (left.statistics.maximum_absolute_balance_factor >
        result->statistics.maximum_absolute_balance_factor) {
        result->statistics.maximum_absolute_balance_factor =
            left.statistics.maximum_absolute_balance_factor;
    }
    if (right.statistics.maximum_absolute_balance_factor >
        result->statistics.maximum_absolute_balance_factor) {
        result->statistics.maximum_absolute_balance_factor =
            right.statistics.maximum_absolute_balance_factor;
    }
    status = ft_ru_validation_store(policy, cache, result);
    if (status != FT_STATUS_OK) {
        ft_ru_object_release(policy, result->measure);
        result->measure = NULL;
    }

cleanup:
    ft_ru_object_release(policy, combined);
    ft_ru_object_release(policy, expected);
    ft_ru_object_release(policy, right_measure);
    ft_ru_object_release(policy, left_measure);
    ft_ru_object_release(policy, right.measure);
    ft_ru_object_release(policy, left.measure);
    return status;
}

static bool ft_ru_sequence_valid(const ft_range_update_sequence* sequence)
{
    return sequence != NULL && sequence->policy != NULL;
}

static ft_status ft_ru_copy_object_to_destination(
    const ft_range_update_policy_rep* policy,
    const ft_ru_object* object,
    void* destination)
{
    const ft_range_update_type_policy* type = NULL;
    if (destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    type = ft_ru_type_for(policy, object->kind);
    if (type->copy == NULL) {
        (void)memcpy(destination, object->bytes, type->size);
        return FT_STATUS_OK;
    }
    return type->copy(destination, object->bytes, type->context);
}

static ft_status ft_ru_sequence_adopt_owned(
    ft_range_update_policy_rep* policy,
    ft_range_update_node* root,
    ft_range_update_sequence* result)
{
    if (policy == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    ft_ru_policy_retain(policy);
    result->policy = policy;
    result->root = root;
    return FT_STATUS_OK;
}

static ft_status ft_ru_sequence_from_borrowed(
    ft_range_update_policy_rep* policy,
    ft_range_update_node* root,
    ft_range_update_sequence* result)
{
    ft_status status = ft_ru_sequence_adopt_owned(policy, root, result);
    if (status == FT_STATUS_OK) {
        ft_ru_node_retain(root);
    }
    return status;
}

static void ft_ru_publish_sequence(
    const ft_range_update_sequence* source,
    ft_range_update_sequence* result,
    ft_range_update_sequence produced)
{
    if (source == result) {
        ft_range_update_sequence old = *result;
        *result = produced;
        ft_range_update_sequence_dispose(&old);
    } else {
        *result = produced;
    }
}

static ft_status ft_ru_publish_owned_root(
    const ft_range_update_sequence* source,
    ft_range_update_node* root,
    ft_range_update_sequence* result)
{
    ft_range_update_sequence produced = {0};
    ft_status status = ft_ru_sequence_adopt_owned(source->policy, root, &produced);
    if (status == FT_STATUS_OK) {
        ft_ru_publish_sequence(source, result, produced);
    }
    return status;
}

static ft_status ft_ru_publish_borrowed_root(
    const ft_range_update_sequence* source,
    ft_range_update_node* root,
    ft_range_update_sequence* result)
{
    ft_range_update_sequence produced = {0};
    ft_status status = ft_ru_sequence_from_borrowed(source->policy, root, &produced);
    if (status == FT_STATUS_OK) {
        ft_ru_publish_sequence(source, result, produced);
    }
    return status;
}

static bool ft_ru_range_out_of_bounds(
    size_t index,
    size_t count,
    size_t available)
{
    return index > available || count > available - index;
}

void ft_range_update_policy_config_init(
    ft_range_update_policy_config* config,
    size_t element_size,
    const void* element_type_identity,
    size_t measure_size,
    const void* measure_type_identity,
    size_t tag_size,
    const void* tag_type_identity,
    const void* empty_measure,
    const void* identity_tag,
    ft_range_update_measure_element_fn measure_element,
    ft_range_update_combine_fn combine,
    ft_range_update_equals_fn measure_equals,
    ft_range_update_is_identity_fn is_identity,
    ft_range_update_compose_fn compose,
    ft_range_update_apply_element_fn apply_element,
    ft_range_update_apply_measure_fn apply_measure)
{
    if (config == NULL) {
        return;
    }
    (void)memset(config, 0, sizeof(*config));
    config->element.size = element_size;
    config->element.type_identity = element_type_identity;
    config->measure.size = measure_size;
    config->measure.type_identity = measure_type_identity;
    config->tag.size = tag_size;
    config->tag.type_identity = tag_type_identity;
    config->empty_measure = empty_measure;
    config->identity_tag = identity_tag;
    config->measure_element = measure_element;
    config->combine = combine;
    config->measure_equals = measure_equals;
    config->is_identity = is_identity;
    config->compose = compose;
    config->apply_element = apply_element;
    config->apply_measure = apply_measure;
    config->allocator.allocate = ft_ru_default_allocate;
    config->allocator.deallocate = ft_ru_default_deallocate;
}

ft_status ft_range_update_policy_create(
    ft_range_update_policy* policy,
    const ft_range_update_policy_config* config)
{
    ft_range_update_policy_rep* rep = NULL;
    bool identity = false;
    ft_status status = FT_STATUS_OK;
    if (policy == NULL || !ft_ru_config_valid(config)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    rep = (ft_range_update_policy_rep*)ft_ru_allocate_config(config, sizeof(*rep));
    if (rep == NULL) {
        return FT_STATUS_NO_MEMORY;
    }
    (void)memset(rep, 0, sizeof(*rep));
    ft_ru_ref_init(&rep->refs);
    rep->config = *config;
    status = ft_ru_object_copy_from_bytes(
        rep, FT_RU_OBJECT_MEASURE, config->empty_measure, &rep->empty_measure);
    if (status == FT_STATUS_OK) {
        status = ft_ru_object_copy_from_bytes(
            rep, FT_RU_OBJECT_TAG, config->identity_tag, &rep->identity_tag);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_is_identity(rep, rep->identity_tag, &identity);
    }
    if (status == FT_STATUS_OK && !identity) {
        status = FT_STATUS_INCONSISTENT_POLICY;
    }
    if (status != FT_STATUS_OK) {
        ft_ru_policy_release(rep);
        return status;
    }
    policy->rep = rep;
    return FT_STATUS_OK;
}

ft_status ft_range_update_policy_copy(
    const ft_range_update_policy* source,
    ft_range_update_policy* destination)
{
    if (source == NULL || source->rep == NULL || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    ft_ru_policy_retain(source->rep);
    destination->rep = source->rep;
    return FT_STATUS_OK;
}

void ft_range_update_policy_move(
    ft_range_update_policy* destination,
    ft_range_update_policy* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_range_update_policy_dispose(ft_range_update_policy* policy)
{
    if (policy == NULL) {
        return;
    }
    ft_ru_policy_release(policy->rep);
    (void)memset(policy, 0, sizeof(*policy));
}

bool ft_range_update_policy_same(
    const ft_range_update_policy* left,
    const ft_range_update_policy* right)
{
    return left != NULL && right != NULL && left->rep != NULL &&
        left->rep == right->rep;
}

ft_status ft_range_update_sequence_init(
    ft_range_update_sequence* sequence,
    const ft_range_update_policy* policy)
{
    if (sequence == NULL || policy == NULL || policy->rep == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_ru_sequence_adopt_owned(policy->rep, NULL, sequence);
}

ft_status ft_range_update_sequence_from_array(
    ft_range_update_sequence* sequence,
    const ft_range_update_policy* policy,
    const void* const* elements,
    size_t count)
{
    ft_range_update_node* root = NULL;
    size_t index = 0;
    ft_status status = FT_STATUS_OK;
    if (sequence == NULL || policy == NULL || policy->rep == NULL ||
        (count != 0 && elements == NULL)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    for (index = 0; index < count; ++index) {
        if (elements[index] == NULL) {
            return FT_STATUS_INVALID_ARGUMENT;
        }
    }
    status = ft_ru_build_balanced(policy->rep, elements, 0, count, &root);
    if (status == FT_STATUS_OK) {
        status = ft_ru_sequence_adopt_owned(policy->rep, root, sequence);
        if (status == FT_STATUS_OK) {
            root = NULL;
        }
    }
    ft_ru_node_release(policy->rep, root);
    return status;
}

ft_status ft_range_update_sequence_copy(
    const ft_range_update_sequence* source,
    ft_range_update_sequence* destination)
{
    if (!ft_ru_sequence_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_ru_sequence_from_borrowed(source->policy, source->root, destination);
}

void ft_range_update_sequence_move(
    ft_range_update_sequence* destination,
    ft_range_update_sequence* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void ft_range_update_sequence_dispose(ft_range_update_sequence* sequence)
{
    if (sequence == NULL) {
        return;
    }
    if (sequence->policy != NULL) {
        ft_ru_node_release(sequence->policy, sequence->root);
        ft_ru_policy_release(sequence->policy);
    }
    (void)memset(sequence, 0, sizeof(*sequence));
}

bool ft_range_update_sequence_empty(const ft_range_update_sequence* sequence)
{
    return ft_ru_sequence_valid(sequence) && sequence->root == NULL;
}

size_t ft_range_update_sequence_size(const ft_range_update_sequence* sequence)
{
    return ft_ru_sequence_valid(sequence) ? ft_ru_count(sequence->root) : 0;
}

size_t ft_range_update_sequence_height(const ft_range_update_sequence* sequence)
{
    return ft_ru_sequence_valid(sequence) ? ft_ru_height(sequence->root) : 0;
}

ft_status ft_range_update_sequence_measure(
    const ft_range_update_sequence* sequence,
    void* destination)
{
    const ft_ru_object* measure = NULL;
    if (!ft_ru_sequence_valid(sequence) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    measure = sequence->root == NULL
        ? sequence->policy->empty_measure
        : sequence->root->measure;
    return ft_ru_copy_object_to_destination(sequence->policy, measure, destination);
}

ft_status ft_range_update_sequence_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    void* destination)
{
    ft_ru_object* element = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_ru_count(sequence->root)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ru_get_element(sequence->policy, sequence->root, index, &element);
    if (status == FT_STATUS_OK) {
        status = ft_ru_copy_object_to_destination(
            sequence->policy, element, destination);
    }
    ft_ru_object_release(sequence->policy, element);
    return status;
}

ft_status ft_range_update_sequence_insert_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    const void* element,
    ft_range_update_sequence* result)
{
    ft_ru_object* stored = NULL;
    ft_range_update_node* root = NULL;
    size_t ignored = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || element == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > ft_ru_count(sequence->root)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (ft_ru_add_overflows(ft_ru_count(sequence->root), 1, &ignored)) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_ru_object_copy_from_bytes(
        sequence->policy, FT_RU_OBJECT_ELEMENT, element, &stored);
    if (status == FT_STATUS_OK) {
        status = ft_ru_insert_node(
            sequence->policy, sequence->root, index, stored, &root);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_publish_owned_root(sequence, root, result);
        if (status == FT_STATUS_OK) {
            root = NULL;
        }
    }
    ft_ru_node_release(sequence->policy, root);
    ft_ru_object_release(sequence->policy, stored);
    return status;
}

ft_status ft_range_update_sequence_prepend(
    const ft_range_update_sequence* sequence,
    const void* element,
    ft_range_update_sequence* result)
{
    return ft_range_update_sequence_insert_at(sequence, 0, element, result);
}

ft_status ft_range_update_sequence_append(
    const ft_range_update_sequence* sequence,
    const void* element,
    ft_range_update_sequence* result)
{
    if (!ft_ru_sequence_valid(sequence)) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_range_update_sequence_insert_at(
        sequence, ft_ru_count(sequence->root), element, result);
}

ft_status ft_range_update_sequence_set_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    const void* element,
    ft_range_update_sequence* result)
{
    ft_ru_object* stored = NULL;
    ft_range_update_node* root = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || element == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_ru_count(sequence->root)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ru_object_copy_from_bytes(
        sequence->policy, FT_RU_OBJECT_ELEMENT, element, &stored);
    if (status == FT_STATUS_OK) {
        status = ft_ru_set_node(
            sequence->policy, sequence->root, index, stored, &root);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_publish_owned_root(sequence, root, result);
        if (status == FT_STATUS_OK) {
            root = NULL;
        }
    }
    ft_ru_node_release(sequence->policy, root);
    ft_ru_object_release(sequence->policy, stored);
    return status;
}

ft_status ft_range_update_sequence_remove_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    ft_range_update_sequence* result)
{
    ft_range_update_node* root = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index >= ft_ru_count(sequence->root)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ru_remove_node(sequence->policy, sequence->root, index, &root);
    if (status == FT_STATUS_OK) {
        status = ft_ru_publish_owned_root(sequence, root, result);
        if (status == FT_STATUS_OK) {
            root = NULL;
        }
    }
    ft_ru_node_release(sequence->policy, root);
    return status;
}

ft_status ft_range_update_sequence_concat(
    const ft_range_update_sequence* left,
    const ft_range_update_sequence* right,
    ft_range_update_sequence* result)
{
    ft_range_update_node* root = NULL;
    ft_range_update_sequence produced = {0};
    ft_range_update_sequence old = {0};
    size_t ignored = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(left) || !ft_ru_sequence_valid(right) ||
        result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    if (ft_ru_add_overflows(ft_ru_count(left->root), ft_ru_count(right->root), &ignored)) {
        return FT_STATUS_OVERFLOW;
    }
    status = ft_ru_concat_nodes(left->policy, left->root, right->root, &root);
    if (status == FT_STATUS_OK) {
        status = ft_ru_sequence_adopt_owned(left->policy, root, &produced);
        if (status == FT_STATUS_OK) {
            root = NULL;
        }
    }
    if (status == FT_STATUS_OK) {
        if (result == left || result == right) {
            old = *result;
            *result = produced;
            ft_range_update_sequence_dispose(&old);
        } else {
            *result = produced;
        }
    }
    ft_ru_node_release(left->policy, root);
    return status;
}

ft_status ft_range_update_sequence_split_at(
    const ft_range_update_sequence* sequence,
    size_t index,
    ft_range_update_split_result* result)
{
    ft_ru_node_pair split = {0};
    ft_range_update_split_result produced = {0};
    ft_range_update_sequence old = {0};
    bool aliases_left = false;
    bool aliases_right = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (index > ft_ru_count(sequence->root)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    status = ft_ru_split_nodes(sequence->policy, sequence->root, index, &split);
    if (status == FT_STATUS_OK) {
        status = ft_ru_sequence_adopt_owned(
            sequence->policy, split.left, &produced.left);
        if (status == FT_STATUS_OK) {
            split.left = NULL;
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_sequence_adopt_owned(
            sequence->policy, split.right, &produced.right);
        if (status == FT_STATUS_OK) {
            split.right = NULL;
        }
    }
    if (status != FT_STATUS_OK) {
        ft_range_update_sequence_dispose(&produced.right);
        ft_range_update_sequence_dispose(&produced.left);
        ft_ru_node_release(sequence->policy, split.right);
        ft_ru_node_release(sequence->policy, split.left);
        return status;
    }
    aliases_left = sequence == &result->left;
    aliases_right = sequence == &result->right;
    if (aliases_left || aliases_right) {
        old = *sequence;
    }
    result->left = produced.left;
    result->right = produced.right;
    if (aliases_left || aliases_right) {
        ft_range_update_sequence_dispose(&old);
    }
    return FT_STATUS_OK;
}

ft_status ft_range_update_sequence_get_range(
    const ft_range_update_sequence* sequence,
    size_t index,
    size_t count,
    ft_range_update_sequence* result)
{
    ft_ru_node_pair first = {0};
    ft_ru_node_pair second = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_ru_range_out_of_bounds(index, count, ft_ru_count(sequence->root))) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == 0) {
        return ft_ru_publish_borrowed_root(sequence, NULL, result);
    }
    if (count == ft_ru_count(sequence->root)) {
        return ft_ru_publish_borrowed_root(sequence, sequence->root, result);
    }
    status = ft_ru_split_nodes(sequence->policy, sequence->root, index, &first);
    if (status == FT_STATUS_OK) {
        status = ft_ru_split_nodes(sequence->policy, first.right, count, &second);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_publish_owned_root(sequence, second.left, result);
        if (status == FT_STATUS_OK) {
            second.left = NULL;
        }
    }
    ft_ru_node_release(sequence->policy, second.right);
    ft_ru_node_release(sequence->policy, second.left);
    ft_ru_node_release(sequence->policy, first.right);
    ft_ru_node_release(sequence->policy, first.left);
    return status;
}

ft_status ft_range_update_sequence_apply_range(
    const ft_range_update_sequence* sequence,
    size_t index,
    size_t count,
    const void* tag,
    ft_range_update_sequence* result)
{
    ft_ru_object* stored_tag = NULL;
    ft_ru_node_pair first = {0};
    ft_ru_node_pair second = {0};
    ft_range_update_node* updated = NULL;
    ft_range_update_node* partial = NULL;
    ft_range_update_node* root = NULL;
    bool identity = false;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || tag == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_ru_range_out_of_bounds(index, count, ft_ru_count(sequence->root))) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == 0) {
        return ft_ru_publish_borrowed_root(sequence, sequence->root, result);
    }
    status = sequence->policy->config.is_identity(
        tag, &identity, sequence->policy->config.algebra_context);
    if (status == FT_STATUS_OK && identity) {
        return ft_ru_publish_borrowed_root(sequence, sequence->root, result);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_object_copy_from_bytes(
            sequence->policy, FT_RU_OBJECT_TAG, tag, &stored_tag);
    }
    if (status == FT_STATUS_OK && count == ft_ru_count(sequence->root)) {
        status = ft_ru_apply_subtree(
            sequence->policy, sequence->root, stored_tag, &root);
    } else if (status == FT_STATUS_OK) {
        status = ft_ru_split_nodes(sequence->policy, sequence->root, index, &first);
        if (status == FT_STATUS_OK) {
            status = ft_ru_split_nodes(
                sequence->policy, first.right, count, &second);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_apply_subtree(
                sequence->policy, second.left, stored_tag, &updated);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_concat_nodes(
                sequence->policy, first.left, updated, &partial);
        }
        if (status == FT_STATUS_OK) {
            status = ft_ru_concat_nodes(
                sequence->policy, partial, second.right, &root);
        }
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_publish_owned_root(sequence, root, result);
        if (status == FT_STATUS_OK) {
            root = NULL;
        }
    }
    ft_ru_node_release(sequence->policy, root);
    ft_ru_node_release(sequence->policy, partial);
    ft_ru_node_release(sequence->policy, updated);
    ft_ru_node_release(sequence->policy, second.right);
    ft_ru_node_release(sequence->policy, second.left);
    ft_ru_node_release(sequence->policy, first.right);
    ft_ru_node_release(sequence->policy, first.left);
    ft_ru_object_release(sequence->policy, stored_tag);
    return status;
}

ft_status ft_range_update_sequence_measure_range(
    const ft_range_update_sequence* sequence,
    size_t index,
    size_t count,
    void* destination)
{
    ft_ru_object* measure = NULL;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (ft_ru_range_out_of_bounds(index, count, ft_ru_count(sequence->root))) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (count == 0) {
        measure = sequence->policy->empty_measure;
        ft_ru_object_retain(measure);
    } else if (count == ft_ru_count(sequence->root)) {
        measure = sequence->root->measure;
        ft_ru_object_retain(measure);
    } else {
        status = ft_ru_measure_range_node(
            sequence->policy, sequence->root, index, count, NULL, &measure);
    }
    if (status == FT_STATUS_OK) {
        status = ft_ru_copy_object_to_destination(
            sequence->policy, measure, destination);
    }
    ft_ru_object_release(sequence->policy, measure);
    return status;
}

ft_status ft_range_update_sequence_visit(
    const ft_range_update_sequence* sequence,
    ft_range_update_visit_fn visitor,
    void* context)
{
    if (!ft_ru_sequence_valid(sequence) || visitor == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_ru_visit_node(
        sequence->policy, sequence->root, NULL, visitor, context);
}

const void* ft_range_update_sequence_root_identity(
    const ft_range_update_sequence* sequence)
{
    return ft_ru_sequence_valid(sequence) ? sequence->root : NULL;
}

ft_status ft_range_update_sequence_physical_node_count(
    const ft_range_update_sequence* sequence,
    size_t* count)
{
    ft_ru_pointer_set set = {0};
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || count == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    status = ft_ru_collect_nodes(sequence->policy, sequence->root, &set);
    if (status == FT_STATUS_OK) {
        *count = set.count;
    }
    ft_ru_deallocate(sequence->policy, set.values);
    return status;
}

ft_status ft_range_update_sequence_shared_node_count(
    const ft_range_update_sequence* left,
    const ft_range_update_sequence* right,
    size_t* count)
{
    ft_ru_pointer_set candidates = {0};
    ft_ru_pointer_set visited = {0};
    size_t local_count = 0;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(left) || !ft_ru_sequence_valid(right) ||
        count == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (left->policy != right->policy) {
        return FT_STATUS_INCOMPATIBLE_POLICY;
    }
    status = ft_ru_collect_nodes(left->policy, left->root, &candidates);
    if (status == FT_STATUS_OK) {
        status = ft_ru_count_shared_nodes(
            left->policy,
            right->root,
            &candidates,
            &visited,
            &local_count);
    }
    if (status == FT_STATUS_OK) {
        *count = local_count;
    }
    ft_ru_deallocate(left->policy, visited.values);
    ft_ru_deallocate(left->policy, candidates.values);
    return status;
}

ft_status ft_range_update_sequence_validate(
    const ft_range_update_sequence* sequence,
    bool* valid,
    ft_range_update_sequence_statistics* statistics)
{
    ft_ru_validation_cache cache = {0};
    ft_ru_validation_entry root = {0};
    ft_range_update_sequence_statistics local_statistics = {0};
    bool local_valid = true;
    ft_status status = FT_STATUS_OK;
    if (!ft_ru_sequence_valid(sequence) || valid == NULL || statistics == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (sequence->root != NULL) {
        status = ft_ru_validate_node(
            sequence->policy,
            sequence->root,
            &cache,
            &local_valid,
            &root);
        if (status == FT_STATUS_OK && local_valid) {
            local_statistics = root.statistics;
            local_statistics.physical_node_count = cache.count;
        }
    }
    if (status == FT_STATUS_OK) {
        *valid = local_valid;
        *statistics = local_statistics;
    }
    ft_ru_object_release(sequence->policy, root.measure);
    ft_ru_validation_dispose(sequence->policy, &cache);
    return status;
}

static bool ft_range_update_sequence_cursor_is_valid(
    const ft_range_update_sequence_cursor* cursor)
{
    return cursor != NULL && ft_ru_sequence_valid(&cursor->sequence) &&
        cursor->position <= ft_range_update_sequence_size(&cursor->sequence);
}

static ft_status ft_range_update_sequence_cursor_stage(
    const ft_range_update_sequence* sequence,
    size_t position,
    ft_range_update_sequence_cursor* cursor)
{
    (void)memset(cursor, 0, sizeof(*cursor));
    ft_status status = ft_range_update_sequence_copy(sequence, &cursor->sequence);
    if (status == FT_STATUS_OK) {
        cursor->position = position;
    }
    return status;
}

static void ft_range_update_sequence_cursor_publish(
    const ft_range_update_sequence_cursor* source,
    ft_range_update_sequence_cursor* staged,
    ft_range_update_sequence_cursor* result)
{
    if (result == source) {
        ft_range_update_sequence_cursor_dispose(result);
    }
    ft_range_update_sequence_cursor_move(result, staged);
}

static ft_status ft_range_update_sequence_cursor_publish_sequence(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence* sequence,
    size_t position,
    ft_range_update_sequence_cursor* result)
{
    ft_range_update_sequence_cursor staged = {0};
    ft_range_update_sequence_move(&staged.sequence, sequence);
    staged.position = position;
    ft_range_update_sequence_cursor_publish(cursor, &staged, result);
    return FT_STATUS_OK;
}

ft_status ft_range_update_sequence_get_cursor(
    const ft_range_update_sequence* sequence,
    size_t position,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_ru_sequence_valid(sequence) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_range_update_sequence_size(sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    return ft_range_update_sequence_cursor_stage(sequence, position, result);
}

ft_status ft_range_update_sequence_cursor_copy(
    const ft_range_update_sequence_cursor* source,
    ft_range_update_sequence_cursor* destination)
{
    if (!ft_range_update_sequence_cursor_is_valid(source) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (source == destination) {
        return FT_STATUS_OK;
    }
    return ft_range_update_sequence_cursor_stage(
        &source->sequence,
        source->position,
        destination);
}

void ft_range_update_sequence_cursor_move(
    ft_range_update_sequence_cursor* destination,
    ft_range_update_sequence_cursor* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    (void)memset(destination, 0, sizeof(*destination));
    ft_range_update_sequence_move(&destination->sequence, &source->sequence);
    destination->position = source->position;
    source->position = 0;
}

void ft_range_update_sequence_cursor_dispose(ft_range_update_sequence_cursor* cursor)
{
    if (cursor != NULL) {
        ft_range_update_sequence_dispose(&cursor->sequence);
        cursor->position = 0;
    }
}

bool ft_range_update_sequence_cursor_valid(const ft_range_update_sequence_cursor* cursor)
{
    return ft_range_update_sequence_cursor_is_valid(cursor);
}

bool ft_range_update_sequence_cursor_empty(const ft_range_update_sequence_cursor* cursor)
{
    return !ft_range_update_sequence_cursor_is_valid(cursor) ||
        ft_range_update_sequence_empty(&cursor->sequence);
}

size_t ft_range_update_sequence_cursor_size(const ft_range_update_sequence_cursor* cursor)
{
    return ft_range_update_sequence_cursor_is_valid(cursor)
        ? ft_range_update_sequence_size(&cursor->sequence)
        : 0;
}

size_t ft_range_update_sequence_cursor_position(const ft_range_update_sequence_cursor* cursor)
{
    return ft_range_update_sequence_cursor_is_valid(cursor) ? cursor->position : 0;
}

ft_status ft_range_update_sequence_cursor_is_at_start(
    const ft_range_update_sequence_cursor* cursor,
    bool* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == 0;
    return FT_STATUS_OK;
}

ft_status ft_range_update_sequence_cursor_is_at_end(
    const ft_range_update_sequence_cursor* cursor,
    bool* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    *result = cursor->position == ft_range_update_sequence_size(&cursor->sequence);
    return FT_STATUS_OK;
}

ft_status ft_range_update_sequence_cursor_measure_before(
    const ft_range_update_sequence_cursor* cursor,
    void* destination)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_range_update_sequence_measure_range(
        &cursor->sequence,
        0,
        cursor->position,
        destination);
}

ft_status ft_range_update_sequence_cursor_measure_after(
    const ft_range_update_sequence_cursor* cursor,
    void* destination)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    return ft_range_update_sequence_measure_range(
        &cursor->sequence,
        cursor->position,
        ft_range_update_sequence_size(&cursor->sequence) - cursor->position,
        destination);
}

ft_status ft_range_update_sequence_cursor_try_peek_previous(
    const ft_range_update_sequence_cursor* cursor,
    bool* found,
    void* value)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || found == NULL || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        *found = false;
        return FT_STATUS_OK;
    }
    ft_status status = ft_range_update_sequence_at(
        &cursor->sequence,
        cursor->position - 1u,
        value);
    if (status == FT_STATUS_OK) {
        *found = true;
    }
    return status;
}

ft_status ft_range_update_sequence_cursor_try_peek_next(
    const ft_range_update_sequence_cursor* cursor,
    bool* found,
    void* value)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || found == NULL || value == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == ft_range_update_sequence_size(&cursor->sequence)) {
        *found = false;
        return FT_STATUS_OK;
    }
    ft_status status = ft_range_update_sequence_at(
        &cursor->sequence,
        cursor->position,
        value);
    if (status == FT_STATUS_OK) {
        *found = true;
    }
    return status;
}

ft_status ft_range_update_sequence_cursor_seek(
    const ft_range_update_sequence_cursor* cursor,
    size_t position,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (position > ft_range_update_sequence_size(&cursor->sequence)) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    if (result == cursor && position == cursor->position) {
        return FT_STATUS_OK;
    }
    ft_range_update_sequence_cursor staged;
    ft_status status = ft_range_update_sequence_cursor_stage(
        &cursor->sequence,
        position,
        &staged);
    if (status == FT_STATUS_OK) {
        ft_range_update_sequence_cursor_publish(cursor, &staged, result);
    }
    return status;
}

ft_status ft_range_update_sequence_cursor_move_previous(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_range_update_sequence_empty(&cursor->sequence)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_range_update_sequence_cursor_seek(cursor, cursor->position - 1u, result);
}

ft_status ft_range_update_sequence_cursor_move_next(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_range_update_sequence_size(&cursor->sequence);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    return ft_range_update_sequence_cursor_seek(cursor, cursor->position + 1u, result);
}

ft_status ft_range_update_sequence_cursor_insert(
    const ft_range_update_sequence_cursor* cursor,
    const void* value,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == SIZE_MAX) {
        return FT_STATUS_OVERFLOW;
    }
    ft_range_update_sequence edited = {0};
    ft_status status = ft_range_update_sequence_insert_at(
        &cursor->sequence,
        cursor->position,
        value,
        &edited);
    return status == FT_STATUS_OK
        ? ft_range_update_sequence_cursor_publish_sequence(
            cursor,
            &edited,
            cursor->position + 1u,
            result)
        : status;
}

ft_status ft_range_update_sequence_cursor_delete_previous(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (cursor->position == 0) {
        return ft_range_update_sequence_empty(&cursor->sequence)
            ? FT_STATUS_EMPTY
            : FT_STATUS_OUT_OF_RANGE;
    }
    ft_range_update_sequence edited = {0};
    ft_status status = ft_range_update_sequence_remove_at(
        &cursor->sequence,
        cursor->position - 1u,
        &edited);
    return status == FT_STATUS_OK
        ? ft_range_update_sequence_cursor_publish_sequence(
            cursor,
            &edited,
            cursor->position - 1u,
            result)
        : status;
}

ft_status ft_range_update_sequence_cursor_delete_next(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_range_update_sequence_size(&cursor->sequence);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    ft_range_update_sequence edited = {0};
    ft_status status = ft_range_update_sequence_remove_at(
        &cursor->sequence,
        cursor->position,
        &edited);
    return status == FT_STATUS_OK
        ? ft_range_update_sequence_cursor_publish_sequence(
            cursor,
            &edited,
            cursor->position,
            result)
        : status;
}

ft_status ft_range_update_sequence_cursor_replace_next(
    const ft_range_update_sequence_cursor* cursor,
    const void* value,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || value == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    const size_t size = ft_range_update_sequence_size(&cursor->sequence);
    if (cursor->position == size) {
        return size == 0 ? FT_STATUS_EMPTY : FT_STATUS_OUT_OF_RANGE;
    }
    ft_range_update_sequence edited = {0};
    ft_status status = ft_range_update_sequence_set_at(
        &cursor->sequence,
        cursor->position,
        value,
        &edited);
    return status == FT_STATUS_OK
        ? ft_range_update_sequence_cursor_publish_sequence(
            cursor,
            &edited,
            cursor->position,
            result)
        : status;
}

ft_status ft_range_update_sequence_cursor_measure_previous(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    void* destination)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > cursor->position) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    return ft_range_update_sequence_measure_range(
        &cursor->sequence,
        cursor->position - count,
        count,
        destination);
}

ft_status ft_range_update_sequence_cursor_measure_next(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    void* destination)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || destination == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > ft_range_update_sequence_size(&cursor->sequence) - cursor->position) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    return ft_range_update_sequence_measure_range(
        &cursor->sequence,
        cursor->position,
        count,
        destination);
}

ft_status ft_range_update_sequence_cursor_apply_previous(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    const void* tag,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || tag == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > cursor->position) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    ft_range_update_sequence edited = {0};
    ft_status status = ft_range_update_sequence_apply_range(
        &cursor->sequence,
        cursor->position - count,
        count,
        tag,
        &edited);
    return status == FT_STATUS_OK
        ? ft_range_update_sequence_cursor_publish_sequence(
            cursor,
            &edited,
            cursor->position,
            result)
        : status;
}

ft_status ft_range_update_sequence_cursor_apply_next(
    const ft_range_update_sequence_cursor* cursor,
    size_t count,
    const void* tag,
    ft_range_update_sequence_cursor* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || tag == NULL || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (count > ft_range_update_sequence_size(&cursor->sequence) - cursor->position) {
        return FT_STATUS_OUT_OF_RANGE;
    }
    ft_range_update_sequence edited = {0};
    ft_status status = ft_range_update_sequence_apply_range(
        &cursor->sequence,
        cursor->position,
        count,
        tag,
        &edited);
    return status == FT_STATUS_OK
        ? ft_range_update_sequence_cursor_publish_sequence(
            cursor,
            &edited,
            cursor->position,
            result)
        : status;
}

ft_status ft_range_update_sequence_cursor_snapshot(
    const ft_range_update_sequence_cursor* cursor,
    ft_range_update_sequence* result)
{
    if (!ft_range_update_sequence_cursor_is_valid(cursor) || result == NULL) {
        return FT_STATUS_INVALID_ARGUMENT;
    }
    if (result == &cursor->sequence) {
        return FT_STATUS_OK;
    }
    return ft_range_update_sequence_copy(&cursor->sequence, result);
}
