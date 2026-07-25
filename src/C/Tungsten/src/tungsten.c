#include <durable7/tungsten/tungsten.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    D7_TUNGSTEN_STAMP_GAP = 1 << 20
};

typedef struct d7_tungsten_slot {
    int64_t stamp;
    void* value;
} d7_tungsten_slot;

typedef struct d7_tungsten_assoc_entry {
    int64_t stamp;
    void* key;
    void* value;
} d7_tungsten_assoc_entry;

typedef struct d7_tungsten_assoc_entry_view {
    int64_t stamp;
    const void* key;
    const void* value;
} d7_tungsten_assoc_entry_view;

struct d7_tungsten_assoc_context {
    size_t ref_count;
    d7_tungsten_association_policy policy;
    d7_hamt_policy hamt_policy;
};

struct d7_tungsten_assoc_node {
    size_t ref_count;
    size_t size;
    int height;
    struct d7_tungsten_assoc_node* left;
    struct d7_tungsten_assoc_node* right;
    d7_tungsten_assoc_entry entry;
};

typedef struct d7_tungsten_sort_context {
    ft_compare_fn compare;
    void* compare_context;
    bool by_key;
} d7_tungsten_sort_context;

static d7_tungsten_status d7_tungsten_from_ft(ft_status status)
{
    switch (status) {
    case FT_STATUS_OK:
        return D7_TUNGSTEN_OK;
    case FT_STATUS_OUT_OF_RANGE:
        return D7_TUNGSTEN_OUT_OF_RANGE;
    case FT_STATUS_EMPTY:
        return D7_TUNGSTEN_EMPTY;
    case FT_STATUS_NOT_FOUND:
        return D7_TUNGSTEN_NOT_FOUND;
    case FT_STATUS_NO_MEMORY:
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    case FT_STATUS_OVERFLOW:
        return D7_TUNGSTEN_OVERFLOW;
    case FT_STATUS_ALREADY_EXISTS:
        return D7_TUNGSTEN_DUPLICATE_KEY;
    case FT_STATUS_INVALID_ARGUMENT:
    default:
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }
}

static d7_tungsten_status d7_tungsten_from_hamt(d7_hamt_status status)
{
    switch (status) {
    case D7_HAMT_OK:
        return D7_TUNGSTEN_OK;
    case D7_HAMT_OUT_OF_MEMORY:
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    case D7_HAMT_DUPLICATE_KEY:
        return D7_TUNGSTEN_DUPLICATE_KEY;
    case D7_HAMT_OVERFLOW:
        return D7_TUNGSTEN_OVERFLOW;
    case D7_HAMT_INVALID_ARGUMENT:
    default:
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }
}

static void d7_tungsten_value_copy(const ft_value_type* type, void* destination, const void* source)
{
    if (type->copy != NULL) {
        type->copy(destination, source, type->context);
    } else {
        (void)memcpy(destination, source, type->size);
    }
}

static void d7_tungsten_value_destroy(const ft_value_type* type, void* value)
{
    if (value != NULL && type->destroy != NULL) {
        type->destroy(value, type->context);
    }
}

static void* d7_tungsten_value_alloc_copy(const ft_value_type* type, const void* source)
{
    void* value = malloc(type->size == 0 ? 1u : type->size);
    if (value == NULL) {
        return NULL;
    }

    d7_tungsten_value_copy(type, value, source);
    return value;
}

static void d7_tungsten_value_free(const ft_value_type* type, void* value)
{
    if (value != NULL) {
        d7_tungsten_value_destroy(type, value);
        free(value);
    }
}

static bool d7_tungsten_value_equal(
    const ft_value_type* type,
    d7_tungsten_equal_fn equal,
    void* context,
    const void* left,
    const void* right)
{
    if (left == right) {
        return true;
    }
    if (equal != NULL) {
        return equal(left, right, context);
    }

    return left == right || (left != NULL && right != NULL && memcmp(left, right, type->size) == 0);
}

static bool d7_tungsten_list_is_valid(const d7_tungsten_list* list)
{
    return list != NULL && list->items.policy != NULL && list->items.rep != NULL;
}

static d7_tungsten_status d7_tungsten_list_prepare(
    const d7_tungsten_list* source,
    d7_tungsten_list* result)
{
    /* Unlike the HAMT surfaces, result must not alias the source: prepare zeroes the
     * result before the source is read, which would leak the source's deque rep and
     * leave the caller holding an empty list even when the operation then fails. */
    if (!d7_tungsten_list_is_valid(source) || result == NULL || result == source) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    (void)memset(result, 0, sizeof(*result));
    result->policy = source->policy;
    return D7_TUNGSTEN_OK;
}

static void d7_tungsten_list_rebind(d7_tungsten_list* list)
{
    if (list != NULL && list->items.rep != NULL) {
        list->items.policy = &list->policy;
    }
}

void d7_tungsten_association_policy_init(
    d7_tungsten_association_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    d7_tungsten_hash_fn hash_key,
    d7_tungsten_equal_fn key_equal,
    void* context)
{
    if (policy == NULL) {
        return;
    }

    (void)memset(policy, 0, sizeof(*policy));
    if (key_type != NULL) {
        policy->key_type = *key_type;
    }
    if (value_type != NULL) {
        policy->value_type = *value_type;
    }
    policy->hash_key = hash_key;
    policy->key_equal = key_equal;
    policy->value_equal = NULL;
    policy->context = context;
}

d7_tungsten_status d7_tungsten_list_init(d7_tungsten_list* list, const ft_value_type* value_type)
{
    if (list == NULL || value_type == NULL || value_type->size == 0) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    (void)memset(list, 0, sizeof(*list));
    ft_tree_policy_init_size(&list->policy, value_type);
    const ft_status status = ft_persistent_deque_init(&list->items, &list->policy);
    if (status != FT_STATUS_OK) {
        (void)memset(list, 0, sizeof(*list));
    }
    return d7_tungsten_from_ft(status);
}

d7_tungsten_status d7_tungsten_list_from_array(
    d7_tungsten_list* list,
    const ft_value_type* value_type,
    const void* values,
    size_t count)
{
    if (list == NULL || value_type == NULL || value_type->size == 0 || (values == NULL && count != 0)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_list_init(list, value_type);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    const unsigned char* bytes = (const unsigned char*)values;
    for (size_t index = 0; index != count; ++index) {
        d7_tungsten_list next;
        status = d7_tungsten_list_push_back(list, bytes + index * value_type->size, &next);
        if (status != D7_TUNGSTEN_OK) {
            d7_tungsten_list_dispose(list);
            return status;
        }

        d7_tungsten_list_dispose(list);
        d7_tungsten_list_move(list, &next);
    }

    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_list_copy(const d7_tungsten_list* source, d7_tungsten_list* destination)
{
    d7_tungsten_status status = d7_tungsten_list_prepare(source, destination);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_ft(ft_persistent_deque_copy(&source->items, &destination->items));
    if (status != D7_TUNGSTEN_OK) {
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    d7_tungsten_list_rebind(destination);
    return D7_TUNGSTEN_OK;
}

void d7_tungsten_list_move(d7_tungsten_list* destination, d7_tungsten_list* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    d7_tungsten_list_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void d7_tungsten_list_dispose(d7_tungsten_list* list)
{
    if (list == NULL) {
        return;
    }

    ft_persistent_deque_dispose(&list->items);
    (void)memset(list, 0, sizeof(*list));
}

bool d7_tungsten_list_empty(const d7_tungsten_list* list)
{
    return !d7_tungsten_list_is_valid(list) || ft_persistent_deque_empty(&list->items);
}

size_t d7_tungsten_list_size(const d7_tungsten_list* list)
{
    return d7_tungsten_list_is_valid(list) ? ft_persistent_deque_size(&list->items) : 0u;
}

d7_tungsten_status d7_tungsten_list_front(const d7_tungsten_list* list, void* destination)
{
    if (!d7_tungsten_list_is_valid(list) || destination == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    return d7_tungsten_from_ft(ft_persistent_deque_front(&list->items, destination));
}

d7_tungsten_status d7_tungsten_list_back(const d7_tungsten_list* list, void* destination)
{
    if (!d7_tungsten_list_is_valid(list) || destination == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    return d7_tungsten_from_ft(ft_persistent_deque_back(&list->items, destination));
}

d7_tungsten_status d7_tungsten_list_at(const d7_tungsten_list* list, size_t index, void* destination)
{
    if (!d7_tungsten_list_is_valid(list) || destination == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    return d7_tungsten_from_ft(ft_persistent_deque_at(&list->items, index, destination));
}

d7_tungsten_status d7_tungsten_list_push_front(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_list* result)
{
    if (value == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_list_prepare(list, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_ft(ft_persistent_deque_push_front(&list->items, value, &result->items));
    if (status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

d7_tungsten_status d7_tungsten_list_push_back(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_list* result)
{
    if (value == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_list_prepare(list, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_ft(ft_persistent_deque_push_back(&list->items, value, &result->items));
    if (status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

d7_tungsten_status d7_tungsten_list_concat(
    const d7_tungsten_list* left,
    const d7_tungsten_list* right,
    d7_tungsten_list* result)
{
    /* Concatenation shares nodes between the operands under the left list's policy, so
     * the value types must agree completely: two payloads of equal size but different
     * copy/destroy callbacks (say plain ints versus owned pointers) would otherwise have
     * some shared nodes released through the wrong callbacks, leaking or double-freeing. */
    if (!d7_tungsten_list_is_valid(left) || !d7_tungsten_list_is_valid(right) || result == right ||
        left->policy.value.size != right->policy.value.size ||
        left->policy.value.copy != right->policy.value.copy ||
        left->policy.value.destroy != right->policy.value.destroy ||
        left->policy.value.context != right->policy.value.context) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_list_prepare(left, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    ft_tree left_items = left->items;
    ft_tree right_items = right->items;
    left_items.policy = &left->policy;
    right_items.policy = &left->policy;
    status = d7_tungsten_from_ft(ft_persistent_deque_concat(&left_items, &right_items, &result->items));
    if (status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

d7_tungsten_status d7_tungsten_list_insert_at(
    const d7_tungsten_list* list,
    size_t index,
    const void* value,
    d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list) || value == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > ft_persistent_deque_size(&list->items)) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    d7_tungsten_status status = d7_tungsten_list_prepare(list, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_ft(ft_persistent_deque_insert_at(&list->items, index, value, &result->items));
    if (status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

d7_tungsten_status d7_tungsten_list_insert_range(
    const d7_tungsten_list* list,
    size_t index,
    const void* values,
    size_t count,
    d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list) || result == NULL || result == list ||
        (values == NULL && count != 0) || index > ft_persistent_deque_size(&list->items)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (count == 0) {
        return d7_tungsten_list_copy(list, result);
    }

    d7_tungsten_list middle;
    d7_tungsten_status status =
        d7_tungsten_list_from_array(&middle, &list->policy.value, values, count);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    ft_tree_split_result split;
    status = d7_tungsten_from_ft(ft_persistent_deque_split_at(&list->items, index, &split));
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_list_dispose(&middle);
        return status;
    }

    d7_tungsten_list left;
    d7_tungsten_list right;
    (void)memset(&left, 0, sizeof(left));
    (void)memset(&right, 0, sizeof(right));
    left.policy = list->policy;
    left.items = split.left;
    d7_tungsten_list_rebind(&left);
    right.policy = list->policy;
    right.items = split.right;
    d7_tungsten_list_rebind(&right);

    d7_tungsten_list joined_left;
    status = d7_tungsten_list_concat(&left, &middle, &joined_left);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_list_concat(&joined_left, &right, result);
        d7_tungsten_list_dispose(&joined_left);
    }

    d7_tungsten_list_dispose(&left);
    d7_tungsten_list_dispose(&right);
    d7_tungsten_list_dispose(&middle);
    return status;
}

d7_tungsten_status d7_tungsten_list_remove_at(
    const d7_tungsten_list* list,
    size_t index,
    d7_tungsten_list* result)
{
    d7_tungsten_status status = d7_tungsten_list_prepare(list, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_ft(ft_persistent_deque_remove_at(&list->items, index, &result->items));
    if (status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

d7_tungsten_status d7_tungsten_list_remove_range(
    const d7_tungsten_list* list,
    size_t index,
    size_t count,
    d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list) || result == NULL || result == list) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > ft_persistent_deque_size(&list->items) ||
        count > ft_persistent_deque_size(&list->items) - index) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    ft_tree_split_result split_left;
    d7_tungsten_status status = d7_tungsten_from_ft(ft_persistent_deque_split_at(&list->items, index, &split_left));
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    ft_tree_split_result split_right;
    status = d7_tungsten_from_ft(ft_persistent_deque_split_at(&split_left.right, count, &split_right));
    if (status != D7_TUNGSTEN_OK) {
        ft_tree_dispose(&split_left.left);
        ft_tree_dispose(&split_left.right);
        return status;
    }

    d7_tungsten_list left;
    d7_tungsten_list right;
    (void)memset(&left, 0, sizeof(left));
    (void)memset(&right, 0, sizeof(right));
    left.policy = list->policy;
    left.items = split_left.left;
    d7_tungsten_list_rebind(&left);
    right.policy = list->policy;
    right.items = split_right.right;
    d7_tungsten_list_rebind(&right);

    ft_tree_dispose(&split_left.right);
    ft_tree_dispose(&split_right.left);
    status = d7_tungsten_list_concat(&left, &right, result);
    d7_tungsten_list_dispose(&left);
    d7_tungsten_list_dispose(&right);
    return status;
}

d7_tungsten_status d7_tungsten_list_set_at(
    const d7_tungsten_list* list,
    size_t index,
    const void* value,
    d7_tungsten_list* result)
{
    if (value == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_list_prepare(list, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_ft(ft_persistent_deque_set_at(&list->items, index, value, &result->items));
    if (status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

d7_tungsten_status d7_tungsten_list_slice(
    const d7_tungsten_list* list,
    size_t index,
    size_t count,
    d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list) || result == NULL || result == list) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > ft_persistent_deque_size(&list->items) ||
        count > ft_persistent_deque_size(&list->items) - index) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    ft_tree_split_result split_left;
    d7_tungsten_status status = d7_tungsten_from_ft(ft_persistent_deque_split_at(&list->items, index, &split_left));
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    ft_tree_split_result split_right;
    status = d7_tungsten_from_ft(ft_persistent_deque_split_at(&split_left.right, count, &split_right));
    if (status != D7_TUNGSTEN_OK) {
        ft_tree_dispose(&split_left.left);
        ft_tree_dispose(&split_left.right);
        return status;
    }

    ft_tree_dispose(&split_left.left);
    ft_tree_dispose(&split_left.right);
    ft_tree_dispose(&split_right.right);
    (void)memset(result, 0, sizeof(*result));
    result->policy = list->policy;
    result->items = split_right.left;
    d7_tungsten_list_rebind(result);
    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_list_take(const d7_tungsten_list* list, size_t count, d7_tungsten_list* result)
{
    return d7_tungsten_list_slice(list, 0, count, result);
}

d7_tungsten_status d7_tungsten_list_drop(const d7_tungsten_list* list, size_t count, d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* Classified like take/slice and the association's drop: a bad range is
     * D7_TUNGSTEN_OUT_OF_RANGE, not a bad handle. */
    if (count > ft_persistent_deque_size(&list->items)) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    return d7_tungsten_list_slice(list, count, ft_persistent_deque_size(&list->items) - count, result);
}

typedef struct d7_tungsten_reverse_context {
    d7_tungsten_list result;
    d7_tungsten_status status;
} d7_tungsten_reverse_context;

static void d7_tungsten_list_reverse_visit(const void* value, void* context)
{
    d7_tungsten_reverse_context* reverse_context = (d7_tungsten_reverse_context*)context;
    if (reverse_context->status != D7_TUNGSTEN_OK) {
        return;
    }

    d7_tungsten_list next;
    reverse_context->status = d7_tungsten_list_push_front(&reverse_context->result, value, &next);
    if (reverse_context->status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_dispose(&reverse_context->result);
        d7_tungsten_list_move(&reverse_context->result, &next);
    }
}

d7_tungsten_status d7_tungsten_list_reverse(const d7_tungsten_list* list, d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list) || result == NULL || result == list) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_reverse_context context;
    context.status = d7_tungsten_list_init(&context.result, &list->policy.value);
    if (context.status != D7_TUNGSTEN_OK) {
        return context.status;
    }

    context.status = d7_tungsten_from_ft(ft_persistent_deque_visit(&list->items, d7_tungsten_list_reverse_visit, &context));
    if (context.status != D7_TUNGSTEN_OK) {
        d7_tungsten_list_dispose(&context.result);
        return context.status;
    }

    d7_tungsten_list_move(result, &context.result);
    return D7_TUNGSTEN_OK;
}

typedef struct d7_tungsten_map_context {
    d7_tungsten_list result;
    const ft_value_type* result_value_type;
    d7_tungsten_map_fn map;
    void* map_context;
    void* buffer;
    d7_tungsten_status status;
} d7_tungsten_map_context;

static void d7_tungsten_list_map_visit(const void* value, void* context)
{
    d7_tungsten_map_context* map_context = (d7_tungsten_map_context*)context;
    if (map_context->status != D7_TUNGSTEN_OK) {
        return;
    }

    map_context->map(map_context->buffer, value, map_context->map_context);
    d7_tungsten_list next;
    map_context->status = d7_tungsten_list_push_back(&map_context->result, map_context->buffer, &next);
    /* push_back deep-copied the mapped value; destroy the callback-constructed
     * original so owning result types do not leak one payload per element. */
    d7_tungsten_value_destroy(map_context->result_value_type, map_context->buffer);
    if (map_context->status == D7_TUNGSTEN_OK) {
        d7_tungsten_list_dispose(&map_context->result);
        d7_tungsten_list_move(&map_context->result, &next);
    }
}

d7_tungsten_status d7_tungsten_list_map(
    const d7_tungsten_list* list,
    const ft_value_type* result_value_type,
    d7_tungsten_map_fn map,
    void* map_context,
    d7_tungsten_list* result)
{
    if (!d7_tungsten_list_is_valid(list) || result_value_type == NULL || result_value_type->size == 0 ||
        map == NULL || result == NULL || result == list) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_map_context context;
    context.result_value_type = result_value_type;
    context.map = map;
    context.map_context = map_context;
    context.buffer = malloc(result_value_type->size == 0 ? 1u : result_value_type->size);
    if (context.buffer == NULL) {
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    context.status = d7_tungsten_list_init(&context.result, result_value_type);
    if (context.status == D7_TUNGSTEN_OK) {
        context.status = d7_tungsten_from_ft(ft_persistent_deque_visit(&list->items, d7_tungsten_list_map_visit, &context));
    }

    free(context.buffer);
    if (context.status != D7_TUNGSTEN_OK) {
        d7_tungsten_list_dispose(&context.result);
        return context.status;
    }

    d7_tungsten_list_move(result, &context.result);
    return D7_TUNGSTEN_OK;
}

static void d7_tungsten_list_visit_adapter(const void* value, void* context)
{
    void** data = (void**)context;
    d7_tungsten_visit_fn visitor = (d7_tungsten_visit_fn)data[0];
    visitor(value, data[1]);
}

d7_tungsten_status d7_tungsten_list_visit(
    const d7_tungsten_list* list,
    d7_tungsten_visit_fn visitor,
    void* context)
{
    if (!d7_tungsten_list_is_valid(list) || visitor == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    void* data[2];
    data[0] = (void*)visitor;
    data[1] = context;
    return d7_tungsten_from_ft(ft_persistent_deque_visit(&list->items, d7_tungsten_list_visit_adapter, data));
}

typedef struct d7_tungsten_index_of_context {
    const void* value;
    d7_tungsten_equal_fn equal;
    void* equal_context;
    size_t current;
    size_t found_index;
    bool found;
} d7_tungsten_index_of_context;

static void d7_tungsten_list_index_of_visit(const void* value, void* context)
{
    d7_tungsten_index_of_context* index_context = (d7_tungsten_index_of_context*)context;
    if (!index_context->found && index_context->equal(value, index_context->value, index_context->equal_context)) {
        index_context->found = true;
        index_context->found_index = index_context->current;
    }
    ++index_context->current;
}

bool d7_tungsten_list_index_of(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_equal_fn equal,
    void* context,
    size_t* index)
{
    if (!d7_tungsten_list_is_valid(list) || value == NULL || equal == NULL) {
        return false;
    }

    d7_tungsten_index_of_context index_context;
    index_context.value = value;
    index_context.equal = equal;
    index_context.equal_context = context;
    index_context.current = 0;
    index_context.found_index = 0;
    index_context.found = false;
    if (ft_persistent_deque_visit(&list->items, d7_tungsten_list_index_of_visit, &index_context) != FT_STATUS_OK) {
        return false;
    }

    if (index != NULL) {
        *index = index_context.found_index;
    }
    return index_context.found;
}

bool d7_tungsten_list_contains(
    const d7_tungsten_list* list,
    const void* value,
    d7_tungsten_equal_fn equal,
    void* context)
{
    return d7_tungsten_list_index_of(list, value, equal, context, NULL);
}

static uint32_t d7_tungsten_hamt_hash(const void* key, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    return assoc_context->policy.hash_key(key, assoc_context->policy.context);
}

static bool d7_tungsten_hamt_key_equal(const void* left, const void* right, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    return assoc_context->policy.key_equal(left, right, assoc_context->policy.context);
}

static bool d7_tungsten_hamt_value_equal(const void* left, const void* right, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    const d7_tungsten_slot* left_slot = (const d7_tungsten_slot*)left;
    const d7_tungsten_slot* right_slot = (const d7_tungsten_slot*)right;
    return left_slot->stamp == right_slot->stamp &&
        d7_tungsten_value_equal(
            &assoc_context->policy.value_type,
            assoc_context->policy.value_equal,
            assoc_context->policy.context,
            left_slot->value,
            right_slot->value);
}

static void* d7_tungsten_hamt_retain_key(const void* key, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    return d7_tungsten_value_alloc_copy(&assoc_context->policy.key_type, key);
}

static void d7_tungsten_hamt_release_key(void* key, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    d7_tungsten_value_free(&assoc_context->policy.key_type, key);
}

static void* d7_tungsten_hamt_retain_value(const void* value, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    const d7_tungsten_slot* source = (const d7_tungsten_slot*)value;
    d7_tungsten_slot* slot = (d7_tungsten_slot*)calloc(1, sizeof(*slot));
    if (slot == NULL) {
        return NULL;
    }

    slot->stamp = source->stamp;
    slot->value = d7_tungsten_value_alloc_copy(&assoc_context->policy.value_type, source->value);
    if (slot->value == NULL) {
        free(slot);
        return NULL;
    }

    return slot;
}

static void d7_tungsten_hamt_release_value(void* value, void* context)
{
    const struct d7_tungsten_assoc_context* assoc_context = (const struct d7_tungsten_assoc_context*)context;
    d7_tungsten_slot* slot = (d7_tungsten_slot*)value;
    if (slot != NULL) {
        d7_tungsten_value_free(&assoc_context->policy.value_type, slot->value);
        free(slot);
    }
}

static bool d7_tungsten_policy_valid(const d7_tungsten_association_policy* policy)
{
    return policy != NULL &&
        policy->key_type.size != 0 &&
        policy->value_type.size != 0 &&
        policy->hash_key != NULL &&
        policy->key_equal != NULL;
}

static d7_tungsten_status d7_tungsten_context_create(
    const d7_tungsten_association_policy* policy,
    struct d7_tungsten_assoc_context** result)
{
    if (!d7_tungsten_policy_valid(policy) || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    struct d7_tungsten_assoc_context* context =
        (struct d7_tungsten_assoc_context*)calloc(1, sizeof(*context));
    if (context == NULL) {
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    context->ref_count = 1;
    context->policy = *policy;
    context->hamt_policy.hash = d7_tungsten_hamt_hash;
    context->hamt_policy.key_equal = d7_tungsten_hamt_key_equal;
    context->hamt_policy.value_equal = d7_tungsten_hamt_value_equal;
    context->hamt_policy.retain_key = d7_tungsten_hamt_retain_key;
    context->hamt_policy.retain_value = d7_tungsten_hamt_retain_value;
    context->hamt_policy.release_key = d7_tungsten_hamt_release_key;
    context->hamt_policy.release_value = d7_tungsten_hamt_release_value;
    context->hamt_policy.context = context;
    *result = context;
    return D7_TUNGSTEN_OK;
}

static void d7_tungsten_context_retain(struct d7_tungsten_assoc_context* context)
{
    if (context != NULL) {
        ++context->ref_count;
    }
}

static void d7_tungsten_context_release(struct d7_tungsten_assoc_context* context)
{
    if (context != NULL && --context->ref_count == 0) {
        free(context);
    }
}

static size_t d7_tungsten_node_size(const struct d7_tungsten_assoc_node* node)
{
    return node == NULL ? 0u : node->size;
}

static int d7_tungsten_node_height(const struct d7_tungsten_assoc_node* node)
{
    return node == NULL ? 0 : node->height;
}

static int d7_tungsten_max_int(int left, int right)
{
    return left > right ? left : right;
}

static void d7_tungsten_entry_destroy(
    const struct d7_tungsten_assoc_context* context,
    d7_tungsten_assoc_entry* entry)
{
    if (entry != NULL) {
        d7_tungsten_value_free(&context->policy.key_type, entry->key);
        d7_tungsten_value_free(&context->policy.value_type, entry->value);
        entry->key = NULL;
        entry->value = NULL;
        entry->stamp = 0;
    }
}

static d7_tungsten_status d7_tungsten_entry_init(
    const struct d7_tungsten_assoc_context* context,
    int64_t stamp,
    const void* key,
    const void* value,
    d7_tungsten_assoc_entry* entry)
{
    entry->stamp = stamp;
    entry->key = d7_tungsten_value_alloc_copy(&context->policy.key_type, key);
    entry->value = d7_tungsten_value_alloc_copy(&context->policy.value_type, value);
    if (entry->key == NULL || entry->value == NULL) {
        d7_tungsten_entry_destroy(context, entry);
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_entry_copy(
    const struct d7_tungsten_assoc_context* context,
    const d7_tungsten_assoc_entry* source,
    d7_tungsten_assoc_entry* entry)
{
    return d7_tungsten_entry_init(context, source->stamp, source->key, source->value, entry);
}

static struct d7_tungsten_assoc_node* d7_tungsten_node_retain(struct d7_tungsten_assoc_node* node)
{
    if (node != NULL) {
        ++node->ref_count;
    }
    return node;
}

static void d7_tungsten_node_release(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* node)
{
    if (node == NULL || --node->ref_count != 0) {
        return;
    }

    d7_tungsten_node_release(context, node->left);
    d7_tungsten_node_release(context, node->right);
    d7_tungsten_entry_destroy(context, &node->entry);
    free(node);
}

static d7_tungsten_status d7_tungsten_node_create(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* left,
    const d7_tungsten_assoc_entry* entry,
    struct d7_tungsten_assoc_node* right,
    struct d7_tungsten_assoc_node** result)
{
    struct d7_tungsten_assoc_node* node = (struct d7_tungsten_assoc_node*)calloc(1, sizeof(*node));
    if (node == NULL) {
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    node->ref_count = 1;
    node->left = d7_tungsten_node_retain(left);
    node->right = d7_tungsten_node_retain(right);
    node->size = d7_tungsten_node_size(left) + 1u + d7_tungsten_node_size(right);
    node->height = d7_tungsten_max_int(d7_tungsten_node_height(left), d7_tungsten_node_height(right)) + 1;

    const d7_tungsten_status status = d7_tungsten_entry_copy(context, entry, &node->entry);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(context, node);
        return status;
    }

    *result = node;
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_rotate_left(
    const struct d7_tungsten_assoc_context* context,
    const struct d7_tungsten_assoc_node* node,
    struct d7_tungsten_assoc_node** result)
{
    struct d7_tungsten_assoc_node* right = node->right;
    struct d7_tungsten_assoc_node* new_left = NULL;
    d7_tungsten_status status =
        d7_tungsten_node_create(context, node->left, &node->entry, right->left, &new_left);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_node_create(context, new_left, &right->entry, right->right, result);
    d7_tungsten_node_release(context, new_left);
    return status;
}

static d7_tungsten_status d7_tungsten_rotate_right(
    const struct d7_tungsten_assoc_context* context,
    const struct d7_tungsten_assoc_node* node,
    struct d7_tungsten_assoc_node** result)
{
    struct d7_tungsten_assoc_node* left = node->left;
    struct d7_tungsten_assoc_node* new_right = NULL;
    d7_tungsten_status status =
        d7_tungsten_node_create(context, left->right, &node->entry, node->right, &new_right);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_node_create(context, left->left, &left->entry, new_right, result);
    d7_tungsten_node_release(context, new_right);
    return status;
}

static d7_tungsten_status d7_tungsten_tree_balance(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* left,
    const d7_tungsten_assoc_entry* entry,
    struct d7_tungsten_assoc_node* right,
    struct d7_tungsten_assoc_node** result)
{
    struct d7_tungsten_assoc_node* node = NULL;
    d7_tungsten_status status = d7_tungsten_node_create(context, left, entry, right, &node);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    if (d7_tungsten_node_height(left) > d7_tungsten_node_height(right) + 1) {
        if (d7_tungsten_node_height(left->right) > d7_tungsten_node_height(left->left)) {
            struct d7_tungsten_assoc_node* rotated_left = NULL;
            status = d7_tungsten_rotate_left(context, left, &rotated_left);
            if (status == D7_TUNGSTEN_OK) {
                struct d7_tungsten_assoc_node* adjusted = NULL;
                status = d7_tungsten_node_create(context, rotated_left, entry, right, &adjusted);
                d7_tungsten_node_release(context, rotated_left);
                if (status == D7_TUNGSTEN_OK) {
                    status = d7_tungsten_rotate_right(context, adjusted, result);
                    d7_tungsten_node_release(context, adjusted);
                }
            }
        } else {
            status = d7_tungsten_rotate_right(context, node, result);
        }
        d7_tungsten_node_release(context, node);
        return status;
    }

    if (d7_tungsten_node_height(right) > d7_tungsten_node_height(left) + 1) {
        if (d7_tungsten_node_height(right->left) > d7_tungsten_node_height(right->right)) {
            struct d7_tungsten_assoc_node* rotated_right = NULL;
            status = d7_tungsten_rotate_right(context, right, &rotated_right);
            if (status == D7_TUNGSTEN_OK) {
                struct d7_tungsten_assoc_node* adjusted = NULL;
                status = d7_tungsten_node_create(context, left, entry, rotated_right, &adjusted);
                d7_tungsten_node_release(context, rotated_right);
                if (status == D7_TUNGSTEN_OK) {
                    status = d7_tungsten_rotate_left(context, adjusted, result);
                    d7_tungsten_node_release(context, adjusted);
                }
            }
        } else {
            status = d7_tungsten_rotate_left(context, node, result);
        }
        d7_tungsten_node_release(context, node);
        return status;
    }

    *result = node;
    return D7_TUNGSTEN_OK;
}

static const d7_tungsten_assoc_entry* d7_tungsten_tree_at(
    const struct d7_tungsten_assoc_node* node,
    size_t index)
{
    while (node != NULL) {
        const size_t left_size = d7_tungsten_node_size(node->left);
        if (index < left_size) {
            node = node->left;
        } else if (index == left_size) {
            return &node->entry;
        } else {
            index -= left_size + 1u;
            node = node->right;
        }
    }

    return NULL;
}

static const d7_tungsten_assoc_entry* d7_tungsten_tree_first(const struct d7_tungsten_assoc_node* node)
{
    if (node == NULL) {
        return NULL;
    }

    while (node->left != NULL) {
        node = node->left;
    }
    return &node->entry;
}

static const d7_tungsten_assoc_entry* d7_tungsten_tree_last(const struct d7_tungsten_assoc_node* node)
{
    if (node == NULL) {
        return NULL;
    }

    while (node->right != NULL) {
        node = node->right;
    }
    return &node->entry;
}

static bool d7_tungsten_tree_index_of_stamp(
    const struct d7_tungsten_assoc_node* node,
    int64_t stamp,
    size_t* index)
{
    size_t offset = 0;
    while (node != NULL) {
        const size_t left_size = d7_tungsten_node_size(node->left);
        if (stamp < node->entry.stamp) {
            node = node->left;
        } else if (stamp > node->entry.stamp) {
            offset += left_size + 1u;
            node = node->right;
        } else {
            if (index != NULL) {
                *index = offset + left_size;
            }
            return true;
        }
    }

    return false;
}

static d7_tungsten_status d7_tungsten_tree_remove_first(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* node,
    d7_tungsten_assoc_entry* entry,
    struct d7_tungsten_assoc_node** rest)
{
    if (node == NULL) {
        return D7_TUNGSTEN_EMPTY;
    }

    if (node->left == NULL) {
        d7_tungsten_status status = d7_tungsten_entry_copy(context, &node->entry, entry);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }
        *rest = d7_tungsten_node_retain(node->right);
        return D7_TUNGSTEN_OK;
    }

    d7_tungsten_assoc_entry first;
    (void)memset(&first, 0, sizeof(first));
    struct d7_tungsten_assoc_node* left_rest = NULL;
    d7_tungsten_status status = d7_tungsten_tree_remove_first(context, node->left, &first, &left_rest);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_tree_balance(context, left_rest, &node->entry, node->right, rest);
    d7_tungsten_node_release(context, left_rest);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_entry_destroy(context, &first);
        return status;
    }

    *entry = first;
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_tree_concat(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* left,
    struct d7_tungsten_assoc_node* right,
    struct d7_tungsten_assoc_node** result)
{
    if (left == NULL) {
        *result = d7_tungsten_node_retain(right);
        return D7_TUNGSTEN_OK;
    }
    if (right == NULL) {
        *result = d7_tungsten_node_retain(left);
        return D7_TUNGSTEN_OK;
    }

    if (d7_tungsten_node_height(left) > d7_tungsten_node_height(right) + 1) {
        struct d7_tungsten_assoc_node* merged = NULL;
        d7_tungsten_status status = d7_tungsten_tree_concat(context, left->right, right, &merged);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        status = d7_tungsten_tree_balance(context, left->left, &left->entry, merged, result);
        d7_tungsten_node_release(context, merged);
        return status;
    }

    if (d7_tungsten_node_height(right) > d7_tungsten_node_height(left) + 1) {
        struct d7_tungsten_assoc_node* merged = NULL;
        d7_tungsten_status status = d7_tungsten_tree_concat(context, left, right->left, &merged);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        status = d7_tungsten_tree_balance(context, merged, &right->entry, right->right, result);
        d7_tungsten_node_release(context, merged);
        return status;
    }

    d7_tungsten_assoc_entry first;
    (void)memset(&first, 0, sizeof(first));
    struct d7_tungsten_assoc_node* rest = NULL;
    d7_tungsten_status status = d7_tungsten_tree_remove_first(context, right, &first, &rest);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_tree_balance(context, left, &first, rest, result);
    d7_tungsten_entry_destroy(context, &first);
    d7_tungsten_node_release(context, rest);
    return status;
}

/* Joins two AVL trees of arbitrary height difference around a middle entry,
 * descending the taller side like d7_tungsten_tree_concat. A single
 * d7_tungsten_tree_balance handles only a height difference of at most two,
 * which is insufficient for the subtrees produced by a recursive split. */
static d7_tungsten_status d7_tungsten_tree_join(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* left,
    const d7_tungsten_assoc_entry* entry,
    struct d7_tungsten_assoc_node* right,
    struct d7_tungsten_assoc_node** result)
{
    if (d7_tungsten_node_height(left) > d7_tungsten_node_height(right) + 1) {
        struct d7_tungsten_assoc_node* joined = NULL;
        d7_tungsten_status status = d7_tungsten_tree_join(context, left->right, entry, right, &joined);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        status = d7_tungsten_tree_balance(context, left->left, &left->entry, joined, result);
        d7_tungsten_node_release(context, joined);
        return status;
    }

    if (d7_tungsten_node_height(right) > d7_tungsten_node_height(left) + 1) {
        struct d7_tungsten_assoc_node* joined = NULL;
        d7_tungsten_status status = d7_tungsten_tree_join(context, left, entry, right->left, &joined);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        status = d7_tungsten_tree_balance(context, joined, &right->entry, right->right, result);
        d7_tungsten_node_release(context, joined);
        return status;
    }

    return d7_tungsten_tree_balance(context, left, entry, right, result);
}

static d7_tungsten_status d7_tungsten_tree_split(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* node,
    size_t index,
    struct d7_tungsten_assoc_node** left,
    struct d7_tungsten_assoc_node** right)
{
    if (node == NULL) {
        *left = NULL;
        *right = NULL;
        return D7_TUNGSTEN_OK;
    }

    const size_t left_size = d7_tungsten_node_size(node->left);
    if (index <= left_size) {
        struct d7_tungsten_assoc_node* before = NULL;
        struct d7_tungsten_assoc_node* after = NULL;
        d7_tungsten_status status = d7_tungsten_tree_split(context, node->left, index, &before, &after);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        struct d7_tungsten_assoc_node* new_right = NULL;
        status = d7_tungsten_tree_join(context, after, &node->entry, node->right, &new_right);
        d7_tungsten_node_release(context, after);
        if (status != D7_TUNGSTEN_OK) {
            d7_tungsten_node_release(context, before);
            return status;
        }

        *left = before;
        *right = new_right;
        return D7_TUNGSTEN_OK;
    }

    if (index == left_size + 1u) {
        d7_tungsten_status status = d7_tungsten_tree_join(context, node->left, &node->entry, NULL, left);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }
        *right = d7_tungsten_node_retain(node->right);
        return D7_TUNGSTEN_OK;
    }

    struct d7_tungsten_assoc_node* before = NULL;
    struct d7_tungsten_assoc_node* after = NULL;
    d7_tungsten_status status =
        d7_tungsten_tree_split(context, node->right, index - left_size - 1u, &before, &after);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    struct d7_tungsten_assoc_node* new_left = NULL;
    status = d7_tungsten_tree_join(context, node->left, &node->entry, before, &new_left);
    d7_tungsten_node_release(context, before);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(context, after);
        return status;
    }

    *left = new_left;
    *right = after;
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_tree_insert_at(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* root,
    size_t index,
    const d7_tungsten_assoc_entry* entry,
    struct d7_tungsten_assoc_node** result)
{
    struct d7_tungsten_assoc_node* left = NULL;
    struct d7_tungsten_assoc_node* right = NULL;
    d7_tungsten_status status = d7_tungsten_tree_split(context, root, index, &left, &right);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    struct d7_tungsten_assoc_node* singleton = NULL;
    status = d7_tungsten_node_create(context, NULL, entry, NULL, &singleton);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(context, left);
        d7_tungsten_node_release(context, right);
        return status;
    }

    struct d7_tungsten_assoc_node* joined_left = NULL;
    status = d7_tungsten_tree_concat(context, left, singleton, &joined_left);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_tree_concat(context, joined_left, right, result);
    }

    d7_tungsten_node_release(context, joined_left);
    d7_tungsten_node_release(context, singleton);
    d7_tungsten_node_release(context, left);
    d7_tungsten_node_release(context, right);
    return status;
}

static d7_tungsten_status d7_tungsten_tree_delete_at(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* root,
    size_t index,
    struct d7_tungsten_assoc_node** result)
{
    if (index >= d7_tungsten_node_size(root)) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    struct d7_tungsten_assoc_node* before = NULL;
    struct d7_tungsten_assoc_node* rest = NULL;
    d7_tungsten_status status = d7_tungsten_tree_split(context, root, index, &before, &rest);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    struct d7_tungsten_assoc_node* removed = NULL;
    struct d7_tungsten_assoc_node* after = NULL;
    status = d7_tungsten_tree_split(context, rest, 1, &removed, &after);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_tree_concat(context, before, after, result);
    }

    d7_tungsten_node_release(context, before);
    d7_tungsten_node_release(context, rest);
    d7_tungsten_node_release(context, removed);
    d7_tungsten_node_release(context, after);
    return status;
}

static d7_tungsten_status d7_tungsten_tree_set_at(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* root,
    size_t index,
    const d7_tungsten_assoc_entry* entry,
    struct d7_tungsten_assoc_node** result)
{
    if (root == NULL) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    const size_t left_size = d7_tungsten_node_size(root->left);
    if (index < left_size) {
        struct d7_tungsten_assoc_node* new_left = NULL;
        d7_tungsten_status status = d7_tungsten_tree_set_at(context, root->left, index, entry, &new_left);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        status = d7_tungsten_tree_balance(context, new_left, &root->entry, root->right, result);
        d7_tungsten_node_release(context, new_left);
        return status;
    }

    if (index == left_size) {
        return d7_tungsten_tree_balance(context, root->left, entry, root->right, result);
    }

    struct d7_tungsten_assoc_node* new_right = NULL;
    d7_tungsten_status status =
        d7_tungsten_tree_set_at(context, root->right, index - left_size - 1u, entry, &new_right);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_tree_balance(context, root->left, &root->entry, new_right, result);
    d7_tungsten_node_release(context, new_right);
    return status;
}

static void d7_tungsten_tree_fill_views(
    const struct d7_tungsten_assoc_node* node,
    d7_tungsten_assoc_entry_view* views,
    size_t* index)
{
    if (node == NULL) {
        return;
    }

    d7_tungsten_tree_fill_views(node->left, views, index);
    views[*index].stamp = node->entry.stamp;
    views[*index].key = node->entry.key;
    views[*index].value = node->entry.value;
    ++*index;
    d7_tungsten_tree_fill_views(node->right, views, index);
}

static d7_tungsten_status d7_tungsten_tree_build_relabel(
    const struct d7_tungsten_assoc_context* context,
    const d7_tungsten_assoc_entry_view* views,
    size_t start,
    size_t count,
    struct d7_tungsten_assoc_node** result)
{
    if (count == 0) {
        *result = NULL;
        return D7_TUNGSTEN_OK;
    }

    const size_t left_count = count / 2u;
    const size_t middle = start + left_count;
    const size_t right_count = count - left_count - 1u;
    if (middle > (size_t)(INT64_MAX / D7_TUNGSTEN_STAMP_GAP)) {
        return D7_TUNGSTEN_OVERFLOW;
    }

    struct d7_tungsten_assoc_node* left = NULL;
    struct d7_tungsten_assoc_node* right = NULL;
    d7_tungsten_status status = d7_tungsten_tree_build_relabel(context, views, start, left_count, &left);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_tree_build_relabel(context, views, middle + 1u, right_count, &right);
    }
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(context, left);
        return status;
    }

    d7_tungsten_assoc_entry entry;
    (void)memset(&entry, 0, sizeof(entry));
    status = d7_tungsten_entry_init(
        context,
        (int64_t)middle * (int64_t)D7_TUNGSTEN_STAMP_GAP,
        views[middle].key,
        views[middle].value,
        &entry);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_node_create(context, left, &entry, right, result);
        d7_tungsten_entry_destroy(context, &entry);
    }

    d7_tungsten_node_release(context, left);
    d7_tungsten_node_release(context, right);
    return status;
}

static d7_tungsten_status d7_tungsten_index_from_views(
    const struct d7_tungsten_assoc_context* context,
    const d7_tungsten_assoc_entry_view* views,
    size_t count,
    bool relabel,
    d7_hamt_map* result)
{
    d7_hamt_map map = d7_hamt_map_create(&context->hamt_policy);
    for (size_t index = 0; index != count; ++index) {
        if (relabel && index > (size_t)(INT64_MAX / D7_TUNGSTEN_STAMP_GAP)) {
            d7_hamt_map_destroy(&map);
            return D7_TUNGSTEN_OVERFLOW;
        }

        d7_tungsten_slot slot;
        slot.stamp = relabel ? (int64_t)index * (int64_t)D7_TUNGSTEN_STAMP_GAP : views[index].stamp;
        slot.value = (void*)views[index].value;

        d7_hamt_map next;
        const d7_tungsten_status status =
            d7_tungsten_from_hamt(d7_hamt_map_set(&map, views[index].key, &slot, &next));
        if (status != D7_TUNGSTEN_OK) {
            d7_hamt_map_destroy(&map);
            return status;
        }

        d7_hamt_map_destroy(&map);
        map = next;
    }

    *result = map;
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_index_from_tree(
    const struct d7_tungsten_assoc_context* context,
    struct d7_tungsten_assoc_node* root,
    d7_hamt_map* result)
{
    const size_t count = d7_tungsten_node_size(root);
    if (count == 0) {
        *result = d7_hamt_map_create(&context->hamt_policy);
        return D7_TUNGSTEN_OK;
    }

    d7_tungsten_assoc_entry_view* views =
        (d7_tungsten_assoc_entry_view*)malloc(count * sizeof(*views));
    if (views == NULL) {
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    size_t index = 0;
    d7_tungsten_tree_fill_views(root, views, &index);
    const d7_tungsten_status status = d7_tungsten_index_from_views(context, views, count, false, result);
    free(views);
    return status;
}

static d7_tungsten_status d7_tungsten_rebuild_from_views(
    const struct d7_tungsten_assoc_context* context,
    const d7_tungsten_assoc_entry_view* views,
    size_t count,
    struct d7_tungsten_assoc_node** root,
    d7_hamt_map* index)
{
    struct d7_tungsten_assoc_node* new_root = NULL;
    d7_tungsten_status status = d7_tungsten_tree_build_relabel(context, views, 0, count, &new_root);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    d7_hamt_map new_index;
    status = d7_tungsten_index_from_views(context, views, count, true, &new_index);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(context, new_root);
        return status;
    }

    *root = new_root;
    *index = new_index;
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_association_take_parts(
    const d7_tungsten_association* source,
    struct d7_tungsten_assoc_node* root,
    d7_hamt_map index,
    d7_tungsten_association* result)
{
    struct d7_tungsten_assoc_context* context = source->context;

    /* The result must not alias the source: publishing overwrites result's root, index,
     * and context reference without releasing the prior contents, so an aliased call
     * would leak the source's whole previous version. */
    if (result == NULL || result == source) {
        d7_tungsten_node_release(context, root);
        d7_hamt_map_destroy(&index);
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_context_retain(context);
    result->context = context;
    result->root = root;
    result->index = index;
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_association_copy_empty_like(
    const d7_tungsten_association* source,
    d7_tungsten_association* result)
{
    d7_tungsten_context_retain(source->context);
    result->context = source->context;
    result->root = NULL;
    result->index = d7_hamt_map_create(&source->context->hamt_policy);
    return D7_TUNGSTEN_OK;
}

static bool d7_tungsten_association_valid(const d7_tungsten_association* association)
{
    return association != NULL && association->context != NULL;
}

d7_tungsten_status d7_tungsten_association_init(
    d7_tungsten_association* association,
    const d7_tungsten_association_policy* policy)
{
    if (association == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    (void)memset(association, 0, sizeof(*association));
    struct d7_tungsten_assoc_context* context = NULL;
    d7_tungsten_status status = d7_tungsten_context_create(policy, &context);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    association->context = context;
    association->root = NULL;
    association->index = d7_hamt_map_create(&context->hamt_policy);
    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_pick_stamp(
    struct d7_tungsten_assoc_node* root,
    size_t position,
    int64_t* stamp,
    bool* picked)
{
    *stamp = 0;
    *picked = true;
    const size_t count = d7_tungsten_node_size(root);
    if (count == 0) {
        return D7_TUNGSTEN_OK;
    }

    if (position == 0) {
        const d7_tungsten_assoc_entry* first = d7_tungsten_tree_first(root);
        if (first->stamp < INT64_MIN + (int64_t)D7_TUNGSTEN_STAMP_GAP) {
            *picked = false;
        } else {
            *stamp = first->stamp - (int64_t)D7_TUNGSTEN_STAMP_GAP;
        }
        return D7_TUNGSTEN_OK;
    }

    if (position == count) {
        const d7_tungsten_assoc_entry* last = d7_tungsten_tree_last(root);
        if (last->stamp > INT64_MAX - (int64_t)D7_TUNGSTEN_STAMP_GAP) {
            *picked = false;
        } else {
            *stamp = last->stamp + (int64_t)D7_TUNGSTEN_STAMP_GAP;
        }
        return D7_TUNGSTEN_OK;
    }

    const d7_tungsten_assoc_entry* left = d7_tungsten_tree_at(root, position - 1u);
    const d7_tungsten_assoc_entry* right = d7_tungsten_tree_at(root, position);
    if (left == NULL || right == NULL) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    /* Subtract in uint64_t: the signed difference can exceed INT64_MAX for
     * spans straddling the label range (matches the C# unchecked cast). */
    const uint64_t gap = (uint64_t)right->stamp - (uint64_t)left->stamp;
    if (gap < 2u) {
        *picked = false;
    } else {
        *stamp = left->stamp + (int64_t)(gap / 2u);
    }

    return D7_TUNGSTEN_OK;
}

static d7_tungsten_status d7_tungsten_insert_absent(
    const d7_tungsten_association* association,
    struct d7_tungsten_assoc_node* root,
    const d7_hamt_map* index,
    size_t position,
    const void* key,
    const void* value,
    d7_tungsten_association* result)
{
    int64_t stamp = 0;
    bool picked = false;
    d7_tungsten_status status = d7_tungsten_pick_stamp(root, position, &stamp, &picked);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    if (!picked) {
        const size_t count = d7_tungsten_node_size(root);
        d7_tungsten_assoc_entry_view* views =
            (d7_tungsten_assoc_entry_view*)malloc((count + 1u) * sizeof(*views));
        if (views == NULL) {
            return D7_TUNGSTEN_OUT_OF_MEMORY;
        }

        /* Collect the existing entries with one in-order walk (the per-index
         * tree descent made this rare relabel path O(n log n)), then splice
         * the new pair in at its position. */
        size_t written = 0;
        d7_tungsten_tree_fill_views(root, views, &written);
        (void)memmove(&views[position + 1u], &views[position], (count - position) * sizeof(*views));
        views[position].stamp = 0;
        views[position].key = key;
        views[position].value = value;
        written = count + 1u;

        struct d7_tungsten_assoc_node* new_root = NULL;
        d7_hamt_map new_index;
        status = d7_tungsten_rebuild_from_views(association->context, views, written, &new_root, &new_index);
        free(views);
        if (status != D7_TUNGSTEN_OK) {
            return status;
        }

        return d7_tungsten_association_take_parts(association, new_root, new_index, result);
    }

    d7_tungsten_assoc_entry entry;
    (void)memset(&entry, 0, sizeof(entry));
    status = d7_tungsten_entry_init(association->context, stamp, key, value, &entry);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    struct d7_tungsten_assoc_node* new_root = NULL;
    status = d7_tungsten_tree_insert_at(association->context, root, position, &entry, &new_root);
    d7_tungsten_entry_destroy(association->context, &entry);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    d7_tungsten_slot slot;
    slot.stamp = stamp;
    slot.value = (void*)value;
    d7_hamt_map new_index;
    status = d7_tungsten_from_hamt(d7_hamt_map_set(index, key, &slot, &new_index));
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(association->context, new_root);
        return status;
    }

    return d7_tungsten_association_take_parts(association, new_root, new_index, result);
}

d7_tungsten_status d7_tungsten_association_from_pairs(
    d7_tungsten_association* association,
    const d7_tungsten_association_policy* policy,
    const d7_tungsten_assoc_pair* pairs,
    size_t count)
{
    if (association == NULL || (pairs == NULL && count != 0)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_association_init(association, policy);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        d7_tungsten_association next;
        status = d7_tungsten_association_set_item(association, pairs[index].key, pairs[index].value, &next);
        if (status != D7_TUNGSTEN_OK) {
            d7_tungsten_association_dispose(association);
            return status;
        }

        d7_tungsten_association_dispose(association);
        d7_tungsten_association_move(association, &next);
    }

    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_association_copy(
    const d7_tungsten_association* source,
    d7_tungsten_association* destination)
{
    /* An aliased copy would retain the context, root, and index a second time and then
     * overwrite the only handles that could release the extra references. */
    if (!d7_tungsten_association_valid(source) || destination == NULL || destination == source) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_context_retain(source->context);
    destination->context = source->context;
    destination->root = d7_tungsten_node_retain(source->root);
    destination->index = d7_hamt_map_clone(&source->index);
    return D7_TUNGSTEN_OK;
}

void d7_tungsten_association_move(d7_tungsten_association* destination, d7_tungsten_association* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void d7_tungsten_association_dispose(d7_tungsten_association* association)
{
    if (association == NULL || association->context == NULL) {
        return;
    }

    struct d7_tungsten_assoc_context* context = association->context;
    d7_hamt_map_destroy(&association->index);
    d7_tungsten_node_release(context, association->root);
    (void)memset(association, 0, sizeof(*association));
    d7_tungsten_context_release(context);
}

bool d7_tungsten_association_empty(const d7_tungsten_association* association)
{
    return !d7_tungsten_association_valid(association) || association->root == NULL;
}

size_t d7_tungsten_association_size(const d7_tungsten_association* association)
{
    return d7_tungsten_association_valid(association) ? d7_tungsten_node_size(association->root) : 0u;
}

bool d7_tungsten_association_contains_key(const d7_tungsten_association* association, const void* key)
{
    return d7_tungsten_association_valid(association) && key != NULL &&
        d7_hamt_map_contains_key(&association->index, key);
}

bool d7_tungsten_association_try_get(
    const d7_tungsten_association* association,
    const void* key,
    void* value)
{
    if (!d7_tungsten_association_valid(association) || key == NULL) {
        return false;
    }

    const void* slot_value = NULL;
    if (!d7_hamt_map_try_get(&association->index, key, &slot_value)) {
        return false;
    }

    if (value != NULL) {
        const d7_tungsten_slot* slot = (const d7_tungsten_slot*)slot_value;
        d7_tungsten_value_copy(&association->context->policy.value_type, value, slot->value);
    }
    return true;
}

bool d7_tungsten_association_try_get_key(
    const d7_tungsten_association* association,
    const void* equal_key,
    void* actual_key)
{
    if (!d7_tungsten_association_valid(association) || equal_key == NULL) {
        return false;
    }

    const void* found_key = NULL;
    const bool found = d7_hamt_map_try_get_key(&association->index, equal_key, &found_key);
    if (found && actual_key != NULL) {
        d7_tungsten_value_copy(&association->context->policy.key_type, actual_key, found_key);
    }
    return found;
}

static d7_tungsten_status d7_tungsten_association_copy_entry(
    const d7_tungsten_association* association,
    const d7_tungsten_assoc_entry* entry,
    void* key,
    void* value)
{
    if (entry == NULL) {
        return D7_TUNGSTEN_EMPTY;
    }

    if (key != NULL) {
        d7_tungsten_value_copy(&association->context->policy.key_type, key, entry->key);
    }
    if (value != NULL) {
        d7_tungsten_value_copy(&association->context->policy.value_type, value, entry->value);
    }
    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_association_front(
    const d7_tungsten_association* association,
    void* key,
    void* value)
{
    if (!d7_tungsten_association_valid(association)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }
    return d7_tungsten_association_copy_entry(association, d7_tungsten_tree_first(association->root), key, value);
}

d7_tungsten_status d7_tungsten_association_back(
    const d7_tungsten_association* association,
    void* key,
    void* value)
{
    if (!d7_tungsten_association_valid(association)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }
    return d7_tungsten_association_copy_entry(association, d7_tungsten_tree_last(association->root), key, value);
}

d7_tungsten_status d7_tungsten_association_entry_at(
    const d7_tungsten_association* association,
    size_t index,
    void* key,
    void* value)
{
    if (!d7_tungsten_association_valid(association)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }
    if (index >= d7_tungsten_node_size(association->root)) {
        /* Distinguish a bad index from an empty association (C# GetAt throws
         * ArgumentOutOfRangeException; front/back keep the EMPTY status). */
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }
    return d7_tungsten_association_copy_entry(association, d7_tungsten_tree_at(association->root, index), key, value);
}

bool d7_tungsten_association_index_of_key(
    const d7_tungsten_association* association,
    const void* key,
    size_t* index)
{
    if (!d7_tungsten_association_valid(association) || key == NULL) {
        return false;
    }

    const void* slot_value = NULL;
    if (!d7_hamt_map_try_get(&association->index, key, &slot_value)) {
        return false;
    }

    const d7_tungsten_slot* slot = (const d7_tungsten_slot*)slot_value;
    return d7_tungsten_tree_index_of_stamp(association->root, slot->stamp, index);
}

d7_tungsten_status d7_tungsten_association_set_item(
    const d7_tungsten_association* association,
    const void* key,
    const void* value,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    const void* slot_value = NULL;
    if (!d7_hamt_map_try_get(&association->index, key, &slot_value)) {
        return d7_tungsten_insert_absent(
            association,
            association->root,
            &association->index,
            d7_tungsten_node_size(association->root),
            key,
            value,
            result);
    }

    const d7_tungsten_slot* slot = (const d7_tungsten_slot*)slot_value;
    if (d7_tungsten_value_equal(
            &association->context->policy.value_type,
            association->context->policy.value_equal,
            association->context->policy.context,
            slot->value,
            value)) {
        return d7_tungsten_association_copy(association, result);
    }

    size_t position = 0;
    if (!d7_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    const d7_tungsten_assoc_entry* old_entry = d7_tungsten_tree_at(association->root, position);
    d7_tungsten_assoc_entry replacement;
    (void)memset(&replacement, 0, sizeof(replacement));
    d7_tungsten_status status =
        d7_tungsten_entry_init(association->context, slot->stamp, old_entry->key, value, &replacement);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    struct d7_tungsten_assoc_node* new_root = NULL;
    status = d7_tungsten_tree_set_at(association->context, association->root, position, &replacement, &new_root);
    d7_tungsten_entry_destroy(association->context, &replacement);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    d7_tungsten_slot new_slot;
    new_slot.stamp = slot->stamp;
    new_slot.value = (void*)value;
    d7_hamt_map new_index;
    status = d7_tungsten_from_hamt(d7_hamt_map_set(&association->index, key, &new_slot, &new_index));
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(association->context, new_root);
        return status;
    }

    return d7_tungsten_association_take_parts(association, new_root, new_index, result);
}

d7_tungsten_status d7_tungsten_association_set_items(
    const d7_tungsten_association* association,
    const d7_tungsten_assoc_pair* pairs,
    size_t count,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || result == NULL || (pairs == NULL && count != 0)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_association_copy(association, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        d7_tungsten_association next;
        status = d7_tungsten_association_set_item(result, pairs[index].key, pairs[index].value, &next);
        if (status != D7_TUNGSTEN_OK) {
            d7_tungsten_association_dispose(result);
            return status;
        }

        d7_tungsten_association_dispose(result);
        d7_tungsten_association_move(result, &next);
    }

    return D7_TUNGSTEN_OK;
}

static void d7_tungsten_collect_pair_visit(const void* key, const void* value, void* context)
{
    d7_tungsten_assoc_pair** cursor = (d7_tungsten_assoc_pair**)context;
    (*cursor)->key = key;
    (*cursor)->value = value;
    ++*cursor;
}

d7_tungsten_status d7_tungsten_association_join(
    const d7_tungsten_association* left,
    const d7_tungsten_association* right,
    d7_tungsten_association* result)
{
    /* The result must not alias either operand: publishing overwrites result in place
     * (see d7_tungsten_association_take_parts). */
    if (!d7_tungsten_association_valid(left) || !d7_tungsten_association_valid(right) || result == NULL ||
        result == left || result == right) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* The join copies right's raw key/value bytes under left's type policy;
     * mismatched payload sizes would read out of bounds. */
    if (left->context->policy.key_type.size != right->context->policy.key_type.size ||
        left->context->policy.value_type.size != right->context->policy.value_type.size) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    const size_t count = d7_tungsten_association_size(right);
    if (count == 0) {
        return d7_tungsten_association_copy(left, result);
    }

    d7_tungsten_assoc_pair* pairs = (d7_tungsten_assoc_pair*)malloc(count * sizeof(*pairs));
    if (pairs == NULL) {
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    d7_tungsten_assoc_pair* cursor = pairs;
    d7_tungsten_status status = d7_tungsten_association_visit(right, d7_tungsten_collect_pair_visit, &cursor);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_association_set_items(left, pairs, count, result);
    }

    free(pairs);
    return status;
}

static d7_tungsten_status d7_tungsten_remove_existing_for_insert(
    const d7_tungsten_association* association,
    const void* key,
    size_t requested,
    struct d7_tungsten_assoc_node** root,
    d7_hamt_map* index,
    size_t* adjusted)
{
    const void* slot_value = NULL;
    if (!d7_hamt_map_try_get(&association->index, key, &slot_value)) {
        *root = d7_tungsten_node_retain(association->root);
        *index = d7_hamt_map_clone(&association->index);
        *adjusted = requested;
        return D7_TUNGSTEN_OK;
    }

    const d7_tungsten_slot* slot = (const d7_tungsten_slot*)slot_value;
    size_t old_position = 0;
    if (!d7_tungsten_tree_index_of_stamp(association->root, slot->stamp, &old_position)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status =
        d7_tungsten_tree_delete_at(association->context, association->root, old_position, root);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    status = d7_tungsten_from_hamt(d7_hamt_map_remove(&association->index, key, index));
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(association->context, *root);
        /* Null the released pointer: callers release *root unconditionally. */
        *root = NULL;
        return status;
    }

    *adjusted = old_position < requested ? requested - 1u : requested;
    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_association_append(
    const d7_tungsten_association* association,
    const void* key,
    const void* value,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* Rule-2 no-op fast path (matches the C# reference and its spec): a key
     * that is already last with an equal value returns the receiver, keeping
     * the stored key payload and consuming no stamp. */
    const void* existing_slot = NULL;
    if (d7_hamt_map_try_get(&association->index, key, &existing_slot)) {
        const d7_tungsten_slot* slot = (const d7_tungsten_slot*)existing_slot;
        size_t position = 0;
        const size_t size = d7_tungsten_node_size(association->root);
        if (d7_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position) &&
            position + 1u == size &&
            d7_tungsten_value_equal(
                &association->context->policy.value_type,
                association->context->policy.value_equal,
                association->context->policy.context,
                slot->value,
                value)) {
            return d7_tungsten_association_copy(association, result);
        }
    }

    struct d7_tungsten_assoc_node* root = NULL;
    d7_hamt_map index = {0};
    size_t adjusted = 0;
    d7_tungsten_status status = d7_tungsten_remove_existing_for_insert(
        association,
        key,
        d7_tungsten_node_size(association->root),
        &root,
        &index,
        &adjusted);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_insert_absent(association, root, &index, d7_tungsten_node_size(root), key, value, result);
    }
    d7_tungsten_node_release(association->context, root);
    d7_hamt_map_destroy(&index);
    return status;
}

d7_tungsten_status d7_tungsten_association_prepend(
    const d7_tungsten_association* association,
    const void* key,
    const void* value,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* Rule-2 no-op fast path: a key already first with an equal value
     * returns the receiver (see append). */
    const void* existing_slot = NULL;
    if (d7_hamt_map_try_get(&association->index, key, &existing_slot)) {
        const d7_tungsten_slot* slot = (const d7_tungsten_slot*)existing_slot;
        size_t position = 0;
        if (d7_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position) &&
            position == 0 &&
            d7_tungsten_value_equal(
                &association->context->policy.value_type,
                association->context->policy.value_equal,
                association->context->policy.context,
                slot->value,
                value)) {
            return d7_tungsten_association_copy(association, result);
        }
    }

    struct d7_tungsten_assoc_node* root = NULL;
    d7_hamt_map index = {0};
    size_t adjusted = 0;
    d7_tungsten_status status =
        d7_tungsten_remove_existing_for_insert(association, key, 0, &root, &index, &adjusted);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_insert_absent(association, root, &index, 0, key, value, result);
    }
    d7_tungsten_node_release(association->context, root);
    d7_hamt_map_destroy(&index);
    return status;
}

d7_tungsten_status d7_tungsten_association_insert_at(
    const d7_tungsten_association* association,
    size_t index,
    const void* key,
    const void* value,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > d7_tungsten_node_size(association->root)) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    struct d7_tungsten_assoc_node* root = NULL;
    d7_hamt_map index_side = {0};
    size_t adjusted = 0;
    d7_tungsten_status status =
        d7_tungsten_remove_existing_for_insert(association, key, index, &root, &index_side, &adjusted);
    if (status == D7_TUNGSTEN_OK) {
        status = d7_tungsten_insert_absent(association, root, &index_side, adjusted, key, value, result);
    }
    d7_tungsten_node_release(association->context, root);
    d7_hamt_map_destroy(&index_side);
    return status;
}

d7_tungsten_status d7_tungsten_association_try_remove(
    const d7_tungsten_association* association,
    const void* key,
    bool* removed,
    void* value,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || key == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_hamt_map index;
    bool local_removed = false;
    const void* removed_value = NULL;
    d7_tungsten_status status = d7_tungsten_from_hamt(
        d7_hamt_map_try_remove(&association->index, key, &index, &local_removed, &removed_value));
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    if (!local_removed) {
        d7_hamt_map_destroy(&index);
        if (removed != NULL) {
            *removed = false;
        }
        return d7_tungsten_association_copy(association, result);
    }

    const d7_tungsten_slot* slot = (const d7_tungsten_slot*)removed_value;
    if (value != NULL) {
        d7_tungsten_value_copy(&association->context->policy.value_type, value, slot->value);
    }

    size_t position = 0;
    if (!d7_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position)) {
        d7_hamt_map_destroy(&index);
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    struct d7_tungsten_assoc_node* root = NULL;
    status = d7_tungsten_tree_delete_at(association->context, association->root, position, &root);
    if (status != D7_TUNGSTEN_OK) {
        d7_hamt_map_destroy(&index);
        return status;
    }

    if (removed != NULL) {
        *removed = true;
    }
    return d7_tungsten_association_take_parts(association, root, index, result);
}

d7_tungsten_status d7_tungsten_association_remove(
    const d7_tungsten_association* association,
    const void* key,
    d7_tungsten_association* result)
{
    bool removed = false;
    return d7_tungsten_association_try_remove(association, key, &removed, NULL, result);
}

d7_tungsten_status d7_tungsten_association_remove_keys(
    const d7_tungsten_association* association,
    const void* const* keys,
    size_t count,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || result == NULL || (keys == NULL && count != 0)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_association_copy(association, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        d7_tungsten_association next;
        status = d7_tungsten_association_remove(result, keys[index], &next);
        if (status != D7_TUNGSTEN_OK) {
            d7_tungsten_association_dispose(result);
            return status;
        }

        d7_tungsten_association_dispose(result);
        d7_tungsten_association_move(result, &next);
    }

    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_association_key_take(
    const d7_tungsten_association* association,
    const void* const* keys,
    size_t count,
    d7_tungsten_association* result)
{
    /* copy_empty_like overwrites result's root and index in place, so an aliased result
     * would leak the source's previous version before the take loop reads it. */
    if (!d7_tungsten_association_valid(association) || result == NULL || result == association ||
        (keys == NULL && count != 0)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_status status = d7_tungsten_association_copy_empty_like(association, result);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        /* A NULL requested key can never be stored (every keyed entry point
         * rejects NULL keys); skip it before it reaches the hash callback. */
        if (keys[index] == NULL) {
            continue;
        }

        if (d7_tungsten_association_contains_key(result, keys[index])) {
            continue;
        }

        const void* slot_value = NULL;
        const void* actual_key = NULL;
        if (!d7_hamt_map_try_get(&association->index, keys[index], &slot_value) ||
            !d7_hamt_map_try_get_key(&association->index, keys[index], &actual_key)) {
            continue;
        }

        const d7_tungsten_slot* slot = (const d7_tungsten_slot*)slot_value;
        d7_tungsten_association next;
        status = d7_tungsten_insert_absent(
            result,
            result->root,
            &result->index,
            d7_tungsten_node_size(result->root),
            actual_key,
            slot->value,
            &next);
        if (status != D7_TUNGSTEN_OK) {
            d7_tungsten_association_dispose(result);
            return status;
        }

        d7_tungsten_association_dispose(result);
        d7_tungsten_association_move(result, &next);
    }

    return D7_TUNGSTEN_OK;
}

d7_tungsten_status d7_tungsten_association_remove_at(
    const d7_tungsten_association* association,
    size_t index,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    const d7_tungsten_assoc_entry* entry = d7_tungsten_tree_at(association->root, index);
    if (entry == NULL) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    struct d7_tungsten_assoc_node* root = NULL;
    d7_tungsten_status status = d7_tungsten_tree_delete_at(association->context, association->root, index, &root);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    d7_hamt_map index_side;
    status = d7_tungsten_from_hamt(d7_hamt_map_remove(&association->index, entry->key, &index_side));
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(association->context, root);
        return status;
    }

    return d7_tungsten_association_take_parts(association, root, index_side, result);
}

static d7_tungsten_status d7_tungsten_index_remove_tree(
    const struct d7_tungsten_assoc_context* context,
    d7_hamt_map* index,
    const struct d7_tungsten_assoc_node* node)
{
    if (node == NULL) {
        return D7_TUNGSTEN_OK;
    }

    d7_tungsten_status status = d7_tungsten_index_remove_tree(context, index, node->left);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    d7_hamt_map next;
    status = d7_tungsten_from_hamt(d7_hamt_map_remove(index, node->entry.key, &next));
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }
    d7_hamt_map_destroy(index);
    *index = next;

    return d7_tungsten_index_remove_tree(context, index, node->right);
}

d7_tungsten_status d7_tungsten_association_slice(
    const d7_tungsten_association* association,
    size_t index,
    size_t count,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > d7_tungsten_node_size(association->root) ||
        count > d7_tungsten_node_size(association->root) - index) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    if (count == d7_tungsten_node_size(association->root)) {
        return d7_tungsten_association_copy(association, result);
    }

    struct d7_tungsten_assoc_node* before = NULL;
    struct d7_tungsten_assoc_node* rest = NULL;
    d7_tungsten_status status =
        d7_tungsten_tree_split(association->context, association->root, index, &before, &rest);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    struct d7_tungsten_assoc_node* kept = NULL;
    struct d7_tungsten_assoc_node* after = NULL;
    status = d7_tungsten_tree_split(association->context, rest, count, &kept, &after);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(association->context, before);
        d7_tungsten_node_release(association->context, rest);
        return status;
    }

    d7_hamt_map index_side;
    const size_t removed_count = d7_tungsten_node_size(association->root) - d7_tungsten_node_size(kept);
    if (d7_tungsten_node_size(kept) <= removed_count) {
        status = d7_tungsten_index_from_tree(association->context, kept, &index_side);
    } else {
        index_side = d7_hamt_map_clone(&association->index);
        status = d7_tungsten_index_remove_tree(association->context, &index_side, before);
        if (status == D7_TUNGSTEN_OK) {
            status = d7_tungsten_index_remove_tree(association->context, &index_side, after);
        }
        if (status != D7_TUNGSTEN_OK) {
            d7_hamt_map_destroy(&index_side);
        }
    }

    d7_tungsten_node_release(association->context, before);
    d7_tungsten_node_release(association->context, rest);
    d7_tungsten_node_release(association->context, after);
    if (status != D7_TUNGSTEN_OK) {
        d7_tungsten_node_release(association->context, kept);
        return status;
    }

    return d7_tungsten_association_take_parts(association, kept, index_side, result);
}

d7_tungsten_status d7_tungsten_association_take(
    const d7_tungsten_association* association,
    size_t count,
    d7_tungsten_association* result)
{
    return d7_tungsten_association_slice(association, 0, count, result);
}

d7_tungsten_status d7_tungsten_association_drop(
    const d7_tungsten_association* association,
    size_t count,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association)) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (count > d7_tungsten_node_size(association->root)) {
        return D7_TUNGSTEN_OUT_OF_RANGE;
    }

    return d7_tungsten_association_slice(
        association,
        count,
        d7_tungsten_node_size(association->root) - count,
        result);
}

d7_tungsten_status d7_tungsten_association_reverse(
    const d7_tungsten_association* association,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    const size_t count = d7_tungsten_node_size(association->root);
    if (count <= 1) {
        return d7_tungsten_association_copy(association, result);
    }

    d7_tungsten_assoc_entry_view* views = (d7_tungsten_assoc_entry_view*)malloc(count * sizeof(*views));
    if (views == NULL) {
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    size_t index = 0;
    d7_tungsten_tree_fill_views(association->root, views, &index);
    for (size_t left = 0, right = count - 1u; left < right; ++left, --right) {
        const d7_tungsten_assoc_entry_view temp = views[left];
        views[left] = views[right];
        views[right] = temp;
    }

    struct d7_tungsten_assoc_node* root = NULL;
    d7_hamt_map index_side;
    d7_tungsten_status status = d7_tungsten_rebuild_from_views(association->context, views, count, &root, &index_side);
    free(views);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    return d7_tungsten_association_take_parts(association, root, index_side, result);
}

static int d7_tungsten_compare_views(
    const d7_tungsten_assoc_entry_view* left,
    const d7_tungsten_assoc_entry_view* right,
    const d7_tungsten_sort_context* context)
{
    const void* left_value = context->by_key ? left->key : left->value;
    const void* right_value = context->by_key ? right->key : right->value;
    const int comparison = context->compare(left_value, right_value, context->compare_context);
    if (comparison != 0) {
        return comparison;
    }

    return (left->stamp > right->stamp) - (left->stamp < right->stamp);
}

static void d7_tungsten_merge_sort_views(
    d7_tungsten_assoc_entry_view* values,
    d7_tungsten_assoc_entry_view* scratch,
    size_t count,
    const d7_tungsten_sort_context* context)
{
    if (count < 2) {
        return;
    }

    const size_t middle = count / 2u;
    d7_tungsten_merge_sort_views(values, scratch, middle, context);
    d7_tungsten_merge_sort_views(values + middle, scratch + middle, count - middle, context);

    size_t left = 0;
    size_t right = middle;
    size_t output = 0;
    while (left != middle && right != count) {
        if (d7_tungsten_compare_views(&values[left], &values[right], context) <= 0) {
            scratch[output++] = values[left++];
        } else {
            scratch[output++] = values[right++];
        }
    }
    while (left != middle) {
        scratch[output++] = values[left++];
    }
    while (right != count) {
        scratch[output++] = values[right++];
    }
    (void)memcpy(values, scratch, count * sizeof(*values));
}

static d7_tungsten_status d7_tungsten_association_sort_core(
    const d7_tungsten_association* association,
    ft_compare_fn compare,
    void* compare_context,
    bool by_key,
    d7_tungsten_association* result)
{
    if (!d7_tungsten_association_valid(association) || compare == NULL || result == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    const size_t count = d7_tungsten_node_size(association->root);
    if (count <= 1) {
        return d7_tungsten_association_copy(association, result);
    }

    d7_tungsten_assoc_entry_view* views = (d7_tungsten_assoc_entry_view*)malloc(count * sizeof(*views));
    d7_tungsten_assoc_entry_view* scratch = (d7_tungsten_assoc_entry_view*)malloc(count * sizeof(*scratch));
    if (views == NULL || scratch == NULL) {
        free(views);
        free(scratch);
        return D7_TUNGSTEN_OUT_OF_MEMORY;
    }

    size_t index = 0;
    d7_tungsten_tree_fill_views(association->root, views, &index);

    d7_tungsten_sort_context context;
    context.compare = compare;
    context.compare_context = compare_context;
    context.by_key = by_key;
    d7_tungsten_merge_sort_views(views, scratch, count, &context);
    free(scratch);

    struct d7_tungsten_assoc_node* root = NULL;
    d7_hamt_map index_side;
    d7_tungsten_status status = d7_tungsten_rebuild_from_views(association->context, views, count, &root, &index_side);
    free(views);
    if (status != D7_TUNGSTEN_OK) {
        return status;
    }

    return d7_tungsten_association_take_parts(association, root, index_side, result);
}

d7_tungsten_status d7_tungsten_association_key_sort(
    const d7_tungsten_association* association,
    ft_compare_fn compare_key,
    void* compare_context,
    d7_tungsten_association* result)
{
    return d7_tungsten_association_sort_core(association, compare_key, compare_context, true, result);
}

d7_tungsten_status d7_tungsten_association_sort(
    const d7_tungsten_association* association,
    ft_compare_fn compare_value,
    void* compare_context,
    d7_tungsten_association* result)
{
    return d7_tungsten_association_sort_core(association, compare_value, compare_context, false, result);
}

static void d7_tungsten_tree_visit_entries(
    const struct d7_tungsten_assoc_node* node,
    d7_tungsten_assoc_visit_fn visitor,
    void* context)
{
    if (node == NULL) {
        return;
    }

    d7_tungsten_tree_visit_entries(node->left, visitor, context);
    visitor(node->entry.key, node->entry.value, context);
    d7_tungsten_tree_visit_entries(node->right, visitor, context);
}

d7_tungsten_status d7_tungsten_association_visit(
    const d7_tungsten_association* association,
    d7_tungsten_assoc_visit_fn visitor,
    void* context)
{
    if (!d7_tungsten_association_valid(association) || visitor == NULL) {
        return D7_TUNGSTEN_INVALID_ARGUMENT;
    }

    d7_tungsten_tree_visit_entries(association->root, visitor, context);
    return D7_TUNGSTEN_OK;
}
