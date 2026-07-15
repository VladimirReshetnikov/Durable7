#include <tools/data_structures/ordered/ordered_set.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    TDS_ORDERED_STAMP_GAP = 1 << 20
};

typedef struct tds_ordered_item_cell tds_ordered_item_cell;

#if defined(_MSC_VER) && !defined(__clang__)
/* The UCRT C <stddef.h> does not declare max_align_t even in /std:c17 mode.
 * The Windows ABI gives long double the maximum fundamental alignment that
 * malloc must support. Other C17 implementations use the standard type. */
typedef long double tds_ordered_max_align_t;
#else
typedef max_align_t tds_ordered_max_align_t;
#endif

typedef struct tds_ordered_entry {
    int64_t stamp;
    tds_ordered_item_cell* cell;
} tds_ordered_entry;

typedef struct tds_ordered_key_ref {
    const void* item;
    tds_ordered_item_cell* cell;
} tds_ordered_key_ref;

struct tds_ordered_context {
    size_t ref_count;
    tds_ordered_policy policy;
    ft_tree_policy order_policy;
    tds_hamt_policy map_policy;
};

/* The max_align_t member makes data suitably aligned without relying on a
 * zero-length or flexible array, both of which are noisy under strict MSVC. */
struct tds_ordered_item_cell {
    size_t ref_count;
    struct tds_ordered_context* context;
    tds_ordered_max_align_t alignment;
    unsigned char data[1];
};

typedef struct tds_ordered_cell_buffer {
    tds_ordered_item_cell** cells;
    size_t count;
} tds_ordered_cell_buffer;

static tds_ordered_status tds_ordered_from_ft(ft_status status)
{
    switch (status) {
    case FT_STATUS_OK:
        return TDS_ORDERED_OK;
    case FT_STATUS_OUT_OF_RANGE:
        return TDS_ORDERED_OUT_OF_RANGE;
    case FT_STATUS_EMPTY:
        return TDS_ORDERED_EMPTY;
    case FT_STATUS_NOT_FOUND:
        return TDS_ORDERED_NOT_FOUND;
    case FT_STATUS_NO_MEMORY:
        return TDS_ORDERED_OUT_OF_MEMORY;
    case FT_STATUS_OVERFLOW:
        return TDS_ORDERED_OVERFLOW;
    case FT_STATUS_INVALID_ARGUMENT:
    case FT_STATUS_ALREADY_EXISTS:
    case FT_STATUS_CALLBACK_FAILURE:
    case FT_STATUS_CRYPTO_FAILURE:
    case FT_STATUS_INCOMPATIBLE_POLICY:
    case FT_STATUS_INCONSISTENT_POLICY:
    default:
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
}

static tds_ordered_status tds_ordered_from_hamt(tds_hamt_status status)
{
    switch (status) {
    case TDS_HAMT_OK:
        return TDS_ORDERED_OK;
    case TDS_HAMT_OUT_OF_MEMORY:
        return TDS_ORDERED_OUT_OF_MEMORY;
    case TDS_HAMT_OVERFLOW:
        return TDS_ORDERED_OVERFLOW;
    case TDS_HAMT_DUPLICATE_KEY:
    case TDS_HAMT_INVALID_ARGUMENT:
    case TDS_HAMT_TRANSIENT_CONSUMED:
    case TDS_HAMT_TRANSIENT_MODIFIED:
    default:
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
}

static void tds_ordered_item_copy(
    const struct tds_ordered_context* context,
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

static void tds_ordered_item_destroy(
    const struct tds_ordered_context* context,
    void* item)
{
    if (context->policy.item_type.destroy != NULL) {
        context->policy.item_type.destroy(item, context->policy.item_type.context);
    }
}

static bool tds_ordered_items_equal(
    const struct tds_ordered_context* context,
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

static tds_ordered_item_cell* tds_ordered_cell_create(
    struct tds_ordered_context* context,
    const void* item)
{
    const size_t prefix = offsetof(tds_ordered_item_cell, data);
    if (context->policy.item_type.size > SIZE_MAX - prefix) {
        return NULL;
    }

    size_t allocation_size = prefix + context->policy.item_type.size;
    if (allocation_size < sizeof(tds_ordered_item_cell)) {
        allocation_size = sizeof(tds_ordered_item_cell);
    }
    tds_ordered_item_cell* cell =
        (tds_ordered_item_cell*)malloc(allocation_size);
    if (cell == NULL) {
        return NULL;
    }

    cell->ref_count = 1u;
    cell->context = context;
    tds_ordered_item_copy(context, cell->data, item);
    return cell;
}

static tds_ordered_item_cell* tds_ordered_cell_retain(tds_ordered_item_cell* cell)
{
    if (cell == NULL || cell->ref_count == SIZE_MAX) {
        return NULL;
    }
    ++cell->ref_count;
    return cell;
}

static void tds_ordered_cell_release(tds_ordered_item_cell* cell)
{
    if (cell == NULL) {
        return;
    }
    --cell->ref_count;
    if (cell->ref_count == 0u) {
        tds_ordered_item_destroy(cell->context, cell->data);
        free(cell);
    }
}

static void tds_ordered_entry_copy(void* destination, const void* source, void* raw_context)
{
    (void)raw_context;
    tds_ordered_entry* target = (tds_ordered_entry*)destination;
    const tds_ordered_entry* value = (const tds_ordered_entry*)source;
    target->stamp = value->stamp;
    target->cell = tds_ordered_cell_retain(value->cell);
}

static void tds_ordered_entry_destroy(void* value, void* raw_context)
{
    (void)raw_context;
    tds_ordered_entry* entry = (tds_ordered_entry*)value;
    tds_ordered_cell_release(entry->cell);
    entry->cell = NULL;
}

static uint32_t tds_ordered_map_hash(const void* value, void* raw_context)
{
    const struct tds_ordered_context* context =
        (const struct tds_ordered_context*)raw_context;
    const tds_ordered_key_ref* key = (const tds_ordered_key_ref*)value;
    return context->policy.hash(key->item, context->policy.context);
}

static bool tds_ordered_map_key_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    const struct tds_ordered_context* context =
        (const struct tds_ordered_context*)raw_context;
    return tds_ordered_items_equal(
        context,
        ((const tds_ordered_key_ref*)left)->item,
        ((const tds_ordered_key_ref*)right)->item);
}

static bool tds_ordered_map_value_equal(
    const void* left,
    const void* right,
    void* raw_context)
{
    (void)raw_context;
    return left == right ||
        (left != NULL && right != NULL && *(const int64_t*)left == *(const int64_t*)right);
}

static void* tds_ordered_map_retain_key(const void* value, void* raw_context)
{
    struct tds_ordered_context* context = (struct tds_ordered_context*)raw_context;
    const tds_ordered_key_ref* source = (const tds_ordered_key_ref*)value;
    tds_ordered_key_ref* target = (tds_ordered_key_ref*)malloc(sizeof(*target));
    if (target == NULL) {
        return NULL;
    }

    target->cell = source->cell != NULL
        ? tds_ordered_cell_retain(source->cell)
        : tds_ordered_cell_create(context, source->item);
    if (target->cell == NULL) {
        free(target);
        return NULL;
    }
    target->item = target->cell->data;
    return target;
}

static void* tds_ordered_map_retain_value(const void* value, void* raw_context)
{
    (void)raw_context;
    int64_t* target = (int64_t*)malloc(sizeof(*target));
    if (target != NULL) {
        *target = *(const int64_t*)value;
    }
    return target;
}

static void tds_ordered_map_release_key(void* value, void* raw_context)
{
    (void)raw_context;
    tds_ordered_key_ref* key = (tds_ordered_key_ref*)value;
    if (key != NULL) {
        tds_ordered_cell_release(key->cell);
        free(key);
    }
}

static void tds_ordered_map_release_value(void* value, void* raw_context)
{
    (void)raw_context;
    free(value);
}

static struct tds_ordered_context* tds_ordered_context_create(
    const tds_ordered_policy* policy)
{
    if (policy == NULL || policy->item_type.size == 0u || policy->hash == NULL) {
        return NULL;
    }

    struct tds_ordered_context* context =
        (struct tds_ordered_context*)malloc(sizeof(*context));
    if (context == NULL) {
        return NULL;
    }

    (void)memset(context, 0, sizeof(*context));
    context->ref_count = 1u;
    context->policy = *policy;

    ft_value_type entry_type;
    ft_value_type_init(&entry_type, sizeof(tds_ordered_entry));
    entry_type.copy = tds_ordered_entry_copy;
    entry_type.destroy = tds_ordered_entry_destroy;
    entry_type.context = context;
    ft_tree_policy_init_size(&context->order_policy, &entry_type);

    context->map_policy.hash = tds_ordered_map_hash;
    context->map_policy.key_equal = tds_ordered_map_key_equal;
    context->map_policy.value_equal = tds_ordered_map_value_equal;
    context->map_policy.retain_key = tds_ordered_map_retain_key;
    context->map_policy.retain_value = tds_ordered_map_retain_value;
    context->map_policy.release_key = tds_ordered_map_release_key;
    context->map_policy.release_value = tds_ordered_map_release_value;
    context->map_policy.context = context;
    return context;
}

static bool tds_ordered_context_retain(struct tds_ordered_context* context)
{
    if (context == NULL || context->ref_count == SIZE_MAX) {
        return false;
    }
    ++context->ref_count;
    return true;
}

static void tds_ordered_context_release(struct tds_ordered_context* context)
{
    if (context == NULL) {
        return;
    }
    --context->ref_count;
    if (context->ref_count == 0u) {
        free(context);
    }
}

static bool tds_ordered_set_valid(const tds_ordered_set* set)
{
    return set != NULL && set->context != NULL && set->order.rep != NULL &&
        set->order.policy == &set->context->order_policy &&
        set->stamps.policy.context == set->context &&
        set->stamps.policy.hash == tds_ordered_map_hash &&
        set->stamps.policy.key_equal == tds_ordered_map_key_equal;
}

static tds_ordered_key_ref tds_ordered_probe(const void* item)
{
    tds_ordered_key_ref probe;
    probe.item = item;
    probe.cell = NULL;
    return probe;
}

static tds_ordered_key_ref tds_ordered_cell_key(tds_ordered_item_cell* cell)
{
    tds_ordered_key_ref key;
    key.item = cell->data;
    key.cell = cell;
    return key;
}

static void tds_ordered_entry_release(tds_ordered_entry* entry)
{
    if (entry != NULL) {
        tds_ordered_cell_release(entry->cell);
        entry->cell = NULL;
    }
}

static bool tds_ordered_entry_at(
    const ft_persistent_deque* order,
    size_t index,
    tds_ordered_entry* entry)
{
    return ft_persistent_deque_at(order, index, entry) == FT_STATUS_OK;
}

static tds_ordered_status tds_ordered_empty_with_context(
    struct tds_ordered_context* context,
    tds_ordered_set* result)
{
    if (!tds_ordered_context_retain(context)) {
        return TDS_ORDERED_OVERFLOW;
    }

    tds_ordered_set candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    candidate.context = context;
    const ft_status status = ft_persistent_deque_init(
        &candidate.order,
        &context->order_policy);
    if (status != FT_STATUS_OK) {
        tds_ordered_context_release(context);
        return tds_ordered_from_ft(status);
    }
    candidate.stamps = tds_hamt_map_create(&context->map_policy);
    *result = candidate;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_publish_parts(
    const tds_ordered_set* source,
    ft_persistent_deque* order,
    tds_hamt_map* stamps,
    tds_ordered_set* result)
{
    if (!tds_ordered_context_retain(source->context)) {
        return TDS_ORDERED_OVERFLOW;
    }

    tds_ordered_set candidate;
    candidate.context = source->context;
    candidate.order = *order;
    candidate.stamps = *stamps;
    (void)memset(order, 0, sizeof(*order));
    (void)memset(stamps, 0, sizeof(*stamps));
    *result = candidate;
    return TDS_ORDERED_OK;
}

void tds_ordered_policy_init(
    tds_ordered_policy* policy,
    const ft_value_type* item_type,
    tds_ordered_hash_fn hash,
    tds_ordered_equal_fn equal,
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

tds_ordered_status tds_ordered_set_init(
    tds_ordered_set* set,
    const tds_ordered_policy* policy)
{
    if (set == NULL || policy == NULL || policy->item_type.size == 0u || policy->hash == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (policy->item_type.size > SIZE_MAX - offsetof(tds_ordered_item_cell, data)) {
        return TDS_ORDERED_OVERFLOW;
    }

    struct tds_ordered_context* context = tds_ordered_context_create(policy);
    if (context == NULL) {
        return TDS_ORDERED_OUT_OF_MEMORY;
    }

    tds_ordered_set candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    candidate.context = context;
    const ft_status status = ft_persistent_deque_init(
        &candidate.order,
        &context->order_policy);
    if (status != FT_STATUS_OK) {
        tds_ordered_context_release(context);
        return tds_ordered_from_ft(status);
    }
    candidate.stamps = tds_hamt_map_create(&context->map_policy);
    *set = candidate;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_clone(
    const tds_ordered_set* source,
    tds_ordered_set* destination)
{
    if (!tds_ordered_set_valid(source) || destination == NULL || destination == source) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (!tds_ordered_context_retain(source->context)) {
        return TDS_ORDERED_OVERFLOW;
    }

    tds_ordered_set candidate;
    (void)memset(&candidate, 0, sizeof(candidate));
    candidate.context = source->context;
    const ft_status status = ft_persistent_deque_copy(&source->order, &candidate.order);
    if (status != FT_STATUS_OK) {
        tds_ordered_context_release(source->context);
        return tds_ordered_from_ft(status);
    }
    candidate.stamps = tds_hamt_map_clone(&source->stamps);
    *destination = candidate;
    return TDS_ORDERED_OK;
}

void tds_ordered_set_move(tds_ordered_set* destination, tds_ordered_set* source)
{
    if (destination == NULL || source == NULL || destination == source) {
        return;
    }
    *destination = *source;
    (void)memset(source, 0, sizeof(*source));
}

void tds_ordered_set_destroy(tds_ordered_set* set)
{
    if (set == NULL) {
        return;
    }
    struct tds_ordered_context* context = set->context;
    if (set->order.rep != NULL) {
        ft_persistent_deque_dispose(&set->order);
    }
    if (context != NULL) {
        tds_hamt_map_destroy(&set->stamps);
        tds_ordered_context_release(context);
    }
    (void)memset(set, 0, sizeof(*set));
}

const tds_ordered_policy* tds_ordered_set_policy(const tds_ordered_set* set)
{
    return tds_ordered_set_valid(set) ? &set->context->policy : NULL;
}

bool tds_ordered_set_empty(const tds_ordered_set* set)
{
    return !tds_ordered_set_valid(set) || ft_persistent_deque_empty(&set->order);
}

size_t tds_ordered_set_size(const tds_ordered_set* set)
{
    return tds_ordered_set_valid(set) ? ft_persistent_deque_size(&set->order) : 0u;
}

bool tds_ordered_set_contains(const tds_ordered_set* set, const void* item)
{
    if (!tds_ordered_set_valid(set) || item == NULL) {
        return false;
    }
    const tds_ordered_key_ref probe = tds_ordered_probe(item);
    return tds_hamt_map_contains_key(&set->stamps, &probe);
}

bool tds_ordered_set_try_get_value(
    const tds_ordered_set* set,
    const void* equal_item,
    const void** actual_item)
{
    if (actual_item != NULL) {
        *actual_item = equal_item;
    }
    if (!tds_ordered_set_valid(set) || equal_item == NULL) {
        return false;
    }

    const tds_ordered_key_ref probe = tds_ordered_probe(equal_item);
    const void* actual_key = NULL;
    const bool found = tds_hamt_map_try_get_key(&set->stamps, &probe, &actual_key);
    if (found && actual_item != NULL) {
        *actual_item = ((const tds_ordered_key_ref*)actual_key)->item;
    }
    return found;
}

tds_ordered_status tds_ordered_set_at(
    const tds_ordered_set* set,
    size_t index,
    const void** item)
{
    if (!tds_ordered_set_valid(set) || item == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (index >= ft_persistent_deque_size(&set->order)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }

    tds_ordered_entry entry;
    if (!tds_ordered_entry_at(&set->order, index, &entry) || entry.cell == NULL) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    *item = entry.cell->data;
    tds_ordered_entry_release(&entry);
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_front(const tds_ordered_set* set, const void** item)
{
    if (!tds_ordered_set_valid(set) || item == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return TDS_ORDERED_EMPTY;
    }
    return tds_ordered_set_at(set, 0u, item);
}

tds_ordered_status tds_ordered_set_back(const tds_ordered_set* set, const void** item)
{
    if (!tds_ordered_set_valid(set) || item == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count == 0u) {
        return TDS_ORDERED_EMPTY;
    }
    return tds_ordered_set_at(set, count - 1u, item);
}

static bool tds_ordered_locate_stamp(
    const ft_persistent_deque* order,
    int64_t stamp,
    size_t* index)
{
    size_t low = 0u;
    size_t high = ft_persistent_deque_size(order);
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        tds_ordered_entry entry;
        if (!tds_ordered_entry_at(order, middle, &entry) || entry.cell == NULL) {
            return false;
        }
        const int64_t middle_stamp = entry.stamp;
        tds_ordered_entry_release(&entry);
        if (middle_stamp < stamp) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }

    if (low >= ft_persistent_deque_size(order)) {
        return false;
    }
    tds_ordered_entry entry;
    if (!tds_ordered_entry_at(order, low, &entry) || entry.cell == NULL) {
        return false;
    }
    const bool found = entry.stamp == stamp;
    tds_ordered_entry_release(&entry);
    if (found && index != NULL) {
        *index = low;
    }
    return found;
}

bool tds_ordered_set_index_of(
    const tds_ordered_set* set,
    const void* equal_item,
    size_t* index)
{
    if (!tds_ordered_set_valid(set) || equal_item == NULL) {
        return false;
    }
    const tds_ordered_key_ref probe = tds_ordered_probe(equal_item);
    const void* value = NULL;
    if (!tds_hamt_map_try_get(&set->stamps, &probe, &value) || value == NULL) {
        return false;
    }
    return tds_ordered_locate_stamp(&set->order, *(const int64_t*)value, index);
}

static void tds_ordered_collect_cell(const void* value, void* raw_buffer)
{
    tds_ordered_cell_buffer* buffer = (tds_ordered_cell_buffer*)raw_buffer;
    const tds_ordered_entry* entry = (const tds_ordered_entry*)value;
    buffer->cells[buffer->count++] = entry->cell;
}

static tds_ordered_status tds_ordered_materialize_cells(
    const ft_persistent_deque* order,
    tds_ordered_item_cell*** cells,
    size_t* count)
{
    const size_t local_count = ft_persistent_deque_size(order);
    if (local_count > SIZE_MAX / sizeof(tds_ordered_item_cell*)) {
        return TDS_ORDERED_OVERFLOW;
    }
    tds_ordered_item_cell** local_cells = local_count == 0u
        ? NULL
        : (tds_ordered_item_cell**)malloc(local_count * sizeof(*local_cells));
    if (local_count != 0u && local_cells == NULL) {
        return TDS_ORDERED_OUT_OF_MEMORY;
    }

    tds_ordered_cell_buffer buffer;
    buffer.cells = local_cells;
    buffer.count = 0u;
    const ft_status status = ft_persistent_deque_visit(order, tds_ordered_collect_cell, &buffer);
    if (status != FT_STATUS_OK || buffer.count != local_count) {
        free(local_cells);
        return status == FT_STATUS_OK
            ? TDS_ORDERED_INVARIANT_VIOLATION
            : tds_ordered_from_ft(status);
    }
    *cells = local_cells;
    *count = local_count;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_choose_stamp(
    const ft_persistent_deque* order,
    size_t index,
    bool* found,
    int64_t* stamp)
{
    const size_t count = ft_persistent_deque_size(order);
    if (index > count || found == NULL || stamp == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (count == 0u) {
        *found = true;
        *stamp = 0;
        return TDS_ORDERED_OK;
    }

    if (index == 0u) {
        tds_ordered_entry next;
        if (!tds_ordered_entry_at(order, 0u, &next) || next.cell == NULL) {
            return TDS_ORDERED_INVARIANT_VIOLATION;
        }
        if (next.stamp >= INT64_MIN + (int64_t)TDS_ORDERED_STAMP_GAP) {
            *stamp = next.stamp - (int64_t)TDS_ORDERED_STAMP_GAP;
            *found = true;
        } else if (next.stamp > INT64_MIN) {
            *stamp = next.stamp - 1;
            *found = true;
        } else {
            *found = false;
        }
        tds_ordered_entry_release(&next);
        return TDS_ORDERED_OK;
    }

    if (index == count) {
        tds_ordered_entry previous;
        if (!tds_ordered_entry_at(order, count - 1u, &previous) || previous.cell == NULL) {
            return TDS_ORDERED_INVARIANT_VIOLATION;
        }
        if (previous.stamp <= INT64_MAX - (int64_t)TDS_ORDERED_STAMP_GAP) {
            *stamp = previous.stamp + (int64_t)TDS_ORDERED_STAMP_GAP;
            *found = true;
        } else if (previous.stamp < INT64_MAX) {
            *stamp = previous.stamp + 1;
            *found = true;
        } else {
            *found = false;
        }
        tds_ordered_entry_release(&previous);
        return TDS_ORDERED_OK;
    }

    tds_ordered_entry previous;
    tds_ordered_entry next;
    if (!tds_ordered_entry_at(order, index - 1u, &previous) || previous.cell == NULL) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    if (!tds_ordered_entry_at(order, index, &next) || next.cell == NULL) {
        tds_ordered_entry_release(&previous);
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }

    const uint64_t distance = (uint64_t)next.stamp - (uint64_t)previous.stamp;
    if (distance <= 1u) {
        *found = false;
    } else {
        *stamp = previous.stamp + (int64_t)(distance / 2u);
        *found = true;
    }
    tds_ordered_entry_release(&previous);
    tds_ordered_entry_release(&next);
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_append_exact(
    tds_ordered_set* set,
    tds_ordered_item_cell* cell,
    int64_t stamp)
{
    tds_ordered_entry entry;
    entry.stamp = stamp;
    entry.cell = cell;
    ft_persistent_deque next_order;
    const ft_status order_status = ft_persistent_deque_push_back(
        &set->order,
        &entry,
        &next_order);
    if (order_status != FT_STATUS_OK) {
        return tds_ordered_from_ft(order_status);
    }

    const tds_ordered_key_ref key = tds_ordered_cell_key(cell);
    tds_hamt_map next_stamps;
    const tds_hamt_status map_status = tds_hamt_map_add(
        &set->stamps,
        &key,
        &stamp,
        &next_stamps);
    if (map_status != TDS_HAMT_OK) {
        ft_persistent_deque_dispose(&next_order);
        return map_status == TDS_HAMT_DUPLICATE_KEY
            ? TDS_ORDERED_INVARIANT_VIOLATION
            : tds_ordered_from_hamt(map_status);
    }

    ft_persistent_deque_dispose(&set->order);
    tds_hamt_map_destroy(&set->stamps);
    set->order = next_order;
    set->stamps = next_stamps;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_rebuild_cells(
    const tds_ordered_set* source,
    tds_ordered_item_cell* const* cells,
    size_t count,
    tds_ordered_set* result)
{
    const size_t left_count = count / 2u;
    const size_t right_count = count == 0u ? 0u : count - 1u - left_count;
    const size_t label_limit = (size_t)(INT64_MAX / (int64_t)TDS_ORDERED_STAMP_GAP);
    if (left_count > label_limit || right_count > label_limit) {
        return TDS_ORDERED_OVERFLOW;
    }

    tds_ordered_set candidate;
    tds_ordered_status status = tds_ordered_empty_with_context(source->context, &candidate);
    if (status != TDS_ORDERED_OK) {
        return status;
    }

    int64_t stamp = -(int64_t)(left_count * (size_t)TDS_ORDERED_STAMP_GAP);
    for (size_t index = 0u; index != count; ++index) {
        status = tds_ordered_append_exact(&candidate, cells[index], stamp);
        if (status != TDS_ORDERED_OK) {
            tds_ordered_set_destroy(&candidate);
            return status;
        }
        if (index + 1u != count) {
            stamp += (int64_t)TDS_ORDERED_STAMP_GAP;
        }
    }
    *result = candidate;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_insert_cell(
    const tds_ordered_set* source,
    size_t index,
    tds_ordered_item_cell* cell,
    bool update_existing_stamp,
    tds_ordered_set* result)
{
    bool has_stamp = false;
    int64_t stamp = 0;
    tds_ordered_status status = tds_ordered_choose_stamp(
        &source->order,
        index,
        &has_stamp,
        &stamp);
    if (status != TDS_ORDERED_OK) {
        return status;
    }

    if (!has_stamp) {
        tds_ordered_item_cell** old_cells = NULL;
        size_t old_count = 0u;
        status = tds_ordered_materialize_cells(&source->order, &old_cells, &old_count);
        if (status != TDS_ORDERED_OK) {
            return status;
        }
        if (old_count == SIZE_MAX || old_count + 1u > SIZE_MAX / sizeof(*old_cells)) {
            free(old_cells);
            return TDS_ORDERED_OVERFLOW;
        }
        tds_ordered_item_cell** cells = (tds_ordered_item_cell**)malloc(
            (old_count + 1u) * sizeof(*cells));
        if (cells == NULL) {
            free(old_cells);
            return TDS_ORDERED_OUT_OF_MEMORY;
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
        status = tds_ordered_rebuild_cells(source, cells, old_count + 1u, result);
        free(cells);
        free(old_cells);
        return status;
    }

    tds_ordered_entry entry;
    entry.stamp = stamp;
    entry.cell = cell;
    ft_persistent_deque order;
    const ft_status order_status = ft_persistent_deque_insert_at(
        &source->order,
        index,
        &entry,
        &order);
    if (order_status != FT_STATUS_OK) {
        return tds_ordered_from_ft(order_status);
    }

    const tds_ordered_key_ref key = tds_ordered_cell_key(cell);
    tds_hamt_map stamps;
    const tds_hamt_status map_status = update_existing_stamp
        ? tds_hamt_map_set(&source->stamps, &key, &stamp, &stamps)
        : tds_hamt_map_add(&source->stamps, &key, &stamp, &stamps);
    if (map_status != TDS_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return map_status == TDS_HAMT_DUPLICATE_KEY
            ? TDS_ORDERED_INVARIANT_VIOLATION
            : tds_ordered_from_hamt(map_status);
    }

    status = tds_ordered_publish_parts(source, &order, &stamps, result);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        tds_hamt_map_destroy(&stamps);
    }
    return status;
}

static tds_ordered_status tds_ordered_add_at_core(
    const tds_ordered_set* set,
    size_t index,
    const void* item,
    tds_ordered_set* result)
{
    const tds_ordered_key_ref probe = tds_ordered_probe(item);
    if (tds_hamt_map_contains_key(&set->stamps, &probe)) {
        return tds_ordered_set_clone(set, result);
    }

    tds_ordered_item_cell* cell = tds_ordered_cell_create(set->context, item);
    if (cell == NULL) {
        return TDS_ORDERED_OUT_OF_MEMORY;
    }
    const tds_ordered_status status = tds_ordered_insert_cell(
        set,
        index,
        cell,
        false,
        result);
    tds_ordered_cell_release(cell);
    return status;
}

tds_ordered_status tds_ordered_set_add(
    const tds_ordered_set* set,
    const void* item,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_add_at_core(
        set,
        ft_persistent_deque_size(&set->order),
        item,
        result);
}

tds_ordered_status tds_ordered_set_add_first(
    const tds_ordered_set* set,
    const void* item,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    return tds_ordered_add_at_core(set, 0u, item, result);
}

tds_ordered_status tds_ordered_set_insert(
    const tds_ordered_set* set,
    size_t index,
    const void* item,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (index > ft_persistent_deque_size(&set->order)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_add_at_core(set, index, item, result);
}

tds_ordered_status tds_ordered_set_from_items(
    tds_ordered_set* set,
    const tds_ordered_policy* policy,
    const void* const* items,
    size_t item_count)
{
    if (set == NULL || policy == NULL || (items == NULL && item_count != 0u)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }

    tds_ordered_set candidate;
    tds_ordered_status status = tds_ordered_set_init(&candidate, policy);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    for (size_t index = 0u; index != item_count; ++index) {
        if (items[index] == NULL) {
            tds_ordered_set_destroy(&candidate);
            return TDS_ORDERED_INVALID_ARGUMENT;
        }
        tds_ordered_set next;
        status = tds_ordered_set_add(&candidate, items[index], &next);
        if (status != TDS_ORDERED_OK) {
            tds_ordered_set_destroy(&candidate);
            return status;
        }
        tds_ordered_set_destroy(&candidate);
        tds_ordered_set_move(&candidate, &next);
    }
    *set = candidate;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_from_array(
    tds_ordered_set* set,
    const tds_ordered_policy* policy,
    const void* items,
    size_t item_count)
{
    if (set == NULL || policy == NULL || policy->item_type.size == 0u ||
        (items == NULL && item_count != 0u)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (item_count > SIZE_MAX / policy->item_type.size) {
        return TDS_ORDERED_OVERFLOW;
    }

    tds_ordered_set candidate;
    tds_ordered_status status = tds_ordered_set_init(&candidate, policy);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    const unsigned char* bytes = (const unsigned char*)items;
    for (size_t index = 0u; index != item_count; ++index) {
        tds_ordered_set next;
        status = tds_ordered_set_add(
            &candidate,
            bytes + index * policy->item_type.size,
            &next);
        if (status != TDS_ORDERED_OK) {
            tds_ordered_set_destroy(&candidate);
            return status;
        }
        tds_ordered_set_destroy(&candidate);
        tds_ordered_set_move(&candidate, &next);
    }
    *set = candidate;
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_move_to(
    const tds_ordered_set* set,
    size_t index,
    const void* equal_item,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (index >= count) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }

    const tds_ordered_key_ref probe = tds_ordered_probe(equal_item);
    const void* stamp_value = NULL;
    const void* actual_key_value = NULL;
    if (!tds_hamt_map_try_get(&set->stamps, &probe, &stamp_value) || stamp_value == NULL ||
        !tds_hamt_map_try_get_key(&set->stamps, &probe, &actual_key_value)) {
        return TDS_ORDERED_NOT_FOUND;
    }

    size_t old_index = 0u;
    if (!tds_ordered_locate_stamp(&set->order, *(const int64_t*)stamp_value, &old_index)) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    if (old_index == index) {
        return tds_ordered_set_clone(set, result);
    }

    tds_ordered_entry moved;
    if (!tds_ordered_entry_at(&set->order, old_index, &moved) || moved.cell == NULL) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    const tds_ordered_key_ref* actual_key = (const tds_ordered_key_ref*)actual_key_value;
    if (actual_key->cell != moved.cell) {
        tds_ordered_entry_release(&moved);
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }

    ft_persistent_deque reduced_order;
    const ft_status remove_status = ft_persistent_deque_remove_at(
        &set->order,
        old_index,
        &reduced_order);
    if (remove_status != FT_STATUS_OK) {
        tds_ordered_entry_release(&moved);
        return tds_ordered_from_ft(remove_status);
    }

    bool has_stamp = false;
    int64_t stamp = 0;
    tds_ordered_status status = tds_ordered_choose_stamp(
        &reduced_order,
        index,
        &has_stamp,
        &stamp);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&reduced_order);
        tds_ordered_entry_release(&moved);
        return status;
    }

    if (!has_stamp) {
        tds_ordered_item_cell** reduced_cells = NULL;
        size_t reduced_count = 0u;
        status = tds_ordered_materialize_cells(
            &reduced_order,
            &reduced_cells,
            &reduced_count);
        if (status != TDS_ORDERED_OK) {
            ft_persistent_deque_dispose(&reduced_order);
            tds_ordered_entry_release(&moved);
            return status;
        }
        if (reduced_count == SIZE_MAX ||
            reduced_count + 1u > SIZE_MAX / sizeof(*reduced_cells)) {
            free(reduced_cells);
            ft_persistent_deque_dispose(&reduced_order);
            tds_ordered_entry_release(&moved);
            return TDS_ORDERED_OVERFLOW;
        }
        tds_ordered_item_cell** cells = (tds_ordered_item_cell**)malloc(
            (reduced_count + 1u) * sizeof(*cells));
        if (cells == NULL) {
            free(reduced_cells);
            ft_persistent_deque_dispose(&reduced_order);
            tds_ordered_entry_release(&moved);
            return TDS_ORDERED_OUT_OF_MEMORY;
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
        status = tds_ordered_rebuild_cells(set, cells, reduced_count + 1u, result);
        free(cells);
        free(reduced_cells);
        ft_persistent_deque_dispose(&reduced_order);
        tds_ordered_entry_release(&moved);
        return status;
    }

    tds_ordered_entry replacement;
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
        tds_ordered_entry_release(&moved);
        return tds_ordered_from_ft(insert_status);
    }

    const tds_ordered_key_ref key = tds_ordered_cell_key(moved.cell);
    tds_hamt_map stamps;
    const tds_hamt_status map_status = tds_hamt_map_set(
        &set->stamps,
        &key,
        &stamp,
        &stamps);
    tds_ordered_entry_release(&moved);
    if (map_status != TDS_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return tds_ordered_from_hamt(map_status);
    }

    status = tds_ordered_publish_parts(set, &order, &stamps, result);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        tds_hamt_map_destroy(&stamps);
    }
    return status;
}

tds_ordered_status tds_ordered_set_move_to_first(
    const tds_ordered_set* set,
    const void* equal_item,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return TDS_ORDERED_NOT_FOUND;
    }
    return tds_ordered_set_move_to(set, 0u, equal_item, result);
}

tds_ordered_status tds_ordered_set_move_to_last(
    const tds_ordered_set* set,
    const void* equal_item,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count == 0u) {
        return TDS_ORDERED_NOT_FOUND;
    }
    return tds_ordered_set_move_to(set, count - 1u, equal_item, result);
}

tds_ordered_status tds_ordered_set_try_remove(
    const tds_ordered_set* set,
    const void* equal_item,
    bool* removed,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || equal_item == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const tds_ordered_key_ref probe = tds_ordered_probe(equal_item);
    const void* stamp_value = NULL;
    if (!tds_hamt_map_try_get(&set->stamps, &probe, &stamp_value) || stamp_value == NULL) {
        const tds_ordered_status status = tds_ordered_set_clone(set, result);
        if (status == TDS_ORDERED_OK && removed != NULL) {
            *removed = false;
        }
        return status;
    }

    size_t index = 0u;
    if (!tds_ordered_locate_stamp(&set->order, *(const int64_t*)stamp_value, &index)) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }

    ft_persistent_deque order;
    const ft_status order_status = ft_persistent_deque_remove_at(
        &set->order,
        index,
        &order);
    if (order_status != FT_STATUS_OK) {
        return tds_ordered_from_ft(order_status);
    }

    tds_hamt_map stamps;
    const tds_hamt_status map_status = tds_hamt_map_remove(
        &set->stamps,
        &probe,
        &stamps);
    if (map_status != TDS_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return tds_ordered_from_hamt(map_status);
    }

    const tds_ordered_status status = tds_ordered_publish_parts(
        set,
        &order,
        &stamps,
        result);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        tds_hamt_map_destroy(&stamps);
        return status;
    }
    if (removed != NULL) {
        *removed = true;
    }
    return TDS_ORDERED_OK;
}

tds_ordered_status tds_ordered_set_remove(
    const tds_ordered_set* set,
    const void* equal_item,
    tds_ordered_set* result)
{
    return tds_ordered_set_try_remove(set, equal_item, NULL, result);
}

tds_ordered_status tds_ordered_set_remove_at(
    const tds_ordered_set* set,
    size_t index,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (index >= ft_persistent_deque_size(&set->order)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }

    tds_ordered_entry removed_entry;
    if (!tds_ordered_entry_at(&set->order, index, &removed_entry) ||
        removed_entry.cell == NULL) {
        return TDS_ORDERED_INVARIANT_VIOLATION;
    }
    ft_persistent_deque order;
    const ft_status order_status = ft_persistent_deque_remove_at(
        &set->order,
        index,
        &order);
    if (order_status != FT_STATUS_OK) {
        tds_ordered_entry_release(&removed_entry);
        return tds_ordered_from_ft(order_status);
    }

    const tds_ordered_key_ref key = tds_ordered_cell_key(removed_entry.cell);
    tds_hamt_map stamps;
    const tds_hamt_status map_status = tds_hamt_map_remove(
        &set->stamps,
        &key,
        &stamps);
    tds_ordered_entry_release(&removed_entry);
    if (map_status != TDS_HAMT_OK) {
        ft_persistent_deque_dispose(&order);
        return tds_ordered_from_hamt(map_status);
    }

    const tds_ordered_status status = tds_ordered_publish_parts(
        set,
        &order,
        &stamps,
        result);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&order);
        tds_hamt_map_destroy(&stamps);
    }
    return status;
}

tds_ordered_status tds_ordered_set_remove_first(
    const tds_ordered_set* set,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return TDS_ORDERED_EMPTY;
    }
    return tds_ordered_set_remove_at(set, 0u, result);
}

tds_ordered_status tds_ordered_set_remove_last(
    const tds_ordered_set* set,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count == 0u) {
        return TDS_ORDERED_EMPTY;
    }
    return tds_ordered_set_remove_at(set, count - 1u, result);
}

tds_ordered_status tds_ordered_set_clear(
    const tds_ordered_set* set,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (ft_persistent_deque_empty(&set->order)) {
        return tds_ordered_set_clone(set, result);
    }
    return tds_ordered_empty_with_context(set->context, result);
}

typedef struct tds_ordered_map_builder {
    tds_hamt_map map;
    tds_ordered_status status;
} tds_ordered_map_builder;

static void tds_ordered_add_entry_to_map(const void* value, void* raw_builder)
{
    tds_ordered_map_builder* builder = (tds_ordered_map_builder*)raw_builder;
    if (builder->status != TDS_ORDERED_OK) {
        return;
    }
    const tds_ordered_entry* entry = (const tds_ordered_entry*)value;
    const tds_ordered_key_ref key = tds_ordered_cell_key(entry->cell);
    tds_hamt_map next;
    const tds_hamt_status status = tds_hamt_map_add(
        &builder->map,
        &key,
        &entry->stamp,
        &next);
    if (status != TDS_HAMT_OK) {
        builder->status = status == TDS_HAMT_DUPLICATE_KEY
            ? TDS_ORDERED_INVARIANT_VIOLATION
            : tds_ordered_from_hamt(status);
        return;
    }
    tds_hamt_map_destroy(&builder->map);
    builder->map = next;
}

static tds_ordered_status tds_ordered_build_map(
    struct tds_ordered_context* context,
    const ft_persistent_deque* order,
    tds_hamt_map* map)
{
    tds_ordered_map_builder builder;
    builder.map = tds_hamt_map_create(&context->map_policy);
    builder.status = TDS_ORDERED_OK;
    const ft_status visit_status = ft_persistent_deque_visit(
        order,
        tds_ordered_add_entry_to_map,
        &builder);
    if (visit_status != FT_STATUS_OK && builder.status == TDS_ORDERED_OK) {
        builder.status = tds_ordered_from_ft(visit_status);
    }
    if (builder.status != TDS_ORDERED_OK) {
        tds_hamt_map_destroy(&builder.map);
        return builder.status;
    }
    *map = builder.map;
    return TDS_ORDERED_OK;
}

static void tds_ordered_remove_entry_from_map(const void* value, void* raw_builder)
{
    tds_ordered_map_builder* builder = (tds_ordered_map_builder*)raw_builder;
    if (builder->status != TDS_ORDERED_OK) {
        return;
    }
    const tds_ordered_entry* entry = (const tds_ordered_entry*)value;
    const tds_ordered_key_ref key = tds_ordered_cell_key(entry->cell);
    if (!tds_hamt_map_contains_key(&builder->map, &key)) {
        builder->status = TDS_ORDERED_INVARIANT_VIOLATION;
        return;
    }
    tds_hamt_map next;
    const tds_hamt_status status = tds_hamt_map_remove(&builder->map, &key, &next);
    if (status != TDS_HAMT_OK) {
        builder->status = tds_ordered_from_hamt(status);
        return;
    }
    tds_hamt_map_destroy(&builder->map);
    builder->map = next;
}

static tds_ordered_status tds_ordered_remove_order_from_map(
    const ft_persistent_deque* order,
    tds_ordered_map_builder* builder)
{
    const ft_status visit_status = ft_persistent_deque_visit(
        order,
        tds_ordered_remove_entry_from_map,
        builder);
    if (visit_status != FT_STATUS_OK && builder->status == TDS_ORDERED_OK) {
        builder->status = tds_ordered_from_ft(visit_status);
    }
    return builder->status;
}

tds_ordered_status tds_ordered_set_get_range(
    const tds_ordered_set* set,
    size_t index,
    size_t count,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t total = ft_persistent_deque_size(&set->order);
    if (index > total || count > total - index) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    if (count == total) {
        return tds_ordered_set_clone(set, result);
    }
    if (count == 0u) {
        return tds_ordered_empty_with_context(set->context, result);
    }

    ft_tree_split_result first;
    ft_status split_status = ft_persistent_deque_split_at(
        &set->order,
        index,
        &first);
    if (split_status != FT_STATUS_OK) {
        return tds_ordered_from_ft(split_status);
    }
    ft_tree_split_result second;
    split_status = ft_persistent_deque_split_at(&first.right, count, &second);
    ft_persistent_deque_dispose(&first.right);
    if (split_status != FT_STATUS_OK) {
        ft_persistent_deque_dispose(&first.left);
        return tds_ordered_from_ft(split_status);
    }

    tds_hamt_map stamps;
    tds_ordered_status status;
    if (count <= total - count) {
        status = tds_ordered_build_map(set->context, &second.left, &stamps);
    } else {
        tds_ordered_map_builder builder;
        builder.map = tds_hamt_map_clone(&set->stamps);
        builder.status = TDS_ORDERED_OK;
        status = tds_ordered_remove_order_from_map(&first.left, &builder);
        if (status == TDS_ORDERED_OK) {
            status = tds_ordered_remove_order_from_map(&second.right, &builder);
        }
        if (status == TDS_ORDERED_OK && tds_hamt_map_count(&builder.map) != count) {
            status = TDS_ORDERED_INVARIANT_VIOLATION;
        }
        if (status == TDS_ORDERED_OK) {
            stamps = builder.map;
        } else {
            tds_hamt_map_destroy(&builder.map);
        }
    }
    ft_persistent_deque_dispose(&first.left);
    ft_persistent_deque_dispose(&second.right);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&second.left);
        return status;
    }
    status = tds_ordered_publish_parts(set, &second.left, &stamps, result);
    if (status != TDS_ORDERED_OK) {
        ft_persistent_deque_dispose(&second.left);
        tds_hamt_map_destroy(&stamps);
    }
    return status;
}

tds_ordered_status tds_ordered_set_take(
    const tds_ordered_set* set,
    size_t count,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    if (count > ft_persistent_deque_size(&set->order)) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_set_get_range(set, 0u, count, result);
}

tds_ordered_status tds_ordered_set_drop(
    const tds_ordered_set* set,
    size_t count,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t total = ft_persistent_deque_size(&set->order);
    if (count > total) {
        return TDS_ORDERED_OUT_OF_RANGE;
    }
    return tds_ordered_set_get_range(set, count, total - count, result);
}

tds_ordered_status tds_ordered_set_reverse(
    const tds_ordered_set* set,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count <= 1u) {
        return tds_ordered_set_clone(set, result);
    }

    tds_ordered_item_cell** cells = NULL;
    size_t materialized_count = 0u;
    tds_ordered_status status = tds_ordered_materialize_cells(
        &set->order,
        &cells,
        &materialized_count);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    for (size_t left = 0u, right = count - 1u; left < right; ++left, --right) {
        tds_ordered_item_cell* temporary = cells[left];
        cells[left] = cells[right];
        cells[right] = temporary;
    }
    status = tds_ordered_rebuild_cells(set, cells, materialized_count, result);
    free(cells);
    return status;
}

static void tds_ordered_merge_sort_cells(
    tds_ordered_item_cell** cells,
    tds_ordered_item_cell** scratch,
    size_t count,
    ft_compare_fn compare,
    void* compare_context)
{
    if (count < 2u) {
        return;
    }
    const size_t middle = count / 2u;
    tds_ordered_merge_sort_cells(cells, scratch, middle, compare, compare_context);
    tds_ordered_merge_sort_cells(
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

tds_ordered_status tds_ordered_set_sort(
    const tds_ordered_set* set,
    ft_compare_fn compare,
    void* compare_context,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || compare == NULL || result == NULL || result == set) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    const size_t count = ft_persistent_deque_size(&set->order);
    if (count <= 1u) {
        return tds_ordered_set_clone(set, result);
    }

    tds_ordered_item_cell** cells = NULL;
    size_t materialized_count = 0u;
    tds_ordered_status status = tds_ordered_materialize_cells(
        &set->order,
        &cells,
        &materialized_count);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    tds_ordered_item_cell** original = (tds_ordered_item_cell**)malloc(
        count * sizeof(*original));
    tds_ordered_item_cell** scratch = (tds_ordered_item_cell**)malloc(
        count * sizeof(*scratch));
    if (original == NULL || scratch == NULL) {
        free(original);
        free(scratch);
        free(cells);
        return TDS_ORDERED_OUT_OF_MEMORY;
    }
    (void)memcpy(original, cells, count * sizeof(*original));
    tds_ordered_merge_sort_cells(cells, scratch, count, compare, compare_context);
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
        return tds_ordered_set_clone(set, result);
    }
    status = tds_ordered_rebuild_cells(set, cells, materialized_count, result);
    free(cells);
    return status;
}

static tds_ordered_status tds_ordered_normalize_many(
    const tds_ordered_set* receiver,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* normalized)
{
    if (items == NULL && item_count != 0u) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set candidate;
    tds_ordered_status status = tds_ordered_empty_with_context(
        receiver->context,
        &candidate);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    for (size_t index = 0u; index != item_count; ++index) {
        if (items[index] == NULL) {
            tds_ordered_set_destroy(&candidate);
            return TDS_ORDERED_INVALID_ARGUMENT;
        }
        tds_ordered_set next;
        status = tds_ordered_set_add(&candidate, items[index], &next);
        if (status != TDS_ORDERED_OK) {
            tds_ordered_set_destroy(&candidate);
            return status;
        }
        tds_ordered_set_destroy(&candidate);
        tds_ordered_set_move(&candidate, &next);
    }
    *normalized = candidate;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_normalize_set(
    const tds_ordered_set* receiver,
    const tds_ordered_set* argument,
    tds_ordered_set* normalized)
{
    tds_ordered_item_cell** cells = NULL;
    size_t count = 0u;
    tds_ordered_status status = tds_ordered_materialize_cells(
        &argument->order,
        &cells,
        &count);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    if (count > SIZE_MAX / sizeof(void*)) {
        free(cells);
        return TDS_ORDERED_OVERFLOW;
    }
    const void** items = count == 0u
        ? NULL
        : (const void**)malloc(count * sizeof(*items));
    if (count != 0u && items == NULL) {
        free(cells);
        return TDS_ORDERED_OUT_OF_MEMORY;
    }
    for (size_t index = 0u; index != count; ++index) {
        items[index] = cells[index]->data;
    }
    status = tds_ordered_normalize_many(receiver, items, count, normalized);
    free(items);
    free(cells);
    return status;
}

typedef enum tds_ordered_algebra_kind {
    TDS_ORDERED_ALGEBRA_UNION,
    TDS_ORDERED_ALGEBRA_INTERSECT,
    TDS_ORDERED_ALGEBRA_EXCEPT,
    TDS_ORDERED_ALGEBRA_SYMMETRIC_EXCEPT
} tds_ordered_algebra_kind;

static tds_ordered_status tds_ordered_apply_algebra(
    const tds_ordered_set* receiver,
    const tds_ordered_set* normalized,
    tds_ordered_algebra_kind kind,
    tds_ordered_set* result)
{
    tds_ordered_item_cell** receiver_cells = NULL;
    tds_ordered_item_cell** argument_cells = NULL;
    size_t receiver_count = 0u;
    size_t argument_count = 0u;
    tds_ordered_status status = tds_ordered_materialize_cells(
        &receiver->order,
        &receiver_cells,
        &receiver_count);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    status = tds_ordered_materialize_cells(
        &normalized->order,
        &argument_cells,
        &argument_count);
    if (status != TDS_ORDERED_OK) {
        free(receiver_cells);
        return status;
    }

    size_t result_count = 0u;
    if (kind == TDS_ORDERED_ALGEBRA_UNION) {
        result_count = receiver_count;
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!tds_ordered_set_contains(receiver, argument_cells[index]->data)) {
                if (result_count == SIZE_MAX) {
                    free(argument_cells);
                    free(receiver_cells);
                    return TDS_ORDERED_OVERFLOW;
                }
                ++result_count;
            }
        }
        if (result_count == receiver_count) {
            free(argument_cells);
            free(receiver_cells);
            return tds_ordered_set_clone(receiver, result);
        }
    } else if (kind == TDS_ORDERED_ALGEBRA_INTERSECT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                ++result_count;
            }
        }
        if (result_count == receiver_count) {
            free(argument_cells);
            free(receiver_cells);
            return tds_ordered_set_clone(receiver, result);
        }
    } else if (kind == TDS_ORDERED_ALGEBRA_EXCEPT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                ++result_count;
            }
        }
        if (result_count == receiver_count) {
            free(argument_cells);
            free(receiver_cells);
            return tds_ordered_set_clone(receiver, result);
        }
    } else {
        if (argument_count == 0u) {
            free(argument_cells);
            free(receiver_cells);
            return tds_ordered_set_clone(receiver, result);
        }
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                ++result_count;
            }
        }
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!tds_ordered_set_contains(receiver, argument_cells[index]->data)) {
                if (result_count == SIZE_MAX) {
                    free(argument_cells);
                    free(receiver_cells);
                    return TDS_ORDERED_OVERFLOW;
                }
                ++result_count;
            }
        }
    }

    if (result_count > SIZE_MAX / sizeof(tds_ordered_item_cell*)) {
        free(argument_cells);
        free(receiver_cells);
        return TDS_ORDERED_OVERFLOW;
    }
    tds_ordered_item_cell** result_cells = result_count == 0u
        ? NULL
        : (tds_ordered_item_cell**)malloc(result_count * sizeof(*result_cells));
    if (result_count != 0u && result_cells == NULL) {
        free(argument_cells);
        free(receiver_cells);
        return TDS_ORDERED_OUT_OF_MEMORY;
    }

    size_t output = 0u;
    if (kind == TDS_ORDERED_ALGEBRA_UNION) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            result_cells[output++] = receiver_cells[index];
        }
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!tds_ordered_set_contains(receiver, argument_cells[index]->data)) {
                result_cells[output++] = argument_cells[index];
            }
        }
    } else if (kind == TDS_ORDERED_ALGEBRA_INTERSECT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                result_cells[output++] = receiver_cells[index];
            }
        }
    } else if (kind == TDS_ORDERED_ALGEBRA_EXCEPT) {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                result_cells[output++] = receiver_cells[index];
            }
        }
    } else {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (!tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                result_cells[output++] = receiver_cells[index];
            }
        }
        for (size_t index = 0u; index != argument_count; ++index) {
            if (!tds_ordered_set_contains(receiver, argument_cells[index]->data)) {
                result_cells[output++] = argument_cells[index];
            }
        }
    }

    status = output == result_count
        ? tds_ordered_rebuild_cells(receiver, result_cells, result_count, result)
        : TDS_ORDERED_INVARIANT_VIOLATION;
    free(result_cells);
    free(argument_cells);
    free(receiver_cells);
    return status;
}

static tds_ordered_status tds_ordered_algebra_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_algebra_kind kind,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(set) || result == NULL || result == set ||
        (items == NULL && item_count != 0u)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set normalized;
    tds_ordered_status status = tds_ordered_normalize_many(
        set,
        items,
        item_count,
        &normalized);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    status = tds_ordered_apply_algebra(set, &normalized, kind, result);
    tds_ordered_set_destroy(&normalized);
    return status;
}

static tds_ordered_status tds_ordered_algebra_set(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_algebra_kind kind,
    tds_ordered_set* result)
{
    if (!tds_ordered_set_valid(left) || !tds_ordered_set_valid(right) ||
        left->context->policy.item_type.size != right->context->policy.item_type.size ||
        result == NULL || result == left || result == right) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set normalized;
    tds_ordered_status status = tds_ordered_normalize_set(left, right, &normalized);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    status = tds_ordered_apply_algebra(left, &normalized, kind, result);
    tds_ordered_set_destroy(&normalized);
    return status;
}

tds_ordered_status tds_ordered_set_union_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_many(
        set,
        items,
        item_count,
        TDS_ORDERED_ALGEBRA_UNION,
        result);
}

tds_ordered_status tds_ordered_set_intersect_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_many(
        set,
        items,
        item_count,
        TDS_ORDERED_ALGEBRA_INTERSECT,
        result);
}

tds_ordered_status tds_ordered_set_except_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_many(
        set,
        items,
        item_count,
        TDS_ORDERED_ALGEBRA_EXCEPT,
        result);
}

tds_ordered_status tds_ordered_set_symmetric_except_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_many(
        set,
        items,
        item_count,
        TDS_ORDERED_ALGEBRA_SYMMETRIC_EXCEPT,
        result);
}

tds_ordered_status tds_ordered_set_union(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_set(
        left,
        right,
        TDS_ORDERED_ALGEBRA_UNION,
        result);
}

tds_ordered_status tds_ordered_set_intersect(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_set(
        left,
        right,
        TDS_ORDERED_ALGEBRA_INTERSECT,
        result);
}

tds_ordered_status tds_ordered_set_except(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_set(
        left,
        right,
        TDS_ORDERED_ALGEBRA_EXCEPT,
        result);
}

tds_ordered_status tds_ordered_set_symmetric_except(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_set* result)
{
    return tds_ordered_algebra_set(
        left,
        right,
        TDS_ORDERED_ALGEBRA_SYMMETRIC_EXCEPT,
        result);
}

typedef enum tds_ordered_relation_kind {
    TDS_ORDERED_RELATION_SUBSET,
    TDS_ORDERED_RELATION_PROPER_SUBSET,
    TDS_ORDERED_RELATION_SUPERSET,
    TDS_ORDERED_RELATION_PROPER_SUPERSET,
    TDS_ORDERED_RELATION_OVERLAPS,
    TDS_ORDERED_RELATION_EQUALS
} tds_ordered_relation_kind;

static bool tds_ordered_all_in(
    tds_ordered_item_cell* const* cells,
    size_t count,
    const tds_ordered_set* set)
{
    for (size_t index = 0u; index != count; ++index) {
        if (!tds_ordered_set_contains(set, cells[index]->data)) {
            return false;
        }
    }
    return true;
}

static tds_ordered_status tds_ordered_apply_relation(
    const tds_ordered_set* receiver,
    const tds_ordered_set* normalized,
    tds_ordered_relation_kind kind,
    bool* answer)
{
    tds_ordered_item_cell** receiver_cells = NULL;
    tds_ordered_item_cell** argument_cells = NULL;
    size_t receiver_count = 0u;
    size_t argument_count = 0u;
    tds_ordered_status status = tds_ordered_materialize_cells(
        &receiver->order,
        &receiver_cells,
        &receiver_count);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    status = tds_ordered_materialize_cells(
        &normalized->order,
        &argument_cells,
        &argument_count);
    if (status != TDS_ORDERED_OK) {
        free(receiver_cells);
        return status;
    }

    bool local_answer = false;
    if (kind == TDS_ORDERED_RELATION_SUBSET ||
        kind == TDS_ORDERED_RELATION_PROPER_SUBSET ||
        kind == TDS_ORDERED_RELATION_EQUALS) {
        local_answer = tds_ordered_all_in(receiver_cells, receiver_count, normalized);
        if (kind == TDS_ORDERED_RELATION_PROPER_SUBSET) {
            local_answer = local_answer && receiver_count < argument_count;
        } else if (kind == TDS_ORDERED_RELATION_EQUALS) {
            local_answer = local_answer && receiver_count == argument_count;
        }
    } else if (kind == TDS_ORDERED_RELATION_SUPERSET ||
        kind == TDS_ORDERED_RELATION_PROPER_SUPERSET) {
        local_answer = tds_ordered_all_in(argument_cells, argument_count, receiver);
        if (kind == TDS_ORDERED_RELATION_PROPER_SUPERSET) {
            local_answer = local_answer && receiver_count > argument_count;
        }
    } else {
        for (size_t index = 0u; index != receiver_count; ++index) {
            if (tds_ordered_set_contains(normalized, receiver_cells[index]->data)) {
                local_answer = true;
                break;
            }
        }
    }

    free(argument_cells);
    free(receiver_cells);
    *answer = local_answer;
    return TDS_ORDERED_OK;
}

static tds_ordered_status tds_ordered_relation_many(
    const tds_ordered_set* set,
    const void* const* items,
    size_t item_count,
    tds_ordered_relation_kind kind,
    bool* answer)
{
    if (!tds_ordered_set_valid(set) || answer == NULL ||
        (items == NULL && item_count != 0u)) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set normalized;
    tds_ordered_status status = tds_ordered_normalize_many(
        set,
        items,
        item_count,
        &normalized);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    status = tds_ordered_apply_relation(set, &normalized, kind, answer);
    tds_ordered_set_destroy(&normalized);
    return status;
}

static tds_ordered_status tds_ordered_relation_set(
    const tds_ordered_set* left,
    const tds_ordered_set* right,
    tds_ordered_relation_kind kind,
    bool* answer)
{
    if (!tds_ordered_set_valid(left) || !tds_ordered_set_valid(right) || answer == NULL ||
        left->context->policy.item_type.size != right->context->policy.item_type.size) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_set normalized;
    tds_ordered_status status = tds_ordered_normalize_set(left, right, &normalized);
    if (status != TDS_ORDERED_OK) {
        return status;
    }
    status = tds_ordered_apply_relation(left, &normalized, kind, answer);
    tds_ordered_set_destroy(&normalized);
    return status;
}

#define TDS_ORDERED_DEFINE_RELATION(name, kind) \
    tds_ordered_status tds_ordered_set_##name##_many( \
        const tds_ordered_set* set, \
        const void* const* items, \
        size_t item_count, \
        bool* answer) \
    { \
        return tds_ordered_relation_many(set, items, item_count, kind, answer); \
    } \
    tds_ordered_status tds_ordered_set_##name( \
        const tds_ordered_set* left, \
        const tds_ordered_set* right, \
        bool* answer) \
    { \
        return tds_ordered_relation_set(left, right, kind, answer); \
    }

TDS_ORDERED_DEFINE_RELATION(is_subset_of, TDS_ORDERED_RELATION_SUBSET)
TDS_ORDERED_DEFINE_RELATION(is_proper_subset_of, TDS_ORDERED_RELATION_PROPER_SUBSET)
TDS_ORDERED_DEFINE_RELATION(is_superset_of, TDS_ORDERED_RELATION_SUPERSET)
TDS_ORDERED_DEFINE_RELATION(is_proper_superset_of, TDS_ORDERED_RELATION_PROPER_SUPERSET)
TDS_ORDERED_DEFINE_RELATION(overlaps, TDS_ORDERED_RELATION_OVERLAPS)
TDS_ORDERED_DEFINE_RELATION(equals, TDS_ORDERED_RELATION_EQUALS)

#undef TDS_ORDERED_DEFINE_RELATION

typedef struct tds_ordered_visit_adapter {
    tds_ordered_visit_fn visitor;
    void* context;
} tds_ordered_visit_adapter;

static void tds_ordered_visit_entry(const void* value, void* raw_adapter)
{
    const tds_ordered_entry* entry = (const tds_ordered_entry*)value;
    tds_ordered_visit_adapter* adapter = (tds_ordered_visit_adapter*)raw_adapter;
    adapter->visitor(entry->cell->data, adapter->context);
}

tds_ordered_status tds_ordered_set_visit(
    const tds_ordered_set* set,
    tds_ordered_visit_fn visitor,
    void* context)
{
    if (!tds_ordered_set_valid(set) || visitor == NULL) {
        return TDS_ORDERED_INVALID_ARGUMENT;
    }
    tds_ordered_visit_adapter adapter;
    adapter.visitor = visitor;
    adapter.context = context;
    return tds_ordered_from_ft(ft_persistent_deque_visit(
        &set->order,
        tds_ordered_visit_entry,
        &adapter));
}

typedef struct tds_ordered_validation_state {
    const tds_ordered_set* set;
    size_t count;
    bool valid;
    bool has_previous;
    int64_t previous_stamp;
} tds_ordered_validation_state;

static void tds_ordered_validate_entry(const void* value, void* raw_state)
{
    tds_ordered_validation_state* state = (tds_ordered_validation_state*)raw_state;
    const tds_ordered_entry* entry = (const tds_ordered_entry*)value;
    if (!state->valid) {
        return;
    }
    if (entry->cell == NULL ||
        (state->has_previous && entry->stamp <= state->previous_stamp)) {
        state->valid = false;
        return;
    }

    const tds_ordered_key_ref probe = tds_ordered_probe(entry->cell->data);
    const void* stamp_value = NULL;
    const void* key_value = NULL;
    if (!tds_hamt_map_try_get(&state->set->stamps, &probe, &stamp_value) ||
        stamp_value == NULL || *(const int64_t*)stamp_value != entry->stamp ||
        !tds_hamt_map_try_get_key(&state->set->stamps, &probe, &key_value) ||
        ((const tds_ordered_key_ref*)key_value)->cell != entry->cell) {
        state->valid = false;
        return;
    }
    state->has_previous = true;
    state->previous_stamp = entry->stamp;
    ++state->count;
}

bool tds_ordered_set_debug_validate(const tds_ordered_set* set)
{
    if (!tds_ordered_set_valid(set) ||
        ft_persistent_deque_size(&set->order) != tds_hamt_map_count(&set->stamps) ||
        !tds_hamt_map_debug_validate_canonical(&set->stamps)) {
        return false;
    }

    tds_ordered_validation_state state;
    state.set = set;
    state.count = 0u;
    state.valid = true;
    state.has_previous = false;
    state.previous_stamp = 0;
    if (ft_persistent_deque_visit(&set->order, tds_ordered_validate_entry, &state) !=
        FT_STATUS_OK || !state.valid || state.count != tds_hamt_map_count(&set->stamps)) {
        return false;
    }

    tds_hamt_map_iterator iterator;
    tds_hamt_map_iterator_init(&set->stamps, &iterator);
    const void* key_value = NULL;
    const void* stamp_value = NULL;
    size_t map_count = 0u;
    while (tds_hamt_map_iterator_next(&iterator, &key_value, &stamp_value)) {
        const tds_ordered_key_ref* key = (const tds_ordered_key_ref*)key_value;
        if (key == NULL || key->cell == NULL || key->item != key->cell->data ||
            stamp_value == NULL) {
            return false;
        }
        size_t position = 0u;
        if (!tds_ordered_locate_stamp(
                &set->order,
                *(const int64_t*)stamp_value,
                &position)) {
            return false;
        }
        tds_ordered_entry entry;
        if (!tds_ordered_entry_at(&set->order, position, &entry) || entry.cell == NULL) {
            return false;
        }
        const bool same_cell = entry.cell == key->cell;
        tds_ordered_entry_release(&entry);
        if (!same_cell) {
            return false;
        }
        ++map_count;
    }
    return map_count == state.count;
}

bool tds_ordered_set_debug_shares_order(
    const tds_ordered_set* left,
    const tds_ordered_set* right)
{
    return tds_ordered_set_valid(left) && tds_ordered_set_valid(right) &&
        left->order.rep == right->order.rep;
}

bool tds_ordered_set_debug_shares_index(
    const tds_ordered_set* left,
    const tds_ordered_set* right)
{
    return tds_ordered_set_valid(left) && tds_ordered_set_valid(right) &&
        tds_hamt_map_shares_root(&left->stamps, &right->stamps);
}
