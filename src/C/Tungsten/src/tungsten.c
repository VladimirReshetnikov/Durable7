#include <tools/data_structures/tungsten/tungsten.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    TDS_TUNGSTEN_STAMP_GAP = 1 << 20
};

typedef struct tds_tungsten_slot {
    int64_t stamp;
    void* value;
} tds_tungsten_slot;

typedef struct tds_tungsten_assoc_entry {
    int64_t stamp;
    void* key;
    void* value;
} tds_tungsten_assoc_entry;

typedef struct tds_tungsten_assoc_entry_view {
    int64_t stamp;
    const void* key;
    const void* value;
} tds_tungsten_assoc_entry_view;

struct tds_tungsten_assoc_context {
    size_t ref_count;
    tds_tungsten_association_policy policy;
    tds_hamt_policy hamt_policy;
};

struct tds_tungsten_assoc_node {
    size_t ref_count;
    size_t size;
    int height;
    struct tds_tungsten_assoc_node* left;
    struct tds_tungsten_assoc_node* right;
    tds_tungsten_assoc_entry entry;
};

typedef struct tds_tungsten_sort_context {
    ft_compare_fn compare;
    void* compare_context;
    bool by_key;
} tds_tungsten_sort_context;

static tds_tungsten_status tds_tungsten_from_ft(ft_status status)
{
    switch (status) {
    case FT_STATUS_OK:
        return TDS_TUNGSTEN_OK;
    case FT_STATUS_OUT_OF_RANGE:
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    case FT_STATUS_EMPTY:
        return TDS_TUNGSTEN_EMPTY;
    case FT_STATUS_NOT_FOUND:
        return TDS_TUNGSTEN_NOT_FOUND;
    case FT_STATUS_NO_MEMORY:
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    case FT_STATUS_OVERFLOW:
        return TDS_TUNGSTEN_OVERFLOW;
    case FT_STATUS_ALREADY_EXISTS:
        return TDS_TUNGSTEN_DUPLICATE_KEY;
    case FT_STATUS_INVALID_ARGUMENT:
    default:
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }
}

static tds_tungsten_status tds_tungsten_from_hamt(tds_hamt_status status)
{
    switch (status) {
    case TDS_HAMT_OK:
        return TDS_TUNGSTEN_OK;
    case TDS_HAMT_OUT_OF_MEMORY:
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    case TDS_HAMT_DUPLICATE_KEY:
        return TDS_TUNGSTEN_DUPLICATE_KEY;
    case TDS_HAMT_INVALID_ARGUMENT:
    default:
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }
}

static void tds_tungsten_value_copy(const ft_value_type* type, void* destination, const void* source)
{
    if (type->copy != NULL) {
        type->copy(destination, source, type->context);
    } else {
        (void)memcpy(destination, source, type->size);
    }
}

static void tds_tungsten_value_destroy(const ft_value_type* type, void* value)
{
    if (value != NULL && type->destroy != NULL) {
        type->destroy(value, type->context);
    }
}

static void* tds_tungsten_value_alloc_copy(const ft_value_type* type, const void* source)
{
    void* value = malloc(type->size == 0 ? 1u : type->size);
    if (value == NULL) {
        return NULL;
    }

    tds_tungsten_value_copy(type, value, source);
    return value;
}

static void tds_tungsten_value_free(const ft_value_type* type, void* value)
{
    if (value != NULL) {
        tds_tungsten_value_destroy(type, value);
        free(value);
    }
}

static bool tds_tungsten_value_equal(
    const ft_value_type* type,
    tds_tungsten_equal_fn equal,
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

static bool tds_tungsten_list_is_valid(const tds_tungsten_list* list)
{
    return list != NULL && list->items.policy != NULL && list->items.rep != NULL;
}

static tds_tungsten_status tds_tungsten_list_prepare(
    const tds_tungsten_list* source,
    tds_tungsten_list* result)
{
    /* Unlike the HAMT surfaces, result must not alias the source: prepare zeroes the
     * result before the source is read, which would leak the source's deque rep and
     * leave the caller holding an empty list even when the operation then fails. */
    if (!tds_tungsten_list_is_valid(source) || result == NULL || result == source) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    (void)memset(result, 0, sizeof(*result));
    result->policy = source->policy;
    return TDS_TUNGSTEN_OK;
}

static void tds_tungsten_list_rebind(tds_tungsten_list* list)
{
    if (list != NULL && list->items.rep != NULL) {
        list->items.policy = &list->policy;
    }
}

void tds_tungsten_association_policy_init(
    tds_tungsten_association_policy* policy,
    const ft_value_type* key_type,
    const ft_value_type* value_type,
    tds_tungsten_hash_fn hash_key,
    tds_tungsten_equal_fn key_equal,
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

tds_tungsten_status tds_tungsten_list_init(tds_tungsten_list* list, const ft_value_type* value_type)
{
    if (list == NULL || value_type == NULL || value_type->size == 0) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    (void)memset(list, 0, sizeof(*list));
    ft_tree_policy_init_size(&list->policy, value_type);
    const ft_status status = ft_persistent_deque_init(&list->items, &list->policy);
    if (status != FT_STATUS_OK) {
        (void)memset(list, 0, sizeof(*list));
    }
    return tds_tungsten_from_ft(status);
}

tds_tungsten_status tds_tungsten_list_from_array(
    tds_tungsten_list* list,
    const ft_value_type* value_type,
    const void* values,
    size_t count)
{
    if (list == NULL || value_type == NULL || value_type->size == 0 || (values == NULL && count != 0)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_list_init(list, value_type);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    const unsigned char* bytes = (const unsigned char*)values;
    for (size_t index = 0; index != count; ++index) {
        tds_tungsten_list next;
        status = tds_tungsten_list_push_back(list, bytes + index * value_type->size, &next);
        if (status != TDS_TUNGSTEN_OK) {
            tds_tungsten_list_dispose(list);
            return status;
        }

        tds_tungsten_list_dispose(list);
        tds_tungsten_list_move(list, &next);
    }

    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_list_copy(const tds_tungsten_list* source, tds_tungsten_list* destination)
{
    tds_tungsten_status status = tds_tungsten_list_prepare(source, destination);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_ft(ft_persistent_deque_copy(&source->items, &destination->items));
    if (status != TDS_TUNGSTEN_OK) {
        (void)memset(destination, 0, sizeof(*destination));
        return status;
    }

    tds_tungsten_list_rebind(destination);
    return TDS_TUNGSTEN_OK;
}

void tds_tungsten_list_move(tds_tungsten_list* destination, tds_tungsten_list* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    tds_tungsten_list_rebind(destination);
    (void)memset(source, 0, sizeof(*source));
}

void tds_tungsten_list_dispose(tds_tungsten_list* list)
{
    if (list == NULL) {
        return;
    }

    ft_persistent_deque_dispose(&list->items);
    (void)memset(list, 0, sizeof(*list));
}

bool tds_tungsten_list_empty(const tds_tungsten_list* list)
{
    return !tds_tungsten_list_is_valid(list) || ft_persistent_deque_empty(&list->items);
}

size_t tds_tungsten_list_size(const tds_tungsten_list* list)
{
    return tds_tungsten_list_is_valid(list) ? ft_persistent_deque_size(&list->items) : 0u;
}

tds_tungsten_status tds_tungsten_list_front(const tds_tungsten_list* list, void* destination)
{
    if (!tds_tungsten_list_is_valid(list) || destination == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    return tds_tungsten_from_ft(ft_persistent_deque_front(&list->items, destination));
}

tds_tungsten_status tds_tungsten_list_back(const tds_tungsten_list* list, void* destination)
{
    if (!tds_tungsten_list_is_valid(list) || destination == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    return tds_tungsten_from_ft(ft_persistent_deque_back(&list->items, destination));
}

tds_tungsten_status tds_tungsten_list_at(const tds_tungsten_list* list, size_t index, void* destination)
{
    if (!tds_tungsten_list_is_valid(list) || destination == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    return tds_tungsten_from_ft(ft_persistent_deque_at(&list->items, index, destination));
}

tds_tungsten_status tds_tungsten_list_push_front(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_list* result)
{
    if (value == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_list_prepare(list, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_ft(ft_persistent_deque_push_front(&list->items, value, &result->items));
    if (status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

tds_tungsten_status tds_tungsten_list_push_back(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_list* result)
{
    if (value == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_list_prepare(list, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_ft(ft_persistent_deque_push_back(&list->items, value, &result->items));
    if (status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

tds_tungsten_status tds_tungsten_list_concat(
    const tds_tungsten_list* left,
    const tds_tungsten_list* right,
    tds_tungsten_list* result)
{
    /* Concatenation shares nodes between the operands under the left list's policy, so
     * the value types must agree completely: two payloads of equal size but different
     * copy/destroy callbacks (say plain ints versus owned pointers) would otherwise have
     * some shared nodes released through the wrong callbacks, leaking or double-freeing. */
    if (!tds_tungsten_list_is_valid(left) || !tds_tungsten_list_is_valid(right) || result == right ||
        left->policy.value.size != right->policy.value.size ||
        left->policy.value.copy != right->policy.value.copy ||
        left->policy.value.destroy != right->policy.value.destroy ||
        left->policy.value.context != right->policy.value.context) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_list_prepare(left, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    ft_tree left_items = left->items;
    ft_tree right_items = right->items;
    left_items.policy = &left->policy;
    right_items.policy = &left->policy;
    status = tds_tungsten_from_ft(ft_persistent_deque_concat(&left_items, &right_items, &result->items));
    if (status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

tds_tungsten_status tds_tungsten_list_insert_at(
    const tds_tungsten_list* list,
    size_t index,
    const void* value,
    tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list) || value == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > ft_persistent_deque_size(&list->items)) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    tds_tungsten_status status = tds_tungsten_list_prepare(list, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_ft(ft_persistent_deque_insert_at(&list->items, index, value, &result->items));
    if (status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

tds_tungsten_status tds_tungsten_list_insert_range(
    const tds_tungsten_list* list,
    size_t index,
    const void* values,
    size_t count,
    tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list) || result == NULL || result == list ||
        (values == NULL && count != 0) || index > ft_persistent_deque_size(&list->items)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (count == 0) {
        return tds_tungsten_list_copy(list, result);
    }

    tds_tungsten_list middle;
    tds_tungsten_status status =
        tds_tungsten_list_from_array(&middle, &list->policy.value, values, count);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    ft_tree_split_result split;
    status = tds_tungsten_from_ft(ft_persistent_deque_split_at(&list->items, index, &split));
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_list_dispose(&middle);
        return status;
    }

    tds_tungsten_list left;
    tds_tungsten_list right;
    (void)memset(&left, 0, sizeof(left));
    (void)memset(&right, 0, sizeof(right));
    left.policy = list->policy;
    left.items = split.left;
    tds_tungsten_list_rebind(&left);
    right.policy = list->policy;
    right.items = split.right;
    tds_tungsten_list_rebind(&right);

    tds_tungsten_list joined_left;
    status = tds_tungsten_list_concat(&left, &middle, &joined_left);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_list_concat(&joined_left, &right, result);
        tds_tungsten_list_dispose(&joined_left);
    }

    tds_tungsten_list_dispose(&left);
    tds_tungsten_list_dispose(&right);
    tds_tungsten_list_dispose(&middle);
    return status;
}

tds_tungsten_status tds_tungsten_list_remove_at(
    const tds_tungsten_list* list,
    size_t index,
    tds_tungsten_list* result)
{
    tds_tungsten_status status = tds_tungsten_list_prepare(list, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_ft(ft_persistent_deque_remove_at(&list->items, index, &result->items));
    if (status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

tds_tungsten_status tds_tungsten_list_remove_range(
    const tds_tungsten_list* list,
    size_t index,
    size_t count,
    tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list) || result == NULL || result == list) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > ft_persistent_deque_size(&list->items) ||
        count > ft_persistent_deque_size(&list->items) - index) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    ft_tree_split_result split_left;
    tds_tungsten_status status = tds_tungsten_from_ft(ft_persistent_deque_split_at(&list->items, index, &split_left));
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    ft_tree_split_result split_right;
    status = tds_tungsten_from_ft(ft_persistent_deque_split_at(&split_left.right, count, &split_right));
    if (status != TDS_TUNGSTEN_OK) {
        ft_tree_dispose(&split_left.left);
        ft_tree_dispose(&split_left.right);
        return status;
    }

    tds_tungsten_list left;
    tds_tungsten_list right;
    (void)memset(&left, 0, sizeof(left));
    (void)memset(&right, 0, sizeof(right));
    left.policy = list->policy;
    left.items = split_left.left;
    tds_tungsten_list_rebind(&left);
    right.policy = list->policy;
    right.items = split_right.right;
    tds_tungsten_list_rebind(&right);

    ft_tree_dispose(&split_left.right);
    ft_tree_dispose(&split_right.left);
    status = tds_tungsten_list_concat(&left, &right, result);
    tds_tungsten_list_dispose(&left);
    tds_tungsten_list_dispose(&right);
    return status;
}

tds_tungsten_status tds_tungsten_list_set_at(
    const tds_tungsten_list* list,
    size_t index,
    const void* value,
    tds_tungsten_list* result)
{
    if (value == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_list_prepare(list, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_ft(ft_persistent_deque_set_at(&list->items, index, value, &result->items));
    if (status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_rebind(result);
    } else {
        (void)memset(result, 0, sizeof(*result));
    }
    return status;
}

tds_tungsten_status tds_tungsten_list_slice(
    const tds_tungsten_list* list,
    size_t index,
    size_t count,
    tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list) || result == NULL || result == list) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > ft_persistent_deque_size(&list->items) ||
        count > ft_persistent_deque_size(&list->items) - index) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    ft_tree_split_result split_left;
    tds_tungsten_status status = tds_tungsten_from_ft(ft_persistent_deque_split_at(&list->items, index, &split_left));
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    ft_tree_split_result split_right;
    status = tds_tungsten_from_ft(ft_persistent_deque_split_at(&split_left.right, count, &split_right));
    if (status != TDS_TUNGSTEN_OK) {
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
    tds_tungsten_list_rebind(result);
    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_list_take(const tds_tungsten_list* list, size_t count, tds_tungsten_list* result)
{
    return tds_tungsten_list_slice(list, 0, count, result);
}

tds_tungsten_status tds_tungsten_list_drop(const tds_tungsten_list* list, size_t count, tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* Classified like take/slice and the association's drop: a bad range is
     * TDS_TUNGSTEN_OUT_OF_RANGE, not a bad handle. */
    if (count > ft_persistent_deque_size(&list->items)) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    return tds_tungsten_list_slice(list, count, ft_persistent_deque_size(&list->items) - count, result);
}

typedef struct tds_tungsten_reverse_context {
    tds_tungsten_list result;
    tds_tungsten_status status;
} tds_tungsten_reverse_context;

static void tds_tungsten_list_reverse_visit(const void* value, void* context)
{
    tds_tungsten_reverse_context* reverse_context = (tds_tungsten_reverse_context*)context;
    if (reverse_context->status != TDS_TUNGSTEN_OK) {
        return;
    }

    tds_tungsten_list next;
    reverse_context->status = tds_tungsten_list_push_front(&reverse_context->result, value, &next);
    if (reverse_context->status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_dispose(&reverse_context->result);
        tds_tungsten_list_move(&reverse_context->result, &next);
    }
}

tds_tungsten_status tds_tungsten_list_reverse(const tds_tungsten_list* list, tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list) || result == NULL || result == list) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_reverse_context context;
    context.status = tds_tungsten_list_init(&context.result, &list->policy.value);
    if (context.status != TDS_TUNGSTEN_OK) {
        return context.status;
    }

    context.status = tds_tungsten_from_ft(ft_persistent_deque_visit(&list->items, tds_tungsten_list_reverse_visit, &context));
    if (context.status != TDS_TUNGSTEN_OK) {
        tds_tungsten_list_dispose(&context.result);
        return context.status;
    }

    tds_tungsten_list_move(result, &context.result);
    return TDS_TUNGSTEN_OK;
}

typedef struct tds_tungsten_map_context {
    tds_tungsten_list result;
    const ft_value_type* result_value_type;
    tds_tungsten_map_fn map;
    void* map_context;
    void* buffer;
    tds_tungsten_status status;
} tds_tungsten_map_context;

static void tds_tungsten_list_map_visit(const void* value, void* context)
{
    tds_tungsten_map_context* map_context = (tds_tungsten_map_context*)context;
    if (map_context->status != TDS_TUNGSTEN_OK) {
        return;
    }

    map_context->map(map_context->buffer, value, map_context->map_context);
    tds_tungsten_list next;
    map_context->status = tds_tungsten_list_push_back(&map_context->result, map_context->buffer, &next);
    /* push_back deep-copied the mapped value; destroy the callback-constructed
     * original so owning result types do not leak one payload per element. */
    tds_tungsten_value_destroy(map_context->result_value_type, map_context->buffer);
    if (map_context->status == TDS_TUNGSTEN_OK) {
        tds_tungsten_list_dispose(&map_context->result);
        tds_tungsten_list_move(&map_context->result, &next);
    }
}

tds_tungsten_status tds_tungsten_list_map(
    const tds_tungsten_list* list,
    const ft_value_type* result_value_type,
    tds_tungsten_map_fn map,
    void* map_context,
    tds_tungsten_list* result)
{
    if (!tds_tungsten_list_is_valid(list) || result_value_type == NULL || result_value_type->size == 0 ||
        map == NULL || result == NULL || result == list) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_map_context context;
    context.result_value_type = result_value_type;
    context.map = map;
    context.map_context = map_context;
    context.buffer = malloc(result_value_type->size == 0 ? 1u : result_value_type->size);
    if (context.buffer == NULL) {
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    context.status = tds_tungsten_list_init(&context.result, result_value_type);
    if (context.status == TDS_TUNGSTEN_OK) {
        context.status = tds_tungsten_from_ft(ft_persistent_deque_visit(&list->items, tds_tungsten_list_map_visit, &context));
    }

    free(context.buffer);
    if (context.status != TDS_TUNGSTEN_OK) {
        tds_tungsten_list_dispose(&context.result);
        return context.status;
    }

    tds_tungsten_list_move(result, &context.result);
    return TDS_TUNGSTEN_OK;
}

static void tds_tungsten_list_visit_adapter(const void* value, void* context)
{
    void** data = (void**)context;
    tds_tungsten_visit_fn visitor = (tds_tungsten_visit_fn)data[0];
    visitor(value, data[1]);
}

tds_tungsten_status tds_tungsten_list_visit(
    const tds_tungsten_list* list,
    tds_tungsten_visit_fn visitor,
    void* context)
{
    if (!tds_tungsten_list_is_valid(list) || visitor == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    void* data[2];
    data[0] = (void*)visitor;
    data[1] = context;
    return tds_tungsten_from_ft(ft_persistent_deque_visit(&list->items, tds_tungsten_list_visit_adapter, data));
}

typedef struct tds_tungsten_index_of_context {
    const void* value;
    tds_tungsten_equal_fn equal;
    void* equal_context;
    size_t current;
    size_t found_index;
    bool found;
} tds_tungsten_index_of_context;

static void tds_tungsten_list_index_of_visit(const void* value, void* context)
{
    tds_tungsten_index_of_context* index_context = (tds_tungsten_index_of_context*)context;
    if (!index_context->found && index_context->equal(value, index_context->value, index_context->equal_context)) {
        index_context->found = true;
        index_context->found_index = index_context->current;
    }
    ++index_context->current;
}

bool tds_tungsten_list_index_of(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_equal_fn equal,
    void* context,
    size_t* index)
{
    if (!tds_tungsten_list_is_valid(list) || value == NULL || equal == NULL) {
        return false;
    }

    tds_tungsten_index_of_context index_context;
    index_context.value = value;
    index_context.equal = equal;
    index_context.equal_context = context;
    index_context.current = 0;
    index_context.found_index = 0;
    index_context.found = false;
    if (ft_persistent_deque_visit(&list->items, tds_tungsten_list_index_of_visit, &index_context) != FT_STATUS_OK) {
        return false;
    }

    if (index != NULL) {
        *index = index_context.found_index;
    }
    return index_context.found;
}

bool tds_tungsten_list_contains(
    const tds_tungsten_list* list,
    const void* value,
    tds_tungsten_equal_fn equal,
    void* context)
{
    return tds_tungsten_list_index_of(list, value, equal, context, NULL);
}

static uint32_t tds_tungsten_hamt_hash(const void* key, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    return assoc_context->policy.hash_key(key, assoc_context->policy.context);
}

static bool tds_tungsten_hamt_key_equal(const void* left, const void* right, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    return assoc_context->policy.key_equal(left, right, assoc_context->policy.context);
}

static bool tds_tungsten_hamt_value_equal(const void* left, const void* right, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    const tds_tungsten_slot* left_slot = (const tds_tungsten_slot*)left;
    const tds_tungsten_slot* right_slot = (const tds_tungsten_slot*)right;
    return left_slot->stamp == right_slot->stamp &&
        tds_tungsten_value_equal(
            &assoc_context->policy.value_type,
            assoc_context->policy.value_equal,
            assoc_context->policy.context,
            left_slot->value,
            right_slot->value);
}

static void* tds_tungsten_hamt_retain_key(const void* key, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    return tds_tungsten_value_alloc_copy(&assoc_context->policy.key_type, key);
}

static void tds_tungsten_hamt_release_key(void* key, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    tds_tungsten_value_free(&assoc_context->policy.key_type, key);
}

static void* tds_tungsten_hamt_retain_value(const void* value, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    const tds_tungsten_slot* source = (const tds_tungsten_slot*)value;
    tds_tungsten_slot* slot = (tds_tungsten_slot*)calloc(1, sizeof(*slot));
    if (slot == NULL) {
        return NULL;
    }

    slot->stamp = source->stamp;
    slot->value = tds_tungsten_value_alloc_copy(&assoc_context->policy.value_type, source->value);
    if (slot->value == NULL) {
        free(slot);
        return NULL;
    }

    return slot;
}

static void tds_tungsten_hamt_release_value(void* value, void* context)
{
    const struct tds_tungsten_assoc_context* assoc_context = (const struct tds_tungsten_assoc_context*)context;
    tds_tungsten_slot* slot = (tds_tungsten_slot*)value;
    if (slot != NULL) {
        tds_tungsten_value_free(&assoc_context->policy.value_type, slot->value);
        free(slot);
    }
}

static bool tds_tungsten_policy_valid(const tds_tungsten_association_policy* policy)
{
    return policy != NULL &&
        policy->key_type.size != 0 &&
        policy->value_type.size != 0 &&
        policy->hash_key != NULL &&
        policy->key_equal != NULL;
}

static tds_tungsten_status tds_tungsten_context_create(
    const tds_tungsten_association_policy* policy,
    struct tds_tungsten_assoc_context** result)
{
    if (!tds_tungsten_policy_valid(policy) || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    struct tds_tungsten_assoc_context* context =
        (struct tds_tungsten_assoc_context*)calloc(1, sizeof(*context));
    if (context == NULL) {
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    context->ref_count = 1;
    context->policy = *policy;
    context->hamt_policy.hash = tds_tungsten_hamt_hash;
    context->hamt_policy.key_equal = tds_tungsten_hamt_key_equal;
    context->hamt_policy.value_equal = tds_tungsten_hamt_value_equal;
    context->hamt_policy.retain_key = tds_tungsten_hamt_retain_key;
    context->hamt_policy.retain_value = tds_tungsten_hamt_retain_value;
    context->hamt_policy.release_key = tds_tungsten_hamt_release_key;
    context->hamt_policy.release_value = tds_tungsten_hamt_release_value;
    context->hamt_policy.context = context;
    *result = context;
    return TDS_TUNGSTEN_OK;
}

static void tds_tungsten_context_retain(struct tds_tungsten_assoc_context* context)
{
    if (context != NULL) {
        ++context->ref_count;
    }
}

static void tds_tungsten_context_release(struct tds_tungsten_assoc_context* context)
{
    if (context != NULL && --context->ref_count == 0) {
        free(context);
    }
}

static size_t tds_tungsten_node_size(const struct tds_tungsten_assoc_node* node)
{
    return node == NULL ? 0u : node->size;
}

static int tds_tungsten_node_height(const struct tds_tungsten_assoc_node* node)
{
    return node == NULL ? 0 : node->height;
}

static int tds_tungsten_max_int(int left, int right)
{
    return left > right ? left : right;
}

static void tds_tungsten_entry_destroy(
    const struct tds_tungsten_assoc_context* context,
    tds_tungsten_assoc_entry* entry)
{
    if (entry != NULL) {
        tds_tungsten_value_free(&context->policy.key_type, entry->key);
        tds_tungsten_value_free(&context->policy.value_type, entry->value);
        entry->key = NULL;
        entry->value = NULL;
        entry->stamp = 0;
    }
}

static tds_tungsten_status tds_tungsten_entry_init(
    const struct tds_tungsten_assoc_context* context,
    int64_t stamp,
    const void* key,
    const void* value,
    tds_tungsten_assoc_entry* entry)
{
    entry->stamp = stamp;
    entry->key = tds_tungsten_value_alloc_copy(&context->policy.key_type, key);
    entry->value = tds_tungsten_value_alloc_copy(&context->policy.value_type, value);
    if (entry->key == NULL || entry->value == NULL) {
        tds_tungsten_entry_destroy(context, entry);
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_entry_copy(
    const struct tds_tungsten_assoc_context* context,
    const tds_tungsten_assoc_entry* source,
    tds_tungsten_assoc_entry* entry)
{
    return tds_tungsten_entry_init(context, source->stamp, source->key, source->value, entry);
}

static struct tds_tungsten_assoc_node* tds_tungsten_node_retain(struct tds_tungsten_assoc_node* node)
{
    if (node != NULL) {
        ++node->ref_count;
    }
    return node;
}

static void tds_tungsten_node_release(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* node)
{
    if (node == NULL || --node->ref_count != 0) {
        return;
    }

    tds_tungsten_node_release(context, node->left);
    tds_tungsten_node_release(context, node->right);
    tds_tungsten_entry_destroy(context, &node->entry);
    free(node);
}

static tds_tungsten_status tds_tungsten_node_create(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* left,
    const tds_tungsten_assoc_entry* entry,
    struct tds_tungsten_assoc_node* right,
    struct tds_tungsten_assoc_node** result)
{
    struct tds_tungsten_assoc_node* node = (struct tds_tungsten_assoc_node*)calloc(1, sizeof(*node));
    if (node == NULL) {
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    node->ref_count = 1;
    node->left = tds_tungsten_node_retain(left);
    node->right = tds_tungsten_node_retain(right);
    node->size = tds_tungsten_node_size(left) + 1u + tds_tungsten_node_size(right);
    node->height = tds_tungsten_max_int(tds_tungsten_node_height(left), tds_tungsten_node_height(right)) + 1;

    const tds_tungsten_status status = tds_tungsten_entry_copy(context, entry, &node->entry);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(context, node);
        return status;
    }

    *result = node;
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_rotate_left(
    const struct tds_tungsten_assoc_context* context,
    const struct tds_tungsten_assoc_node* node,
    struct tds_tungsten_assoc_node** result)
{
    struct tds_tungsten_assoc_node* right = node->right;
    struct tds_tungsten_assoc_node* new_left = NULL;
    tds_tungsten_status status =
        tds_tungsten_node_create(context, node->left, &node->entry, right->left, &new_left);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_node_create(context, new_left, &right->entry, right->right, result);
    tds_tungsten_node_release(context, new_left);
    return status;
}

static tds_tungsten_status tds_tungsten_rotate_right(
    const struct tds_tungsten_assoc_context* context,
    const struct tds_tungsten_assoc_node* node,
    struct tds_tungsten_assoc_node** result)
{
    struct tds_tungsten_assoc_node* left = node->left;
    struct tds_tungsten_assoc_node* new_right = NULL;
    tds_tungsten_status status =
        tds_tungsten_node_create(context, left->right, &node->entry, node->right, &new_right);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_node_create(context, left->left, &left->entry, new_right, result);
    tds_tungsten_node_release(context, new_right);
    return status;
}

static tds_tungsten_status tds_tungsten_tree_balance(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* left,
    const tds_tungsten_assoc_entry* entry,
    struct tds_tungsten_assoc_node* right,
    struct tds_tungsten_assoc_node** result)
{
    struct tds_tungsten_assoc_node* node = NULL;
    tds_tungsten_status status = tds_tungsten_node_create(context, left, entry, right, &node);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    if (tds_tungsten_node_height(left) > tds_tungsten_node_height(right) + 1) {
        if (tds_tungsten_node_height(left->right) > tds_tungsten_node_height(left->left)) {
            struct tds_tungsten_assoc_node* rotated_left = NULL;
            status = tds_tungsten_rotate_left(context, left, &rotated_left);
            if (status == TDS_TUNGSTEN_OK) {
                struct tds_tungsten_assoc_node* adjusted = NULL;
                status = tds_tungsten_node_create(context, rotated_left, entry, right, &adjusted);
                tds_tungsten_node_release(context, rotated_left);
                if (status == TDS_TUNGSTEN_OK) {
                    status = tds_tungsten_rotate_right(context, adjusted, result);
                    tds_tungsten_node_release(context, adjusted);
                }
            }
        } else {
            status = tds_tungsten_rotate_right(context, node, result);
        }
        tds_tungsten_node_release(context, node);
        return status;
    }

    if (tds_tungsten_node_height(right) > tds_tungsten_node_height(left) + 1) {
        if (tds_tungsten_node_height(right->left) > tds_tungsten_node_height(right->right)) {
            struct tds_tungsten_assoc_node* rotated_right = NULL;
            status = tds_tungsten_rotate_right(context, right, &rotated_right);
            if (status == TDS_TUNGSTEN_OK) {
                struct tds_tungsten_assoc_node* adjusted = NULL;
                status = tds_tungsten_node_create(context, left, entry, rotated_right, &adjusted);
                tds_tungsten_node_release(context, rotated_right);
                if (status == TDS_TUNGSTEN_OK) {
                    status = tds_tungsten_rotate_left(context, adjusted, result);
                    tds_tungsten_node_release(context, adjusted);
                }
            }
        } else {
            status = tds_tungsten_rotate_left(context, node, result);
        }
        tds_tungsten_node_release(context, node);
        return status;
    }

    *result = node;
    return TDS_TUNGSTEN_OK;
}

static const tds_tungsten_assoc_entry* tds_tungsten_tree_at(
    const struct tds_tungsten_assoc_node* node,
    size_t index)
{
    while (node != NULL) {
        const size_t left_size = tds_tungsten_node_size(node->left);
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

static const tds_tungsten_assoc_entry* tds_tungsten_tree_first(const struct tds_tungsten_assoc_node* node)
{
    if (node == NULL) {
        return NULL;
    }

    while (node->left != NULL) {
        node = node->left;
    }
    return &node->entry;
}

static const tds_tungsten_assoc_entry* tds_tungsten_tree_last(const struct tds_tungsten_assoc_node* node)
{
    if (node == NULL) {
        return NULL;
    }

    while (node->right != NULL) {
        node = node->right;
    }
    return &node->entry;
}

static bool tds_tungsten_tree_index_of_stamp(
    const struct tds_tungsten_assoc_node* node,
    int64_t stamp,
    size_t* index)
{
    size_t offset = 0;
    while (node != NULL) {
        const size_t left_size = tds_tungsten_node_size(node->left);
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

static tds_tungsten_status tds_tungsten_tree_remove_first(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* node,
    tds_tungsten_assoc_entry* entry,
    struct tds_tungsten_assoc_node** rest)
{
    if (node == NULL) {
        return TDS_TUNGSTEN_EMPTY;
    }

    if (node->left == NULL) {
        tds_tungsten_status status = tds_tungsten_entry_copy(context, &node->entry, entry);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }
        *rest = tds_tungsten_node_retain(node->right);
        return TDS_TUNGSTEN_OK;
    }

    tds_tungsten_assoc_entry first;
    (void)memset(&first, 0, sizeof(first));
    struct tds_tungsten_assoc_node* left_rest = NULL;
    tds_tungsten_status status = tds_tungsten_tree_remove_first(context, node->left, &first, &left_rest);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_tree_balance(context, left_rest, &node->entry, node->right, rest);
    tds_tungsten_node_release(context, left_rest);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_entry_destroy(context, &first);
        return status;
    }

    *entry = first;
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_tree_concat(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* left,
    struct tds_tungsten_assoc_node* right,
    struct tds_tungsten_assoc_node** result)
{
    if (left == NULL) {
        *result = tds_tungsten_node_retain(right);
        return TDS_TUNGSTEN_OK;
    }
    if (right == NULL) {
        *result = tds_tungsten_node_retain(left);
        return TDS_TUNGSTEN_OK;
    }

    if (tds_tungsten_node_height(left) > tds_tungsten_node_height(right) + 1) {
        struct tds_tungsten_assoc_node* merged = NULL;
        tds_tungsten_status status = tds_tungsten_tree_concat(context, left->right, right, &merged);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        status = tds_tungsten_tree_balance(context, left->left, &left->entry, merged, result);
        tds_tungsten_node_release(context, merged);
        return status;
    }

    if (tds_tungsten_node_height(right) > tds_tungsten_node_height(left) + 1) {
        struct tds_tungsten_assoc_node* merged = NULL;
        tds_tungsten_status status = tds_tungsten_tree_concat(context, left, right->left, &merged);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        status = tds_tungsten_tree_balance(context, merged, &right->entry, right->right, result);
        tds_tungsten_node_release(context, merged);
        return status;
    }

    tds_tungsten_assoc_entry first;
    (void)memset(&first, 0, sizeof(first));
    struct tds_tungsten_assoc_node* rest = NULL;
    tds_tungsten_status status = tds_tungsten_tree_remove_first(context, right, &first, &rest);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_tree_balance(context, left, &first, rest, result);
    tds_tungsten_entry_destroy(context, &first);
    tds_tungsten_node_release(context, rest);
    return status;
}

/* Joins two AVL trees of arbitrary height difference around a middle entry,
 * descending the taller side like tds_tungsten_tree_concat. A single
 * tds_tungsten_tree_balance handles only a height difference of at most two,
 * which is insufficient for the subtrees produced by a recursive split. */
static tds_tungsten_status tds_tungsten_tree_join(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* left,
    const tds_tungsten_assoc_entry* entry,
    struct tds_tungsten_assoc_node* right,
    struct tds_tungsten_assoc_node** result)
{
    if (tds_tungsten_node_height(left) > tds_tungsten_node_height(right) + 1) {
        struct tds_tungsten_assoc_node* joined = NULL;
        tds_tungsten_status status = tds_tungsten_tree_join(context, left->right, entry, right, &joined);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        status = tds_tungsten_tree_balance(context, left->left, &left->entry, joined, result);
        tds_tungsten_node_release(context, joined);
        return status;
    }

    if (tds_tungsten_node_height(right) > tds_tungsten_node_height(left) + 1) {
        struct tds_tungsten_assoc_node* joined = NULL;
        tds_tungsten_status status = tds_tungsten_tree_join(context, left, entry, right->left, &joined);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        status = tds_tungsten_tree_balance(context, joined, &right->entry, right->right, result);
        tds_tungsten_node_release(context, joined);
        return status;
    }

    return tds_tungsten_tree_balance(context, left, entry, right, result);
}

static tds_tungsten_status tds_tungsten_tree_split(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* node,
    size_t index,
    struct tds_tungsten_assoc_node** left,
    struct tds_tungsten_assoc_node** right)
{
    if (node == NULL) {
        *left = NULL;
        *right = NULL;
        return TDS_TUNGSTEN_OK;
    }

    const size_t left_size = tds_tungsten_node_size(node->left);
    if (index <= left_size) {
        struct tds_tungsten_assoc_node* before = NULL;
        struct tds_tungsten_assoc_node* after = NULL;
        tds_tungsten_status status = tds_tungsten_tree_split(context, node->left, index, &before, &after);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        struct tds_tungsten_assoc_node* new_right = NULL;
        status = tds_tungsten_tree_join(context, after, &node->entry, node->right, &new_right);
        tds_tungsten_node_release(context, after);
        if (status != TDS_TUNGSTEN_OK) {
            tds_tungsten_node_release(context, before);
            return status;
        }

        *left = before;
        *right = new_right;
        return TDS_TUNGSTEN_OK;
    }

    if (index == left_size + 1u) {
        tds_tungsten_status status = tds_tungsten_tree_join(context, node->left, &node->entry, NULL, left);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }
        *right = tds_tungsten_node_retain(node->right);
        return TDS_TUNGSTEN_OK;
    }

    struct tds_tungsten_assoc_node* before = NULL;
    struct tds_tungsten_assoc_node* after = NULL;
    tds_tungsten_status status =
        tds_tungsten_tree_split(context, node->right, index - left_size - 1u, &before, &after);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    struct tds_tungsten_assoc_node* new_left = NULL;
    status = tds_tungsten_tree_join(context, node->left, &node->entry, before, &new_left);
    tds_tungsten_node_release(context, before);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(context, after);
        return status;
    }

    *left = new_left;
    *right = after;
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_tree_insert_at(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* root,
    size_t index,
    const tds_tungsten_assoc_entry* entry,
    struct tds_tungsten_assoc_node** result)
{
    struct tds_tungsten_assoc_node* left = NULL;
    struct tds_tungsten_assoc_node* right = NULL;
    tds_tungsten_status status = tds_tungsten_tree_split(context, root, index, &left, &right);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    struct tds_tungsten_assoc_node* singleton = NULL;
    status = tds_tungsten_node_create(context, NULL, entry, NULL, &singleton);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(context, left);
        tds_tungsten_node_release(context, right);
        return status;
    }

    struct tds_tungsten_assoc_node* joined_left = NULL;
    status = tds_tungsten_tree_concat(context, left, singleton, &joined_left);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_tree_concat(context, joined_left, right, result);
    }

    tds_tungsten_node_release(context, joined_left);
    tds_tungsten_node_release(context, singleton);
    tds_tungsten_node_release(context, left);
    tds_tungsten_node_release(context, right);
    return status;
}

static tds_tungsten_status tds_tungsten_tree_delete_at(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* root,
    size_t index,
    struct tds_tungsten_assoc_node** result)
{
    if (index >= tds_tungsten_node_size(root)) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    struct tds_tungsten_assoc_node* before = NULL;
    struct tds_tungsten_assoc_node* rest = NULL;
    tds_tungsten_status status = tds_tungsten_tree_split(context, root, index, &before, &rest);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    struct tds_tungsten_assoc_node* removed = NULL;
    struct tds_tungsten_assoc_node* after = NULL;
    status = tds_tungsten_tree_split(context, rest, 1, &removed, &after);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_tree_concat(context, before, after, result);
    }

    tds_tungsten_node_release(context, before);
    tds_tungsten_node_release(context, rest);
    tds_tungsten_node_release(context, removed);
    tds_tungsten_node_release(context, after);
    return status;
}

static tds_tungsten_status tds_tungsten_tree_set_at(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* root,
    size_t index,
    const tds_tungsten_assoc_entry* entry,
    struct tds_tungsten_assoc_node** result)
{
    if (root == NULL) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    const size_t left_size = tds_tungsten_node_size(root->left);
    if (index < left_size) {
        struct tds_tungsten_assoc_node* new_left = NULL;
        tds_tungsten_status status = tds_tungsten_tree_set_at(context, root->left, index, entry, &new_left);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        status = tds_tungsten_tree_balance(context, new_left, &root->entry, root->right, result);
        tds_tungsten_node_release(context, new_left);
        return status;
    }

    if (index == left_size) {
        return tds_tungsten_tree_balance(context, root->left, entry, root->right, result);
    }

    struct tds_tungsten_assoc_node* new_right = NULL;
    tds_tungsten_status status =
        tds_tungsten_tree_set_at(context, root->right, index - left_size - 1u, entry, &new_right);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_tree_balance(context, root->left, &root->entry, new_right, result);
    tds_tungsten_node_release(context, new_right);
    return status;
}

static void tds_tungsten_tree_fill_views(
    const struct tds_tungsten_assoc_node* node,
    tds_tungsten_assoc_entry_view* views,
    size_t* index)
{
    if (node == NULL) {
        return;
    }

    tds_tungsten_tree_fill_views(node->left, views, index);
    views[*index].stamp = node->entry.stamp;
    views[*index].key = node->entry.key;
    views[*index].value = node->entry.value;
    ++*index;
    tds_tungsten_tree_fill_views(node->right, views, index);
}

static tds_tungsten_status tds_tungsten_tree_build_relabel(
    const struct tds_tungsten_assoc_context* context,
    const tds_tungsten_assoc_entry_view* views,
    size_t start,
    size_t count,
    struct tds_tungsten_assoc_node** result)
{
    if (count == 0) {
        *result = NULL;
        return TDS_TUNGSTEN_OK;
    }

    const size_t left_count = count / 2u;
    const size_t middle = start + left_count;
    const size_t right_count = count - left_count - 1u;
    if (middle > (size_t)(INT64_MAX / TDS_TUNGSTEN_STAMP_GAP)) {
        return TDS_TUNGSTEN_OVERFLOW;
    }

    struct tds_tungsten_assoc_node* left = NULL;
    struct tds_tungsten_assoc_node* right = NULL;
    tds_tungsten_status status = tds_tungsten_tree_build_relabel(context, views, start, left_count, &left);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_tree_build_relabel(context, views, middle + 1u, right_count, &right);
    }
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(context, left);
        return status;
    }

    tds_tungsten_assoc_entry entry;
    (void)memset(&entry, 0, sizeof(entry));
    status = tds_tungsten_entry_init(
        context,
        (int64_t)middle * (int64_t)TDS_TUNGSTEN_STAMP_GAP,
        views[middle].key,
        views[middle].value,
        &entry);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_node_create(context, left, &entry, right, result);
        tds_tungsten_entry_destroy(context, &entry);
    }

    tds_tungsten_node_release(context, left);
    tds_tungsten_node_release(context, right);
    return status;
}

static tds_tungsten_status tds_tungsten_index_from_views(
    const struct tds_tungsten_assoc_context* context,
    const tds_tungsten_assoc_entry_view* views,
    size_t count,
    bool relabel,
    tds_hamt_map* result)
{
    tds_hamt_map map = tds_hamt_map_create(&context->hamt_policy);
    for (size_t index = 0; index != count; ++index) {
        if (relabel && index > (size_t)(INT64_MAX / TDS_TUNGSTEN_STAMP_GAP)) {
            tds_hamt_map_destroy(&map);
            return TDS_TUNGSTEN_OVERFLOW;
        }

        tds_tungsten_slot slot;
        slot.stamp = relabel ? (int64_t)index * (int64_t)TDS_TUNGSTEN_STAMP_GAP : views[index].stamp;
        slot.value = (void*)views[index].value;

        tds_hamt_map next;
        const tds_tungsten_status status =
            tds_tungsten_from_hamt(tds_hamt_map_set(&map, views[index].key, &slot, &next));
        if (status != TDS_TUNGSTEN_OK) {
            tds_hamt_map_destroy(&map);
            return status;
        }

        tds_hamt_map_destroy(&map);
        map = next;
    }

    *result = map;
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_index_from_tree(
    const struct tds_tungsten_assoc_context* context,
    struct tds_tungsten_assoc_node* root,
    tds_hamt_map* result)
{
    const size_t count = tds_tungsten_node_size(root);
    if (count == 0) {
        *result = tds_hamt_map_create(&context->hamt_policy);
        return TDS_TUNGSTEN_OK;
    }

    tds_tungsten_assoc_entry_view* views =
        (tds_tungsten_assoc_entry_view*)malloc(count * sizeof(*views));
    if (views == NULL) {
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    size_t index = 0;
    tds_tungsten_tree_fill_views(root, views, &index);
    const tds_tungsten_status status = tds_tungsten_index_from_views(context, views, count, false, result);
    free(views);
    return status;
}

static tds_tungsten_status tds_tungsten_rebuild_from_views(
    const struct tds_tungsten_assoc_context* context,
    const tds_tungsten_assoc_entry_view* views,
    size_t count,
    struct tds_tungsten_assoc_node** root,
    tds_hamt_map* index)
{
    struct tds_tungsten_assoc_node* new_root = NULL;
    tds_tungsten_status status = tds_tungsten_tree_build_relabel(context, views, 0, count, &new_root);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    tds_hamt_map new_index;
    status = tds_tungsten_index_from_views(context, views, count, true, &new_index);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(context, new_root);
        return status;
    }

    *root = new_root;
    *index = new_index;
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_association_take_parts(
    const tds_tungsten_association* source,
    struct tds_tungsten_assoc_node* root,
    tds_hamt_map index,
    tds_tungsten_association* result)
{
    struct tds_tungsten_assoc_context* context = source->context;

    /* The result must not alias the source: publishing overwrites result's root, index,
     * and context reference without releasing the prior contents, so an aliased call
     * would leak the source's whole previous version. */
    if (result == NULL || result == source) {
        tds_tungsten_node_release(context, root);
        tds_hamt_map_destroy(&index);
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_context_retain(context);
    result->context = context;
    result->root = root;
    result->index = index;
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_association_copy_empty_like(
    const tds_tungsten_association* source,
    tds_tungsten_association* result)
{
    tds_tungsten_context_retain(source->context);
    result->context = source->context;
    result->root = NULL;
    result->index = tds_hamt_map_create(&source->context->hamt_policy);
    return TDS_TUNGSTEN_OK;
}

static bool tds_tungsten_association_valid(const tds_tungsten_association* association)
{
    return association != NULL && association->context != NULL;
}

tds_tungsten_status tds_tungsten_association_init(
    tds_tungsten_association* association,
    const tds_tungsten_association_policy* policy)
{
    if (association == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    (void)memset(association, 0, sizeof(*association));
    struct tds_tungsten_assoc_context* context = NULL;
    tds_tungsten_status status = tds_tungsten_context_create(policy, &context);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    association->context = context;
    association->root = NULL;
    association->index = tds_hamt_map_create(&context->hamt_policy);
    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_pick_stamp(
    struct tds_tungsten_assoc_node* root,
    size_t position,
    int64_t* stamp,
    bool* picked)
{
    *stamp = 0;
    *picked = true;
    const size_t count = tds_tungsten_node_size(root);
    if (count == 0) {
        return TDS_TUNGSTEN_OK;
    }

    if (position == 0) {
        const tds_tungsten_assoc_entry* first = tds_tungsten_tree_first(root);
        if (first->stamp < INT64_MIN + (int64_t)TDS_TUNGSTEN_STAMP_GAP) {
            *picked = false;
        } else {
            *stamp = first->stamp - (int64_t)TDS_TUNGSTEN_STAMP_GAP;
        }
        return TDS_TUNGSTEN_OK;
    }

    if (position == count) {
        const tds_tungsten_assoc_entry* last = tds_tungsten_tree_last(root);
        if (last->stamp > INT64_MAX - (int64_t)TDS_TUNGSTEN_STAMP_GAP) {
            *picked = false;
        } else {
            *stamp = last->stamp + (int64_t)TDS_TUNGSTEN_STAMP_GAP;
        }
        return TDS_TUNGSTEN_OK;
    }

    const tds_tungsten_assoc_entry* left = tds_tungsten_tree_at(root, position - 1u);
    const tds_tungsten_assoc_entry* right = tds_tungsten_tree_at(root, position);
    if (left == NULL || right == NULL) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    /* Subtract in uint64_t: the signed difference can exceed INT64_MAX for
     * spans straddling the label range (matches the C# unchecked cast). */
    const uint64_t gap = (uint64_t)right->stamp - (uint64_t)left->stamp;
    if (gap < 2u) {
        *picked = false;
    } else {
        *stamp = left->stamp + (int64_t)(gap / 2u);
    }

    return TDS_TUNGSTEN_OK;
}

static tds_tungsten_status tds_tungsten_insert_absent(
    const tds_tungsten_association* association,
    struct tds_tungsten_assoc_node* root,
    const tds_hamt_map* index,
    size_t position,
    const void* key,
    const void* value,
    tds_tungsten_association* result)
{
    int64_t stamp = 0;
    bool picked = false;
    tds_tungsten_status status = tds_tungsten_pick_stamp(root, position, &stamp, &picked);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    if (!picked) {
        const size_t count = tds_tungsten_node_size(root);
        tds_tungsten_assoc_entry_view* views =
            (tds_tungsten_assoc_entry_view*)malloc((count + 1u) * sizeof(*views));
        if (views == NULL) {
            return TDS_TUNGSTEN_OUT_OF_MEMORY;
        }

        /* Collect the existing entries with one in-order walk (the per-index
         * tree descent made this rare relabel path O(n log n)), then splice
         * the new pair in at its position. */
        size_t written = 0;
        tds_tungsten_tree_fill_views(root, views, &written);
        (void)memmove(&views[position + 1u], &views[position], (count - position) * sizeof(*views));
        views[position].stamp = 0;
        views[position].key = key;
        views[position].value = value;
        written = count + 1u;

        struct tds_tungsten_assoc_node* new_root = NULL;
        tds_hamt_map new_index;
        status = tds_tungsten_rebuild_from_views(association->context, views, written, &new_root, &new_index);
        free(views);
        if (status != TDS_TUNGSTEN_OK) {
            return status;
        }

        return tds_tungsten_association_take_parts(association, new_root, new_index, result);
    }

    tds_tungsten_assoc_entry entry;
    (void)memset(&entry, 0, sizeof(entry));
    status = tds_tungsten_entry_init(association->context, stamp, key, value, &entry);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    struct tds_tungsten_assoc_node* new_root = NULL;
    status = tds_tungsten_tree_insert_at(association->context, root, position, &entry, &new_root);
    tds_tungsten_entry_destroy(association->context, &entry);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    tds_tungsten_slot slot;
    slot.stamp = stamp;
    slot.value = (void*)value;
    tds_hamt_map new_index;
    status = tds_tungsten_from_hamt(tds_hamt_map_set(index, key, &slot, &new_index));
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(association->context, new_root);
        return status;
    }

    return tds_tungsten_association_take_parts(association, new_root, new_index, result);
}

tds_tungsten_status tds_tungsten_association_from_pairs(
    tds_tungsten_association* association,
    const tds_tungsten_association_policy* policy,
    const tds_tungsten_assoc_pair* pairs,
    size_t count)
{
    if (association == NULL || (pairs == NULL && count != 0)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_association_init(association, policy);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        tds_tungsten_association next;
        status = tds_tungsten_association_set_item(association, pairs[index].key, pairs[index].value, &next);
        if (status != TDS_TUNGSTEN_OK) {
            tds_tungsten_association_dispose(association);
            return status;
        }

        tds_tungsten_association_dispose(association);
        tds_tungsten_association_move(association, &next);
    }

    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_association_copy(
    const tds_tungsten_association* source,
    tds_tungsten_association* destination)
{
    /* An aliased copy would retain the context, root, and index a second time and then
     * overwrite the only handles that could release the extra references. */
    if (!tds_tungsten_association_valid(source) || destination == NULL || destination == source) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_context_retain(source->context);
    destination->context = source->context;
    destination->root = tds_tungsten_node_retain(source->root);
    destination->index = tds_hamt_map_clone(&source->index);
    return TDS_TUNGSTEN_OK;
}

void tds_tungsten_association_move(tds_tungsten_association* destination, tds_tungsten_association* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }

    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void tds_tungsten_association_dispose(tds_tungsten_association* association)
{
    if (association == NULL || association->context == NULL) {
        return;
    }

    struct tds_tungsten_assoc_context* context = association->context;
    tds_hamt_map_destroy(&association->index);
    tds_tungsten_node_release(context, association->root);
    (void)memset(association, 0, sizeof(*association));
    tds_tungsten_context_release(context);
}

bool tds_tungsten_association_empty(const tds_tungsten_association* association)
{
    return !tds_tungsten_association_valid(association) || association->root == NULL;
}

size_t tds_tungsten_association_size(const tds_tungsten_association* association)
{
    return tds_tungsten_association_valid(association) ? tds_tungsten_node_size(association->root) : 0u;
}

bool tds_tungsten_association_contains_key(const tds_tungsten_association* association, const void* key)
{
    return tds_tungsten_association_valid(association) && key != NULL &&
        tds_hamt_map_contains_key(&association->index, key);
}

bool tds_tungsten_association_try_get(
    const tds_tungsten_association* association,
    const void* key,
    void* value)
{
    if (!tds_tungsten_association_valid(association) || key == NULL) {
        return false;
    }

    const void* slot_value = NULL;
    if (!tds_hamt_map_try_get(&association->index, key, &slot_value)) {
        return false;
    }

    if (value != NULL) {
        const tds_tungsten_slot* slot = (const tds_tungsten_slot*)slot_value;
        tds_tungsten_value_copy(&association->context->policy.value_type, value, slot->value);
    }
    return true;
}

bool tds_tungsten_association_try_get_key(
    const tds_tungsten_association* association,
    const void* equal_key,
    void* actual_key)
{
    if (!tds_tungsten_association_valid(association) || equal_key == NULL) {
        return false;
    }

    const void* found_key = NULL;
    const bool found = tds_hamt_map_try_get_key(&association->index, equal_key, &found_key);
    if (found && actual_key != NULL) {
        tds_tungsten_value_copy(&association->context->policy.key_type, actual_key, found_key);
    }
    return found;
}

static tds_tungsten_status tds_tungsten_association_copy_entry(
    const tds_tungsten_association* association,
    const tds_tungsten_assoc_entry* entry,
    void* key,
    void* value)
{
    if (entry == NULL) {
        return TDS_TUNGSTEN_EMPTY;
    }

    if (key != NULL) {
        tds_tungsten_value_copy(&association->context->policy.key_type, key, entry->key);
    }
    if (value != NULL) {
        tds_tungsten_value_copy(&association->context->policy.value_type, value, entry->value);
    }
    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_association_front(
    const tds_tungsten_association* association,
    void* key,
    void* value)
{
    if (!tds_tungsten_association_valid(association)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }
    return tds_tungsten_association_copy_entry(association, tds_tungsten_tree_first(association->root), key, value);
}

tds_tungsten_status tds_tungsten_association_back(
    const tds_tungsten_association* association,
    void* key,
    void* value)
{
    if (!tds_tungsten_association_valid(association)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }
    return tds_tungsten_association_copy_entry(association, tds_tungsten_tree_last(association->root), key, value);
}

tds_tungsten_status tds_tungsten_association_entry_at(
    const tds_tungsten_association* association,
    size_t index,
    void* key,
    void* value)
{
    if (!tds_tungsten_association_valid(association)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }
    if (index >= tds_tungsten_node_size(association->root)) {
        /* Distinguish a bad index from an empty association (C# GetAt throws
         * ArgumentOutOfRangeException; front/back keep the EMPTY status). */
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }
    return tds_tungsten_association_copy_entry(association, tds_tungsten_tree_at(association->root, index), key, value);
}

bool tds_tungsten_association_index_of_key(
    const tds_tungsten_association* association,
    const void* key,
    size_t* index)
{
    if (!tds_tungsten_association_valid(association) || key == NULL) {
        return false;
    }

    const void* slot_value = NULL;
    if (!tds_hamt_map_try_get(&association->index, key, &slot_value)) {
        return false;
    }

    const tds_tungsten_slot* slot = (const tds_tungsten_slot*)slot_value;
    return tds_tungsten_tree_index_of_stamp(association->root, slot->stamp, index);
}

tds_tungsten_status tds_tungsten_association_set_item(
    const tds_tungsten_association* association,
    const void* key,
    const void* value,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    const void* slot_value = NULL;
    if (!tds_hamt_map_try_get(&association->index, key, &slot_value)) {
        return tds_tungsten_insert_absent(
            association,
            association->root,
            &association->index,
            tds_tungsten_node_size(association->root),
            key,
            value,
            result);
    }

    const tds_tungsten_slot* slot = (const tds_tungsten_slot*)slot_value;
    if (tds_tungsten_value_equal(
            &association->context->policy.value_type,
            association->context->policy.value_equal,
            association->context->policy.context,
            slot->value,
            value)) {
        return tds_tungsten_association_copy(association, result);
    }

    size_t position = 0;
    if (!tds_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    const tds_tungsten_assoc_entry* old_entry = tds_tungsten_tree_at(association->root, position);
    tds_tungsten_assoc_entry replacement;
    (void)memset(&replacement, 0, sizeof(replacement));
    tds_tungsten_status status =
        tds_tungsten_entry_init(association->context, slot->stamp, old_entry->key, value, &replacement);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    struct tds_tungsten_assoc_node* new_root = NULL;
    status = tds_tungsten_tree_set_at(association->context, association->root, position, &replacement, &new_root);
    tds_tungsten_entry_destroy(association->context, &replacement);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    tds_tungsten_slot new_slot;
    new_slot.stamp = slot->stamp;
    new_slot.value = (void*)value;
    tds_hamt_map new_index;
    status = tds_tungsten_from_hamt(tds_hamt_map_set(&association->index, key, &new_slot, &new_index));
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(association->context, new_root);
        return status;
    }

    return tds_tungsten_association_take_parts(association, new_root, new_index, result);
}

tds_tungsten_status tds_tungsten_association_set_items(
    const tds_tungsten_association* association,
    const tds_tungsten_assoc_pair* pairs,
    size_t count,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || result == NULL || (pairs == NULL && count != 0)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_association_copy(association, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        tds_tungsten_association next;
        status = tds_tungsten_association_set_item(result, pairs[index].key, pairs[index].value, &next);
        if (status != TDS_TUNGSTEN_OK) {
            tds_tungsten_association_dispose(result);
            return status;
        }

        tds_tungsten_association_dispose(result);
        tds_tungsten_association_move(result, &next);
    }

    return TDS_TUNGSTEN_OK;
}

static void tds_tungsten_collect_pair_visit(const void* key, const void* value, void* context)
{
    tds_tungsten_assoc_pair** cursor = (tds_tungsten_assoc_pair**)context;
    (*cursor)->key = key;
    (*cursor)->value = value;
    ++*cursor;
}

tds_tungsten_status tds_tungsten_association_join(
    const tds_tungsten_association* left,
    const tds_tungsten_association* right,
    tds_tungsten_association* result)
{
    /* The result must not alias either operand: publishing overwrites result in place
     * (see tds_tungsten_association_take_parts). */
    if (!tds_tungsten_association_valid(left) || !tds_tungsten_association_valid(right) || result == NULL ||
        result == left || result == right) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* The join copies right's raw key/value bytes under left's type policy;
     * mismatched payload sizes would read out of bounds. */
    if (left->context->policy.key_type.size != right->context->policy.key_type.size ||
        left->context->policy.value_type.size != right->context->policy.value_type.size) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    const size_t count = tds_tungsten_association_size(right);
    if (count == 0) {
        return tds_tungsten_association_copy(left, result);
    }

    tds_tungsten_assoc_pair* pairs = (tds_tungsten_assoc_pair*)malloc(count * sizeof(*pairs));
    if (pairs == NULL) {
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    tds_tungsten_assoc_pair* cursor = pairs;
    tds_tungsten_status status = tds_tungsten_association_visit(right, tds_tungsten_collect_pair_visit, &cursor);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_association_set_items(left, pairs, count, result);
    }

    free(pairs);
    return status;
}

static tds_tungsten_status tds_tungsten_remove_existing_for_insert(
    const tds_tungsten_association* association,
    const void* key,
    size_t requested,
    struct tds_tungsten_assoc_node** root,
    tds_hamt_map* index,
    size_t* adjusted)
{
    const void* slot_value = NULL;
    if (!tds_hamt_map_try_get(&association->index, key, &slot_value)) {
        *root = tds_tungsten_node_retain(association->root);
        *index = tds_hamt_map_clone(&association->index);
        *adjusted = requested;
        return TDS_TUNGSTEN_OK;
    }

    const tds_tungsten_slot* slot = (const tds_tungsten_slot*)slot_value;
    size_t old_position = 0;
    if (!tds_tungsten_tree_index_of_stamp(association->root, slot->stamp, &old_position)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status =
        tds_tungsten_tree_delete_at(association->context, association->root, old_position, root);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    status = tds_tungsten_from_hamt(tds_hamt_map_remove(&association->index, key, index));
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(association->context, *root);
        /* Null the released pointer: callers release *root unconditionally. */
        *root = NULL;
        return status;
    }

    *adjusted = old_position < requested ? requested - 1u : requested;
    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_association_append(
    const tds_tungsten_association* association,
    const void* key,
    const void* value,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* Rule-2 no-op fast path (matches the C# reference and its spec): a key
     * that is already last with an equal value returns the receiver, keeping
     * the stored key payload and consuming no stamp. */
    const void* existing_slot = NULL;
    if (tds_hamt_map_try_get(&association->index, key, &existing_slot)) {
        const tds_tungsten_slot* slot = (const tds_tungsten_slot*)existing_slot;
        size_t position = 0;
        const size_t size = tds_tungsten_node_size(association->root);
        if (tds_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position) &&
            position + 1u == size &&
            tds_tungsten_value_equal(
                &association->context->policy.value_type,
                association->context->policy.value_equal,
                association->context->policy.context,
                slot->value,
                value)) {
            return tds_tungsten_association_copy(association, result);
        }
    }

    struct tds_tungsten_assoc_node* root = NULL;
    tds_hamt_map index = {0};
    size_t adjusted = 0;
    tds_tungsten_status status = tds_tungsten_remove_existing_for_insert(
        association,
        key,
        tds_tungsten_node_size(association->root),
        &root,
        &index,
        &adjusted);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_insert_absent(association, root, &index, tds_tungsten_node_size(root), key, value, result);
    }
    tds_tungsten_node_release(association->context, root);
    tds_hamt_map_destroy(&index);
    return status;
}

tds_tungsten_status tds_tungsten_association_prepend(
    const tds_tungsten_association* association,
    const void* key,
    const void* value,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    /* Rule-2 no-op fast path: a key already first with an equal value
     * returns the receiver (see append). */
    const void* existing_slot = NULL;
    if (tds_hamt_map_try_get(&association->index, key, &existing_slot)) {
        const tds_tungsten_slot* slot = (const tds_tungsten_slot*)existing_slot;
        size_t position = 0;
        if (tds_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position) &&
            position == 0 &&
            tds_tungsten_value_equal(
                &association->context->policy.value_type,
                association->context->policy.value_equal,
                association->context->policy.context,
                slot->value,
                value)) {
            return tds_tungsten_association_copy(association, result);
        }
    }

    struct tds_tungsten_assoc_node* root = NULL;
    tds_hamt_map index = {0};
    size_t adjusted = 0;
    tds_tungsten_status status =
        tds_tungsten_remove_existing_for_insert(association, key, 0, &root, &index, &adjusted);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_insert_absent(association, root, &index, 0, key, value, result);
    }
    tds_tungsten_node_release(association->context, root);
    tds_hamt_map_destroy(&index);
    return status;
}

tds_tungsten_status tds_tungsten_association_insert_at(
    const tds_tungsten_association* association,
    size_t index,
    const void* key,
    const void* value,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || key == NULL || value == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > tds_tungsten_node_size(association->root)) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    struct tds_tungsten_assoc_node* root = NULL;
    tds_hamt_map index_side = {0};
    size_t adjusted = 0;
    tds_tungsten_status status =
        tds_tungsten_remove_existing_for_insert(association, key, index, &root, &index_side, &adjusted);
    if (status == TDS_TUNGSTEN_OK) {
        status = tds_tungsten_insert_absent(association, root, &index_side, adjusted, key, value, result);
    }
    tds_tungsten_node_release(association->context, root);
    tds_hamt_map_destroy(&index_side);
    return status;
}

tds_tungsten_status tds_tungsten_association_try_remove(
    const tds_tungsten_association* association,
    const void* key,
    bool* removed,
    void* value,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || key == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_hamt_map index;
    bool local_removed = false;
    const void* removed_value = NULL;
    tds_tungsten_status status = tds_tungsten_from_hamt(
        tds_hamt_map_try_remove(&association->index, key, &index, &local_removed, &removed_value));
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    if (!local_removed) {
        tds_hamt_map_destroy(&index);
        if (removed != NULL) {
            *removed = false;
        }
        return tds_tungsten_association_copy(association, result);
    }

    const tds_tungsten_slot* slot = (const tds_tungsten_slot*)removed_value;
    if (value != NULL) {
        tds_tungsten_value_copy(&association->context->policy.value_type, value, slot->value);
    }

    size_t position = 0;
    if (!tds_tungsten_tree_index_of_stamp(association->root, slot->stamp, &position)) {
        tds_hamt_map_destroy(&index);
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    struct tds_tungsten_assoc_node* root = NULL;
    status = tds_tungsten_tree_delete_at(association->context, association->root, position, &root);
    if (status != TDS_TUNGSTEN_OK) {
        tds_hamt_map_destroy(&index);
        return status;
    }

    if (removed != NULL) {
        *removed = true;
    }
    return tds_tungsten_association_take_parts(association, root, index, result);
}

tds_tungsten_status tds_tungsten_association_remove(
    const tds_tungsten_association* association,
    const void* key,
    tds_tungsten_association* result)
{
    bool removed = false;
    return tds_tungsten_association_try_remove(association, key, &removed, NULL, result);
}

tds_tungsten_status tds_tungsten_association_remove_keys(
    const tds_tungsten_association* association,
    const void* const* keys,
    size_t count,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || result == NULL || (keys == NULL && count != 0)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_association_copy(association, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        tds_tungsten_association next;
        status = tds_tungsten_association_remove(result, keys[index], &next);
        if (status != TDS_TUNGSTEN_OK) {
            tds_tungsten_association_dispose(result);
            return status;
        }

        tds_tungsten_association_dispose(result);
        tds_tungsten_association_move(result, &next);
    }

    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_association_key_take(
    const tds_tungsten_association* association,
    const void* const* keys,
    size_t count,
    tds_tungsten_association* result)
{
    /* copy_empty_like overwrites result's root and index in place, so an aliased result
     * would leak the source's previous version before the take loop reads it. */
    if (!tds_tungsten_association_valid(association) || result == NULL || result == association ||
        (keys == NULL && count != 0)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_status status = tds_tungsten_association_copy_empty_like(association, result);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    for (size_t index = 0; index != count; ++index) {
        /* A NULL requested key can never be stored (every keyed entry point
         * rejects NULL keys); skip it before it reaches the hash callback. */
        if (keys[index] == NULL) {
            continue;
        }

        if (tds_tungsten_association_contains_key(result, keys[index])) {
            continue;
        }

        const void* slot_value = NULL;
        const void* actual_key = NULL;
        if (!tds_hamt_map_try_get(&association->index, keys[index], &slot_value) ||
            !tds_hamt_map_try_get_key(&association->index, keys[index], &actual_key)) {
            continue;
        }

        const tds_tungsten_slot* slot = (const tds_tungsten_slot*)slot_value;
        tds_tungsten_association next;
        status = tds_tungsten_insert_absent(
            result,
            result->root,
            &result->index,
            tds_tungsten_node_size(result->root),
            actual_key,
            slot->value,
            &next);
        if (status != TDS_TUNGSTEN_OK) {
            tds_tungsten_association_dispose(result);
            return status;
        }

        tds_tungsten_association_dispose(result);
        tds_tungsten_association_move(result, &next);
    }

    return TDS_TUNGSTEN_OK;
}

tds_tungsten_status tds_tungsten_association_remove_at(
    const tds_tungsten_association* association,
    size_t index,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    const tds_tungsten_assoc_entry* entry = tds_tungsten_tree_at(association->root, index);
    if (entry == NULL) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    struct tds_tungsten_assoc_node* root = NULL;
    tds_tungsten_status status = tds_tungsten_tree_delete_at(association->context, association->root, index, &root);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    tds_hamt_map index_side;
    status = tds_tungsten_from_hamt(tds_hamt_map_remove(&association->index, entry->key, &index_side));
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(association->context, root);
        return status;
    }

    return tds_tungsten_association_take_parts(association, root, index_side, result);
}

static tds_tungsten_status tds_tungsten_index_remove_tree(
    const struct tds_tungsten_assoc_context* context,
    tds_hamt_map* index,
    const struct tds_tungsten_assoc_node* node)
{
    if (node == NULL) {
        return TDS_TUNGSTEN_OK;
    }

    tds_tungsten_status status = tds_tungsten_index_remove_tree(context, index, node->left);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    tds_hamt_map next;
    status = tds_tungsten_from_hamt(tds_hamt_map_remove(index, node->entry.key, &next));
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }
    tds_hamt_map_destroy(index);
    *index = next;

    return tds_tungsten_index_remove_tree(context, index, node->right);
}

tds_tungsten_status tds_tungsten_association_slice(
    const tds_tungsten_association* association,
    size_t index,
    size_t count,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (index > tds_tungsten_node_size(association->root) ||
        count > tds_tungsten_node_size(association->root) - index) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    if (count == tds_tungsten_node_size(association->root)) {
        return tds_tungsten_association_copy(association, result);
    }

    struct tds_tungsten_assoc_node* before = NULL;
    struct tds_tungsten_assoc_node* rest = NULL;
    tds_tungsten_status status =
        tds_tungsten_tree_split(association->context, association->root, index, &before, &rest);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    struct tds_tungsten_assoc_node* kept = NULL;
    struct tds_tungsten_assoc_node* after = NULL;
    status = tds_tungsten_tree_split(association->context, rest, count, &kept, &after);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(association->context, before);
        tds_tungsten_node_release(association->context, rest);
        return status;
    }

    tds_hamt_map index_side;
    const size_t removed_count = tds_tungsten_node_size(association->root) - tds_tungsten_node_size(kept);
    if (tds_tungsten_node_size(kept) <= removed_count) {
        status = tds_tungsten_index_from_tree(association->context, kept, &index_side);
    } else {
        index_side = tds_hamt_map_clone(&association->index);
        status = tds_tungsten_index_remove_tree(association->context, &index_side, before);
        if (status == TDS_TUNGSTEN_OK) {
            status = tds_tungsten_index_remove_tree(association->context, &index_side, after);
        }
        if (status != TDS_TUNGSTEN_OK) {
            tds_hamt_map_destroy(&index_side);
        }
    }

    tds_tungsten_node_release(association->context, before);
    tds_tungsten_node_release(association->context, rest);
    tds_tungsten_node_release(association->context, after);
    if (status != TDS_TUNGSTEN_OK) {
        tds_tungsten_node_release(association->context, kept);
        return status;
    }

    return tds_tungsten_association_take_parts(association, kept, index_side, result);
}

tds_tungsten_status tds_tungsten_association_take(
    const tds_tungsten_association* association,
    size_t count,
    tds_tungsten_association* result)
{
    return tds_tungsten_association_slice(association, 0, count, result);
}

tds_tungsten_status tds_tungsten_association_drop(
    const tds_tungsten_association* association,
    size_t count,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association)) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    if (count > tds_tungsten_node_size(association->root)) {
        return TDS_TUNGSTEN_OUT_OF_RANGE;
    }

    return tds_tungsten_association_slice(
        association,
        count,
        tds_tungsten_node_size(association->root) - count,
        result);
}

tds_tungsten_status tds_tungsten_association_reverse(
    const tds_tungsten_association* association,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    const size_t count = tds_tungsten_node_size(association->root);
    if (count <= 1) {
        return tds_tungsten_association_copy(association, result);
    }

    tds_tungsten_assoc_entry_view* views = (tds_tungsten_assoc_entry_view*)malloc(count * sizeof(*views));
    if (views == NULL) {
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    size_t index = 0;
    tds_tungsten_tree_fill_views(association->root, views, &index);
    for (size_t left = 0, right = count - 1u; left < right; ++left, --right) {
        const tds_tungsten_assoc_entry_view temp = views[left];
        views[left] = views[right];
        views[right] = temp;
    }

    struct tds_tungsten_assoc_node* root = NULL;
    tds_hamt_map index_side;
    tds_tungsten_status status = tds_tungsten_rebuild_from_views(association->context, views, count, &root, &index_side);
    free(views);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    return tds_tungsten_association_take_parts(association, root, index_side, result);
}

static int tds_tungsten_compare_views(
    const tds_tungsten_assoc_entry_view* left,
    const tds_tungsten_assoc_entry_view* right,
    const tds_tungsten_sort_context* context)
{
    const void* left_value = context->by_key ? left->key : left->value;
    const void* right_value = context->by_key ? right->key : right->value;
    const int comparison = context->compare(left_value, right_value, context->compare_context);
    if (comparison != 0) {
        return comparison;
    }

    return (left->stamp > right->stamp) - (left->stamp < right->stamp);
}

static void tds_tungsten_merge_sort_views(
    tds_tungsten_assoc_entry_view* values,
    tds_tungsten_assoc_entry_view* scratch,
    size_t count,
    const tds_tungsten_sort_context* context)
{
    if (count < 2) {
        return;
    }

    const size_t middle = count / 2u;
    tds_tungsten_merge_sort_views(values, scratch, middle, context);
    tds_tungsten_merge_sort_views(values + middle, scratch + middle, count - middle, context);

    size_t left = 0;
    size_t right = middle;
    size_t output = 0;
    while (left != middle && right != count) {
        if (tds_tungsten_compare_views(&values[left], &values[right], context) <= 0) {
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

static tds_tungsten_status tds_tungsten_association_sort_core(
    const tds_tungsten_association* association,
    ft_compare_fn compare,
    void* compare_context,
    bool by_key,
    tds_tungsten_association* result)
{
    if (!tds_tungsten_association_valid(association) || compare == NULL || result == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    const size_t count = tds_tungsten_node_size(association->root);
    if (count <= 1) {
        return tds_tungsten_association_copy(association, result);
    }

    tds_tungsten_assoc_entry_view* views = (tds_tungsten_assoc_entry_view*)malloc(count * sizeof(*views));
    tds_tungsten_assoc_entry_view* scratch = (tds_tungsten_assoc_entry_view*)malloc(count * sizeof(*scratch));
    if (views == NULL || scratch == NULL) {
        free(views);
        free(scratch);
        return TDS_TUNGSTEN_OUT_OF_MEMORY;
    }

    size_t index = 0;
    tds_tungsten_tree_fill_views(association->root, views, &index);

    tds_tungsten_sort_context context;
    context.compare = compare;
    context.compare_context = compare_context;
    context.by_key = by_key;
    tds_tungsten_merge_sort_views(views, scratch, count, &context);
    free(scratch);

    struct tds_tungsten_assoc_node* root = NULL;
    tds_hamt_map index_side;
    tds_tungsten_status status = tds_tungsten_rebuild_from_views(association->context, views, count, &root, &index_side);
    free(views);
    if (status != TDS_TUNGSTEN_OK) {
        return status;
    }

    return tds_tungsten_association_take_parts(association, root, index_side, result);
}

tds_tungsten_status tds_tungsten_association_key_sort(
    const tds_tungsten_association* association,
    ft_compare_fn compare_key,
    void* compare_context,
    tds_tungsten_association* result)
{
    return tds_tungsten_association_sort_core(association, compare_key, compare_context, true, result);
}

tds_tungsten_status tds_tungsten_association_sort(
    const tds_tungsten_association* association,
    ft_compare_fn compare_value,
    void* compare_context,
    tds_tungsten_association* result)
{
    return tds_tungsten_association_sort_core(association, compare_value, compare_context, false, result);
}

static void tds_tungsten_tree_visit_entries(
    const struct tds_tungsten_assoc_node* node,
    tds_tungsten_assoc_visit_fn visitor,
    void* context)
{
    if (node == NULL) {
        return;
    }

    tds_tungsten_tree_visit_entries(node->left, visitor, context);
    visitor(node->entry.key, node->entry.value, context);
    tds_tungsten_tree_visit_entries(node->right, visitor, context);
}

tds_tungsten_status tds_tungsten_association_visit(
    const tds_tungsten_association* association,
    tds_tungsten_assoc_visit_fn visitor,
    void* context)
{
    if (!tds_tungsten_association_valid(association) || visitor == NULL) {
        return TDS_TUNGSTEN_INVALID_ARGUMENT;
    }

    tds_tungsten_tree_visit_entries(association->root, visitor, context);
    return TDS_TUNGSTEN_OK;
}
