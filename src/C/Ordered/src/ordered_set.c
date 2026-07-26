/*
 * Implementation of the insertion-ordered persistent set.
 */

#include <durable7/ordered/ordered_set.h>

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum {
    D7_ORDERED_STAMP_GAP = 1 << 20
};

typedef struct d7_ordered_item_cell d7_ordered_item_cell;

#if defined(_MSC_VER) && !defined(__clang__)
/* The UCRT C <stddef.h> does not declare max_align_t even in /std:c17 mode.
 * The Windows ABI gives long double the maximum fundamental alignment that
 * malloc must support. Other C17 implementations use the standard type. */
typedef long double d7_ordered_max_align_t;
#else
typedef max_align_t d7_ordered_max_align_t;
#endif

typedef struct d7_ordered_entry {
    int64_t stamp;
    d7_ordered_item_cell* cell;
} d7_ordered_entry;

typedef struct d7_ordered_key_ref {
    const void* item;
    d7_ordered_item_cell* cell;
} d7_ordered_key_ref;

struct d7_ordered_context {
    size_t ref_count;
    d7_ordered_policy policy;
    ft_tree_policy order_policy;
    d7_hamt_policy map_policy;
};

/* The max_align_t member makes data suitably aligned without relying on a
 * zero-length or flexible array, both of which are noisy under strict MSVC. */
struct d7_ordered_item_cell {
    size_t ref_count;
    struct d7_ordered_context* context;
    d7_ordered_max_align_t alignment;
    unsigned char data[1];
};

typedef struct d7_ordered_cell_buffer {
    d7_ordered_item_cell** cells;
    size_t count;
} d7_ordered_cell_buffer;

static d7_ordered_status d7_ordered_from_ft(ft_status status)
{
    switch (status) {
    case FT_STATUS_OK:
        return D7_ORDERED_OK;
    case FT_STATUS_OUT_OF_RANGE:
        return D7_ORDERED_OUT_OF_RANGE;
    case FT_STATUS_EMPTY:
        return D7_ORDERED_EMPTY;
    case FT_STATUS_NOT_FOUND:
        return D7_ORDERED_NOT_FOUND;
    case FT_STATUS_NO_MEMORY:
        return D7_ORDERED_OUT_OF_MEMORY;
    case FT_STATUS_OVERFLOW:
        return D7_ORDERED_OVERFLOW;
    case FT_STATUS_INVALID_ARGUMENT:
    case FT_STATUS_ALREADY_EXISTS:
    case FT_STATUS_CALLBACK_FAILURE:
    case FT_STATUS_CRYPTO_FAILURE:
    case FT_STATUS_INCOMPATIBLE_POLICY:
    case FT_STATUS_INCONSISTENT_POLICY:
    default:
        return D7_ORDERED_INVALID_ARGUMENT;
    }
}

static d7_ordered_status d7_ordered_from_hamt(d7_hamt_status status)
{
    switch (status) {
    case D7_HAMT_OK:
        return D7_ORDERED_OK;
    case D7_HAMT_OUT_OF_MEMORY:
        return D7_ORDERED_OUT_OF_MEMORY;
    case D7_HAMT_OVERFLOW:
        return D7_ORDERED_OVERFLOW;
    case D7_HAMT_DUPLICATE_KEY:
    case D7_HAMT_INVALID_ARGUMENT:
    case D7_HAMT_TRANSIENT_CONSUMED:
    case D7_HAMT_TRANSIENT_MODIFIED:
    default:
        return D7_ORDERED_INVALID_ARGUMENT;
    }
}

static void d7_ordered_item_copy(
    const struct d7_ordered_context* context,
    void* destination,
    const void* source)
{
    if (context->policy.item_type.copy != NULL) {
        context->policy.item_type.copy(
            destination,
            source,
            context->policy.item_type.context);
    } else {
        (void)memcpy(destination, source, context->policy.item_type.size);
    }
}

static void d7_ordered_item_destroy(
    const struct d7_ordered_context* context,
    void* item)
{
    if (context->policy.item_type.destroy != NULL) {
        context->policy.item_type.destroy(item, context->policy.item_type.context);
    }
}

static bool d7_ordered_items_equal(
    const struct d7_ordered_context* context,
    const void* left,
    const void* right)
{
    if (left == right) {
        return true;
    }
    if (context->policy.equal != NULL) {
        return context->policy.equal(left, right, context->policy.context);
    }
    return memcmp(left, right, context->policy.item_type.size) == 0;
}

static d7_ordered_item_cell* d7_ordered_cell_create(
    struct d7_ordered_context* context,
    const void* item)
{
    const size_t prefix = offsetof(d7_ordered_item_cell, data);
    if (context->policy.item_type.size > SIZE_MAX - prefix) {
        return NULL;
    }

    size_t allocation_size = prefix + context->policy.item_type.size;
    if (allocation_size < sizeof(d7_ordered_item_cell)) {
        allocation_size = sizeof(d7_ordered_item_cell);
    }
    d7_ordered_item_cell* cell =
        (d7_ordered_item_cell*)malloc(allocation_size);
    if (cell == NULL) {
        return NULL;
    }

    cell->ref_count = 1u;
    cell->context = context;
    d7_ordered_item_copy(context, cell->data, item);
    return cell;
}

static d7_ordered_item_cell* d7_ordered_cell_retain(d7_ordered_item_cell* cell)
{
    if (cell == NULL || cell->ref_count == SIZE_MAX) {
        return NULL;
    }
    ++cell->ref_count;
    return cell;
}

static void d7_ordered_cell_release(d7_ordered_item_cell* cell)
{
    if (cell == NULL) {
        return;
    }
    --cell->ref_count;
    if (cell->ref_count == 0u) {
        d7_ordered_item_destroy(cell->context, cell->data);
        free(cell);
    }
}

static void d7_ordered_entry_copy(void* destination, const void* source, void* raw_context)
{
    (void)raw_context;
    d7_ordered_entry* target = (d7_ordered_entry*)destination;
    const d7_ordered_entry* value = (const d7_ordered_entry*)source;
    target->stamp = value->stamp;
    target->cell = d7_ordered_cell_retain(value->cell);
}

static void d7_ordered_entry_destroy(void* value, void* raw_context)
{
    (void)raw_context;
    d7_ordered_entry* entry = (d7_ordered_entry*)value;
    d7_ordered_cell_release(entry->cell);
    entry->cell = NULL;
}

static uint32_t d7_ordered_map_hash(const void* value, void* raw_context)
{
    const struct d7_ordered_context* context =
        (const struct d7_ordered_context*)raw_context;
    const d7_ordered_key_ref* key = (const d7_ordered_key_ref*)value;
    return context->policy.hash(key->item, context->policy.context);
}

static bool d7_ordered_map_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    const struct d7_ordered_context* context =
        (const struct d7_ordered_context*)raw_context;
    return d7_ordered_items_equal(
        context,
        ((const d7_ordered_key_ref*)left)->item,
        ((const d7_ordered_key_ref*)right)->item);
}

static bool d7_ordered_map_value_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    (void)raw_context;
    return left == right ||
        (left != NULL && right != NULL && *(const int64_t*)left == *(const int64_t*)right);
}

static void* d7_ordered_map_retain_key(const void* value, void* raw_context)
{
    struct d7_ordered_context* context = (struct d7_ordered_context*)raw_context;
    const d7_ordered_key_ref* source = (const d7_ordered_key_ref*)value;
    d7_ordered_key_ref* target = (d7_ordered_key_ref*)malloc(sizeof(*target));
    if (target == NULL) {
        return NULL;
    }

    target->cell = source->cell != NULL
        ? d7_ordered_cell_retain(source->cell)
        : d7_ordered_cell_create(context, source->item);
    if (target->cell == NULL) {
        free(target);
        return NULL;
    }
    target->item = target->cell->data;
    return target;
}

static void* d7_ordered_map_retain_value(const void* value, void* raw_context)
{
    (void)raw_context;
    int64_t* target = (int64_t*)malloc(sizeof(*target));
    if (target != NULL) {
        *target = *(const int64_t*)value;
    }
    return target;
}

static void d7_ordered_map_release_key(void* value, void* raw_context)
{
    (void)raw_context;
    d7_ordered_key_ref* key = (d7_ordered_key_ref*)value;
    if (key != NULL) {
        d7_ordered_cell_release(key->cell);
        free(key);
    }
}

static void d7_ordered_map_release_value(void* value, void* raw_context)
{
    (void)raw_context;
    free(value);
}

static struct d7_ordered_context* d7_ordered_context_create(
    const d7_ordered_policy* policy)
{
    if (policy == NULL || policy->item_type.size == 0u || policy->hash == NULL) {
        return NULL;
    }

    struct d7_ordered_context* context =
        (struct d7_ordered_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return NULL;
    }

    (void)memset(context, 0, sizeof(*context));
    context->ref_count = 1u;
    context->policy = *policy;

    ft_value_type entry_type;
    ft_value_type_init(&entry_type, sizeof(d7_ordered_entry));
    entry_type.copy = d7_ordered_entry_copy;
    entry_type.destroy = d7_ordered_entry_destroy;
    entry_type.context = context;
    ft_tree_policy_init_size(&context->order_policy, &entry_type);

    context->map_policy.hash = d7_ordered_map_hash;
    context->map_policy.key_equal = d7_ordered_map_key_equal;
    context->map_policy.value_equal = d7_ordered_map_value_equal;
    context->map_policy.retain_key = d7_ordered_map_retain_key;
    context->map_policy.retain_value = d7_ordered_map_retain_value;
    context->map_policy.release_key = d7_ordered_map_release_key;
    context->map_policy.release_value = d7_ordered_map_release_value;
    context->map_policy.context = context;
    return context;
}

static bool d7_ordered_context_retain(struct d7_ordered_context* context)
{
    if (context == NULL || context->ref_count == SIZE_MAX) {
        return false;
    }
    ++context->ref_count;
    return true;
}

static void d7_ordered_context_release(struct d7_ordered_context* context)
{
    if (context == NULL) {
        return;
    }
    --context->ref_count;
    if (context->ref_count == 0u) {
        free(context);
    }
}

static bool d7_ordered_set_valid(const d7_ordered_set* set)
{
    return set != NULL && set->context != NULL && set->order.rep != NULL &&
        set->order.policy == &set->context->order_policy &&
        set->stamps.policy.context == set->context &&
        set->stamps.policy.hash == d7_ordered_map_hash &&
        set->stamps.policy.key_equal == d7_ordered_map_key_equal;
}

static d7_ordered_key_ref d7_ordered_probe(const void* item)
{
    d7_ordered_key_ref probe;
    probe.item = item;
    probe.cell = NULL;
    return probe;
}

static d7_ordered_key_ref d7_ordered_cell_key(d7_ordered_item_cell* cell)
{
    d7_ordered_key_ref key;
    key.item = cell->data;
    key.cell = cell;
    return key;
}

static void d7_ordered_entry_release(d7_ordered_entry* entry)
{
    if (entry != NULL) {
        d7_ordered_cell_release(entry->cell);
        entry->cell = NULL;
    }
}

static bool d7_ordered_entry_at(
    const ft_persistent_deque* order,
    size_t index,
    d7_ordered_entry* entry)
{
    return ft_persistent_deque_at(order, index, entry) == FT_STATUS_OK;
}

static d7_ordered_status d7_ordered_empty_with_context(
    struct d7_ordered_context* context,
    d7_ordered_set* result)
{
    if (!d7_ordered_context_retain(context)) {
        return D7_ORDERED_OVERFLOW;
    }

    d7_ordered_set candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    candidate.context = context;
    const ft_status status = ft_persistent_deque_init(
        &candidate.order,
        &context->order_policy);
    if (status != FT_STATUS_OK) {
        d7_ordered_context_release(context);
        return d7_ordered_from_ft(status);
    }
    candidate.stamps = d7_hamt_map_create(&context->map_policy);
    *result = candidate;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_publish_parts(
    const d7_ordered_set* source,
    ft_persistent_deque* order,
    d7_hamt_map* stamps,
    d7_ordered_set* result)
{
    if (!d7_ordered_context_retain(source->context)) {
        return D7_ORDERED_OVERFLOW;
    }

    d7_ordered_set candidate;
    candidate.context = source->context;
    candidate.order = *order;
    candidate.stamps = *stamps;
    (void)memset(order, 0, sizeof(*order));
    (void)memset(stamps, 0, sizeof(*stamps));
    *result = candidate;
    return D7_ORDERED_OK;
}

void d7_ordered_policy_init(
    d7_ordered_policy* policy,
    const ft_value_type* item_type,
    d7_ordered_hash_fn hash,
    d7_ordered_equal_fn equal,
    void* context)
{
    if (policy == NULL) {
        return;
    }
    (void)memset(policy, 0, sizeof(*policy));
    if (item_type != NULL) {
        policy->item_type = *item_type;
    }
    policy->hash = hash;
    policy->equal = equal;
    policy->context = context;
}

d7_ordered_status d7_ordered_set_init(
    d7_ordered_set* set,
    const d7_ordered_policy* policy)
{
    if (set == NULL || policy == NULL || policy->item_type.size == 0u || policy->hash == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (policy->item_type.size > SIZE_MAX - offsetof(d7_ordered_item_cell, data)) {
        return D7_ORDERED_OVERFLOW;
    }

    struct d7_ordered_context* context = d7_ordered_context_create(policy);
    if (context == NULL) {
        return D7_ORDERED_OUT_OF_MEMORY;
    }

    d7_ordered_set candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    candidate.context = context;
    const ft_status status = ft_persistent_deque_init(
        &candidate.order,
        &context->order_policy);
    if (status != FT_STATUS_OK) {
        d7_ordered_context_release(context);
        return d7_ordered_from_ft(status);
    }
    candidate.stamps = d7_hamt_map_create(&context->map_policy);
    *set = candidate;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_clone(
    const d7_ordered_set* source,
    d7_ordered_set* destination)
{
    if (!d7_ordered_set_valid(source) || destination == NULL || destination == source) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (!d7_ordered_context_retain(source->context)) {
        return D7_ORDERED_OVERFLOW;
    }

    d7_ordered_set candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    candidate.context = source->context;
    const ft_status status = ft_persistent_deque_copy(&source->order, &candidate.order);
    if (status != FT_STATUS_OK) {
        d7_ordered_context_release(source->context);
        return d7_ordered_from_ft(status);
    }
    candidate.stamps = d7_hamt_map_clone(&source->stamps);
    *destination = candidate;
    return D7_ORDERED_OK;
}

void d7_ordered_set_move(d7_ordered_set* destination, d7_ordered_set* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void d7_ordered_set_destroy(d7_ordered_set* set)
{
    if (set == NULL) {
        return;
    }
    struct d7_ordered_context* context = set->context;
    if (set->order.rep != NULL) {
        ft_persistent_deque_dispose(&set->order);
    }
    if (context != NULL) {
        d7_hamt_map_destroy(&set->stamps);
        d7_ordered_context_release(context);
    }
    (void)memset(set, 0, sizeof(*set));
}

const d7_ordered_policy* d7_ordered_set_policy(const d7_ordered_set* set)
{
    return d7_ordered_set_valid(set) ? &set->context->policy : NULL;
}

bool d7_ordered_set_empty(const d7_ordered_set* set)
{
    return !d7_ordered_set_valid(set) || ft_persistent_deque_empty(&set->order);
}

size_t d7_ordered_set_size(const d7_ordered_set* set)
{
    return d7_ordered_set_valid(set) ? ft_persistent_deque_size(&set->order) : 0u;
}

bool d7_ordered_set_contains(const d7_ordered_set* set, const void* item)
{
    if (!d7_ordered_set_valid(set) || item == NULL) {
        return false;
    }
    const d7_ordered_key_ref probe = d7_ordered_probe(item);
    return d7_hamt_map_contains_key(&set->stamps, &probe);
}

bool d7_ordered_set_try_get_value(
    const d7_ordered_set* set,
    const void* equal_item,
    const void** actual_item)
{
    if (actual_item != NULL) {
        *actual_item = equal_item;
    }
    if (!d7_ordered_set_valid(set) || equal_item == NULL) {
        return false;
    }

    const d7_ordered_key_ref probe = d7_ordered_probe(equal_item);
    const void* actual_key = NULL;
    const bool found = d7_hamt_map_try_get_key(&set->stamps, &probe, &actual_key);
    if (found && actual_item != NULL) {
        *actual_item = ((const d7_ordered_key_ref*)actual_key)->item;
    }
    return found;
}

d7_ordered_status d7_ordered_set_at(
    const d7_ordered_set* set,
    size_t index,
    const void** item)
{
    if (!d7_ordered_set_valid(set) || item == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (index >= ft_persistent_deque_size(&set->order)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }

    d7_ordered_entry entry;
    if (!d7_ordered_entry_at(&set->order, index, &entry) || entry.cell == NULL) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    *item = entry.cell->data;
    d7_ordered_entry_release(&entry);
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_front(const d7_ordered_set* set, const void** item)
{
    if (!d7_ordered_set_valid(set) || item == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return D7_ORDERED_EMPTY;
    }
    return d7_ordered_set_at(set, 0u, item);
}

d7_ordered_status d7_ordered_set_back(const d7_ordered_set* set, const void** item)
{
    if (!d7_ordered_set_valid(set) || item == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count == 0u) {
        return D7_ORDERED_EMPTY;
    }
    return d7_ordered_set_at(set, count - 1u, item);
}

static bool d7_ordered_locate_stamp(
    const ft_persistent_deque* order,
    int64_t stamp,
    size_t* index)
{
    size_t low = 0u;
    size_t high = ft_persistent_deque_size(order);
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        d7_ordered_entry entry;
        if (!d7_ordered_entry_at(order, middle, &entry) || entry.cell == NULL) {
            return false;
        }
        const int64_t middle_stamp = entry.stamp;
        d7_ordered_entry_release(&entry);
        if (middle_stamp < stamp) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }

    if (low >= ft_persistent_deque_size(order)) {
        return false;
    }
    d7_ordered_entry entry;
    if (!d7_ordered_entry_at(order, low, &entry) || entry.cell == NULL) {
        return false;
    }
    const bool found = entry.stamp == stamp;
    d7_ordered_entry_release(&entry);
    if (found && index != NULL) {
        *index = low;
    }
    return found;
}

bool d7_ordered_set_index_of(
    const d7_ordered_set* set,
    const void* equal_item,
    size_t* index)
{
    if (!d7_ordered_set_valid(set) || equal_item == NULL) {
        return false;
    }
    const d7_ordered_key_ref probe = d7_ordered_probe(equal_item);
    const void* value = NULL;
    if (!d7_hamt_map_try_get(&set->stamps, &probe, &value) || value == NULL) {
        return false;
    }
    return d7_ordered_locate_stamp(&set->order, *(const int64_t*)value, index);
}

static void d7_ordered_collect_cell(const void* value, void* raw_buffer)
{
    d7_ordered_cell_buffer* buffer = (d7_ordered_cell_buffer*)raw_buffer;
    const d7_ordered_entry* entry = (const d7_ordered_entry*)value;
    buffer->cells[buffer->count++] = entry->cell;
}

static d7_ordered_status d7_ordered_materialize_cells(
    const ft_persistent_deque* order,
    d7_ordered_item_cell*** cells,
    size_t* count)
{
    const size_t local_count = ft_persistent_deque_size(order);
    if (local_count > SIZE_MAX / sizeof(d7_ordered_item_cell*)) {
        return D7_ORDERED_OVERFLOW;
    }
    d7_ordered_item_cell** local_cells = local_count == 0u
        ? NULL
        : (d7_ordered_item_cell**)malloc(local_count * sizeof(*local_cells));
    if (local_count != 0u && local_cells == NULL) {
        return D7_ORDERED_OUT_OF_MEMORY;
    }

    d7_ordered_cell_buffer buffer;
    buffer.cells = local_cells;
    buffer.count = 0u;
    const ft_status status = ft_persistent_deque_visit(order, d7_ordered_collect_cell, &buffer);
    if (status != FT_STATUS_OK || buffer.count != local_count) {
        free(local_cells);
        return status == FT_STATUS_OK
            ? D7_ORDERED_INVARIANT_VIOLATION
            : d7_ordered_from_ft(status);
    }
    *cells = local_cells;
    *count = local_count;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_choose_stamp(
    const ft_persistent_deque* order,
    size_t index,
    bool* found,
    int64_t* stamp)
{
    const size_t count = ft_persistent_deque_size(order);
    if (index > count || found == NULL || stamp == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (count == 0u) {
        *found = true;
        *stamp = 0;
        return D7_ORDERED_OK;
    }

    if (index == 0u) {
        d7_ordered_entry next;
        if (!d7_ordered_entry_at(order, 0u, &next) || next.cell == NULL) {
            return D7_ORDERED_INVARIANT_VIOLATION;
        }
        if (next.stamp >= INT64_MIN + (int64_t)D7_ORDERED_STAMP_GAP) {
            *stamp = next.stamp - (int64_t)D7_ORDERED_STAMP_GAP;
            *found = true;
        } else if (next.stamp > INT64_MIN) {
            *stamp = next.stamp - 1;
            *found = true;
        } else {
            *found = false;
        }
        d7_ordered_entry_release(&next);
        return D7_ORDERED_OK;
    }

    if (index == count) {
        d7_ordered_entry previous;
        if (!d7_ordered_entry_at(order, count - 1u, &previous) || previous.cell == NULL) {
            return D7_ORDERED_INVARIANT_VIOLATION;
        }
        if (previous.stamp <= INT64_MAX - (int64_t)D7_ORDERED_STAMP_GAP) {
            *stamp = previous.stamp + (int64_t)D7_ORDERED_STAMP_GAP;
            *found = true;
        } else if (previous.stamp < INT64_MAX) {
            *stamp = previous.stamp + 1;
            *found = true;
        } else {
            *found = false;
        }
        d7_ordered_entry_release(&previous);
        return D7_ORDERED_OK;
    }

    d7_ordered_entry previous;
    d7_ordered_entry next;
    if (!d7_ordered_entry_at(order, index - 1u, &previous) || previous.cell == NULL) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    if (!d7_ordered_entry_at(order, index, &next) || next.cell == NULL) {
        d7_ordered_entry_release(&previous);
        return D7_ORDERED_INVARIANT_VIOLATION;
    }

    const uint64_t distance = (uint64_t)next.stamp - (uint64_t)previous.stamp;
    if (distance <= 1u) {
        *found = false;
    } else {
        *stamp = previous.stamp + (int64_t)(distance / 2u);
        *found = true;
    }
    d7_ordered_entry_release(&previous);
    d7_ordered_entry_release(&next);
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_append_exact(
    d7_ordered_set* set,
    d7_ordered_item_cell* cell,
    int64_t stamp)
{
    d7_ordered_entry entry;
    entry.stamp = stamp;
    entry.cell = cell;
    ft_persistent_deque next_order;
    const ft_status order_status = ft_persistent_deque_push_back(
        &set->order,
        &entry,
        &next_order);
    if (order_status != FT_STATUS_OK) {
        return d7_ordered_from_ft(order_status);
    }

    const d7_ordered_key_ref key = d7_ordered_cell_key(cell);
    d7_hamt_map next_stamps;
    const d7_hamt_status map_status = d7_hamt_map_add(
        &set->stamps,
        &key,
        &stamp,
        &next_stamps);
    if (map_status != D7_HAMT_OK) {
        ft_persistent_deque_dispose(&next_order);
        return map_status == D7_HAMT_DUPLICATE_KEY
            ? D7_ORDERED_INVARIANT_VIOLATION
            : d7_ordered_from_hamt(map_status);
    }

    ft_persistent_deque_dispose(&set->order);
    d7_hamt_map_destroy(&set->stamps);
    set->order = next_order;
    set->stamps = next_stamps;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_rebuild_cells(
    const d7_ordered_set* source,
    d7_ordered_item_cell* const* cells,
    size_t count,
    d7_ordered_set* result)
{
    const size_t left_count = count / 2u;
    const size_t right_count = count == 0u ? 0u : count - 1u - left_count;
    const size_t label_limit = (size_t)(INT64_MAX / (int64_t)D7_ORDERED_STAMP_GAP);
    if (left_count > label_limit || right_count > label_limit) {
        return D7_ORDERED_OVERFLOW;
    }

    d7_ordered_set candidate;
    d7_ordered_status status = d7_ordered_empty_with_context(source->context, &candidate);
    if (status != D7_ORDERED_OK) {
        return status;
    }

    int64_t stamp = -(int64_t)(left_count * (size_t)D7_ORDERED_STAMP_GAP);
    for (size_t index = 0u; index != count; ++index) {
        status = d7_ordered_append_exact(&candidate, cells[index], stamp);
        if (status != D7_ORDERED_OK) {
            d7_ordered_set_destroy(&candidate);
            return status;
        }
        if (index + 1u != count) {
            stamp += (int64_t)D7_ORDERED_STAMP_GAP;
        }
    }
    *result = candidate;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_insert_cell(
    const d7_ordered_set* source,
    size_t index,
    d7_ordered_item_cell* cell,
    bool update_existing_stamp,
    d7_ordered_set* result)
{
    bool has_stamp = false;
    int64_t stamp = 0;
    d7_ordered_status status = d7_ordered_choose_stamp(
        &source->order,
        index,
        &has_stamp,
        &stamp);
    if (status != D7_ORDERED_OK) {
        return status;
    }

    if (!has_stamp) {
        d7_ordered_item_cell** old_cells = NULL;
        size_t old_count = 0u;
        status = d7_ordered_materialize_cells(&source->order, &old_cells, &old_count);
        if (status != D7_ORDERED_OK) {
            return status;
        }
        if (old_count == SIZE_MAX || old_count + 1u > SIZE_MAX / sizeof(*old_cells)) {
            free(old_cells);
            return D7_ORDERED_OVERFLOW;
        }
        d7_ordered_item_cell** cells = (d7_ordered_item_cell**)malloc(
            (old_count + 1u) * sizeof(*cells));
        if (cells == NULL) {
            free(old_cells);
            return D7_ORDERED_OUT_OF_MEMORY;
        }
        if (index != 0u) {
            (void)memcpy(cells, old_cells, index * sizeof(*cells));
        }
        cells[index] = cell;
        if (index != old_count) {
            (void)memcpy(
                cells + index + 1u,
                old_cells + index,
                (old_count - index) * sizeof(*cells));
        }
        status = d7_ordered_rebuild_cells(source, cells, old_count + 1u, result);
        free(cells);
        free(old_cells);
        return status;
    }

    d7_ordered_entry entry;
    entry.stamp = stamp;
    entry.cell = cell;
    ft_persistent_deque order;
    const ft_status order_status = ft_persistent_deque_insert_at(
        &source->order,
        index,
        &entry,
        &order);
    if (order_status != FT_STATUS_OK) {
        return d7_ordered_from_ft(order_status);
    }

    const d7_ordered_key_ref key = d7_ordered_cell_key(cell);
    d7_hamt_map stamps;
    const d7_hamt_status map_status = update_existing_stamp
        ? d7_hamt_map_set(&source->stamps, &key, &stamp, &stamps)
        : d7_hamt_map_add(&source->stamps, &key, &stamp, &stamps);
    if (map_status != D7_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return map_status == D7_HAMT_DUPLICATE_KEY
            ? D7_ORDERED_INVARIANT_VIOLATION
            : d7_ordered_from_hamt(map_status);
    }

    status = d7_ordered_publish_parts(source, &order, &stamps, result);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        d7_hamt_map_destroy(&stamps);
    }
    return status;
}

static d7_ordered_status d7_ordered_add_at_core(
    const d7_ordered_set* set,
    size_t index,
    const void* item,
    d7_ordered_set* result)
{
    const d7_ordered_key_ref probe = d7_ordered_probe(item);
    if (d7_hamt_map_contains_key(&set->stamps, &probe)) {
        return d7_ordered_set_clone(set, result);
    }

    d7_ordered_item_cell* cell = d7_ordered_cell_create(set->context, item);
    if (cell == NULL) {
        return D7_ORDERED_OUT_OF_MEMORY;
    }
    const d7_ordered_status status = d7_ordered_insert_cell(
        set,
        index,
        cell,
        false,
        result);
    d7_ordered_cell_release(cell);
    return status;
}

d7_ordered_status d7_ordered_set_add(
    const d7_ordered_set* set,
    const void* item,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_add_at_core(
        set,
        ft_persistent_deque_size(&set->order),
        item,
        result);
}

d7_ordered_status d7_ordered_set_add_first(
    const d7_ordered_set* set,
    const void* item,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    return d7_ordered_add_at_core(set, 0u, item, result);
}

d7_ordered_status d7_ordered_set_insert(
    const d7_ordered_set* set,
    size_t index,
    const void* item,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (index > ft_persistent_deque_size(&set->order)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_add_at_core(set, index, item, result);
}

d7_ordered_status d7_ordered_set_from_items(
    d7_ordered_set* set,
    const d7_ordered_policy* policy,
    const void* const* items,
    size_t item_count)
{
    if (set == NULL || policy == NULL || (items == NULL && item_count != 0u)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }

    d7_ordered_set candidate;
    d7_ordered_status status = d7_ordered_set_init(&candidate, policy);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    for (size_t index = 0u; index != item_count; ++index) {
        if (items[index] == NULL) {
            d7_ordered_set_destroy(&candidate);
            return D7_ORDERED_INVALID_ARGUMENT;
        }
        d7_ordered_set next;
        status = d7_ordered_set_add(&candidate, items[index], &next);
        if (status != D7_ORDERED_OK) {
            d7_ordered_set_destroy(&candidate);
            return status;
        }
        d7_ordered_set_destroy(&candidate);
        d7_ordered_set_move(&candidate, &next);
    }
    *set = candidate;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_from_array(
    d7_ordered_set* set,
    const d7_ordered_policy* policy,
    const void* items,
    size_t item_count)
{
    if (set == NULL || policy == NULL || policy->item_type.size == 0u ||
        (items == NULL && item_count != 0u)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (item_count > SIZE_MAX / policy->item_type.size) {
        return D7_ORDERED_OVERFLOW;
    }

    d7_ordered_set candidate;
    d7_ordered_status status = d7_ordered_set_init(&candidate, policy);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    const unsigned char* bytes = (const unsigned char*)items;
    for (size_t index = 0u; index != item_count; ++index) {
        d7_ordered_set next;
        status = d7_ordered_set_add(
            &candidate,
            bytes + index * policy->item_type.size,
            &next);
        if (status != D7_ORDERED_OK) {
            d7_ordered_set_destroy(&candidate);
            return status;
        }
        d7_ordered_set_destroy(&candidate);
        d7_ordered_set_move(&candidate, &next);
    }
    *set = candidate;
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_move_to(
    const d7_ordered_set* set,
    size_t index,
    const void* equal_item,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (index >= count) {
        return D7_ORDERED_OUT_OF_RANGE;
    }

    const d7_ordered_key_ref probe = d7_ordered_probe(equal_item);
    const void* stamp_value = NULL;
    const void* actual_key_value = NULL;
    if (!d7_hamt_map_try_get(&set->stamps, &probe, &stamp_value) || stamp_value == NULL ||
        !d7_hamt_map_try_get_key(&set->stamps, &probe, &actual_key_value)) {
        return D7_ORDERED_NOT_FOUND;
    }

    size_t old_index = 0u;
    if (!d7_ordered_locate_stamp(&set->order, *(const int64_t*)stamp_value, &old_index)) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    if (old_index == index) {
        return d7_ordered_set_clone(set, result);
    }

    d7_ordered_entry moved;
    if (!d7_ordered_entry_at(&set->order, old_index, &moved) || moved.cell == NULL) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    const d7_ordered_key_ref* actual_key = (const d7_ordered_key_ref*)actual_key_value;
    if (actual_key->cell != moved.cell) {
        d7_ordered_entry_release(&moved);
        return D7_ORDERED_INVARIANT_VIOLATION;
    }

    ft_persistent_deque reduced_order;
    const ft_status remove_status = ft_persistent_deque_remove_at(
        &set->order,
        old_index,
        &reduced_order);
    if (remove_status != FT_STATUS_OK) {
        d7_ordered_entry_release(&moved);
        return d7_ordered_from_ft(remove_status);
    }

    bool has_stamp = false;
    int64_t stamp = 0;
    d7_ordered_status status = d7_ordered_choose_stamp(
        &reduced_order,
        index,
        &has_stamp,
        &stamp);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&reduced_order);
        d7_ordered_entry_release(&moved);
        return status;
    }

    if (!has_stamp) {
        d7_ordered_item_cell** reduced_cells = NULL;
        size_t reduced_count = 0u;
        status = d7_ordered_materialize_cells(
            &reduced_order,
            &reduced_cells,
            &reduced_count);
        if (status != D7_ORDERED_OK) {
            ft_persistent_deque_dispose(&reduced_order);
            d7_ordered_entry_release(&moved);
            return status;
        }
        if (reduced_count == SIZE_MAX ||
            reduced_count + 1u > SIZE_MAX / sizeof(*reduced_cells)) {
            free(reduced_cells);
            ft_persistent_deque_dispose(&reduced_order);
            d7_ordered_entry_release(&moved);
            return D7_ORDERED_OVERFLOW;
        }
        d7_ordered_item_cell** cells = (d7_ordered_item_cell**)malloc(
            (reduced_count + 1u) * sizeof(*cells));
        if (cells == NULL) {
            free(reduced_cells);
            ft_persistent_deque_dispose(&reduced_order);
            d7_ordered_entry_release(&moved);
            return D7_ORDERED_OUT_OF_MEMORY;
        }
        if (index != 0u) {
            (void)memcpy(cells, reduced_cells, index * sizeof(*cells));
        }
        cells[index] = moved.cell;
        if (index != reduced_count) {
            (void)memcpy(
                cells + index + 1u,
                reduced_cells + index,
                (reduced_count - index) * sizeof(*cells));
        }
        status = d7_ordered_rebuild_cells(set, cells, reduced_count + 1u, result);
        free(cells);
        free(reduced_cells);
        ft_persistent_deque_dispose(&reduced_order);
        d7_ordered_entry_release(&moved);
        return status;
    }

    d7_ordered_entry replacement;
    replacement.stamp = stamp;
    replacement.cell = moved.cell;
    ft_persistent_deque order;
    const ft_status insert_status = ft_persistent_deque_insert_at(
        &reduced_order,
        index,
        &replacement,
        &order);
    ft_persistent_deque_dispose(&reduced_order);
    if (insert_status != FT_STATUS_OK) {
        d7_ordered_entry_release(&moved);
        return d7_ordered_from_ft(insert_status);
    }

    const d7_ordered_key_ref key = d7_ordered_cell_key(moved.cell);
    d7_hamt_map stamps;
    const d7_hamt_status map_status = d7_hamt_map_set(
        &set->stamps,
        &key,
        &stamp,
        &stamps);
    d7_ordered_entry_release(&moved);
    if (map_status != D7_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return d7_ordered_from_hamt(map_status);
    }

    status = d7_ordered_publish_parts(set, &order, &stamps, result);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        d7_hamt_map_destroy(&stamps);
    }
    return status;
}

d7_ordered_status d7_ordered_set_move_to_first(
    const d7_ordered_set* set,
    const void* equal_item,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return D7_ORDERED_NOT_FOUND;
    }
    return d7_ordered_set_move_to(set, 0u, equal_item, result);
}

d7_ordered_status d7_ordered_set_move_to_last(
    const d7_ordered_set* set,
    const void* equal_item,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count == 0u) {
        return D7_ORDERED_NOT_FOUND;
    }
    return d7_ordered_set_move_to(set, count - 1u, equal_item, result);
}

d7_ordered_status d7_ordered_set_try_remove(
    const d7_ordered_set* set,
    const void* equal_item,
    bool* removed,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const d7_ordered_key_ref probe = d7_ordered_probe(equal_item);
    const void* stamp_value = NULL;
    if (!d7_hamt_map_try_get(&set->stamps, &probe, &stamp_value) || stamp_value == NULL) {
        const d7_ordered_status status = d7_ordered_set_clone(set, result);
        if (status == D7_ORDERED_OK && removed != NULL) {
            *removed = false;
        }
        return status;
    }

    size_t index = 0u;
    if (!d7_ordered_locate_stamp(&set->order, *(const int64_t*)stamp_value, &index)) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }

    ft_persistent_deque order;
    const ft_status order_status = ft_persistent_deque_remove_at(
        &set->order,
        index,
        &order);
    if (order_status != FT_STATUS_OK) {
        return d7_ordered_from_ft(order_status);
    }

    d7_hamt_map stamps;
    const d7_hamt_status map_status = d7_hamt_map_remove(
        &set->stamps,
        &probe,
        &stamps);
    if (map_status != D7_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return d7_ordered_from_hamt(map_status);
    }

    const d7_ordered_status status = d7_ordered_publish_parts(
        set,
        &order,
        &stamps,
        result);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        d7_hamt_map_destroy(&stamps);
        return status;
    }
    if (removed != NULL) {
        *removed = true;
    }
    return D7_ORDERED_OK;
}

d7_ordered_status d7_ordered_set_remove(
    const d7_ordered_set* set,
    const void* equal_item,
    d7_ordered_set* result)
{
    return d7_ordered_set_try_remove(set, equal_item, NULL, result);
}

d7_ordered_status d7_ordered_set_remove_at(
    const d7_ordered_set* set,
    size_t index,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (index >= ft_persistent_deque_size(&set->order)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }

    d7_ordered_entry removed_entry;
    if (!d7_ordered_entry_at(&set->order, index, &removed_entry) ||
        removed_entry.cell == NULL) {
        return D7_ORDERED_INVARIANT_VIOLATION;
    }
    ft_persistent_deque order;
    const ft_status order_status = ft_persistent_deque_remove_at(
        &set->order,
        index,
        &order);
    if (order_status != FT_STATUS_OK) {
        d7_ordered_entry_release(&removed_entry);
        return d7_ordered_from_ft(order_status);
    }

    const d7_ordered_key_ref key = d7_ordered_cell_key(removed_entry.cell);
    d7_hamt_map stamps;
    const d7_hamt_status map_status = d7_hamt_map_remove(
        &set->stamps,
        &key,
        &stamps);
    d7_ordered_entry_release(&removed_entry);
    if (map_status != D7_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return d7_ordered_from_hamt(map_status);
    }

    const d7_ordered_status status = d7_ordered_publish_parts(
        set,
        &order,
        &stamps,
        result);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        d7_hamt_map_destroy(&stamps);
    }
    return status;
}

d7_ordered_status d7_ordered_set_remove_first(
    const d7_ordered_set* set,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return D7_ORDERED_EMPTY;
    }
    return d7_ordered_set_remove_at(set, 0u, result);
}

d7_ordered_status d7_ordered_set_remove_last(
    const d7_ordered_set* set,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count == 0u) {
        return D7_ORDERED_EMPTY;
    }
    return d7_ordered_set_remove_at(set, count - 1u, result);
}

d7_ordered_status d7_ordered_set_clear(
    const d7_ordered_set* set,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return d7_ordered_set_clone(set, result);
    }
    return d7_ordered_empty_with_context(set->context, result);
}

typedef struct d7_ordered_map_builder {
    d7_hamt_map map;
    d7_ordered_status status;
} d7_ordered_map_builder;

static void d7_ordered_add_entry_to_map(const void* value, void* raw_builder)
{
    d7_ordered_map_builder* builder = (d7_ordered_map_builder*)raw_builder;
    if (builder->status != D7_ORDERED_OK) {
        return;
    }
    const d7_ordered_entry* entry = (const d7_ordered_entry*)value;
    const d7_ordered_key_ref key = d7_ordered_cell_key(entry->cell);
    d7_hamt_map next;
    const d7_hamt_status status = d7_hamt_map_add(
        &builder->map,
        &key,
        &entry->stamp,
        &next);
    if (status != D7_HAMT_OK) {
        builder->status = status == D7_HAMT_DUPLICATE_KEY
            ? D7_ORDERED_INVARIANT_VIOLATION
            : d7_ordered_from_hamt(status);
        return;
    }
    d7_hamt_map_destroy(&builder->map);
    builder->map = next;
}

static d7_ordered_status d7_ordered_build_map(
    struct d7_ordered_context* context,
    const ft_persistent_deque* order,
    d7_hamt_map* map)
{
    d7_ordered_map_builder builder;
    builder.map = d7_hamt_map_create(&context->map_policy);
    builder.status = D7_ORDERED_OK;
    const ft_status visit_status = ft_persistent_deque_visit(
        order,
        d7_ordered_add_entry_to_map,
        &builder);
    if (visit_status != FT_STATUS_OK && builder.status == D7_ORDERED_OK) {
        builder.status = d7_ordered_from_ft(visit_status);
    }
    if (builder.status != D7_ORDERED_OK) {
        d7_hamt_map_destroy(&builder.map);
        return builder.status;
    }
    *map = builder.map;
    return D7_ORDERED_OK;
}

static void d7_ordered_remove_entry_from_map(const void* value, void* raw_builder)
{
    d7_ordered_map_builder* builder = (d7_ordered_map_builder*)raw_builder;
    if (builder->status != D7_ORDERED_OK) {
        return;
    }
    const d7_ordered_entry* entry = (const d7_ordered_entry*)value;
    const d7_ordered_key_ref key = d7_ordered_cell_key(entry->cell);
    if (!d7_hamt_map_contains_key(&builder->map, &key)) {
        builder->status = D7_ORDERED_INVARIANT_VIOLATION;
        return;
    }
    d7_hamt_map next;
    const d7_hamt_status status = d7_hamt_map_remove(&builder->map, &key, &next);
    if (status != D7_HAMT_OK) {
        builder->status = d7_ordered_from_hamt(status);
        return;
    }
    d7_hamt_map_destroy(&builder->map);
    builder->map = next;
}

static d7_ordered_status d7_ordered_remove_order_from_map(
    const ft_persistent_deque* order,
    d7_ordered_map_builder* builder)
{
    const ft_status visit_status = ft_persistent_deque_visit(
        order,
        d7_ordered_remove_entry_from_map,
        builder);
    if (visit_status != FT_STATUS_OK && builder->status == D7_ORDERED_OK) {
        builder->status = d7_ordered_from_ft(visit_status);
    }
    return builder->status;
}

d7_ordered_status d7_ordered_set_get_range(
    const d7_ordered_set* set,
    size_t index,
    size_t count,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t total = ft_persistent_deque_size(&set->order);
    if (index > total || count > total - index) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    if (count == total) {
        return d7_ordered_set_clone(set, result);
    }
    if (count == 0u) {
        return d7_ordered_empty_with_context(set->context, result);
    }

    ft_tree_split_result first;
    ft_status split_status = ft_persistent_deque_split_at(
        &set->order,
        index,
        &first);
    if (split_status != FT_STATUS_OK) {
        return d7_ordered_from_ft(split_status);
    }
    ft_tree_split_result second;
    split_status = ft_persistent_deque_split_at(&first.right, count, &second);
    ft_persistent_deque_dispose(&first.right);
    if (split_status != FT_STATUS_OK) {
        ft_persistent_deque_dispose(&first.left);
        return d7_ordered_from_ft(split_status);
    }

    d7_hamt_map stamps;
    d7_ordered_status status;
    if (count <= total - count) {
        status = d7_ordered_build_map(set->context, &second.left, &stamps);
    } else {
        d7_ordered_map_builder builder;
        builder.map = d7_hamt_map_clone(&set->stamps);
        builder.status = D7_ORDERED_OK;
        status = d7_ordered_remove_order_from_map(&first.left, &builder);
        if (status == D7_ORDERED_OK) {
            status = d7_ordered_remove_order_from_map(&second.right, &builder);
        }
        if (status == D7_ORDERED_OK && d7_hamt_map_count(&builder.map) != count) {
            status = D7_ORDERED_INVARIANT_VIOLATION;
        }
        if (status == D7_ORDERED_OK) {
            stamps = builder.map;
        } else {
            d7_hamt_map_destroy(&builder.map);
        }
    }
    ft_persistent_deque_dispose(&first.left);
    ft_persistent_deque_dispose(&second.right);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&second.left);
        return status;
    }
    status = d7_ordered_publish_parts(set, &second.left, &stamps, result);
    if (status != D7_ORDERED_OK) {
        ft_persistent_deque_dispose(&second.left);
        d7_hamt_map_destroy(&stamps);
    }
    return status;
}

d7_ordered_status d7_ordered_set_take(
    const d7_ordered_set* set,
    size_t count,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    if (count > ft_persistent_deque_size(&set->order)) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_set_get_range(set, 0u, count, result);
}

d7_ordered_status d7_ordered_set_drop(
    const d7_ordered_set* set,
    size_t count,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t total = ft_persistent_deque_size(&set->order);
    if (count > total) {
        return D7_ORDERED_OUT_OF_RANGE;
    }
    return d7_ordered_set_get_range(set, count, total - count, result);
}

d7_ordered_status d7_ordered_set_reverse(
    const d7_ordered_set* set,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count <= 1u) {
        return d7_ordered_set_clone(set, result);
    }

    d7_ordered_item_cell** cells = NULL;
    size_t materialized_count = 0u;
    d7_ordered_status status = d7_ordered_materialize_cells(
        &set->order,
        &cells,
        &materialized_count);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    for (size_t left = 0u, right = count - 1u; left < right; ++left, --right) {
        d7_ordered_item_cell* temporary = cells[left];
        cells[left] = cells[right];
        cells[right] = temporary;
    }
    status = d7_ordered_rebuild_cells(set, cells, materialized_count, result);
    free(cells);
    return status;
}

static void d7_ordered_merge_sort_cells(
    d7_ordered_item_cell** cells,
    d7_ordered_item_cell** scratch,
    size_t count,
    ft_compare_fn compare,
    void* compare_context)
{
    if (count < 2u) {
        return;
    }
    const size_t middle = count / 2u;
    d7_ordered_merge_sort_cells(cells, scratch, middle, compare, compare_context);
    d7_ordered_merge_sort_cells(
        cells + middle,
        scratch + middle,
        count - middle,
        compare,
        compare_context);

    size_t left = 0u;
    size_t right = middle;
    size_t output = 0u;
    while (left != middle && right != count) {
        if (compare(cells[left]->data, cells[right]->data, compare_context) <= 0) {
            scratch[output++] = cells[left++];
        } else {
            scratch[output++] = cells[right++];
        }
    }
    while (left != middle) {
        scratch[output++] = cells[left++];
    }
    while (right != count) {
        scratch[output++] = cells[right++];
    }
    (void)memcpy(cells, scratch, count * sizeof(*cells));
}

d7_ordered_status d7_ordered_set_sort(
    const d7_ordered_set* set,
    ft_compare_fn compare,
    void* compare_context,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || compare == NULL || result == NULL || result == set) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count <= 1u) {
        return d7_ordered_set_clone(set, result);
    }

    d7_ordered_item_cell** cells = NULL;
    size_t materialized_count = 0u;
    d7_ordered_status status = d7_ordered_materialize_cells(
        &set->order,
        &cells,
        &materialized_count);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    d7_ordered_item_cell** original = (d7_ordered_item_cell**)malloc(
        count * sizeof(*original));
    d7_ordered_item_cell** scratch = (d7_ordered_item_cell**)malloc(
        count * sizeof(*scratch));
    if (original == NULL || scratch == NULL) {
        free(original);
        free(scratch);
        free(cells);
        return D7_ORDERED_OUT_OF_MEMORY;
    }
    (void)memcpy(original, cells, count * sizeof(*original));
    d7_ordered_merge_sort_cells(cells, scratch, count, compare, compare_context);
    free(scratch);

    bool changed = false;
    for (size_t index = 0u; index != count; ++index) {
        if (cells[index] != original[index]) {
            changed = true;
            break;
        }
    }
    free(original);
    if (!changed) {
        free(cells);
        return d7_ordered_set_clone(set, result);
    }
    status = d7_ordered_rebuild_cells(set, cells, materialized_count, result);
    free(cells);
    return status;
}

static d7_ordered_status d7_ordered_normalize_many(
    const d7_ordered_set* receiver,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* normalized)
{
    if (items == NULL && item_count != 0u) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set candidate;
    d7_ordered_status status = d7_ordered_empty_with_context(
        receiver->context,
        &candidate);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    for (size_t index = 0u; index != item_count; ++index) {
        if (items[index] == NULL) {
            d7_ordered_set_destroy(&candidate);
            return D7_ORDERED_INVALID_ARGUMENT;
        }
        d7_ordered_set next;
        status = d7_ordered_set_add(&candidate, items[index], &next);
        if (status != D7_ORDERED_OK) {
            d7_ordered_set_destroy(&candidate);
            return status;
        }
        d7_ordered_set_destroy(&candidate);
        d7_ordered_set_move(&candidate, &next);
    }
    *normalized = candidate;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_normalize_set(
    const d7_ordered_set* receiver,
    const d7_ordered_set* argument,
    d7_ordered_set* normalized)
{
    d7_ordered_item_cell** cells = NULL;
    size_t count = 0u;
    d7_ordered_status status = d7_ordered_materialize_cells(
        &argument->order,
        &cells,
        &count);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    if (count > SIZE_MAX / sizeof(void*)) {
        free(cells);
        return D7_ORDERED_OVERFLOW;
    }
    const void** items = count == 0u
        ? NULL
        : (const void**)malloc(count * sizeof(*items));
    if (count != 0u && items == NULL) {
        free(cells);
        return D7_ORDERED_OUT_OF_MEMORY;
    }
    for (size_t index = 0u; index != count; ++index) {
        items[index] = cells[index]->data;
    }
    status = d7_ordered_normalize_many(receiver, items, count, normalized);
    free(items);
    free(cells);
    return status;
}

typedef enum d7_ordered_algebra_kind {
    D7_ORDERED_ALGEBRA_UNION,
    D7_ORDERED_ALGEBRA_INTERSECT,
    D7_ORDERED_ALGEBRA_EXCEPT,
    D7_ORDERED_ALGEBRA_SYMMETRIC_EXCEPT
} d7_ordered_algebra_kind;

static d7_ordered_status d7_ordered_apply_algebra(
    const d7_ordered_set* receiver,
    const d7_ordered_set* normalized,
    d7_ordered_algebra_kind kind,
    d7_ordered_set* result)
{
    d7_ordered_item_cell** receiver_cells = NULL;
    d7_ordered_item_cell** argument_cells = NULL;
    size_t receiver_count = 0u;
    size_t argument_count = 0u;
    d7_ordered_status status = d7_ordered_materialize_cells(
        &receiver->order,
        &receiver_cells,
        &receiver_count);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    status = d7_ordered_materialize_cells(
        &normalized->order,
        &argument_cells,
        &argument_count);
    if (status != D7_ORDERED_OK) {
        free(receiver_cells);
        return status;
    }

    size_t result_count = 0u;
    if (kind == D7_ORDERED_ALGEBRA_UNION) {
        result_count = receiver_count;
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!d7_ordered_set_contains(receiver, argument_cells[index]->data)) {
                if (result_count == SIZE_MAX) {
                    free(argument_cells);
                    free(receiver_cells);
                    return D7_ORDERED_OVERFLOW;
                }
                ++result_count;
            }
        }
        if (result_count == receiver_count) {
            free(argument_cells);
            free(receiver_cells);
            return d7_ordered_set_clone(receiver, result);
        }
    } else if (kind == D7_ORDERED_ALGEBRA_INTERSECT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                ++result_count;
            }
        }
        if (result_count == receiver_count) {
            free(argument_cells);
            free(receiver_cells);
            return d7_ordered_set_clone(receiver, result);
        }
    } else if (kind == D7_ORDERED_ALGEBRA_EXCEPT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                ++result_count;
            }
        }
        if (result_count == receiver_count) {
            free(argument_cells);
            free(receiver_cells);
            return d7_ordered_set_clone(receiver, result);
        }
    } else {
        if (argument_count == 0u) {
            free(argument_cells);
            free(receiver_cells);
            return d7_ordered_set_clone(receiver, result);
        }
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                ++result_count;
            }
        }
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!d7_ordered_set_contains(receiver, argument_cells[index]->data)) {
                if (result_count == SIZE_MAX) {
                    free(argument_cells);
                    free(receiver_cells);
                    return D7_ORDERED_OVERFLOW;
                }
                ++result_count;
            }
        }
    }

    if (result_count > SIZE_MAX / sizeof(d7_ordered_item_cell*)) {
        free(argument_cells);
        free(receiver_cells);
        return D7_ORDERED_OVERFLOW;
    }
    d7_ordered_item_cell** result_cells = result_count == 0u
        ? NULL
        : (d7_ordered_item_cell**)malloc(result_count * sizeof(*result_cells));
    if (result_count != 0u && result_cells == NULL) {
        free(argument_cells);
        free(receiver_cells);
        return D7_ORDERED_OUT_OF_MEMORY;
    }

    size_t output = 0u;
    if (kind == D7_ORDERED_ALGEBRA_UNION) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            result_cells[output++] = receiver_cells[index];
        }
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!d7_ordered_set_contains(receiver, argument_cells[index]->data)) {
                result_cells[output++] = argument_cells[index];
            }
        }
    } else if (kind == D7_ORDERED_ALGEBRA_INTERSECT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                result_cells[output++] = receiver_cells[index];
            }
        }
    } else if (kind == D7_ORDERED_ALGEBRA_EXCEPT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                result_cells[output++] = receiver_cells[index];
            }
        }
    } else {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                result_cells[output++] = receiver_cells[index];
            }
        }
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!d7_ordered_set_contains(receiver, argument_cells[index]->data)) {
                result_cells[output++] = argument_cells[index];
            }
        }
    }

    status = output == result_count
        ? d7_ordered_rebuild_cells(receiver, result_cells, result_count, result)
        : D7_ORDERED_INVARIANT_VIOLATION;
    free(result_cells);
    free(argument_cells);
    free(receiver_cells);
    return status;
}

static d7_ordered_status d7_ordered_algebra_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_algebra_kind kind,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(set) || result == NULL || result == set ||
        (items == NULL && item_count != 0u)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set normalized;
    d7_ordered_status status = d7_ordered_normalize_many(
        set,
        items,
        item_count,
        &normalized);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    status = d7_ordered_apply_algebra(set, &normalized, kind, result);
    d7_ordered_set_destroy(&normalized);
    return status;
}

static d7_ordered_status d7_ordered_algebra_set(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_algebra_kind kind,
    d7_ordered_set* result)
{
    if (!d7_ordered_set_valid(left) || !d7_ordered_set_valid(right) ||
        left->context->policy.item_type.size != right->context->policy.item_type.size ||
        result == NULL || result == left || result == right) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set normalized;
    d7_ordered_status status = d7_ordered_normalize_set(left, right, &normalized);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    status = d7_ordered_apply_algebra(left, &normalized, kind, result);
    d7_ordered_set_destroy(&normalized);
    return status;
}

d7_ordered_status d7_ordered_set_union_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_many(
        set,
        items,
        item_count,
        D7_ORDERED_ALGEBRA_UNION,
        result);
}

d7_ordered_status d7_ordered_set_intersect_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_many(
        set,
        items,
        item_count,
        D7_ORDERED_ALGEBRA_INTERSECT,
        result);
}

d7_ordered_status d7_ordered_set_except_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_many(
        set,
        items,
        item_count,
        D7_ORDERED_ALGEBRA_EXCEPT,
        result);
}

d7_ordered_status d7_ordered_set_symmetric_except_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_many(
        set,
        items,
        item_count,
        D7_ORDERED_ALGEBRA_SYMMETRIC_EXCEPT,
        result);
}

d7_ordered_status d7_ordered_set_union(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_set(
        left,
        right,
        D7_ORDERED_ALGEBRA_UNION,
        result);
}

d7_ordered_status d7_ordered_set_intersect(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_set(
        left,
        right,
        D7_ORDERED_ALGEBRA_INTERSECT,
        result);
}

d7_ordered_status d7_ordered_set_except(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_set(
        left,
        right,
        D7_ORDERED_ALGEBRA_EXCEPT,
        result);
}

d7_ordered_status d7_ordered_set_symmetric_except(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_set* result)
{
    return d7_ordered_algebra_set(
        left,
        right,
        D7_ORDERED_ALGEBRA_SYMMETRIC_EXCEPT,
        result);
}

typedef enum d7_ordered_relation_kind {
    D7_ORDERED_RELATION_SUBSET,
    D7_ORDERED_RELATION_PROPER_SUBSET,
    D7_ORDERED_RELATION_SUPERSET,
    D7_ORDERED_RELATION_PROPER_SUPERSET,
    D7_ORDERED_RELATION_OVERLAPS,
    D7_ORDERED_RELATION_EQUALS
} d7_ordered_relation_kind;

static bool d7_ordered_all_in(
    d7_ordered_item_cell* const* cells,
    size_t count,
    const d7_ordered_set* set)
{
    for (size_t index = 0u; index != count; ++index) {
        if (!d7_ordered_set_contains(set, cells[index]->data)) {
            return false;
        }
    }
    return true;
}

static d7_ordered_status d7_ordered_apply_relation(
    const d7_ordered_set* receiver,
    const d7_ordered_set* normalized,
    d7_ordered_relation_kind kind,
    bool* answer)
{
    d7_ordered_item_cell** receiver_cells = NULL;
    d7_ordered_item_cell** argument_cells = NULL;
    size_t receiver_count = 0u;
    size_t argument_count = 0u;
    d7_ordered_status status = d7_ordered_materialize_cells(
        &receiver->order,
        &receiver_cells,
        &receiver_count);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    status = d7_ordered_materialize_cells(
        &normalized->order,
        &argument_cells,
        &argument_count);
    if (status != D7_ORDERED_OK) {
        free(receiver_cells);
        return status;
    }

    bool local_answer = false;
    if (kind == D7_ORDERED_RELATION_SUBSET ||
        kind == D7_ORDERED_RELATION_PROPER_SUBSET ||
        kind == D7_ORDERED_RELATION_EQUALS) {
        local_answer = d7_ordered_all_in(receiver_cells, receiver_count, normalized);
        if (kind == D7_ORDERED_RELATION_PROPER_SUBSET) {
            local_answer = local_answer && receiver_count < argument_count;
        } else if (kind == D7_ORDERED_RELATION_EQUALS) {
            local_answer = local_answer && receiver_count == argument_count;
        }
    } else if (kind == D7_ORDERED_RELATION_SUPERSET ||
        kind == D7_ORDERED_RELATION_PROPER_SUPERSET) {
        local_answer = d7_ordered_all_in(argument_cells, argument_count, receiver);
        if (kind == D7_ORDERED_RELATION_PROPER_SUPERSET) {
            local_answer = local_answer && receiver_count > argument_count;
        }
    } else {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (d7_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                local_answer = true;
                break;
            }
        }
    }

    free(argument_cells);
    free(receiver_cells);
    *answer = local_answer;
    return D7_ORDERED_OK;
}

static d7_ordered_status d7_ordered_relation_many(
    const d7_ordered_set* set,
    const void* const* items,
    size_t item_count,
    d7_ordered_relation_kind kind,
    bool* answer)
{
    if (!d7_ordered_set_valid(set) || answer == NULL ||
        (items == NULL && item_count != 0u)) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set normalized;
    d7_ordered_status status = d7_ordered_normalize_many(
        set,
        items,
        item_count,
        &normalized);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    status = d7_ordered_apply_relation(set, &normalized, kind, answer);
    d7_ordered_set_destroy(&normalized);
    return status;
}

static d7_ordered_status d7_ordered_relation_set(
    const d7_ordered_set* left,
    const d7_ordered_set* right,
    d7_ordered_relation_kind kind,
    bool* answer)
{
    if (!d7_ordered_set_valid(left) || !d7_ordered_set_valid(right) || answer == NULL ||
        left->context->policy.item_type.size != right->context->policy.item_type.size) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_set normalized;
    d7_ordered_status status = d7_ordered_normalize_set(left, right, &normalized);
    if (status != D7_ORDERED_OK) {
        return status;
    }
    status = d7_ordered_apply_relation(left, &normalized, kind, answer);
    d7_ordered_set_destroy(&normalized);
    return status;
}

#define D7_ORDERED_DEFINE_RELATION(name, kind) \
    d7_ordered_status d7_ordered_set_##name##_many( \
        const d7_ordered_set* set, \
        const void* const* items, \
        size_t item_count, \
        bool* answer) \
    { \
        return d7_ordered_relation_many(set, items, item_count, kind, answer); \
    } \
    d7_ordered_status d7_ordered_set_##name( \
        const d7_ordered_set* left, \
        const d7_ordered_set* right, \
        bool* answer) \
    { \
        return d7_ordered_relation_set(left, right, kind, answer); \
    }

D7_ORDERED_DEFINE_RELATION(is_subset_of, D7_ORDERED_RELATION_SUBSET)
D7_ORDERED_DEFINE_RELATION(is_proper_subset_of, D7_ORDERED_RELATION_PROPER_SUBSET)
D7_ORDERED_DEFINE_RELATION(is_superset_of, D7_ORDERED_RELATION_SUPERSET)
D7_ORDERED_DEFINE_RELATION(is_proper_superset_of, D7_ORDERED_RELATION_PROPER_SUPERSET)
D7_ORDERED_DEFINE_RELATION(overlaps, D7_ORDERED_RELATION_OVERLAPS)
D7_ORDERED_DEFINE_RELATION(equals, D7_ORDERED_RELATION_EQUALS)

#undef D7_ORDERED_DEFINE_RELATION

typedef struct d7_ordered_visit_adapter {
    d7_ordered_visit_fn visitor;
    void* context;
} d7_ordered_visit_adapter;

static void d7_ordered_visit_entry(const void* value, void* raw_adapter)
{
    const d7_ordered_entry* entry = (const d7_ordered_entry*)value;
    d7_ordered_visit_adapter* adapter = (d7_ordered_visit_adapter*)raw_adapter;
    adapter->visitor(entry->cell->data, adapter->context);
}

d7_ordered_status d7_ordered_set_visit(
    const d7_ordered_set* set,
    d7_ordered_visit_fn visitor,
    void* context)
{
    if (!d7_ordered_set_valid(set) || visitor == NULL) {
        return D7_ORDERED_INVALID_ARGUMENT;
    }
    d7_ordered_visit_adapter adapter;
    adapter.visitor = visitor;
    adapter.context = context;
    return d7_ordered_from_ft(ft_persistent_deque_visit(
        &set->order,
        d7_ordered_visit_entry,
        &adapter));
}

typedef struct d7_ordered_validation_state {
    const d7_ordered_set* set;
    size_t count;
    bool valid;
    bool has_previous;
    int64_t previous_stamp;
} d7_ordered_validation_state;

static void d7_ordered_validate_entry(const void* value, void* raw_state)
{
    d7_ordered_validation_state* state = (d7_ordered_validation_state*)raw_state;
    const d7_ordered_entry* entry = (const d7_ordered_entry*)value;
    if (!state->valid) {
        return;
    }
    if (entry->cell == NULL ||
        (state->has_previous && entry->stamp <= state->previous_stamp)) {
        state->valid = false;
        return;
    }

    const d7_ordered_key_ref probe = d7_ordered_probe(entry->cell->data);
    const void* stamp_value = NULL;
    const void* key_value = NULL;
    if (!d7_hamt_map_try_get(&state->set->stamps, &probe, &stamp_value) ||
        stamp_value == NULL || *(const int64_t*)stamp_value != entry->stamp ||
        !d7_hamt_map_try_get_key(&state->set->stamps, &probe, &key_value) ||
        ((const d7_ordered_key_ref*)key_value)->cell != entry->cell) {
        state->valid = false;
        return;
    }
    state->has_previous = true;
    state->previous_stamp = entry->stamp;
    ++state->count;
}

bool d7_ordered_set_debug_validate(const d7_ordered_set* set)
{
    if (!d7_ordered_set_valid(set) ||
        ft_persistent_deque_size(&set->order) != d7_hamt_map_count(&set->stamps) ||
        !d7_hamt_map_debug_validate_canonical(&set->stamps)) {
        return false;
    }

    d7_ordered_validation_state state;
    state.set = set;
    state.count = 0u;
    state.valid = true;
    state.has_previous = false;
    state.previous_stamp = 0;
    if (ft_persistent_deque_visit(&set->order, d7_ordered_validate_entry, &state) !=
        FT_STATUS_OK || !state.valid || state.count != d7_hamt_map_count(&set->stamps)) {
        return false;
    }

    d7_hamt_map_iterator iterator;
    d7_hamt_map_iterator_init(&set->stamps, &iterator);
    const void* key_value = NULL;
    const void* stamp_value = NULL;
    size_t map_count = 0u;
    while (d7_hamt_map_iterator_next(&iterator, &key_value, &stamp_value)) {
        const d7_ordered_key_ref* key = (const d7_ordered_key_ref*)key_value;
        if (key == NULL || key->cell == NULL || key->item != key->cell->data ||
            stamp_value == NULL) {
            return false;
        }
        size_t position = 0u;
        if (!d7_ordered_locate_stamp(
                &set->order,
                *(const int64_t*)stamp_value,
                &position)) {
            return false;
        }
        d7_ordered_entry entry;
        if (!d7_ordered_entry_at(&set->order, position, &entry) || entry.cell == NULL) {
            return false;
        }
        const bool same_cell = entry.cell == key->cell;
        d7_ordered_entry_release(&entry);
        if (!same_cell) {
            return false;
        }
        ++map_count;
    }
    return map_count == state.count;
}

bool d7_ordered_set_debug_shares_order(
    const d7_ordered_set* left,
    const d7_ordered_set* right)
{
    return d7_ordered_set_valid(left) && d7_ordered_set_valid(right) &&
        left->order.rep == right->order.rep;
}

bool d7_ordered_set_debug_shares_index(
    const d7_ordered_set* left,
    const d7_ordered_set* right)
{
    return d7_ordered_set_valid(left) && d7_ordered_set_valid(right) &&
        d7_hamt_map_shares_root(&left->stamps, &right->stamps);
}
